#include "mod/JavaChatMod.h"

#include "mod/ChatBehaviorHooks.h"
#include "mod/ChatCommands.h"
#include "mod/ChatFontManager.h"
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
#include <ll/api/event/client/ClientStartJoinLevelEvent.h>
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
#include <mc/client/gui/ViewRequest.h>
#include <mc/client/gui/controls/TextCharEventResult.h>
#include <mc/client/gui/controls/TextComponent.h>
#include <mc/client/gui/controls/UIControl.h>
#include <mc/client/gui/controls/VisualTree.h>
#include <mc/client/gui/screens/IntellisenseHandler.h>
#include <mc/client/gui/screens/ScreenView.h>
#include <mc/client/gui/screens/controllers/ChatScreenController.h>
#include <mc/client/options/IOptionRegistry.h>
#include <mc/client/player/LocalPlayer.h>
#include <mc/client/renderer/screen/MinecraftUIRenderContext.h>
#include <mc/deps/application/AppPlatform.h>
#include <mc/deps/input/MouseAction.h>
#include <mc/deps/input/RectangleArea.h>
#include <mc/deps/input/enums/ButtonState.h>
#include <mc/deps/input/enums/FocusImpact.h>

namespace javachat {
namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr uint32_t fnv1a32(std::string_view sv) noexcept {
    uint32_t hash = 0x811C9DC5u;
    for (char c : sv) {
        hash = (hash ^ static_cast<uint8_t>(c)) * 0x01000193u;
    }
    return hash;
}

constexpr uint32_t kButtonMenuAutocompleteId     = fnv1a32("button.menu_autocomplete");
constexpr uint32_t kButtonChatAutocompleteId     = fnv1a32("button.chat_autocomplete");
constexpr uint32_t kButtonChatAutocompleteBackId = fnv1a32("button.chat_autocomplete_back");
constexpr uint32_t kButtonChatPrevMsgId          = fnv1a32("button.chat_previous_message");
constexpr uint32_t kButtonChatNextMsgId          = fnv1a32("button.chat_next_message");

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

std::optional<std::string> readUtf8FromClipboard() noexcept {
    try {
        if (!OpenClipboard(nullptr)) return std::nullopt;
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (!hData) {
            CloseClipboard();
            return std::nullopt;
        }
        auto const* wstr = static_cast<wchar_t const*>(GlobalLock(hData));
        if (!wstr) {
            CloseClipboard();
            return std::nullopt;
        }
        std::string result;
        int const   needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 1) {
            result.resize(static_cast<size_t>(needed - 1));
            WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), needed - 1, nullptr, nullptr);
        }
        GlobalUnlock(hData);
        CloseClipboard();
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

void syncChatTextBox(
    ChatScreenController& chatCtrl,
    std::string const&    newText,
    bool                  refreshIntellisense,
    int                   caretPos = -1
) noexcept {
    try {
        chatCtrl.mCurrentMessage.get()       = newText;
        chatCtrl.mCurrentHistoryPrefix.get() = newText;
        chatCtrl.mUpdateIntellisense         = refreshIntellisense;
        if (auto app = ll::service::getAppPlatform()) {
            app->setTextBoxBackend(newText);
            app->setCaretPosition(caretPos);
        }
    } catch (...) {
    }
}

std::string_view extractGuiMessageText(GuiMessage const& gm, std::string& scratch) {
    std::string const& full = gm.mFullString.get();
    if (!full.empty()) return full;
    std::string const& user = gm.mUsername.get();
    std::string const& body = gm.mMessage.get();
    if (!user.empty() && !body.empty()) {
        scratch = "<" + user + "> " + body;
        return scratch;
    }
    return body;
}

TextComponent* findDisplayTextComponent(
    ScreenView const&           screenView,
    bool                        sameScreen,
    std::shared_ptr<UIControl>& ctrlSlot
) noexcept {
    try {
        if (!sameScreen) ctrlSlot.reset();
        if (!ctrlSlot) {
            auto const& tree = static_cast<std::unique_ptr<VisualTree> const&>(screenView.mVisualTree);
            if (!tree) return nullptr;
            ctrlSlot = tree->getControlByName("display_text", false);
        }
        if (!ctrlSlot) return nullptr;
        auto const& lookup = ctrlSlot->mRenderableComponentLookup.get();
        if (lookup.size() != 1 || !lookup[0]) return nullptr;
        return reinterpret_cast<TextComponent*>(lookup[0]);
    } catch (...) {
        return nullptr;
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
    std::error_code ec;
    auto            cfgDir = mSelf->getConfigDir();
    auto            modDir = mSelf->getModDir();
    if (auto absCfg = std::filesystem::absolute(cfgDir, ec); !ec) cfgDir = std::move(absCfg);
    ec.clear();
    if (auto absMod = std::filesystem::absolute(modDir, ec); !ec) modDir = std::move(absMod);
    mConfigPath = cfgDir / "config.json";

    auto loaded = loadConfigFile(mConfigPath);
    {
        std::lock_guard lock(mMutex);
        mConfig = loaded.config;
    }

    if (!loaded.existed || loaded.corrupted || loaded.migrated) {
        if (loaded.corrupted) {
            mSelf->getLogger().warn(
                "JavaChat: corrupt config backed up ({}); writing defaults to {}",
                loaded.warning,
                mConfigPath.string()
            );
        } else if (loaded.migrated) {
            mSelf->getLogger().info(
                "JavaChat: migrated config to v3 (lineSpacing={}, backgroundOpacity={}, clearHistoryOnWorldSwitch={})",
                loaded.config.lineSpacing,
                loaded.config.backgroundOpacity,
                loaded.config.clearHistoryOnWorldSwitch
            );
        }
        if (!saveConfigFile(mConfigPath, loaded.config)) {
            mSelf->getLogger().warn("JavaChat: could not write config to {}", mConfigPath.string());
        }
    }
    if (!loaded.corrupted && !loaded.warning.empty()) {
        mSelf->getLogger().warn("JavaChat: config warnings: {}", loaded.warning);
    }

    // Initialize font manager and stage custom TTF (if configured) before Bedrock registers packs.
    auto& fontMgr = ChatFontManager::instance();
    fontMgr.initialize(modDir, cfgDir, &mSelf->getLogger());
    {
        std::string fontMsg;
        bool        isCustom = false;
        if (!fontMgr.prepareFontSpec(loaded.config.font, fontMsg, isCustom)) {
            mSelf->getLogger().warn("JavaChat: {}; falling back to default font", fontMsg);
            std::string fallbackMsg;
            bool        dummy = false;
            fontMgr.prepareFontSpec("default", fallbackMsg, dummy);
        } else if (loaded.config.verboseLog || isCustom) {
            mSelf->getLogger().info("JavaChat: {}", fontMsg);
        }
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

    ChatBehaviorHooks::instance().install(
        &mSelf->getLogger(),
        cfgCopy.enabled,
        cfgCopy.autoCloseOnSend,
        cfgCopy.disableChatAnimations,
        cfgCopy.keepHudVisibleInChat
    );
    ChatBehaviorHooks::instance().setChatNavHandler(
        [this](ChatScreenController& ctrl, uint32_t btnId, bool isNewPress) {
            return handleChatNavButton(ctrl, btnId, isNewPress);
        }
    );
    ChatBehaviorHooks::instance().setChatTextEditAfterHandler(
        [this](ChatScreenController& ctrl, int textEditResult) {
            return handleChatTextEditAfter(ctrl, textEditResult);
        }
    );

    {
        std::lock_guard lock(mMutex);
        mSuggestions.reset();
        mSuggestions.clearKnownCommands();
        mLastSuggestionLayout    = {};
        clearInputSelectionStateLocked();
        mLastHoverMouseX         = -1.0f;
        mLastHoverMouseY         = -1.0f;
        mDraggingScrollbar       = false;
        mLastLevelPtr            = nullptr;
        mCachedDisplayTextScreen = nullptr;
        mCachedDisplayTextCtrl.reset();
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
            } catch (std::exception const& e) {
                if (mSelf) mSelf->getLogger().error("JavaChat: failed to register /jchat commands: {}", e.what());
            } catch (...) {
                if (mSelf) mSelf->getLogger().error("JavaChat: failed to register /jchat commands (unknown error)");
            }
        },
        ll::event::EventPriority::Low,
        mSelf
    );
    mStartJoinListener = bus.emplaceListener<ll::event::client::ClientStartJoinLevelEvent>(
        [this](auto&) { onWorldTransition(); },
        ll::event::EventPriority::Normal,
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
        "JavaChat enabled (overrideServerChatUi={}, pin={}, {})",
        cfgCopy.overrideServerChatUi,
        PackPinner::instance().statusSummary(),
        ChatBehaviorHooks::instance().statusSummary()
    );
    return true;
}

bool JavaChatMod::disable() {
    mShutdown.store(true, std::memory_order_relaxed);
    mChatOpen.store(false, std::memory_order_relaxed);
    mLastChatOpenMs.store(0, std::memory_order_relaxed);

    ChatBehaviorHooks::instance().uninstall();
    PackPinner::instance().uninstall();
    ChatFontManager::instance().shutdown();

    {
        std::lock_guard lock(mMutex);
        mSuggestions.reset();
        mSuggestions.clearKnownCommands();
        mLastSuggestionLayout    = {};
        clearInputSelectionStateLocked();
        mLastHoverMouseX         = -1.0f;
        mLastHoverMouseY         = -1.0f;
        mDraggingScrollbar       = false;
        mLastLevelPtr            = nullptr;
        mCachedDisplayTextScreen = nullptr;
        mCachedDisplayTextCtrl.reset();
    }

    auto& bus = ll::event::EventBus::getInstance();
    for (auto* ptr : {&mBeforeRenderListener, &mRenderListener, &mMouseListener, &mKeyListener, &mCommandListener, &mStartJoinListener, &mJoinListener, &mExitListener}) {
        if (*ptr) {
            bus.removeListener(*ptr);
            ptr->reset();
        }
    }
    {
        std::lock_guard lock(mMutex);
        mSuggestions.reset();
        mSuggestions.clearKnownCommands();
        mLastSuggestionLayout    = {};
        clearInputSelectionStateLocked();
        mLastHoverMouseX         = -1.0f;
        mLastHoverMouseY         = -1.0f;
        mDraggingScrollbar       = false;
        mLastLevelPtr            = nullptr;
        mCachedDisplayTextScreen = nullptr;
        mCachedDisplayTextCtrl.reset();
    }
    return true;
}

void JavaChatMod::onWorldTransition() noexcept {
    try {
        ChatBehaviorHooks::instance().clearPendingAutoClose();
        ChatBehaviorHooks::instance().clearChatInputState();
        ChatFontManager::instance().invalidateUiSync();
        std::lock_guard lock(mMutex);
        mLastLevelPtr = nullptr;
        mHistory.resetSyncCursor();
        if (mConfig.clearHistoryOnWorldSwitch) {
            mHistory.clear();
        }
        mHistory.scrollToBottom();
        mWasChatOpen             = false;
        mDraggingScrollbar       = false;
        mSuggestions.reset();
        mSuggestions.clearKnownCommands();
        mLastSuggestionLayout    = {};
        clearInputSelectionStateLocked();
        mLastHoverMouseX         = -1.0f;
        mLastHoverMouseY         = -1.0f;
        mCachedDisplayTextScreen = nullptr;
        mCachedDisplayTextCtrl.reset();
        mChatOpen.store(false, std::memory_order_relaxed);
    } catch (...) {
    }
}

void JavaChatMod::clearInputSelectionStateLocked() noexcept {
    mPendingInputEdit.reset();
    mPendingCaretPos.reset();
    mLastObservedInput.clear();
    mInputAllSelected = false;
    mSelectedTextSnapshot.clear();
}

void JavaChatMod::showToastLocked(std::string text, double nowSec) {
    mToastText     = std::move(text);
    mToastUntilSec = nowSec + 1.6;
}

void JavaChatMod::saveConfigLocked() noexcept {
    try {
        if (!mConfigPath.empty() && !saveConfigFile(mConfigPath, mConfig) && mSelf) {
            mSelf->getLogger().warn("JavaChat: failed to save config to {}", mConfigPath.string());
        }
    } catch (...) {
    }
}

void JavaChatMod::onBeforeUiRender(ll::event::render::BeforeUIRenderEvent& event) noexcept {
    renderChatLayer(event, true);
}

void JavaChatMod::onAfterUiRender(ll::event::render::AfterUIRenderEvent& event) noexcept {
    if (event.screenView().getScreenName() == "chat_screen") {
        renderChatOverlayPass(event);
        return;
    }
    renderChatLayer(event, false);
}

void JavaChatMod::renderChatLayer(ll::event::render::UIRenderEvent& event, bool isBeforePass) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;

        std::string const viewName = event.screenView().getScreenName();
        // Open chat history renders BEFORE chat_screen's JSON-UI; suggestions popup renders AFTER.
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
        void const* const curLevel = static_cast<void const*>(client.getLevel());

        std::string const topScreen = client.getScreenName();
        bool const        isChatTop = (topScreen == "chat_screen");
        bool const        isHudTop  = (topScreen == "hud_screen");

        if (isChatTop) {
            if (!isBeforePass) return;
            mChatOpen.store(true, std::memory_order_relaxed);
            mLastChatOpenMs.store(steadyNowMs(), std::memory_order_relaxed);
            ChatBehaviorHooks::instance().pollPendingAutoClose(event.screenView());
        } else if (isHudTop) {
            if (isBeforePass) return;
            mChatOpen.store(false, std::memory_order_relaxed);
        } else {
            mChatOpen.store(false, std::memory_order_relaxed);
            ChatBehaviorHooks::instance().clearChatInputState();
            std::lock_guard lock(mMutex);
            mDraggingScrollbar       = false;
            mSuggestions.reset();
            mLastSuggestionLayout    = {};
            clearInputSelectionStateLocked();
            mCachedDisplayTextScreen = nullptr;
            mCachedDisplayTextCtrl.reset();
            return;
        }

        auto& ctx  = event.uiRenderContext();
        auto  clip = ctx.getFullClippingRectangle();
        float ox   = clip._x0;
        float oy   = clip._y0;
        float sw   = clip._x1 - clip._x0;
        float sh   = clip._y1 - clip._y0;
        if (!(sw > 0.0f && sh > 0.0f)) return;

        // O(1) fast-path check before copying GuiMessages or formatting timestamps.
        auto        guiPtr = client.getGuiData();
        auto const& msgs   = guiPtr->mGuiMessages.get();
        std::string firstScratch;
        std::string lastScratch;
        std::string_view firstSv;
        std::string_view lastSv;
        int   firstType = 0;
        int   lastType  = 0;
        float lastDur   = 0.0f;
        if (!msgs.empty()) {
            firstSv   = extractGuiMessageText(msgs.front(), firstScratch);
            firstType = static_cast<int>(msgs.front().mType);
            lastSv    = extractGuiMessageText(msgs.back(), lastScratch);
            lastType  = static_cast<int>(msgs.back().mType);
            lastDur   = msgs.back().mDuration;
        }

        double const nowSec = steadyNowSec();

        auto buildIncomingAndSync = [&](ChatConfig const& cfg) {
            std::vector<GuiMessageSnapshot> incoming;
            incoming.reserve(msgs.size());
            std::string scratch;
            for (GuiMessage const& gm : msgs) {
                GuiMessageSnapshot snap;
                snap.fullText    = std::string(extractGuiMessageText(gm, scratch));
                snap.duration    = gm.mDuration;
                snap.messageType = static_cast<int>(gm.mType);
                incoming.push_back(std::move(snap));
            }
            std::string tsPrefix = formatLocalTimestamp();
            mHistory.syncFromGuiMessages(incoming, nowSec, cfg, tsPrefix);
        };

        // Fast-check enabled/sync/closed-visibility before touching fonts or layout.
        bool                       skipFrameRender = false;
        ToastNotice                toast;
        bool                       hasToast        = false;
        bool                       sameChatScreen  = false;
        std::weak_ptr<UIControl>   cachedCtrlWeak;
        {
            std::lock_guard lock(mMutex);
            if (curLevel != nullptr && mLastLevelPtr != nullptr && curLevel != mLastLevelPtr) {
                mHistory.resetSyncCursor();
                if (mConfig.clearHistoryOnWorldSwitch) {
                    mHistory.clear();
                }
                mHistory.scrollToBottom();
                mDraggingScrollbar       = false;
                mSuggestions.reset();
                mSuggestions.clearKnownCommands();
                mLastSuggestionLayout    = {};
                clearInputSelectionStateLocked();
                mCachedDisplayTextScreen = nullptr;
                mCachedDisplayTextCtrl.reset();
            }
            if (curLevel != nullptr) {
                mLastLevelPtr = curLevel;
            }
            if (mHistory.needsSync(msgs.size(), firstSv, firstType, lastSv, lastType, lastDur, nowSec)) {
                buildIncomingAndSync(mConfig);
            }
            if (!mConfig.enabled) {
                mDraggingScrollbar    = false;
                mSuggestions.reset();
                mLastSuggestionLayout = {};
                clearInputSelectionStateLocked();
                ChatBehaviorHooks::instance().clearChatInputState();
                return;
            }
            if (mWasChatOpen && !isChatTop) {
                mHistory.scrollToBottom();
                mDraggingScrollbar       = false;
                mSuggestions.reset();
                mLastSuggestionLayout    = {};
                clearInputSelectionStateLocked();
                mCachedDisplayTextScreen = nullptr;
                mCachedDisplayTextCtrl.reset();
                ChatBehaviorHooks::instance().clearChatInputState();
            }
            mWasChatOpen = isChatTop;

            if (!mToastText.empty() && nowSec < mToastUntilSec) {
                double rem  = mToastUntilSec - nowSec;
                toast.text  = mToastText;
                toast.alpha = static_cast<float>(std::clamp(rem / 0.4, 0.0, 1.0));
                hasToast    = true;
            }

            if (!isChatTop && !hasToast && !mHistory.hasVisibleClosedMessages(nowSec, mConfig)) {
                skipFrameRender = true;
            } else if (isChatTop) {
                sameChatScreen = (mCachedDisplayTextScreen == &event.screenView());
                cachedCtrlWeak = mCachedDisplayTextCtrl;
            }
        }

        // Sync suggestions & input bar visual state EVEN IF chat history is empty so `/command`
        // syntax highlighting, ghost preview, and suggestions work with zero history lines.
        if (isChatTop) {
            auto* chatCtrl = static_cast<::ChatScreenController*>(event.screenView().mController.get());
            std::shared_ptr<UIControl> displayCtrl = cachedCtrlWeak.lock();
            TextComponent*             displayComp = findDisplayTextComponent(
                event.screenView(), sameChatScreen, displayCtrl
            );

            std::string              curMsg;
            std::vector<std::string> rawAutoCompleteTexts;
            bool                     hasUsage            = false;
            bool                     intellisensePending = false;
            if (chatCtrl && !chatCtrl->mIsDevConsole) {
                curMsg              = chatCtrl->mCurrentMessage.get();
                intellisensePending = chatCtrl->mUpdateIntellisense;
                auto const& ih      = chatCtrl->mIntellisense.get();
                hasUsage            = !ih.mIntellisenseMessages.get().empty();
                auto const& ac      = ih.mAutoCompleteMessages.get();
                rawAutoCompleteTexts.reserve(ac.size());
                for (auto const& item : ac) {
                    rawAutoCompleteTexts.push_back(item.text.get());
                }
            }

            InputVisualState visState;
            bool             firePendingEdit = false;
            {
                std::lock_guard lock(mMutex);
                mCachedDisplayTextScreen = &event.screenView();
                mCachedDisplayTextCtrl   = displayCtrl;
                if (mConfig.enabled && chatCtrl && !chatCtrl->mIsDevConsole) {
                    if (mInputAllSelected && curMsg != mSelectedTextSnapshot) {
                        if (auto ins = extractInsertedTextAfterSelectAll(mSelectedTextSnapshot, curMsg)) {
                            mInputAllSelected = false;
                            mSelectedTextSnapshot.clear();
                            mPendingCaretPos.reset();
                            mPendingInputEdit = PendingInputEdit{*ins, true, -1, std::nullopt};
                        } else {
                            mInputAllSelected = false;
                            mSelectedTextSnapshot.clear();
                        }
                    }
                    if (!mPendingInputEdit.has_value()) {
                        mLastObservedInput = curMsg;
                    }

                    mSuggestions.syncFromBedrock(curMsg, rawAutoCompleteTexts, intellisensePending);
                    bool const knownCmdValid   = mSuggestions.isKnownCommand(curMsg);
                    visState.expectedPlainText = curMsg;
                    visState.allSelected =
                        mInputAllSelected && !curMsg.empty() && (curMsg == mSelectedTextSnapshot);
                    visState.suggestionTokens  = mSuggestions.items();
                    visState.selectedToken     = std::string(mSuggestions.selectedToken());
                    visState.hasUsage          = hasUsage;
                    visState.knownCommandValid = knownCmdValid;
                    visState.enableSyntaxColor =
                        (!curMsg.empty() && curMsg.front() == '/' && curMsg.find("\xC2\xA7") == std::string_view::npos);
                    if (visState.enableSyntaxColor) {
                        visState.coloredText =
                            formatJavaCommandSyntax(curMsg, mSuggestions.items(), hasUsage, knownCmdValid);
                        if (mSuggestions.active()) {
                            visState.ghostSuffix = computeInlineGhostSuffix(curMsg, mSuggestions.selectedToken());
                            visState.enableGhost = !visState.ghostSuffix.empty();
                        }
                    }
                    firePendingEdit = mPendingInputEdit.has_value() || mPendingCaretPos.has_value();
                } else {
                    mSuggestions.reset();
                    mLastSuggestionLayout = {};
                    clearInputSelectionStateLocked();
                }
            }

            ChatBehaviorHooks::instance().updateChatInputState(displayComp, chatCtrl, std::move(visState));
            if (firePendingEdit) {
                event.screenView().handleButtonEvent(
                    kButtonMenuAutocompleteId, ::ButtonState::Down, ::FocusImpact::Neutral
                );
                event.screenView().handleButtonEvent(
                    kButtonMenuAutocompleteId, ::ButtonState::Up, ::FocusImpact::Neutral
                );
            }
        }
        if (skipFrameRender) return;

        auto& fontMgr = ChatFontManager::instance();
        fontMgr.ensureResolved(client, ctx);
        FontRenderSet const fontSet = fontMgr.renderSet();
        uint32_t const      fontRev = fontMgr.revision();
        if (isChatTop) {
            fontMgr.syncChatScreenFonts(event.screenView(), client);
        }

        ChatConfig            cfgCopy;
        ChatViewLayout        view;
        bool                  scrollbarActive = false;
        std::optional<size_t> hovered;

        {
            std::lock_guard lock(mMutex);
            cfgCopy = mConfig;
            if (!cfgCopy.enabled) {
                mDraggingScrollbar    = false;
                mSuggestions.reset();
                mLastSuggestionLayout = {};
                clearInputSelectionStateLocked();
                ChatBehaviorHooks::instance().clearChatInputState();
                return;
            }

            auto measure = ChatRenderer::makeMeasureFn(ctx, fontSet, cfgCopy.fontScale);
            // Authentic Java Edition keeps chat history firmly anchored at cfgCopy.bottomOffset (40.0);
            // command suggestions layer in front via AfterUIRenderEvent without pushing chat upward.
            view         = mHistory.buildView(sw, sh, isChatTop, nowSec, cfgCopy, measure, -1.0f, fontRev);
            if (isChatTop) {
                mLastView    = view;
                mLastScreenW = sw;
                mLastScreenH = sh;
                if (!view.showScrollbar) {
                    mDraggingScrollbar = false;
                }
                scrollbarActive = mDraggingScrollbar;
                if (auto mouse = getGuiMousePos(sw, sh)) {
                    bool inPopup = mLastSuggestionLayout.visible
                                && mLastSuggestionLayout.popupRect.contains(mouse->first, mouse->second);
                    if (!inPopup) {
                        if (ChatHistory::hitTestScrollbar(view, mouse->first, mouse->second)) {
                            scrollbarActive = true;
                        } else if (!mDraggingScrollbar) {
                            hovered = ChatHistory::hitTestLine(view, mouse->first, mouse->second);
                        }
                    }
                }
            } else {
                mDraggingScrollbar = false;
            }
        }

        ChatRenderer::render(
            ctx, ox, oy, view, cfgCopy, fontSet, isChatTop, hovered, hasToast ? &toast : nullptr, scrollbarActive
        );
    } catch (...) {
    }
}

void JavaChatMod::renderChatOverlayPass(ll::event::render::AfterUIRenderEvent& event) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;

        auto clientRef = ll::service::getClientInstance();
        if (!clientRef) return;
        auto& client = clientRef.value();

        auto* player = client.getLocalPlayer();
        if (!player || !client.getMinecraftGame().isInGame() || client.getOptions().getHideHud()) return;
        if (client.getScreenName() != "chat_screen") return;

        auto&       ctx  = event.uiRenderContext();
        auto        clip = ctx.getFullClippingRectangle();
        float const ox   = clip._x0;
        float const oy   = clip._y0;
        float const sw   = clip._x1 - clip._x0;
        float const sh   = clip._y1 - clip._y0;
        if (!(sw > 0.0f && sh > 0.0f)) return;

        auto& fontMgr = ChatFontManager::instance();
        fontMgr.ensureResolved(client, ctx);
        FontRenderSet const fontSet = fontMgr.renderSet();

        CapturedInputGeometry const geom = ChatBehaviorHooks::instance().consumeCapturedInputGeometry();
        float const localInputX          = geom.valid ? (geom.textOriginX - ox) : 4.0f;

        ChatConfig            cfgCopy;
        SuggestionPopupLayout layout;
        Rect                  midCaretLocal;
        bool                  hasMidCaret = false;
        Rect                  selectionLocal;
        bool                  hasSelection = false;

        {
            std::lock_guard lock(mMutex);
            cfgCopy = mConfig;
            if (!cfgCopy.enabled) return;

            mLastScreenW = sw;
            mLastScreenH = sh;

            float const inputScale =
                (geom.valid && geom.fontScale > 0.1f) ? geom.fontScale : cfgCopy.fontScale;
            auto measure = ChatRenderer::makeMeasureFn(ctx, fontSet, inputScale);
            layout       = mSuggestions.buildLayout(sw, sh, localInputX, inputScale, measure);

            if (auto mouse = getGuiMousePos(sw, sh)) {
                bool const moved = (std::fabs(mouse->first - mLastHoverMouseX) > 0.5f)
                                || (std::fabs(mouse->second - mLastHoverMouseY) > 0.5f);
                if (moved) {
                    mLastHoverMouseX = mouse->first;
                    mLastHoverMouseY = mouse->second;
                    if (auto hitRow = CommandSuggestionState::hitTestRow(layout, mouse->first, mouse->second)) {
                        if (mSuggestions.setSelectedIndex(*hitRow)) {
                            layout = mSuggestions.buildLayout(sw, sh, localInputX, inputScale, measure);
                        }
                    }
                }
            }

            mLastSuggestionLayout = layout;
            if (geom.valid && geom.hasMidCaret) {
                midCaretLocal = Rect{
                    geom.midCaretRect.x0 - ox,
                    geom.midCaretRect.y0 - oy,
                    geom.midCaretRect.x1 - ox,
                    geom.midCaretRect.y1 - oy
                };
                hasMidCaret = true;
            }
            if (geom.valid && geom.hasSelection) {
                selectionLocal = Rect{
                    geom.selectionRect.x0 - ox,
                    geom.selectionRect.y0 - oy,
                    geom.selectionRect.x1 - ox,
                    geom.selectionRect.y1 - oy
                };
                hasSelection = true;
            }
        }

        ChatRenderer::renderSuggestions(
            ctx,
            ox,
            oy,
            layout,
            cfgCopy,
            fontSet,
            hasMidCaret ? &midCaretLocal : nullptr,
            hasSelection ? &selectionLocal : nullptr
        );
    } catch (...) {
    }
}

std::optional<unsigned int> JavaChatMod::handleChatNavButton(
    ChatScreenController& chatCtrl,
    uint32_t              buttonId,
    bool                  isNewPress
) noexcept {
    try {
        std::lock_guard lock(mMutex);
        if (!mConfig.enabled || chatCtrl.mIsDevConsole) return std::nullopt;

        if (buttonId == kButtonChatPrevMsgId || buttonId == kButtonChatNextMsgId) {
            mInputAllSelected = false;
            mSelectedTextSnapshot.clear();
            if (!mSuggestions.active()) {
                return std::nullopt; // Fall through to Bedrock sent-message history.
            }
            if (isNewPress) {
                mSuggestions.moveSelection(buttonId == kButtonChatPrevMsgId ? -1 : +1);
            }
            return static_cast<unsigned int>(::ui::ViewRequest::ConsumeEvent);
        }

        if (buttonId == kButtonChatAutocompleteId || buttonId == kButtonChatAutocompleteBackId) {
            if (mPendingInputEdit.has_value()) {
                PendingInputEdit edit = std::move(*mPendingInputEdit);
                mPendingInputEdit.reset();
                mPendingCaretPos.reset();
                mInputAllSelected = false;
                mSelectedTextSnapshot.clear();
                mLastObservedInput = edit.text;
                syncChatTextBox(chatCtrl, edit.text, edit.refreshIntellisense, edit.caretPos);
                return static_cast<unsigned int>(::ui::ViewRequest::Refresh)
                     | static_cast<unsigned int>(::ui::ViewRequest::ConsumeEvent);
            }
            if (mPendingCaretPos.has_value()) {
                int const pos = *mPendingCaretPos;
                mPendingCaretPos.reset();
                if (auto app = ll::service::getAppPlatform()) {
                    app->setCaretPosition(pos);
                }
                return static_cast<unsigned int>(::ui::ViewRequest::Refresh)
                     | static_cast<unsigned int>(::ui::ViewRequest::ConsumeEvent);
            }
            if (!mSuggestions.active()) {
                return std::nullopt;
            }
            if (!isNewPress) {
                return static_cast<unsigned int>(::ui::ViewRequest::ConsumeEvent);
            }
            int const dir = (buttonId == kButtonChatAutocompleteBackId) ? -1 : +1;
            if (auto res = mSuggestions.applyByTab(dir)) {
                mInputAllSelected = false;
                mSelectedTextSnapshot.clear();
                mLastObservedInput = res->newText;
                syncChatTextBox(chatCtrl, res->newText, res->refreshIntellisense, -1);
                return static_cast<unsigned int>(::ui::ViewRequest::Refresh)
                     | static_cast<unsigned int>(::ui::ViewRequest::ConsumeEvent);
            }
            return static_cast<unsigned int>(::ui::ViewRequest::ConsumeEvent);
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<unsigned int> JavaChatMod::handleChatTextEditAfter(
    ChatScreenController& chatCtrl,
    int                   textEditResult
) noexcept {
    try {
        std::lock_guard lock(mMutex);
        if (!mConfig.enabled || chatCtrl.mIsDevConsole) return std::nullopt;

        std::string const curMsg = chatCtrl.mCurrentMessage.get();
        if (mPendingInputEdit.has_value()) {
            if (mPendingInputEdit->clearFromSnapshot.has_value()
                && textEditResult == static_cast<int>(::TextCharEventResult::Success)) {
                if (auto ins = extractInsertedTextAfterSelectAll(*mPendingInputEdit->clearFromSnapshot, curMsg)) {
                    mPendingInputEdit.reset();
                    mPendingCaretPos.reset();
                    mInputAllSelected = false;
                    mSelectedTextSnapshot.clear();
                    mLastObservedInput = *ins;
                    syncChatTextBox(chatCtrl, *ins, true, -1);
                    return static_cast<unsigned int>(::ui::ViewRequest::Refresh);
                }
            }
            PendingInputEdit edit = std::move(*mPendingInputEdit);
            mPendingInputEdit.reset();
            mPendingCaretPos.reset();
            mInputAllSelected = false;
            mSelectedTextSnapshot.clear();
            mLastObservedInput = edit.text;
            syncChatTextBox(chatCtrl, edit.text, edit.refreshIntellisense, edit.caretPos);
            return static_cast<unsigned int>(::ui::ViewRequest::Refresh);
        }

        if (mInputAllSelected) {
            if (curMsg != mSelectedTextSnapshot) {
                if (auto ins = extractInsertedTextAfterSelectAll(mSelectedTextSnapshot, curMsg)) {
                    mInputAllSelected = false;
                    mSelectedTextSnapshot.clear();
                    mPendingCaretPos.reset();
                    mLastObservedInput = *ins;
                    syncChatTextBox(chatCtrl, *ins, true, -1);
                    return static_cast<unsigned int>(::ui::ViewRequest::Refresh);
                }
                mInputAllSelected = false;
                mSelectedTextSnapshot.clear();
                mLastObservedInput = curMsg;
            }
        } else {
            mLastObservedInput = curMsg;
        }
    } catch (...) {
    }
    return std::nullopt;
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
            if (auto mouse = getGuiMousePos(mLastScreenW, mLastScreenH)) {
                if (mLastSuggestionLayout.visible
                    && mLastSuggestionLayout.popupRect.contains(mouse->first, mouse->second)) {
                    mSuggestions.scrollByRows(data > 0 ? -1 : +1);
                    event.cancel();
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
            // Handle clicks inside the Java command suggestions popup without defocusing text_edit_box.
            if (mLastSuggestionLayout.visible
                && mLastSuggestionLayout.popupRect.contains(mouse->first, mouse->second)) {
                if (isLeftDown) {
                    if (auto hitRow = CommandSuggestionState::hitTestRow(
                            mLastSuggestionLayout, mouse->first, mouse->second
                        )) {
                        if (auto res = mSuggestions.applyByClick(*hitRow)) {
                            mInputAllSelected = false;
                            mSelectedTextSnapshot.clear();
                            mPendingCaretPos.reset();
                            mPendingInputEdit = PendingInputEdit{res->newText, true, -1, std::nullopt};
                        }
                    }
                }
                event.cancel();
                return;
            }

            if (isLeftDown && ChatHistory::hitTestScrollbar(mLastView, mouse->first, mouse->second)) {
                mDraggingScrollbar = true;
                mHistory.setScrollFromTrackY(mLastView, mouse->second);
                event.cancel();
                return;
            }

            auto hitIdx = ChatHistory::hitTestLine(mLastView, mouse->first, mouse->second);
            if (!hitIdx.has_value()) {
                if (isLeftDown) {
                    mInputAllSelected = false;
                    mSelectedTextSnapshot.clear();
                    mPendingCaretPos.reset();
                }
                return;
            }

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

        int const  vk       = event.keyCode();
        bool const ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool const altHeld  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool const ctrlOnly = ctrlHeld && !altHeld;

        if (vk == VK_PRIOR) { // Page Up
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollBy(std::max(1, mLastView.maxVisibleLines / 2));
            event.cancel();
            return;
        }
        if (vk == VK_NEXT) { // Page Down
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollBy(-std::max(1, mLastView.maxVisibleLines / 2));
            event.cancel();
            return;
        }
        if (ctrlOnly && vk == VK_HOME) {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollToTop();
            event.cancel();
            return;
        }
        if (ctrlOnly && vk == VK_END) {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            mHistory.scrollToBottom();
            event.cancel();
            return;
        }

        if (ctrlOnly && vk == 'A') {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled) return;
            event.cancel();
            if (!mLastObservedInput.empty()) {
                mInputAllSelected     = true;
                mSelectedTextSnapshot = mLastObservedInput;
                mPendingCaretPos      = -1;
            }
            return;
        }

        std::optional<std::string> copySnapshot;
        bool                       needPaste = false;
        {
            std::lock_guard lock(mMutex);
            if (!mConfig.enabled || !mInputAllSelected) return;

            if (ctrlOnly && vk == 'C') {
                copySnapshot = mSelectedTextSnapshot;
                event.cancel();
            } else if (ctrlOnly && vk == 'X') {
                copySnapshot          = mSelectedTextSnapshot;
                mInputAllSelected     = false;
                mPendingInputEdit     = PendingInputEdit{"", true, -1, mSelectedTextSnapshot};
                mSelectedTextSnapshot.clear();
                mLastObservedInput.clear();
                mPendingCaretPos.reset();
                event.cancel();
            } else if (ctrlOnly && vk == 'V') {
                needPaste = true;
                event.cancel();
            } else if (vk == VK_BACK || vk == VK_DELETE) {
                mInputAllSelected = false;
                mPendingInputEdit = PendingInputEdit{"", true, -1, mSelectedTextSnapshot};
                mSelectedTextSnapshot.clear();
                mLastObservedInput.clear();
                mPendingCaretPos.reset();
                event.cancel();
            } else if (!ctrlHeld && !altHeld && vk == VK_LEFT) {
                mInputAllSelected = false;
                mSelectedTextSnapshot.clear();
                mPendingCaretPos = 0;
                event.cancel();
            } else if (!ctrlHeld && !altHeld && vk == VK_RIGHT) {
                mInputAllSelected = false;
                mSelectedTextSnapshot.clear();
                mPendingCaretPos = -1;
                event.cancel();
            } else if (vk == VK_HOME || vk == VK_END || vk == VK_UP || vk == VK_DOWN
                       || vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_TAB) {
                mInputAllSelected = false;
                mSelectedTextSnapshot.clear();
                mPendingCaretPos.reset();
            }
        }

        if (copySnapshot.has_value()) {
            copyUtf8ToClipboard(*copySnapshot);
            return;
        }
        if (needPaste) {
            if (auto raw = readUtf8FromClipboard()) {
                std::string     pasted = sanitizeClipboardForChat(*raw, 512);
                std::lock_guard lock(mMutex);
                if (mConfig.enabled && mInputAllSelected) {
                    mInputAllSelected = false;
                    mSelectedTextSnapshot.clear();
                    mPendingCaretPos.reset();
                    mPendingInputEdit  = PendingInputEdit{pasted, true, -1, std::nullopt};
                    mLastObservedInput = pasted;
                }
            }
            return;
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
        << " | font=" << ChatFontManager::instance().statusSummary()
        << " | bgAlpha=" << mConfig.backgroundOpacity
        << " | textAlpha=" << mConfig.textOpacity
        << " | fade=" << mConfig.fadeDelaySec << "s/" << mConfig.fadeDurationSec << "s"
        << " | autoClose=" << (mConfig.autoCloseOnSend ? "on" : "off")
        << " | anim=" << (mConfig.disableChatAnimations ? "off" : "on")
        << " | keepHud=" << (mConfig.keepHudVisibleInChat ? "on" : "off")
        << " | compact=" << (mConfig.compactDuplicates ? "on" : "off")
        << " | timestamps=" << (mConfig.showTimestamps ? "on" : "off")
        << " | clearOnSwitch=" << (mConfig.clearHistoryOnWorldSwitch ? "on" : "off")
        << " | uiPack=" << PackPinner::instance().statusSummary()
        << " | hooks=" << ChatBehaviorHooks::instance().statusSummary();
    return oss.str();
}

std::string JavaChatMod::describeHelp() const {
    return "JavaChat commands:\n"
           "  /jchat status                 - Show current settings, font, & hook status\n"
           "  /jchat toggle                 - Toggle JavaChat overlay ON/OFF\n"
           "  /jchat clear                  - Clear buffered chat history\n"
           "  /jchat reload | reset         - Reload config.json or reset defaults\n"
           "  /jchat width <120..640>       - Set chat panel width\n"
           "  /jchat height <closed> <open> - Set closed & open max heights\n"
           "  /jchat position <x> <bottom>  - Set left & bottom offsets\n"
           "  /jchat scale <0.5..2.0>       - Set text scale\n"
           "  /jchat spacing <0..8>         - Set line spacing\n"
           "  /jchat font [list|<name|ttf>] - Show, list, or change chat font\n"
           "  /jchat opacity <0..1>         - Set background opacity\n"
           "  /jchat textopacity <0.1..1>   - Set text opacity\n"
           "  /jchat fade <delay> [dur]     - Set HUD fade delay (1..120s) & duration (0..10s)\n"
           "  /jchat autoclose <true|false> - Auto-close chat after sending a message\n"
           "  /jchat animations <t|f>       - Enable/disable chat open/close animations\n"
           "  /jchat keephud <true|false>   - Keep HUD & other mods' HUD visible in chat\n"
           "  /jchat clearonswitch <t|f>    - Clear history on world/server switch\n"
           "  /jchat shadow|compact|timestamps|clickcopy|clickurl <true|false>";
}

bool JavaChatMod::toggleEnabled() {
    std::lock_guard lock(mMutex);
    mConfig.enabled = !mConfig.enabled;
    if (!mConfig.enabled) mDraggingScrollbar = false;
    PackPinner::instance().setEnabled(mConfig.enabled && mConfig.overrideServerChatUi);
    ChatBehaviorHooks::instance().setEnabled(mConfig.enabled);
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
    std::string fontMsg;
    bool        isCustom = false;
    bool        fontOk   = ChatFontManager::instance().prepareFontSpec(loaded.config.font, fontMsg, isCustom);
    if (!fontOk) {
        std::string dummyMsg;
        bool        dummyCustom = false;
        ChatFontManager::instance().prepareFontSpec("default", dummyMsg, dummyCustom);
    }
    {
        std::lock_guard lock(mMutex);
        mConfig = loaded.config;
        if (!fontOk) {
            mConfig.font = "default";
        }
        mHistory.trimToMax(mConfig.maxHistoryMessages);
        PackPinner::instance().setEnabled(mConfig.enabled && mConfig.overrideServerChatUi);
        PackPinner::instance().setVerbose(mConfig.verboseLog);
        ChatBehaviorHooks::instance().setEnabled(mConfig.enabled);
        ChatBehaviorHooks::instance().setAutoCloseOnSend(mConfig.autoCloseOnSend);
        ChatBehaviorHooks::instance().setDisableAnimations(mConfig.disableChatAnimations);
        ChatBehaviorHooks::instance().setKeepHudVisibleInChat(mConfig.keepHudVisibleInChat);
    }
    messageOut = "JavaChat: config reloaded";
    if (!fontOk) messageOut += " (" + fontMsg + ")";
    else if (isCustom) messageOut += " (" + fontMsg + "; restart game if new .ttf)";
    if (!loaded.warning.empty()) messageOut += " (warnings: " + loaded.warning + ")";
    return true;
}

void JavaChatMod::resetConfig() {
    {
        std::lock_guard lock(mMutex);
        mConfig = ChatConfig{};
        sanitizeConfig(mConfig);
        mHistory.trimToMax(mConfig.maxHistoryMessages);
        PackPinner::instance().setEnabled(mConfig.enabled && mConfig.overrideServerChatUi);
        PackPinner::instance().setVerbose(mConfig.verboseLog);
        ChatBehaviorHooks::instance().setEnabled(mConfig.enabled);
        ChatBehaviorHooks::instance().setAutoCloseOnSend(mConfig.autoCloseOnSend);
        ChatBehaviorHooks::instance().setDisableAnimations(mConfig.disableChatAnimations);
        ChatBehaviorHooks::instance().setKeepHudVisibleInChat(mConfig.keepHudVisibleInChat);
        saveConfigLocked();
    }
    std::string fontMsg;
    bool        isCustom = false;
    ChatFontManager::instance().prepareFontSpec("default", fontMsg, isCustom);
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

std::pair<float, float> JavaChatMod::setFade(float delaySec, float durationSec) {
    std::lock_guard lock(mMutex);
    mConfig.fadeDelaySec    = delaySec;
    mConfig.fadeDurationSec = durationSec;
    sanitizeConfig(mConfig);
    saveConfigLocked();
    return {mConfig.fadeDelaySec, mConfig.fadeDurationSec};
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

void JavaChatMod::setAutoCloseOnSend(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.autoCloseOnSend = enabled;
    ChatBehaviorHooks::instance().setAutoCloseOnSend(enabled);
    saveConfigLocked();
}

void JavaChatMod::setDisableChatAnimations(bool disabled) {
    std::lock_guard lock(mMutex);
    mConfig.disableChatAnimations = disabled;
    ChatBehaviorHooks::instance().setDisableAnimations(disabled);
    saveConfigLocked();
}

void JavaChatMod::setKeepHudVisibleInChat(bool keep) {
    std::lock_guard lock(mMutex);
    mConfig.keepHudVisibleInChat = keep;
    ChatBehaviorHooks::instance().setKeepHudVisibleInChat(keep);
    saveConfigLocked();
}

void JavaChatMod::setClearHistoryOnWorldSwitch(bool enabled) {
    std::lock_guard lock(mMutex);
    mConfig.clearHistoryOnWorldSwitch = enabled;
    saveConfigLocked();
}

bool JavaChatMod::setFont(std::string const& spec, std::string& messageOut) {
    std::string prepMsg;
    bool        isCustom = false;
    if (!ChatFontManager::instance().prepareFontSpec(spec, prepMsg, isCustom)) {
        messageOut = "JavaChat: " + prepMsg;
        return false;
    }

    {
        std::lock_guard lock(mMutex);
        mConfig.font = spec;
        sanitizeConfig(mConfig);
        saveConfigLocked();
    }
    if (mSelf) {
        mSelf->getLogger().info("JavaChat: {}", prepMsg);
    }

    if (isCustom) {
        messageOut = "JavaChat: " + prepMsg + ". Saved to config.json; restart the game to load the new .ttf.";
    } else {
        messageOut = "JavaChat: " + prepMsg + " (applied immediately).";
    }
    return true;
}

std::string JavaChatMod::describeFonts() {
    return ChatFontManager::instance().listAvailableFonts();
}

} // namespace javachat
