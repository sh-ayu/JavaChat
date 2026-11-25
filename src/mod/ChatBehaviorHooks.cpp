#include "mod/ChatBehaviorHooks.h"

#include "core/ChatFormatter.h"
#include "core/CommandSuggestions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>

#include <ll/api/io/Logger.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/memory/Memory.h>
#include <ll/api/memory/Symbol.h>
#include <ll/api/service/TargetedBedrock.h>

#include <mc/client/game/ClientInstance.h>
#include <mc/client/gui/CaretMeasureData.h>
#include <mc/client/gui/Font.h>
#include <mc/client/gui/TextAlignment.h>
#include <mc/client/gui/TextMeasureData.h>
#include <mc/client/gui/ViewRequest.h>
#include <mc/client/gui/controls/TextCharEventResult.h>
#include <mc/client/gui/controls/TextComponent.h>
#include <mc/client/gui/controls/UIRenderContext.h>
#include <mc/client/gui/screens/AbstractScene.h>
#include <mc/client/gui/screens/ButtonScreenEventData.h>
#include <mc/client/gui/screens/ScreenController.h>
#include <mc/client/gui/screens/ScreenEvent.h>
#include <mc/client/gui/screens/ScreenEventData.h>
#include <mc/client/gui/screens/ScreenEventType.h>
#include <mc/client/gui/screens/ScreenView.h>
#include <mc/client/gui/screens/TextEditScreenEventData.h>
#include <mc/client/gui/screens/UIScene.h>
#include <mc/client/gui/screens/controllers/ChatScreenController.h>
#include <mc/client/gui/screens/interfaces/ISceneStack.h>
#include <mc/client/renderer/screen/MinecraftUIRenderContext.h>
#include <mc/deps/core/math/Color.h>
#include <mc/deps/input/RectangleArea.h>
#include <mc/deps/input/enums/ButtonState.h>

namespace javachat {
namespace {

constexpr uint32_t fnv1a32(std::string_view sv) noexcept {
    uint32_t hash = 0x811C9DC5u;
    for (char c : sv) {
        hash = (hash ^ static_cast<uint8_t>(c)) * 0x01000193u;
    }
    return hash;
}

constexpr uint32_t kButtonSendId                 = fnv1a32("button.send");
constexpr uint32_t kButtonChatAutocompleteId     = fnv1a32("button.chat_autocomplete");
constexpr uint32_t kButtonChatAutocompleteBackId = fnv1a32("button.chat_autocomplete_back");
constexpr uint32_t kButtonChatPrevMsgId          = fnv1a32("button.chat_previous_message");
constexpr uint32_t kButtonChatNextMsgId          = fnv1a32("button.chat_next_message");

long long steadyNowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
    )
        .count();
}

size_t countUtf8Codepoints(std::string_view sv) noexcept {
    size_t count = 0;
    size_t pos   = 0;
    while (pos < sv.size()) {
        nextUtf8Codepoint(sv, pos);
        ++count;
    }
    return count;
}

std::string_view prefixUtf8Codepoints(std::string_view sv, size_t maxCodepoints) noexcept {
    size_t pos   = 0;
    size_t count = 0;
    while (pos < sv.size() && count < maxCodepoints) {
        nextUtf8Codepoint(sv, pos);
        ++count;
    }
    return sv.substr(0, pos);
}

LL_TYPE_INSTANCE_HOOK(
    ChatEntranceAnimHook,
    ::ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$onScreenEntrance,
    void,
    bool isRevisiting,
    bool doScreenTransitions
) {
    ChatBehaviorHooks::instance().onScreenEntranceBefore(this, doScreenTransitions);
    origin(isRevisiting, doScreenTransitions);
}

LL_TYPE_INSTANCE_HOOK(
    ChatExitAnimHook,
    ::ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$onScreenExit,
    void,
    bool                               isPopping,
    bool                               doScreenTransitions,
    ::std::shared_ptr<::AbstractScene> pushedScene
) {
    ChatBehaviorHooks::instance().onScreenExitBefore(this, pushedScene.get(), doScreenTransitions);
    origin(isPopping, doScreenTransitions, std::move(pushedScene));
}

LL_TYPE_INSTANCE_HOOK(
    ChatHandleEventHook,
    ::ll::memory::HookPriority::Normal,
    ScreenController,
    &ScreenController::$handleEvent,
    ::ui::ViewRequest,
    ::ScreenEvent& screenEvent
) {
    auto& hooks = ChatBehaviorHooks::instance();
    if (auto navResult = hooks.onChatNavButtonBefore(this, &screenEvent)) {
        return static_cast<::ui::ViewRequest>(*navResult);
    }
    bool const isTextEdit  = (screenEvent.type == ::ScreenEventType::TextEditChange);
    int const  textEditRes = isTextEdit ? static_cast<int>(screenEvent.data.get().textEdit.get().result) : 0;
    bool const sendTrigger = hooks.isSendTriggerEvent(&screenEvent);
    auto       req         = origin(screenEvent);
    if (isTextEdit && !sendTrigger) {
        if (auto extraFlags = hooks.onChatTextEditAfter(this, textEditRes)) {
            req = static_cast<::ui::ViewRequest>(static_cast<unsigned int>(req) | *extraFlags);
        }
    }
    if (sendTrigger) {
        hooks.onAfterSendTrigger(this, static_cast<unsigned int>(req));
    }
    return req;
}

LL_TYPE_INSTANCE_HOOK(
    ChatTextRenderHook,
    ::ll::memory::HookPriority::Normal,
    TextComponent,
    &TextComponent::$render,
    void,
    ::UIRenderContext& context
) {
    auto& hooks = ChatBehaviorHooks::instance();
    if (!hooks.shouldHookDisplayText(this)) {
        origin(context);
        return;
    }

    auto&        mcCtx   = static_cast<::MinecraftUIRenderContext&>(context);
    size_t const beforeN = mcCtx.mTextToDraw.get().size();
    origin(context);
    hooks.processDisplayTextAfterRender(this, &mcCtx, beforeN, this->mShouldRenderCaret, this->mCaretBlinkVisible);
}

using IgnoreAsTopFn = bool (*)(::UIScene const*);
std::atomic<IgnoreAsTopFn> gOrigIgnoreAsTop{nullptr};

bool ignoreAsTopDetour(::UIScene const* self) noexcept {
    if (ChatBehaviorHooks::instance().shouldTreatAsIgnoreTop(self)) {
        return true;
    }
    auto orig = gOrigIgnoreAsTop.load(std::memory_order_acquire);
    return orig ? orig(self) : false;
}

} // namespace

ChatBehaviorHooks& ChatBehaviorHooks::instance() noexcept {
    static ChatBehaviorHooks inst;
    return inst;
}

bool ChatBehaviorHooks::install(
    ll::io::Logger* logger,
    bool            enabled,
    bool            autoCloseOnSend,
    bool            disableAnimations,
    bool            keepHudVisibleInChat
) noexcept {
    mLogger = logger;
    mShutdown.store(false, std::memory_order_relaxed);
    mEnabled.store(enabled, std::memory_order_relaxed);
    mAutoCloseOnSend.store(autoCloseOnSend, std::memory_order_relaxed);
    mDisableAnimations.store(disableAnimations, std::memory_order_relaxed);
    mKeepHudVisibleInChat.store(keepHudVisibleInChat, std::memory_order_relaxed);
    if (!mIgnoreAsTopHookInstalled.load(std::memory_order_relaxed)) {
        mIgnoreAsTopAttempted.store(false, std::memory_order_relaxed);
    }
    mLoggedAnchorMismatch.store(false, std::memory_order_relaxed);
    mJustExitedChat.store(false, std::memory_order_relaxed);
    mSuppressedHudExit.store(false, std::memory_order_relaxed);
    clearPendingAutoClose();
    clearChatInputState();

    if (!mEntranceHookInstalled.load(std::memory_order_relaxed)) {
        try {
            bool ok = (ChatEntranceAnimHook::hook() == 0);
            mEntranceHookInstalled.store(ok, std::memory_order_relaxed);
        } catch (...) {
            mEntranceHookInstalled.store(false, std::memory_order_relaxed);
        }
    }
    if (!mExitHookInstalled.load(std::memory_order_relaxed)) {
        try {
            bool ok = (ChatExitAnimHook::hook() == 0);
            mExitHookInstalled.store(ok, std::memory_order_relaxed);
        } catch (...) {
            mExitHookInstalled.store(false, std::memory_order_relaxed);
        }
    }
    if (!mHandleEventHookInstalled.load(std::memory_order_relaxed)) {
        try {
            bool ok = (ChatHandleEventHook::hook() == 0);
            mHandleEventHookInstalled.store(ok, std::memory_order_relaxed);
        } catch (...) {
            mHandleEventHookInstalled.store(false, std::memory_order_relaxed);
        }
    }
    if (!mTextRenderHookInstalled.load(std::memory_order_relaxed)) {
        try {
            bool ok = (ChatTextRenderHook::hook() == 0);
            mTextRenderHookInstalled.store(ok, std::memory_order_relaxed);
        } catch (...) {
            mTextRenderHookInstalled.store(false, std::memory_order_relaxed);
        }
    }

    if (mLogger && (!areAnimHooksInstalled() || !isAutoCloseHookInstalled() || !isTextRenderHookInstalled())) {
        mLogger->warn("JavaChat: partial behavior hooks ({})", statusSummary());
    }
    return areAnimHooksInstalled() && isAutoCloseHookInstalled();
}

void ChatBehaviorHooks::uninstall() noexcept {
    mShutdown.store(true, std::memory_order_relaxed);
    mJustExitedChat.store(false, std::memory_order_relaxed);
    mSuppressedHudExit.store(false, std::memory_order_relaxed);
    clearPendingAutoClose();
    clearChatInputState();
    setChatNavHandler({});
    setChatTextEditAfterHandler({});

    if (mTextRenderHookInstalled.exchange(false, std::memory_order_relaxed)) {
        try {
            ChatTextRenderHook::unhook();
        } catch (...) {
        }
    }
    {
        std::lock_guard lock(mIgnoreAsTopMutex);
        if (mIgnoreAsTopHookInstalled.exchange(false, std::memory_order_relaxed) && mIgnoreAsTopTarget) {
            try {
                ::ll::memory::unhook(
                    mIgnoreAsTopTarget,
                    reinterpret_cast<::ll::memory::FuncPtr>(&ignoreAsTopDetour)
                );
            } catch (...) {
            }
        }
        mIgnoreAsTopTarget = nullptr;
        gOrigIgnoreAsTop.store(nullptr, std::memory_order_release);
        mIgnoreAsTopAttempted.store(false, std::memory_order_relaxed);
        mLoggedAnchorMismatch.store(false, std::memory_order_relaxed);
    }
    if (mHandleEventHookInstalled.exchange(false, std::memory_order_relaxed)) {
        try {
            ChatHandleEventHook::unhook();
        } catch (...) {
        }
    }
    if (mExitHookInstalled.exchange(false, std::memory_order_relaxed)) {
        try {
            ChatExitAnimHook::unhook();
        } catch (...) {
        }
    }
    if (mEntranceHookInstalled.exchange(false, std::memory_order_relaxed)) {
        try {
            ChatEntranceAnimHook::unhook();
        } catch (...) {
        }
    }
}

void ChatBehaviorHooks::setEnabled(bool enabled) noexcept {
    mEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        mJustExitedChat.store(false, std::memory_order_relaxed);
        mSuppressedHudExit.store(false, std::memory_order_relaxed);
        clearPendingAutoClose();
        clearChatInputState();
    }
}

void ChatBehaviorHooks::setAutoCloseOnSend(bool enabled) noexcept {
    mAutoCloseOnSend.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        clearPendingAutoClose();
    }
}

void ChatBehaviorHooks::setDisableAnimations(bool disabled) noexcept {
    mDisableAnimations.store(disabled, std::memory_order_relaxed);
    if (!disabled) {
        mJustExitedChat.store(false, std::memory_order_relaxed);
        mSuppressedHudExit.store(false, std::memory_order_relaxed);
    }
}

void ChatBehaviorHooks::setKeepHudVisibleInChat(bool keep) noexcept {
    mKeepHudVisibleInChat.store(keep, std::memory_order_relaxed);
}

void ChatBehaviorHooks::ensureIgnoreAsTopHook(void const* scenePtr) noexcept {
    try {
        if (!scenePtr) return;
        if (mIgnoreAsTopHookInstalled.load(std::memory_order_acquire)
            || mIgnoreAsTopAttempted.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard lock(mIgnoreAsTopMutex);
        if (mShutdown.load(std::memory_order_relaxed)
            || mIgnoreAsTopHookInstalled.load(std::memory_order_relaxed)
            || mIgnoreAsTopAttempted.load(std::memory_order_relaxed)) {
            return;
        }

        auto const idxIgnore   = ::ll::memory::getVtableIndex(&::AbstractScene::ignoreAsTop);
        auto const idxRenderGb = ::ll::memory::getVtableIndex(&::AbstractScene::renderGameBehind);
        auto const idxExit     = ::ll::memory::getVtableIndex(&::AbstractScene::onScreenExit);
        auto const idxEnter    = ::ll::memory::getVtableIndex(&::AbstractScene::onScreenEntrance);
        if (!idxIgnore || !idxRenderGb || !idxExit || !idxEnter) {
            mIgnoreAsTopAttempted.store(true, std::memory_order_release);
            if (mLogger) mLogger->warn("JavaChat: failed to resolve AbstractScene vtable indices for ignoreAsTop");
            return;
        }

        void* const* vtbl = *reinterpret_cast<void* const* const*>(scenePtr);
        if (!vtbl) return;

        void* const expectedRenderGb =
            ::ll::memory::SymbolView("?$renderGameBehind@UIScene@@QEBA_NXZ").resolve(true);
        void* const expectedExit =
            ::ll::memory::SymbolView("?$onScreenExit@UIScene@@QEAAX_N0V?$shared_ptr@VAbstractScene@@@std@@@Z")
                .resolve(true);
        void* const expectedEnter =
            ::ll::memory::SymbolView("?$onScreenEntrance@UIScene@@QEAAX_N0@Z").resolve(true);
        if (!expectedRenderGb || !expectedExit || !expectedEnter) {
            mIgnoreAsTopAttempted.store(true, std::memory_order_release);
            if (mLogger) {
                mLogger->warn(
                    "JavaChat: failed to resolve UIScene anchors (renderGameBehind={}, onScreenExit={}, onScreenEntrance={})",
                    expectedRenderGb != nullptr,
                    expectedExit != nullptr,
                    expectedEnter != nullptr
                );
            }
            return;
        }

        if (vtbl[*idxRenderGb] != expectedRenderGb || vtbl[*idxExit] != expectedExit
            || vtbl[*idxEnter] != expectedEnter) {
            // Derived UIScene subclass (or mismatch); log once and wait for a plain UIScene instance.
            if (!mLoggedAnchorMismatch.exchange(true, std::memory_order_relaxed) && mLogger) {
                mLogger->warn(
                    "JavaChat: UIScene vtable anchor mismatch (rgb=0x{:x}/0x{:x}, exit=0x{:x}/0x{:x}, ent=0x{:x}/0x{:x})",
                    reinterpret_cast<uintptr_t>(vtbl[*idxRenderGb]),
                    reinterpret_cast<uintptr_t>(expectedRenderGb),
                    reinterpret_cast<uintptr_t>(vtbl[*idxExit]),
                    reinterpret_cast<uintptr_t>(expectedExit),
                    reinterpret_cast<uintptr_t>(vtbl[*idxEnter]),
                    reinterpret_cast<uintptr_t>(expectedEnter)
                );
            }
            return;
        }

        void* target = const_cast<void*>(vtbl[*idxIgnore]);
        if (!target || target == vtbl[*idxRenderGb] || target == vtbl[*idxEnter]) {
            mIgnoreAsTopAttempted.store(true, std::memory_order_release);
            if (mLogger) mLogger->warn("JavaChat: invalid UIScene::ignoreAsTop target address");
            return;
        }

        mIgnoreAsTopAttempted.store(true, std::memory_order_release);
        void*     origRaw = nullptr;
        int const rc      = ::ll::memory::hook(
            target,
            reinterpret_cast<::ll::memory::FuncPtr>(&ignoreAsTopDetour),
            reinterpret_cast<::ll::memory::FuncPtr*>(&origRaw),
            ::ll::memory::HookPriority::Normal
        );
        if (rc == 0 && origRaw != nullptr) {
            gOrigIgnoreAsTop.store(reinterpret_cast<IgnoreAsTopFn>(origRaw), std::memory_order_release);
            mIgnoreAsTopTarget = target;
            mIgnoreAsTopHookInstalled.store(true, std::memory_order_release);
            if (mLogger) {
                mLogger->info("JavaChat: keepHud hook ready (UIScene::ignoreAsTop)");
            }
        } else if (mLogger) {
            mLogger->warn("JavaChat: UIScene::ignoreAsTop hook failed (rc={})", rc);
        }
    } catch (...) {
        mIgnoreAsTopAttempted.store(true, std::memory_order_release);
    }
}

bool ChatBehaviorHooks::shouldTreatAsIgnoreTop(void const* scenePtr) const noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)
            || !mKeepHudVisibleInChat.load(std::memory_order_relaxed) || !scenePtr) {
            return false;
        }
        return static_cast<::UIScene const*>(scenePtr)->equalsScreenName("chat_screen");
    } catch (...) {
        return false;
    }
}

void ChatBehaviorHooks::setChatNavHandler(ChatNavHandler handler) noexcept {
    std::lock_guard lock(mNavMutex);
    mChatNavHandler = std::move(handler);
}

void ChatBehaviorHooks::setChatTextEditAfterHandler(ChatTextEditAfterHandler handler) noexcept {
    std::lock_guard lock(mNavMutex);
    mChatTextEditAfterHandler = std::move(handler);
}

void ChatBehaviorHooks::updateChatInputState(
    void*            displayTextComp,
    void*            chatController,
    InputVisualState state
) noexcept {
    try {
        mTrackedDisplayTextComp.store(displayTextComp, std::memory_order_release);
        mTrackedChatCtrl.store(chatController, std::memory_order_release);
        std::lock_guard lock(mInputVisualMutex);
        mInputVisualState = std::move(state);
    } catch (...) {
    }
}

void ChatBehaviorHooks::clearChatInputState() noexcept {
    try {
        mTrackedDisplayTextComp.store(nullptr, std::memory_order_release);
        mTrackedChatCtrl.store(nullptr, std::memory_order_release);
        std::lock_guard lock(mInputVisualMutex);
        mInputVisualState       = InputVisualState{};
        mCapturedGeom           = CapturedInputGeometry{};
        mLastObservedCaretAtEnd = true;
        mLastObservedText.clear();
    } catch (...) {
    }
}

CapturedInputGeometry ChatBehaviorHooks::consumeCapturedInputGeometry() noexcept {
    std::lock_guard lock(mInputVisualMutex);
    return mCapturedGeom;
}

void ChatBehaviorHooks::clearPendingAutoClose() noexcept {
    mPendingCloseCtrl.store(nullptr, std::memory_order_release);
    mPendingCloseDeadlineMs.store(0, std::memory_order_release);
}

void ChatBehaviorHooks::onScreenEntranceBefore(void const* scenePtr, bool& doScreenTransitions) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;
        ensureIgnoreAsTopHook(scenePtr);
        if (!mEnabled.load(std::memory_order_relaxed)) return;

        auto const* scene = static_cast<::UIScene const*>(scenePtr);
        if (!scene) return;

        bool const disableAnim = mDisableAnimations.load(std::memory_order_relaxed);
        bool const keepHud     = mKeepHudVisibleInChat.load(std::memory_order_relaxed)
                              && mIgnoreAsTopHookInstalled.load(std::memory_order_acquire);
        if (scene->equalsScreenName("chat_screen")) {
            if (disableAnim) {
                doScreenTransitions = false;
            }
            if (keepHud) {
                mSuppressedHudExit.store(true, std::memory_order_release);
            }
        } else if (scene->equalsScreenName("hud_screen")) {
            bool const exitedChat     = mJustExitedChat.exchange(false, std::memory_order_acq_rel);
            bool const skippedHudExit = mSuppressedHudExit.exchange(false, std::memory_order_acq_rel);
            if (exitedChat && skippedHudExit) {
                doScreenTransitions = false;
            }
        }
    } catch (...) {
    }
}

void ChatBehaviorHooks::onScreenExitBefore(
    void const* scenePtr,
    void const* pushedScenePtr,
    bool&       doScreenTransitions
) noexcept {
    try {
        if (!mShutdown.load(std::memory_order_relaxed)) {
            ensureIgnoreAsTopHook(scenePtr);
        }
        auto const* scene  = static_cast<::UIScene const*>(scenePtr);
        auto const* pushed = static_cast<::AbstractScene const*>(pushedScenePtr);
        if (!scene) return;

        bool const isChat = scene->equalsScreenName("chat_screen");
        if (isChat) {
            clearPendingAutoClose();
            clearChatInputState();
        }
        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)) return;

        bool const disableAnim = mDisableAnimations.load(std::memory_order_relaxed);
        if (isChat) {
            mJustExitedChat.store(true, std::memory_order_release);
            if (disableAnim) {
                doScreenTransitions = false;
            }
        } else if (scene->equalsScreenName("hud_screen")) {
            if (disableAnim && pushed && pushed->equalsScreenName("chat_screen")) {
                mSuppressedHudExit.store(true, std::memory_order_release);
                doScreenTransitions = false;
            } else {
                mSuppressedHudExit.store(false, std::memory_order_release);
            }
        }
    } catch (...) {
    }
}

std::optional<unsigned int> ChatBehaviorHooks::onChatNavButtonBefore(
    void*       controllerPtr,
    void const* screenEventPtr
) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)) {
            return std::nullopt;
        }
        if (!controllerPtr || controllerPtr != mTrackedChatCtrl.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        auto const& ev = *static_cast<::ScreenEvent const*>(screenEventPtr);
        if (ev.type != ::ScreenEventType::ButtonEvent) return std::nullopt;

        auto const& btn = ev.data.get().button.get();
        if (btn.state != ::ButtonState::Down) return std::nullopt;

        bool const isNavBtn = (btn.id == kButtonChatAutocompleteId) || (btn.id == kButtonChatAutocompleteBackId)
                           || (btn.id == kButtonChatPrevMsgId) || (btn.id == kButtonChatNextMsgId);
        if (!isNavBtn) return std::nullopt;

        ChatNavHandler handlerCopy;
        {
            std::lock_guard lock(mNavMutex);
            handlerCopy = mChatNavHandler;
        }
        if (!handlerCopy) return std::nullopt;

        auto*      chatCtrl   = static_cast<::ChatScreenController*>(controllerPtr);
        bool const isNewPress = (btn.previousState != ::ButtonState::Down);
        return handlerCopy(*chatCtrl, btn.id, isNewPress);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<unsigned int> ChatBehaviorHooks::onChatTextEditAfter(
    void* controllerPtr,
    int   textEditResult
) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)) {
            return std::nullopt;
        }
        if (!controllerPtr || controllerPtr != mTrackedChatCtrl.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        ChatTextEditAfterHandler handlerCopy;
        {
            std::lock_guard lock(mNavMutex);
            handlerCopy = mChatTextEditAfterHandler;
        }
        if (!handlerCopy) return std::nullopt;

        auto* chatCtrl = static_cast<::ChatScreenController*>(controllerPtr);
        return handlerCopy(*chatCtrl, textEditResult);
    } catch (...) {
        return std::nullopt;
    }
}

bool ChatBehaviorHooks::shouldHookDisplayText(void const* textCompPtr) const noexcept {
    if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)) return false;
    return textCompPtr != nullptr && textCompPtr == mTrackedDisplayTextComp.load(std::memory_order_acquire);
}

void ChatBehaviorHooks::processDisplayTextAfterRender(
    void*  textCompPtr,
    void*  renderContextPtr,
    size_t beforeCount,
    bool   caretFocused,
    bool   caretBlinkVisible
) noexcept {
    try {
        if (!shouldHookDisplayText(textCompPtr) || !renderContextPtr) return;
        auto& mcCtx = *static_cast<::MinecraftUIRenderContext*>(renderContextPtr);
        auto& items = mcCtx.mTextToDraw.get();
        if (items.size() != beforeCount + 1) return;

        auto& item = items.back();

        std::string_view rawSv;
        auto const&      textVar = item.mText.get();
        if (auto const* sp = std::get_if<std::string>(&textVar)) {
            rawSv = *sp;
        } else if (auto const* svp = std::get_if<std::string_view>(&textVar)) {
            rawSv = *svp;
        } else {
            return;
        }

        // Copy string & scalar state BEFORE calling mcCtx.drawText (which may reallocate `items`).
        std::string         plainCopy(rawSv);
        RectangleArea const origArea     = item.mArea.get();
        int const           origCaret    = item.mCaretLocation;
        Font* const         fontPtr      = &item.mFont;
        float const         fontScale    = item.mFontScale;
        mce::Color const    origColor    = item.mColor.get();
        float const         linePad      = item.mLinePadding;
        ::ui::TextAlignment align        = item.mAlignment;
        bool const          shadow       = item.mShadow;
        bool const          blinkVisible = caretFocused && caretBlinkVisible;

        size_t const     cpCount = countUtf8Codepoints(plainCopy);
        InputVisualState stateCopy;
        bool             atEnd = true;
        {
            std::lock_guard lock(mInputVisualMutex);
            stateCopy = mInputVisualState;

            size_t const prevCpCount   = countUtf8Codepoints(mLastObservedText);
            bool const   appendedAtEnd =
                mLastObservedCaretAtEnd && !mLastObservedText.empty()
                && plainCopy.size() > mLastObservedText.size()
                && plainCopy.starts_with(mLastObservedText);
            if (plainCopy != mLastObservedText) {
                mLastObservedText       = plainCopy;
                mLastObservedCaretAtEnd = true;
            }
            if (origCaret >= 0) {
                if (appendedAtEnd && static_cast<size_t>(origCaret) == prevCpCount) {
                    mLastObservedCaretAtEnd = true;
                } else {
                    mLastObservedCaretAtEnd = (static_cast<size_t>(origCaret) >= cpCount);
                }
            } else if (blinkVisible) {
                mLastObservedCaretAtEnd = true;
            }
            atEnd = mLastObservedCaretAtEnd;

            mCapturedGeom              = CapturedInputGeometry{};
            mCapturedGeom.valid        = true;
            mCapturedGeom.textOriginX  = origArea._x0;
            mCapturedGeom.textOriginY  = origArea._y0;
            mCapturedGeom.fontScale    = fontScale;
        }

        bool const isSelectAll =
            stateCopy.allSelected && !plainCopy.empty() && (plainCopy == stateCopy.expectedPlainText);
        if (isSelectAll && fontPtr) {
            float const selW  = static_cast<float>(fontPtr->getLineLength(plainCopy, fontScale, false));
            float const selX1 = std::min(origArea._x1, origArea._x0 + selW);
            float const selH  = std::max(9.0f, std::round(10.0f * fontScale));
            float const selY0 = origArea._y0 - 0.5f;
            float const selY1 = std::min(origArea._y1 + 1.0f, selY0 + selH);
            if (selX1 > origArea._x0) {
                std::lock_guard lock(mInputVisualMutex);
                mCapturedGeom.hasSelection  = true;
                mCapturedGeom.selectionRect = Rect{origArea._x0, selY0, selX1, selY1};
            }
            // Hide Bedrock's blinking caret while all text is selected (works for plain text too).
            item.mCaretLocation = -1;
        }

        bool const isSlashPlain =
            !plainCopy.empty() && plainCopy.front() == '/'
            && plainCopy.find("\xC2\xA7") == std::string::npos;
        if (!isSlashPlain || !fontPtr) return;

        std::string coloredToDraw;
        std::string ghostToDraw;
        if (plainCopy == stateCopy.expectedPlainText && stateCopy.enableSyntaxColor
            && !stateCopy.coloredText.empty()) {
            coloredToDraw = stateCopy.coloredText;
            if (stateCopy.enableGhost) {
                ghostToDraw = stateCopy.ghostSuffix;
            }
        } else {
            // 1-frame lag between ChatScreenController::mCurrentMessage and TextComponent::mLabel:
            // compute syntax color & ghost suffix on-the-fly for `plainCopy` so typing fast never flashes white.
            ActiveTokenSpan const plainSpan    = findActiveTokenSpan(plainCopy);
            ActiveTokenSpan const expectedSpan = findActiveTokenSpan(stateCopy.expectedPlainText);
            bool const            sameSlot = !stateCopy.expectedPlainText.empty()
                                          && plainSpan.isSlashCommand == expectedSpan.isSlashCommand
                                          && plainSpan.argIndex == expectedSpan.argIndex
                                          && plainSpan.startByte == expectedSpan.startByte;
            static const std::vector<std::string> kEmptyTokens;
            auto const& tokensForPlain = sameSlot ? stateCopy.suggestionTokens : kEmptyTokens;
            coloredToDraw              = formatJavaCommandSyntax(
                plainCopy, tokensForPlain, stateCopy.hasUsage, stateCopy.knownCommandValid
            );
            if (sameSlot) {
                if (!stateCopy.selectedToken.empty()) {
                    ghostToDraw = computeInlineGhostSuffix(plainCopy, stateCopy.selectedToken);
                }
                if (ghostToDraw.empty()) {
                    for (std::string const& tok : stateCopy.suggestionTokens) {
                        std::string cand = computeInlineGhostSuffix(plainCopy, tok);
                        if (!cand.empty()) {
                            ghostToDraw = std::move(cand);
                            break;
                        }
                    }
                }
            }
        }

        if (!coloredToDraw.empty()) {
            item.mText.get()    = std::string{};
            item.mCaretLocation = -1;

            mce::Color const baseRgb(origColor.r, origColor.g, origColor.b, 1.0f);
            mcCtx.drawText(
                *fontPtr,
                origArea,
                std::move(coloredToDraw),
                baseRgb,
                origColor.a,
                align,
                TextMeasureData{fontScale, linePad, shadow, false, false, align},
                CaretMeasureData{0, false}
            );

            float const typedW = static_cast<float>(fontPtr->getLineLength(plainCopy, fontScale, false));
            if (!isSelectAll && !ghostToDraw.empty() && caretFocused && atEnd) {
                RectangleArea ghostArea = origArea;
                ghostArea._x0           = origArea._x0 + typedW;
                if (ghostArea._x0 < ghostArea._x1 - 2.0f) {
                    mcCtx.drawText(
                        *fontPtr,
                        ghostArea,
                        std::move(ghostToDraw),
                        mce::Color(128, 128, 128),
                        origColor.a,
                        align,
                        TextMeasureData{fontScale, linePad, shadow, false, false, align},
                        CaretMeasureData{0, false}
                    );
                }
            }

            bool const caretAtEndNow = atEnd || (origCaret < 0) || (static_cast<size_t>(origCaret) >= cpCount);
            if (!isSelectAll && blinkVisible) {
                if (!caretAtEndNow && origCaret >= 0 && static_cast<size_t>(origCaret) < cpCount) {
                    std::string_view prefixSv = prefixUtf8Codepoints(plainCopy, static_cast<size_t>(origCaret));
                    float const      caretX =
                        origArea._x0 + static_cast<float>(fontPtr->getLineLength(prefixSv, fontScale, false));
                    float const caretH = std::max(8.0f, std::round(9.0f * fontScale));
                    std::lock_guard lock(mInputVisualMutex);
                    mCapturedGeom.hasMidCaret  = true;
                    mCapturedGeom.midCaretRect = Rect{caretX, origArea._y0, caretX + 1.0f, origArea._y0 + caretH};
                } else {
                    RectangleArea caretArea = origArea;
                    caretArea._x0           = origArea._x0 + typedW;
                    if (caretArea._x0 < caretArea._x1 - 1.0f) {
                        mcCtx.drawText(
                            *fontPtr,
                            caretArea,
                            std::string("_"),
                            mce::Color(224, 224, 224),
                            origColor.a,
                            align,
                            TextMeasureData{fontScale, linePad, shadow, false, false, align},
                            CaretMeasureData{0, false}
                        );
                    }
                }
            }
        }
    } catch (...) {
    }
}

bool ChatBehaviorHooks::isSendTriggerEvent(void const* screenEventPtr) const noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)) return false;
        if (!mAutoCloseOnSend.load(std::memory_order_relaxed)) return false;
        auto const& ev = *static_cast<::ScreenEvent const*>(screenEventPtr);
        if (ev.type == ::ScreenEventType::TextEditChange) {
            auto const& te = ev.data.get().textEdit.get();
            return te.result == ::TextCharEventResult::EnterPressed;
        }
        if (ev.type == ::ScreenEventType::ButtonEvent) {
            auto const& btn = ev.data.get().button.get();
            return btn.id == kButtonSendId && btn.state == ::ButtonState::Down
                && btn.previousState != ::ButtonState::Down;
        }
    } catch (...) {
    }
    return false;
}

void ChatBehaviorHooks::onAfterSendTrigger(void* controllerPtr, unsigned int viewRequestFlags) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)) return;
        if (!mAutoCloseOnSend.load(std::memory_order_relaxed)) return;
        if (!controllerPtr) return;
        if ((viewRequestFlags & static_cast<unsigned int>(::ui::ViewRequest::Exit)) != 0) return;

        auto clientRef = ll::service::getClientInstance();
        if (!clientRef) return;
        auto& client = clientRef.value();
        auto  stack  = client.getCurrentSceneStack();
        if (!stack.get().get()) return;
        ::AbstractScene* topScene = stack->getTopScene();
        if (!topScene || !topScene->equalsScreenName("chat_screen")) return;

        auto* uiScene = static_cast<::UIScene*>(topScene);
        if (!uiScene->mScreenView) return;
        if (uiScene->mScreenView->mController.get() != controllerPtr) return;

        auto* chatCtrl = static_cast<::ChatScreenController*>(controllerPtr);
        if (chatCtrl->mIsDevConsole) return;
        if (chatCtrl->mLeaveScreen.get().mShouldLeave) return;

        if (chatCtrl->mSendMessage) {
            // Bedrock deferred _sendChatMessage() to ChatScreenController::tick(); close once sent.
            mPendingCloseDeadlineMs.store(steadyNowMs() + 1000, std::memory_order_release);
            mPendingCloseCtrl.store(controllerPtr, std::memory_order_release);
        } else {
            chatCtrl->leaveScreen("chat_screen");
        }
    } catch (...) {
    }
}

void ChatBehaviorHooks::pollPendingAutoClose(ScreenView const& screenView) noexcept {
    try {
        void* pending = mPendingCloseCtrl.load(std::memory_order_acquire);
        if (!pending) return;

        if (mShutdown.load(std::memory_order_relaxed) || !mEnabled.load(std::memory_order_relaxed)
            || !mAutoCloseOnSend.load(std::memory_order_relaxed)) {
            clearPendingAutoClose();
            return;
        }
        if (steadyNowMs() > mPendingCloseDeadlineMs.load(std::memory_order_acquire)) {
            clearPendingAutoClose();
            return;
        }
        if (screenView.mController.get() != pending) {
            clearPendingAutoClose();
            return;
        }

        auto* chatCtrl = static_cast<::ChatScreenController*>(pending);
        if (chatCtrl->mSendMessage) {
            return; // Wait until tick() dispatches _sendChatMessage().
        }
        clearPendingAutoClose();
        if (!chatCtrl->mIsDevConsole && !chatCtrl->mLeaveScreen.get().mShouldLeave) {
            chatCtrl->leaveScreen("chat_screen");
        }
    } catch (...) {
        clearPendingAutoClose();
    }
}

std::string ChatBehaviorHooks::statusSummary() const {
    char const* keepHudState = isKeepHudHookInstalled()
                                 ? "ready"
                                 : (mIgnoreAsTopAttempted.load(std::memory_order_relaxed) ? "off" : "pending");
    return std::string("autoClose=") + (isAutoCloseHookInstalled() ? "ready" : "off")
         + ", noAnim=" + (areAnimHooksInstalled() ? "ready" : "off")
         + ", syntax=" + (isTextRenderHookInstalled() ? "ready" : "off")
         + ", keepHud=" + keepHudState;
}

} // namespace javachat
