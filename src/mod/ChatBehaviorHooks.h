#pragma once

#include "core/ChatHistory.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ChatScreenController;
class ScreenController;
class ScreenView;

namespace Json {
class Value;
}

namespace ll::io {
class Logger;
}

namespace javachat {

struct InputVisualState {
    std::string              expectedPlainText;
    std::string              coloredText;
    std::string              ghostSuffix;
    std::vector<std::string> suggestionTokens;
    std::string              selectedToken;
    bool                     hasUsage          = false;
    bool                     knownCommandValid = false;
    bool                     enableSyntaxColor = false;
    bool                     enableGhost       = false;
    bool                     allSelected       = false;
};

struct CapturedInputGeometry {
    bool  valid        = false;
    float textOriginX  = 4.0f;
    float textOriginY  = 0.0f;
    float fontScale    = 1.0f;
    bool  hasMidCaret  = false;
    Rect  midCaretRect;
    bool  hasSelection = false;
    Rect  selectionRect;
};

// Hooks Java Edition chat behaviors using MCAPI/MCFOLD targets (and verified UIScene vtable):
//  1. UIScene::$onScreenEntrance / $onScreenExit: disable slide/fade transitions when opening/closing chat.
//  2. ScreenController::$handleEvent: auto-close chat_screen after send + intercept Tab/Up/Down navigation + post-edit sync.
//  3. TextComponent::$render: apply Java command syntax coloring, inline ghost completion, and underscore/select-all caret.
//  4. UIScene::ignoreAsTop (resolved from verified UIScene vtable): keep hud_screen visible behind chat_screen.
class ChatBehaviorHooks {
public:
    using ChatNavHandler =
        std::function<std::optional<unsigned int>(::ChatScreenController&, uint32_t buttonId, bool isNewPress)>;
    using ChatTextEditAfterHandler =
        std::function<std::optional<unsigned int>(::ChatScreenController&, int textEditResult)>;
    using ChatSearchExitHandler = std::function<void(::ChatScreenController&)>;

    static ChatBehaviorHooks& instance() noexcept;

    bool install(
        ll::io::Logger* logger,
        bool            enabled,
        bool            autoCloseOnSend,
        bool            disableAnimations,
        bool            keepHudVisibleInChat
    ) noexcept;
    void uninstall() noexcept;

    void setEnabled(bool enabled) noexcept;
    bool isShutdown() const noexcept { return mShutdown.load(std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return mEnabled.load(std::memory_order_relaxed); }
    void requestCloseCurrentChatOnce() noexcept;
    void setAutoCloseOnSend(bool enabled) noexcept;
    void setDisableAnimations(bool disabled) noexcept;
    void setKeepHudVisibleInChat(bool keep) noexcept;
    void setSearchMode(bool active) noexcept;

    void setChatNavHandler(ChatNavHandler handler) noexcept;
    void setChatTextEditAfterHandler(ChatTextEditAfterHandler handler) noexcept;
    void setChatSearchExitHandler(ChatSearchExitHandler handler) noexcept;
    void updateChatInputState(
        void*            displayTextComp,
        void*            chatController,
        InputVisualState state,
        void const*      expectedRenderCtxVtbl = nullptr
    ) noexcept;
    void clearChatInputState() noexcept;
    CapturedInputGeometry consumeCapturedInputGeometry() noexcept;

    void pollPendingAutoClose(ScreenView const& screenView) noexcept;
    void clearPendingAutoClose() noexcept;

    bool isAutoCloseHookInstalled() const noexcept { return mHandleEventHookInstalled.load(std::memory_order_relaxed); }
    bool isTextRenderHookInstalled() const noexcept { return mTextRenderHookInstalled.load(std::memory_order_relaxed); }
    bool isKeepHudHookInstalled() const noexcept { return mIgnoreAsTopHookInstalled.load(std::memory_order_relaxed); }
    bool isCreateSceneHookInstalled() const noexcept { return mCreateSceneHookInstalled.load(std::memory_order_relaxed); }
    bool isHudCtorHookInstalled() const noexcept { return mHudCtorHookInstalled.load(std::memory_order_relaxed); }
    bool isHudBindingReady() const noexcept { return mHudBindingReady.load(std::memory_order_acquire); }
    bool isChatStaticVarsHookInstalled() const noexcept { return mChatStaticVarsHookInstalled.load(std::memory_order_relaxed); }
    bool areAnimHooksInstalled() const noexcept {
        return mEntranceHookInstalled.load(std::memory_order_relaxed)
            && mExitHookInstalled.load(std::memory_order_relaxed);
    }
    std::string statusSummary() const;

    // Hook callbacks (public for hook trampolines).
    void                       onHudControllerCreated(::ScreenController* controller, bool fromCtor = false) noexcept;
    void                       onCreateUISceneBefore(std::string const& screenName, std::shared_ptr<::ScreenController> const& controller) noexcept;
    void                       onChatAddStaticScreenVarsAfter(::ScreenController const* ctrl, ::Json::Value& globalVars) const noexcept;
    void                       onScreenEntranceBefore(void const* scenePtr, bool& doScreenTransitions) noexcept;
    void                       onScreenExitBefore(void const* scenePtr, void const* pushedScenePtr, bool& doScreenTransitions) noexcept;
    bool                       shouldTreatAsIgnoreTop(void const* scenePtr) const noexcept;
    bool                        isSendTriggerEvent(void const* screenEventPtr) const noexcept;
    void                        onAfterSendTrigger(void* controllerPtr, unsigned int viewRequestFlags) noexcept;
    std::optional<unsigned int> onChatSearchEventBefore(void* controllerPtr, void const* screenEventPtr) noexcept;
    std::optional<unsigned int> onChatNavButtonBefore(void* controllerPtr, void const* screenEventPtr) noexcept;
    std::optional<unsigned int> onChatTextEditAfter(void* controllerPtr, int textEditResult) noexcept;

    bool shouldHookDisplayText(void const* textCompPtr, void const* renderContextPtr = nullptr) const noexcept;
    void processDisplayTextAfterRender(
        void*  textCompPtr,
        void*  renderContextPtr,
        size_t beforeCount,
        bool   caretFocused,
        bool   caretBlinkVisible
    ) noexcept;

private:
    ChatBehaviorHooks() = default;
    void ensureIgnoreAsTopHook(void const* scenePtr) noexcept;
    void ensureChatStaticVarsHook(::ScreenController const* ctrl) noexcept;

    ll::io::Logger*        mLogger = nullptr;
    std::atomic<bool>      mShutdown{false};
    std::atomic<bool>      mEnabled{true};
    std::atomic<bool>        mAutoCloseOnSend{true};
    std::atomic<bool>        mDisableAnimations{true};
    std::atomic<bool>        mKeepHudVisibleInChat{true};
    std::atomic<bool>        mSearchMode{false};
    std::atomic<bool>        mEntranceHookInstalled{false};
    std::atomic<bool>        mExitHookInstalled{false};
    std::atomic<bool>        mHandleEventHookInstalled{false};
    std::atomic<bool>        mTextRenderHookInstalled{false};
    std::atomic<bool>        mCreateSceneHookInstalled{false};
    std::atomic<bool>        mHudCtorHookInstalled{false};
    std::atomic<bool>        mHudBindingReady{false};
    std::atomic<void const*> mLastBoundHudCtrl{nullptr};
    std::atomic<bool>        mIgnoreAsTopHookInstalled{false};
    std::atomic<bool>        mIgnoreAsTopAttempted{false};
    std::atomic<bool>        mLoggedAnchorMismatch{false};
    std::mutex               mIgnoreAsTopMutex;
    void*                    mIgnoreAsTopTarget = nullptr;
    std::atomic<bool>        mChatStaticVarsHookInstalled{false};
    std::atomic<bool>        mChatStaticVarsAttempted{false};
    std::atomic<bool>        mLoggedChatStaticVarsMismatch{false};
    std::atomic<void const*> mChatStaticVarsVtbl{nullptr};
    std::atomic<bool>        mLoggedHudBindingReady{false};
    std::atomic<bool>        mLoggedSceneNameDiag{false};
    std::mutex               mChatStaticVarsMutex;
    void*                    mChatStaticVarsTarget = nullptr;
    std::atomic<bool>        mJustExitedChat{false};
    std::atomic<bool>        mSuppressedHudExit{false};
    std::atomic<bool>        mForceCloseOnce{false};
    std::atomic<void*>       mPendingCloseCtrl{nullptr};
    std::atomic<long long>   mPendingCloseDeadlineMs{0};

    std::atomic<void*>       mTrackedDisplayTextComp{nullptr};
    std::atomic<void*>       mTrackedChatCtrl{nullptr};
    std::atomic<void const*> mTrackedRenderCtxVtbl{nullptr};
    std::mutex               mNavMutex;
    ChatNavHandler           mChatNavHandler;
    ChatTextEditAfterHandler mChatTextEditAfterHandler;
    ChatSearchExitHandler    mChatSearchExitHandler;
    std::mutex               mInputVisualMutex;
    InputVisualState         mInputVisualState;
    CapturedInputGeometry    mCapturedGeom;
    bool                     mLastObservedCaretAtEnd = true;
    std::string              mLastObservedText;
};

} // namespace javachat
