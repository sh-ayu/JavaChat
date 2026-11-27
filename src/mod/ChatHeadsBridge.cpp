#include "mod/ChatHeadsBridge.h"

#include "mod/ChatHeadsApi.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace javachat {
namespace {

constexpr long long kProbeIntervalMs = 3000;

long long nowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <class Fn>
Fn as(void* p) noexcept {
    return reinterpret_cast<Fn>(p);
}

} // namespace

ChatHeadsBridge& ChatHeadsBridge::instance() noexcept {
    static ChatHeadsBridge inst;
    return inst;
}

bool ChatHeadsBridge::bind() noexcept {
    if (mBound) return true;
    long long const t = nowMs();
    if (t < mNextProbeMs) return false;
    mNextProbeMs = t + kProbeIntervalMs;

    // Pin the module so it can never unload while we hold its function pointers.
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(0, L"ChatHeads.dll", &mod) || !mod) return false;

    auto version = reinterpret_cast<ChatHeads_ApiVersion_t>(GetProcAddress(mod, "ChatHeads_ApiVersion"));
    if (!version || version() < CHATHEADS_API_VERSION) {
        FreeLibrary(mod);
        mNextProbeMs = t + 60'000; // incompatible build; don't spin
        return false;
    }
    mIsEnabled = reinterpret_cast<void*>(GetProcAddress(mod, "ChatHeads_IsEnabled"));
    mResolve   = reinterpret_cast<void*>(GetProcAddress(mod, "ChatHeads_ResolveSender"));
    mDraw      = reinterpret_cast<void*>(GetProcAddress(mod, "ChatHeads_DrawHead"));
    mHeadSize  = reinterpret_cast<void*>(GetProcAddress(mod, "ChatHeads_HeadSize"));
    mHeadGap   = reinterpret_cast<void*>(GetProcAddress(mod, "ChatHeads_HeadGap"));
    if (!mIsEnabled || !mResolve || !mDraw || !mHeadSize || !mHeadGap) {
        FreeLibrary(mod);
        mIsEnabled = mResolve = mDraw = mHeadSize = mHeadGap = nullptr;
        mNextProbeMs = t + 60'000;
        return false;
    }
    mModule = mod;
    mBound  = true;
    return true;
}

bool ChatHeadsBridge::installed() noexcept { return bind() || GetModuleHandleW(L"ChatHeads.dll") != nullptr; }

bool ChatHeadsBridge::available() noexcept {
    if (!bind()) return false;
    return as<ChatHeads_IsEnabled_t>(mIsEnabled)() != 0;
}

std::string ChatHeadsBridge::resolve(
    int              messageType,
    std::string_view username,
    std::string_view xuid,
    std::string_view text
) noexcept {
    if (!available()) return {};
    char   buf[128];
    size_t n = as<ChatHeads_ResolveSender_t>(mResolve)(
        messageType,
        username.data(),
        username.size(),
        xuid.data(),
        xuid.size(),
        text.data(),
        text.size(),
        buf,
        sizeof(buf)
    );
    if (n == 0 || n >= sizeof(buf)) return {};
    try {
        return std::string(buf, n);
    } catch (...) {
        return {};
    }
}

bool ChatHeadsBridge::draw(
    MinecraftUIRenderContext& ctx,
    std::string_view          key,
    float                     x,
    float                     y,
    float                     size,
    float                     alpha
) noexcept {
    if (key.empty() || !mBound) return false;
    return as<ChatHeads_DrawHead_t>(mDraw)(&ctx, key.data(), key.size(), x, y, size, alpha) != 0;
}

float ChatHeadsBridge::headSize() noexcept {
    if (!mBound) return 8.0f;
    float v = as<ChatHeads_HeadSize_t>(mHeadSize)();
    return std::isfinite(v) ? std::clamp(v, 4.0f, 32.0f) : 8.0f;
}

float ChatHeadsBridge::headGap() noexcept {
    if (!mBound) return 2.0f;
    float v = as<ChatHeads_HeadGap_t>(mHeadGap)();
    return std::isfinite(v) ? std::clamp(v, 0.0f, 16.0f) : 2.0f;
}

} // namespace javachat
