#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace ll::io {
class Logger;
}

namespace javachat {

enum class PackPinMode : int {
    PinTop      = 0,
    Passthrough = 1,
    Strip       = 2,
};

constexpr PackPinMode computePackPinMode(bool modEnabled, bool overrideServerChatUi) noexcept {
    if (!modEnabled) return PackPinMode::Strip;
    return overrideServerChatUi ? PackPinMode::PinTop : PackPinMode::Passthrough;
}

// Pins (or strips when disabled) the bundled JavaChatUI resource pack during
// ResourcePackManager::composeFullStack so vanilla or server UIs behave cleanly.
class PackPinner {
public:
    static PackPinner& instance() noexcept;

    bool install(ll::io::Logger* logger, std::string uiPackUuid, PackPinMode mode, bool verbose) noexcept;
    void uninstall() noexcept;

    void setMode(PackPinMode mode) noexcept;
    void setVerbose(bool verbose) noexcept;

    bool        isHookInstalled() const noexcept { return mHookInstalled.load(std::memory_order_relaxed); }
    bool        isPinnedLastCompose() const noexcept { return mPinnedLast.load(std::memory_order_relaxed); }
    bool        isPinned() const noexcept { return mPinnedLast.load(std::memory_order_relaxed); }
    bool        isStrippedLastCompose() const noexcept { return mStrippedLast.load(std::memory_order_relaxed); }
    bool        hasFailedLatch() const noexcept { return mFailedLatch.load(std::memory_order_relaxed); }
    std::string statusSummary() const;

    // Called from hook body.
    void onAfterCompose(void const* mgrVoid, void* outputStackVoid) noexcept;

private:
    PackPinner() = default;

    mutable std::mutex mConfigMutex;
    ll::io::Logger*    mLogger = nullptr;
    std::string        mTargetUuidLower;
    std::atomic<bool>  mHookInstalled{false};
    std::atomic<int>   mMode{static_cast<int>(PackPinMode::PinTop)};
    std::atomic<bool>  mVerbose{false};
    std::atomic<bool>  mComposeObserved{false};
    std::atomic<bool>  mPinnedLast{false};
    std::atomic<bool>  mStrippedLast{false};
    std::atomic<bool>  mPackFoundLast{false};
    std::atomic<bool>  mFailedLatch{false};
    std::atomic<bool>  mShutdown{false};
};

} // namespace javachat
