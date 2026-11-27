#pragma once

#include <string>
#include <string_view>

class MinecraftUIRenderContext;

namespace javachat {

// Optional runtime integration with the ChatHeads mod (github.com/sh-ayu/ChatHeads).
// No link-time dependency: ChatHeads.dll is discovered with GetModuleHandle/GetProcAddress.
// Client/UI thread only.
class ChatHeadsBridge {
public:
    static ChatHeadsBridge& instance() noexcept;

    // True when ChatHeads is loaded, API-compatible and enabled by the user.
    bool available() noexcept;
    // True when ChatHeads.dll is loaded at all (for /jchat status).
    bool installed() noexcept;

    std::string resolve(int messageType, std::string_view username, std::string_view xuid, std::string_view text) noexcept;
    bool        draw(MinecraftUIRenderContext& ctx, std::string_view key, float x, float y, float size, float alpha) noexcept;
    float       headSize() noexcept;
    float       headGap() noexcept;

private:
    ChatHeadsBridge() = default;
    bool bind() noexcept;

    void*     mModule        = nullptr;
    bool      mBound         = false;
    long long mNextProbeMs   = 0;
    void*     mIsEnabled     = nullptr;
    void*     mResolve       = nullptr;
    void*     mDraw          = nullptr;
    void*     mHeadSize      = nullptr;
    void*     mHeadGap       = nullptr;
};

} // namespace javachat
