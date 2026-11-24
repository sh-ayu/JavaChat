#include "mod/JavaChatMod.h"

#include "mod/ChatCommands.h"
#include "mod/ChatRenderer.h"
#include "mod/PackPinner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <ll/api/event/EventBus.h>
#include <ll/api/event/client/ClientExitLevelEvent.h>
#include <ll/api/event/client/ClientJoinLevelEvent.h>
#include <ll/api/event/command/ClientCommandRegisterEvent.h>
#include <ll/api/event/input/KeyInputEvent.h>
#include <ll/api/event/input/MouseInputEvent.h>
#include <ll/api/event/render/UIRenderEvent.h>
#include <ll/api/io/Logger.h>
#include <ll/api/mod/NativeMod.h>
#include <ll/api/service/TargetedBedrock.h>

#include <mc/client/game/ClientInstance.h>
#include <mc/client/game/IMinecraftGame.h>
#include <mc/client/gui/GuiData.h>
#include <mc/client/gui/GuiMessage.h>
#include <mc/client/gui/controls/UIControl.h>
#include <mc/client/gui/controls/VisualTree.h>
#include <mc/client/gui/screens/ScreenView.h>
#include <mc/client/options/IOptionRegistry.h>
#include <mc/client/player/LocalPlayer.h>
#include <mc/client/renderer/screen/MinecraftUIRenderContext.h>
#include <mc/deps/input/MouseAction.h>
#include <mc/deps/input/RectangleArea.h>

namespace javachat {
namespace {

using SteadyClock = std::chrono::steady_clock;

double steadyNowSec() noexcept {
    return std::chrono::duration<double>(SteadyClock::now().time_since_epoch()).count();
}

long long steadyNowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now().time_since_epoch()).count();
}

bool isGameForeground() noexcept {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool isPrimaryMouseButtonDown() noexcept {
    bool swap = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    return (GetAsyncKeyState(swap ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
}

std::optional<std::pair<float, float>> getGuiMousePos(float screenW, float screenH) noexcept {
    if (!(screenW > 0.0f && screenH > 0.0f)) return std::nullopt;
    HWND fg = GetForegroundWindow();
    if (!fg) return std::nullopt;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return std::nullopt;

    POINT cursor{};
    RECT  rect{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(fg, &cursor) || !GetClientRect(fg, &rect)) return std::nullopt;
    if (rect.right <= 0 || rect.bottom <= 0) return std::nullopt;

    float mx = static_cast<float>(cursor.x) * screenW / static_cast<float>(rect.right);
    float my = static_cast<float>(cursor.y) * screenH / static_cast<float>(rect.bottom);
    return std::make_pair(mx, my);
}

std::string formatLocalTimestamp() {
    auto    t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_{};
    localtime_s(&tm_, &t);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "\xC2\xA7" "8[%02d:%02d] \xC2\xA7" "r", tm_.tm_hour, tm_.tm_min);
    return buf;
}

bool copyUtf8ToClipboard(std::string_view utf8) noexcept {
    try {
        int needed = MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0
        );
        if (needed < 0) return false;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(needed + 1) * sizeof(wchar_t));
        if (!hMem) return false;
        auto* dst = static_cast<wchar_t*>(GlobalLock(hMem));
        if (!dst) {
            GlobalFree(hMem);
            return false;
        }
        if (needed > 0) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), dst, needed);
        }
        dst[needed] = L'\0';
        GlobalUnlock(hMem);

        if (!OpenClipboard(nullptr)) {
            GlobalFree(hMem);
            return false;
        }
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
            CloseClipboard();
            GlobalFree(hMem);
            return false;
        }
        CloseClipboard();
        return true;
    } catch (...) {
        return false;
    }
}

bool openSafeHttpUrl(std::string_view url) noexcept {
    try {
        if (!(url.starts_with("https://") || url.starts_with("http://"))) return false;
        int needed = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
        if (needed <= 0) return false;
        std::wstring wurl(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wurl.data(), needed);
        HINSTANCE res = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(res) > 32;
    } catch (...) {
        return false;
    }
}

float detectAutocompletePopup(ScreenView const& screenView, float screenW, float screenH, Rect& outRect) noexcept {
    try {
        auto const& tree = static_cast<std::unique_ptr<VisualTree> const&>(screenView.mVisualTree);
        if (!tree) return 0.0f;
        auto grid = tree->getControlByName("auto_complete_grid", true);
        if (!grid) return 0.0f;

        auto const& children    = grid->mChildren.get();
        int         visibleRows = 0;
        for (auto const& child : children) {
            if (!child) continue;
            if (child->mIsTemplate || !child->mIsVisibleInTree) continue;
            // Empty autocomplete grid slots hide their `row_bg` via (#auto_complete_text != '').
            // Only count slots whose row_bg is actually visible.
            bool rowActive = false;
            for (auto const& sub : child->mChildren.get()) {
                if (!sub) continue;
                if (sub->mName.get() == "row_bg") {
                    rowActive = sub->mIsVisibleInTree;
                    break;
                }
            }
            if (rowActive) {
                ++visibleRows;
            }
        }
        if (visibleRows <= 0) return 0.0f;

        constexpr float kRowH       = 11.0f;
        constexpr float kMaxPopupH  = 110.0f; // Matches chat_screen.json commands_panel max_size
        constexpr float kBottomBase = 18.0f;  // Matches chat_screen.json commands_panel offset y=-18
        float           h           = std::min(kMaxPopupH, static_cast<float>(visibleRows) * kRowH);
        float           w           = std::min(screenW * 0.65f, 340.0f);
        outRect                     = Rect{2.0f, screenH - kBottomBase - h, 2.0f + w, screenH - kBottomBase};
        return h;
    } catch (...) {
        return 0.0f;
    }
}

} // namespace

JavaChatMod& JavaChatMod::instance() noexcept {
    static JavaChatMod inst;
    return inst;
}

bool JavaChatMod::load() {
    mSelf = ll::mod::NativeMod::current();
    if (!mSelf) return false;

    mShutdown.store(false, std::memory_order_relaxed);
    mConfigPath = mSelf->getConfigDir() / "config.json";

    auto loaded = loadConfigFile(mConfigPath);
    {
        std::lock_guard lock(mMutex);
        mConfig = loaded.config;
    }

    if (!loaded.existed || loaded.corrupted) {
        if (loaded.corrupted) {
            mSelf->getLogger().warn(
                "JavaChat: corrupt config backed up ({}); writing defaults to {}",
                loaded.warning,
                mConfigPath.string()
            );
        }
        if (!saveConfigFile(mConfigPath, loaded.config)) {
            mSelf->getLogger().warn("JavaChat: could not write default config to {}", mConfigPath.string());
        }
    } else if (!loaded.warning.empty()) {
        mSelf->getLogger().warn("JavaChat: config warnings: {}", loaded.warning);
    }

    mSelf->getLogger().info("JavaChat loaded (config: {})", mConfigPath.string());
    return true;
}

bool JavaChatMod::enable() {
    if (!mSelf) return false;
    mShutdown.store(false, std::memory_order_relaxed);

    ChatConfig cfgCopy;
    {
        std::lock_guard lock(mMutex);
        cfgCopy = mConfig;
    }

    bool pinHookOk = PackPinner::instance().install(
        &mSelf->getLogger(),
        kUiPackUuid,
        cfgCopy.enabled && cfgCopy.overrideServerChatUi,
        cfgCopy.verboseLog
    );
    if (!pinHookOk) {
        mSelf->getLogger().warn(
            "JavaChat: composeFullStack hook unavailable; JavaChatUI will rely on standard pack order"
        );
    }

    {
        std::lock_guard lock(mMutex);
        mDraggingScrollbar = false;
        mHasAutocomplete   = false;
    }

    auto& bus = ll::event::EventBus::getInstance();

    // Draw chat history BEFORE chat_screen's JSON-UI so autocomplete popups & input bar
    // always layer cleanly in front of chat history (matching Java Edition).
    mBeforeRenderListener = bus.emplaceListener<ll::event::render::BeforeUIRenderEvent>(
        [this](auto& ev) { onBeforeUiRender(ev); },
        ll::event::EventPriority::Normal,
        mSelf
    );
    mRenderListener = bus.emplaceListener<ll::event::render::AfterUIRenderEvent>(
        [this](auto& ev) { onAfterUiRender(ev); },
        ll::event::EventPriority::Normal,
        mSelf
    );
    mMouseListener = bus.emplaceListener<ll::event::input::MouseInputEvent>(
        [this](auto& ev) { onMouseInput(ev); },
        ll::event::EventPriority::High,
        mSelf
    );
    mKeyListener = bus.emplaceListener<ll::event::input::KeyInputEvent>(
        [this](auto& ev) { onKeyInput(ev); },
        ll::event::EventPriority::High,
        mSelf
    );
    mCommandListener = bus.emplaceListener<ll::event::command::ClientCommandRegisterEvent>(
        [this](auto&) {
            try {
                registerChatCommands(mSelf);
            } catch (...) {
            }
        },
        ll::event::EventPriority::Low,
        mSelf
    );
    mJoinListener = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
        [this](auto&) { onWorldTransition(); },
        ll::event::EventPriority::Normal,
        mSelf
    );
    mExitListener = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>(
        [this](auto&) { onWorldTransition(); },
        ll::event::EventPriority::Normal,
        mSelf
    );

    if (!mBeforeRenderListener || !mRenderListener || !mMouseListener || !mKeyListener) {
        disable();
        return false;
    }

    mSelf->getLogger().info(
        "JavaChat enabled (overrideServerChatUi={}, pin={})",
        cfgCopy.overrideServerChatUi,
        PackPinner::instance().statusSummary()
    );
    return true;
}

bool JavaChatMod::disable() {
    mShutdown.store(true, std::memory_order_relaxed);
    mChatOpen.store(false, std::memory_order_relaxed);
    mLastChatOpenMs.store(0, std::memory_order_relaxed);

    PackPinner::instance().uninstall();

    {
        std::lock_guard lock(mMutex);
        mDraggingScrollbar = false;
        mHasAutocomplete   = false;
    }

    auto& bus = ll::event::EventBus::getInstance();
    for (auto* ptr : {&mBeforeRenderListener, &mRenderListener, &mMouseListener, &mKeyListener, &mCommandListener, &mJoinListener, &mExitListener}) {
        if (*ptr) {
            bus.removeListener(*ptr);
            ptr->reset();
        }
    }
    {
        std::lock_guard lock(mMutex);
        mDraggingScrollbar = false;
        mHasAutocomplete   = false;
    }
    return true;
}

void JavaChatMod::onWorldTransition() noexcept {
    try {
        std::lock_guard lock(mMutex);
        mHistory.resetSyncCursor();
        if (mConfig.clearHistoryOnWorldSwitch) {
            mHistory.clear();
        }
        mHistory.scrollToBottom();
        mWasChatOpen       = false;
        mDraggingScrollbar = false;
        mHasAutocomplete   = false;
        mChatOpen.store(false, std::memory_order_relaxed);
    } catch (...) {
    }
}

void JavaChatMod::showToastLocked(std::string text, double nowSec) {
    mToastText     = std::move(text);
    mToastUntilSec = nowSec + 1.6;
}

void JavaChatMod::saveConfigLocked() noexcept {
    try {
        if (!mConfigPath.empty()) {
            saveConfigFile(mConfigPath, mConfig);
        }
    } catch (...) {
    }
}

void JavaChatMod::onBeforeUiRender(ll::event::render::BeforeUIRenderEvent& event) noexcept {
    renderChatLayer(event, true);
}

void JavaChatMod::onAfterUiRender(ll::event::render::AfterUIRenderEvent& event) noexcept {
    renderChatLayer(event, false);
}

void JavaChatMod::renderChatLayer(ll::event::render::UIRenderEvent& event, bool isBeforePass) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;

        std::string const viewName = event.screenView().getScreenName();
        // Open chat renders BEFORE chat_screen's JSON-UI (so autocomplete & input stay on top).
        // Closed HUD renders AFTER hud_screen's JSON-UI.
        if (isBeforePass) {
            if (viewName != "chat_screen") return;
        } else {
            if (viewName != "hud_screen") return;
        }

        auto clientRef = ll::service::getClientInstance();
        if (!clientRef) return;
        auto& client = clientRef.value();

        auto* player = client.getLocalPlayer();
        if (!player || !client.getMinecraftGame().isInGame() || client.getOptions().getHideHud()) {
            mChatOpen.store(false, std::memory_order_relaxed);
            return;
        }

        std::string const topScreen = client.getScreenName();
        bool const        isChatTop = (topScreen == "chat_screen");
        bool const        isHudTop  = (topScreen == "hud_screen");

        if (isChatTop) {
            if (!isBeforePass) return;
            mChatOpen.store(true, std::memory_order_relaxed);
            mLastChatOpenMs.store(steadyNowMs(), std::memory_order_relaxed);
        } else if (isHudTop) {
            if (isBeforePass) return;
            mChatOpen.store(false, std::memory_order_relaxed);
        } else {
            mChatOpen.store(false, std::memory_order_relaxed);
            std::lock_guard lock(mMutex);
            mDraggingScrollbar = false;
            mHasAutocomplete   = false;
            return;
        }

        // Snapshot GuiData::mGuiMessages before acquiring mMutex.
        std::vector<GuiMessageSnapshot> incoming;
        {
            auto        guiPtr = client.getGuiData();
            auto const& msgs   = guiPtr->mGuiMessages.get();
            incoming.reserve(msgs.size());
            for (GuiMessage const& gm : msgs) {
                GuiMessageSnapshot snap;
                std::string const& full = gm.mFullString.get();
                if (!full.empty()) {
                    snap.fullText = full;
                } else {
                    std::string const& user = gm.mUsername.get();
                    std::string const& body = gm.mMessage.get();
                    if (!user.empty() && !body.empty()) {
                        snap.fullText = "<" + user + "> " + body;
                    } else {
                        snap.fullText = body;
                    }
                }
                snap.duration    = gm.mDuration;
                snap.messageType = static_cast<int>(gm.mType);
                incoming.push_back(std::move(snap));
            }
        }

        auto& ctx  = event.uiRenderContext();
        auto  clip = ctx.getFullClippingRectangle();
        float ox   = clip._x0;
        float oy   = clip._y0;
        float sw   = clip._x1 - clip._x0;
        float sh   = clip._y1 - clip._y0;
        if (!(sw > 0.0f && sh > 0.0f)) return;

        // Detect active command autocomplete popup so chat history slides cleanly above it
        // with zero overlap and clicks inside autocomplete pass straight through.
        Rect  acRect;
        float acHeight = isChatTop ? detectAutocompletePopup(event.screenView(), sw, sh, acRect) : 0.0f;
        float bottomOverride = (isChatTop && acHeight > 0.0f) ? (18.0f + acHeight + 4.0f) : -1.0f;

        double const nowSec   = steadyNowSec();
        std::string  tsPrefix = formatLocalTimestamp();

        ChatConfig            cfgCopy;
        ChatViewLayout        view;
        ToastNotice           toast;
        bool                  hasToast        = false;
        bool                  scrollbarActive = false;
        std::optional<size_t> hovered;

        {
            std::lock_guard lock(mMutex);
            cfgCopy = mConfig;
            if (!cfgCopy.enabled) {
                mHistory.syncFromGuiMessages(incoming, nowSec, cfgCopy, tsPrefix);
                mHasAutocomplete   = false;
                mDraggingScrollbar = false;
                return;
            }

            if (mWasChatOpen && !isChatTop) {
                mHistory.scrollToBottom();
                mDraggingScrollbar = false;
            }
            mWasChatOpen = isChatTop;

            mHistory.syncFromGuiMessages(incoming, nowSec, cfgCopy, tsPrefix);

            auto measure          = ChatRenderer::makeMeasureFn(ctx, cfgCopy.fontScale);
            view                  = mHistory.buildView(sw, sh, isChatTop, nowSec, cfgCopy, measure, bottomOverride);
            mLastView             = view;
            mHasAutocomplete      = (acHeight > 0.0f);
            mLastAutocompleteRect = acRect;
            mLastScreenW          = sw;
            mLastScreenH          = sh;

            if (!isChatTop || !view.showScrollbar) {
                mDraggingScrollbar = false;
            }

            if (isChatTop) {
                scrollbarActive = mDraggingScrollbar;
                if (auto mouse = getGuiMousePos(sw, sh)) {
                    bool inAc = mHasAutocomplete && mLastAutocompleteRect.contains(mouse->first, mouse->second);
                    if (!inAc) {
                        if (ChatHistory::hitTestScrollbar(view, mouse->first, mouse->second)) {
                            scrollbarActive = true;
                        } else if (!mDraggingScrollbar) {
                            hovered = ChatHistory::hitTestLine(view, mouse->first, mouse->second);
                        }
                    }
                }
            }

            if (!mToastText.empty() && nowSec < mToastUntilSec) {
                double rem  = mToastUntilSec - nowSec;
                toast.text  = mToastText;
                toast.alpha = static_cast<float>(std::clamp(rem / 0.4, 0.0, 1.0));
                hasToast    = true;
            }
        }

        ChatRenderer::render(
            ctx, ox, oy, view, cfgCopy, isChatTop, hovered, hasToast ? &toast : nullptr, scrollbarActive
        );
    } catch (...) {
    }
}

void JavaChatMod::onMouseInput(ll::event::input::MouseInputEvent& event) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;
        if (!mChatOpen.load(std::memory_order_relaxed)) return;
        if (steadyNowMs() - mLastChatOpenMs.load(std::memory_order_relaxed) > 300) return;
        if (!isGameForeground()) return;

        char  btn  = event.actionButtonId();
        schar data = event.buttonData();

        if (btn == MouseAction::ActionWheel) {
            if (data == 0) return;
            bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            // Let autocomplete list receive wheel input if cursor hovers over it.
            if (auto mouse = getGuiMousePos(mLastScreenW, mLastScreenH)) {
                if (mHasAutocomplete && mLastAutocompleteRect.contains(mouse->first, mouse->second)) {
                    return;
                }
            }
            int step  = shiftHeld ? 1 : mConfig.scrollStepLines;
            int delta = (data > 0) ? step : -step;
            mHistory.scrollBy(delta);
            event.cancel();
            return;
        }

        if (btn == MouseAction::ActionMove) {
            std::lock_guard lock(mMutex);
            if (!mDraggingScrollbar) return;
            if (!mConfig.enabled || !isPrimaryMouseButtonDown()) {
                mDraggingScrollbar = false;
                return;
            }
            if (auto mouse = getGuiMousePos(mLastScreenW, mLastScreenH)) {
                mHistory.setScrollFromTrackY(mLastView, mouse->second);
            }
            return;
        }

        if (btn == MouseAction::ActionLeft && data == MouseAction::DataUp) {
            std::lock_guard lock(mMutex);
            if (mDraggingScrollbar) {
                mDraggingScrollbar = false;
                event.cancel();
            }
            return;
        }

        bool isLeftDown  = (btn == MouseAction::ActionLeft && data == MouseAction::DataDown);
        bool isRightDown = (btn == MouseAction::ActionRight && data == MouseAction::DataDown);
        if (!isLeftDown && !isRightDown) return;

        std::string urlToOpen;
        std::string textToCopy;
        bool        hitHandled = false;

        {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            auto mouse = getGuiMousePos(mLastScreenW, mLastScreenH);
            if (!mouse) return;
            // Never intercept clicks inside the active command autocomplete popup.
            if (mHasAutocomplete && mLastAutocompleteRect.contains(mouse->first, mouse->second)) return;

            if (isLeftDown && ChatHistory::hitTestScrollbar(mLastView, mouse->first, mouse->second)) {
                mDraggingScrollbar = true;
                mHistory.setScrollFromTrackY(mLastView, mouse->second);
                event.cancel();
                return;
            }

            auto hitIdx = ChatHistory::hitTestLine(mLastView, mouse->first, mouse->second);
            if (!hitIdx.has_value()) return;

            auto const& clickedLine = mLastView.lines[*hitIdx];
            auto const* entry       = mHistory.findById(clickedLine.entryId);
            if (!entry) return;

            std::optional<std::string> targetUrl =
                clickedLine.lineUrl.has_value() ? clickedLine.lineUrl : entry->firstUrl;

            double nowSec = steadyNowSec();
            if (isLeftDown && mConfig.clickToOpenUrl && targetUrl.has_value()) {
                urlToOpen  = *targetUrl;
                hitHandled = true;
                showToastLocked("Opened URL", nowSec);
            } else if (mConfig.clickToCopy && !entry->plainText.empty()) {
                textToCopy = entry->plainText;
                hitHandled = true;
                showToastLocked("Copied message", nowSec);
            }
        }

        if (hitHandled) {
            // Cancel so Bedrock's text_edit_box does not lose focus on global menu_select.
            event.cancel();
            if (!urlToOpen.empty()) {
                if (!openSafeHttpUrl(urlToOpen)) {
                    std::lock_guard lock(mMutex);
                    showToastLocked("Failed to open URL", steadyNowSec());
                }
            } else if (!textToCopy.empty()) {
                if (!copyUtf8ToClipboard(textToCopy)) {
                    std::lock_guard lock(mMutex);
                    showToastLocked("Clipboard busy", steadyNowSec());
                }
            }
        }
    } catch (...) {
    }
}

void JavaChatMod::onKeyInput(ll::event::input::KeyInputEvent& event) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;
        if (!event.isDown()) return;
        if (!mChatOpen.load(std::memory_order_relaxed)) return;
        if (steadyNowMs() - mLastChatOpenMs.load(std::memory_order_relaxed) > 300) return;
        if (!isGameForeground()) return;

        int  vk       = event.keyCode();
        bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        if (vk == VK_PRIOR) { // Page Up
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollBy(std::max(1, mLastView.maxVisibleLines / 2));
            event.cancel();
        } else if (vk == VK_NEXT) { // Page Down
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollBy(-std::max(1, mLastView.maxVisibleLines / 2));
            event.cancel();
        } else if (ctrlHeld && vk == VK_HOME) {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollToTop();
            event.cancel();
        } else if (ctrlHeld && vk == VK_END) {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollToBottom();
            event.cancel();
        }
    } catch (...) {
    }
}

std::string JavaChatMod::describeStatus() {
    std::lock_guard lock(mMutex);
    std::ostringstream oss;
    oss << "JavaChat: " << (mConfig.enabled ? "ON" : "OFF")
        << " | history=" << mHistory.size() << "/" << mConfig.maxHistoryMessages
        << " | width=" << static_cast<int>(std::lround(mConfig.width))
        << " | height=" << static_cast<int>(std::lround(mConfig.closedHeight))
        << "/" << static_cast<int>(std::lround(mConfig.openHeight))
        << " | scale=" << mConfig.fontScale
        << " | spacing=" << mConfig.lineSpacing
        << " | bgAlpha=" << mConfig.backgroundOpacity
        << " | textAlpha=" << mConfig.textOpacity
        << " | fade=" << mConfig.fadeDelaySec << "s"
        << " | compact=" << (mConfig.compactDuplicates ? "on" : "off")
        << " | timestamps=" << (mConfig.showTimestamps ? "on" : "off")
        << " | uiPack=" << PackPinner::instance().statusSummary();
    return oss.str();
}

std::string JavaChatMod::describeHelp() const {
    return "JavaChat commands:\n"
           "  /jchat status                 - Show current settings & UI pack status\n"
           "  /jchat toggle                 - Toggle JavaChat overlay ON/OFF\n"
           "  /jchat clear                  - Clear buffered chat history\n"
           "  /jchat reload | reset         - Reload config.json or reset defaults\n"
           "  /jchat width <120..640>       - Set chat panel width\n"
           "  /jchat height <closed> <open> - Set closed & open max heights\n"
           "  /jchat position <x> <bottom>  - Set left & bottom offsets\n"
           "  /jchat scale <0.5..2.0>       - Set text scale\n"
           "  /jchat spacing <0..8>         - Set line spacing\n"
           "  /jchat opacity <0..1>         - Set background opacity\n"
           "  /jchat textopacity <0.1..1>   - Set text opacity\n"
           "  /jchat fade <1..120>          - Set HUD fade delay (seconds)\n"
           "  /jchat shadow|compact|timestamps|clickcopy|clickurl <true|false>";
}

bool JavaChatMod::toggleEnabled() {
    std::lock_guard lock(mMutex);
    mConfig.enabled = !mConfig.enabled;
    if (!mConfig.enabled) mDraggingScrollbar = false;
    PackPinner::instance().setEnabled(mConfig.enabled && mConfig.overrideServerChatUi);
    saveConfigLocked();
    return mConfig.enabled;
}

void JavaChatMod::clearHistory() {
    std::lock_guard lock(mMutex);
    mHistory.clear();
    mDraggingScrollbar = false;
}

bool JavaChatMod::reloadConfig(std::string& messageOut) {
    auto loaded = loadConfigFile(mConfigPath);
    if (!loaded.existed) {
        messageOut = "JavaChat: config.json not found (" + mConfigPath.string() + ")";
        return false;
    }
    if (loaded.corrupted) {
        messageOut = "JavaChat: config.json is invalid (" + loaded.warning + "); kept current settings";
        return false;
    }
    {
        std::lock_guard lock(mMutex);
        mConfig = loaded.config;
        mHistory.trimToMax(mConfig.maxHistoryMessages);
        PackPinner::instance().setEnabled(mConfig.enabled && mConfig.overrideServerChatUi);
        PackPinner::instance().setVerbose(mConfig.verboseLog);
    }
    messageOut = "JavaChat: config reloaded";
    if (!loaded.warning.empty()) messageOut += " (warnings: " + loaded.warning + ")";
    return true;
}

void JavaChatMod::resetConfig() {
    std::lock_guard lock(mMutex);
    mConfig = ChatConfig{};
    sanitizeConfig(mConfig);
    mHistory.trimToMax(mConfig.maxHistoryMessages);
    PackPinner::instance().setEnabled(mConfig.enabled && mConfig.overrideServerChatUi);
    PackPinner::instance().setVerbose(mConfig.verboseLog);
    saveConfigLocked();
}

float JavaChatMod::setWidth(float width) {
    std::lock_guard lock(mMutex);
    mConfig.width = width;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return mConfig.width;
}

std::pair<float, float> JavaChatMod::setHeight(float closedH, float openH) {
    std::lock_guard lock(mMutex);
    mConfig.closedHeight = closedH;
    mConfig.openHeight   = openH;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return {mConfig.closedHeight, mConfig.openHeight};
}

float JavaChatMod::setFontScale(float scale) {
    std::lock_guard lock(mMutex);
    mConfig.fontScale = scale;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return mConfig.fontScale;
}

float JavaChatMod::setLineSpacing(float spacing) {
    std::lock_guard lock(mMutex);
    mConfig.lineSpacing = spacing;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return mConfig.lineSpacing;
}

float JavaChatMod::setBackgroundOpacity(float opacity) {
    std::lock_guard lock(mMutex);
    mConfig.backgroundOpacity = opacity;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return mConfig.backgroundOpacity;
}

float JavaChatMod::setTextOpacity(float opacity) {
    std::lock_guard lock(mMutex);
    mConfig.textOpacity = opacity;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return mConfig.textOpacity;
}

float JavaChatMod::setFadeDelay(float delaySec) {
    std::lock_guard lock(mMutex);
    mConfig.fadeDelaySec = delaySec;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return mConfig.fadeDelaySec;
}

void JavaChatMod::setTextShadow(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.textShadow = enabled;
    saveConfigLocked();
}

std::pair<float, float> JavaChatMod::setPosition(float leftOffset, float bottomOffset) {
    std::lock_guard lock(mMutex);
    mConfig.leftOffset   = leftOffset;
    mConfig.bottomOffset = bottomOffset;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return {mConfig.leftOffset, mConfig.bottomOffset};
}

void JavaChatMod::setCompactDuplicates(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.compactDuplicates = enabled;
    saveConfigLocked();
}

void JavaChatMod::setShowTimestamps(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.showTimestamps = enabled;
    saveConfigLocked();
}

void JavaChatMod::setClickToCopy(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.clickToCopy = enabled;
    saveConfigLocked();
}

void JavaChatMod::setClickToOpenUrl(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.clickToOpenUrl = enabled;
    saveConfigLocked();
}

} // namespace javachat
