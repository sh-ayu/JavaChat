#pragma once

#include <atomic>
#include <string>

namespace ll::io {
class Logger;
}

namespace javachat {

// Pins the bundled JavaChatUI resource pack to index 0 (highest priority) during
// ResourcePackManager::composeFullStack so server-distributed packs cannot override
// our lightweight chat_screen.json / hud_screen.json.
class PackPinner {
public:
    static PackPinner& instance() noexcept;

    bool install(ll::io::Logger* logger, std::string uiPackUuid, bool enabled, bool verbose) noexcept;
    void uninstall() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setVerbose(bool verbose) noexcept;

    bool        isHookInstalled() const noexcept { return mHookInstalled.load(std::memory_order_relaxed); }
    bool        isPinnedLastCompose() const noexcept { return mPinnedLast.load(std::memory_order_relaxed); }
    bool        isPinned() const noexcept { return mPinnedLast.load(std::memory_order_relaxed); }
    bool        hasFailedLatch() const noexcept { return mFailedLatch.load(std::memory_order_relaxed); }
    std::string statusSummary() const;

    // Called from hook body.
    void onAfterCompose(void const* mgrVoid, void* outputStackVoid) noexcept;

private:
    PackPinner() = default;

    ll::io::Logger*   mLogger = nullptr;
    std::string       mTargetUuidLower;
    std::atomic<bool> mHookInstalled{false};
    std::atomic<bool> mEnabled{true};
    std::atomic<bool> mVerbose{false};
    std::atomic<bool> mPinnedLast{false};
    std::atomic<bool> mPackFoundLast{false};
    std::atomic<bool> mFailedLatch{false};
    std::atomic<bool> mShutdown{false};
};

} // namespace javachat
