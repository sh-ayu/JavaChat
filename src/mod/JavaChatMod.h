#pragma once

#include "core/ChatConfig.h"
#include "core/ChatHistory.h"
#include "core/CommandSuggestions.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <ll/api/event/ListenerBase.h>

class ChatScreenController;
class ScreenView;
class UIControl;

namespace ll::mod {
class NativeMod;
}

namespace ll::event {
inline namespace render {
class BeforeUIRenderEvent;
class AfterUIRenderEvent;
class UIRenderEvent;
}
inline namespace input {
class KeyInputEvent;
class MouseInputEvent;
}
} // namespace ll::event

namespace javachat {

class JavaChatMod {
public:
    static JavaChatMod& instance() noexcept;

    bool load();
    bool enable();
    bool disable();

    // Runtime controls (used by /jchat commands).
    std::string             describeStatus();
    std::string             describeHelp() const;
    bool                    toggleEnabled();
    void                    clearHistory();
    bool                    reloadConfig(std::string& messageOut);
    void                    resetConfig();
    float                   setWidth(float width);
    std::pair<float, float> setHeight(float closedH, float openH);
    float                   setFontScale(float scale);
    float                   setLineSpacing(float spacing);
    float                   setBackgroundOpacity(float opacity);
    float                   setTextOpacity(float opacity);
    float                   setFadeDelay(float delaySec);
    std::pair<float, float> setFade(float delaySec, float durationSec);
    void                    setTextShadow(bool enabled);
    std::pair<float, float> setPosition(float leftOffset, float bottomOffset);
    void                    setCompactDuplicates(bool enabled);
    void                    setShowTimestamps(bool enabled);
    void                    setClickToCopy(bool enabled);
    void                    setClickToOpenUrl(bool enabled);
    void                    setAutoCloseOnSend(bool enabled);
    void                    setDisableChatAnimations(bool disabled);
    void                    setKeepHudVisibleInChat(bool keep);
    void                    setClearHistoryOnWorldSwitch(bool enabled);
    bool                    setFont(std::string const& spec, std::string& messageOut);
    std::string             describeFonts();

private:
    JavaChatMod() = default;

    void onBeforeUiRender(ll::event::render::BeforeUIRenderEvent& event) noexcept;
    void onAfterUiRender(ll::event::render::AfterUIRenderEvent& event) noexcept;
    void renderChatLayer(ll::event::render::UIRenderEvent& event, bool isBeforePass) noexcept;
    void renderChatOverlayPass(ll::event::render::AfterUIRenderEvent& event) noexcept;
    void onMouseInput(ll::event::input::MouseInputEvent& event) noexcept;
    void onKeyInput(ll::event::input::KeyInputEvent& event) noexcept;
    void onWorldTransition() noexcept;
    std::optional<unsigned int>
    handleChatNavButton(ChatScreenController& chatCtrl, uint32_t buttonId, bool isNewPress) noexcept;
    std::optional<unsigned int>
    handleChatTextEditAfter(ChatScreenController& chatCtrl, int textEditResult) noexcept;

    void clearInputSelectionStateLocked() noexcept;
    void showToastLocked(std::string text, double nowSec);
    void saveConfigLocked() noexcept;

    std::shared_ptr<ll::mod::NativeMod> mSelf;
    std::filesystem::path               mConfigPath;

    ll::event::ListenerPtr mBeforeRenderListener;
    ll::event::ListenerPtr mRenderListener;
    ll::event::ListenerPtr mMouseListener;
    ll::event::ListenerPtr mKeyListener;
    ll::event::ListenerPtr mCommandListener;
    ll::event::ListenerPtr mStartJoinListener;
    ll::event::ListenerPtr mJoinListener;
    ll::event::ListenerPtr mExitListener;

    struct PendingInputEdit {
        std::string                text;
        bool                       refreshIntellisense = true;
        int                        caretPos            = -1;
        std::optional<std::string> clearFromSnapshot;
    };

    std::mutex                      mMutex;
    ChatConfig                      mConfig;
    ChatHistory                     mHistory;
    ChatViewLayout                  mLastView;
    CommandSuggestionState          mSuggestions;
    SuggestionPopupLayout           mLastSuggestionLayout;
    std::optional<PendingInputEdit> mPendingInputEdit;
    std::optional<int>              mPendingCaretPos;
    std::string                     mLastObservedInput;
    bool                            mInputAllSelected = false;
    std::string                     mSelectedTextSnapshot;
    float                           mLastHoverMouseX         = -1.0f;
    float                      mLastHoverMouseY         = -1.0f;
    bool                       mDraggingScrollbar       = false;
    float                      mLastScreenW             = 0.0f;
    float                      mLastScreenH             = 0.0f;
    bool                       mWasChatOpen             = false;
    std::string                mToastText;
    double                     mToastUntilSec           = 0.0;
    void const*                mLastLevelPtr            = nullptr;
    ScreenView const*          mCachedDisplayTextScreen = nullptr;
    std::weak_ptr<UIControl>   mCachedDisplayTextCtrl;

    std::atomic<bool>      mShutdown{false};
    std::atomic<bool>      mChatOpen{false};
    std::atomic<long long> mLastChatOpenMs{0};
};

} // namespace javachat
