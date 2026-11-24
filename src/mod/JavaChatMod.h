#pragma once

#include "core/ChatConfig.h"
#include "core/ChatHistory.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <ll/api/event/ListenerBase.h>

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
    void                    setTextShadow(bool enabled);
    std::pair<float, float> setPosition(float leftOffset, float bottomOffset);
    void                    setCompactDuplicates(bool enabled);
    void                    setShowTimestamps(bool enabled);
    void                    setClickToCopy(bool enabled);
    void                    setClickToOpenUrl(bool enabled);

private:
    JavaChatMod() = default;

    void onBeforeUiRender(ll::event::render::BeforeUIRenderEvent& event) noexcept;
    void onAfterUiRender(ll::event::render::AfterUIRenderEvent& event) noexcept;
    void renderChatLayer(ll::event::render::UIRenderEvent& event, bool isBeforePass) noexcept;
    void onMouseInput(ll::event::input::MouseInputEvent& event) noexcept;
    void onKeyInput(ll::event::input::KeyInputEvent& event) noexcept;
    void onWorldTransition() noexcept;

    void showToastLocked(std::string text, double nowSec);
    void saveConfigLocked() noexcept;

    std::shared_ptr<ll::mod::NativeMod> mSelf;
    std::filesystem::path               mConfigPath;

    ll::event::ListenerPtr mBeforeRenderListener;
    ll::event::ListenerPtr mRenderListener;
    ll::event::ListenerPtr mMouseListener;
    ll::event::ListenerPtr mKeyListener;
    ll::event::ListenerPtr mCommandListener;
    ll::event::ListenerPtr mJoinListener;
    ll::event::ListenerPtr mExitListener;

    std::mutex     mMutex;
    ChatConfig     mConfig;
    ChatHistory    mHistory;
    ChatViewLayout mLastView;
    Rect           mLastAutocompleteRect;
    bool           mHasAutocomplete   = false;
    bool           mDraggingScrollbar = false;
    float          mLastScreenW       = 0.0f;
    float          mLastScreenH     = 0.0f;
    bool           mWasChatOpen  = false;
    std::string    mToastText;
    double         mToastUntilSec = 0.0;

    std::atomic<bool>      mShutdown{false};
    std::atomic<bool>      mChatOpen{false};
    std::atomic<long long> mLastChatOpenMs{0};
};

} // namespace javachat
