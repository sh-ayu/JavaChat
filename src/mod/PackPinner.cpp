#include "mod/PackPinner.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ll/api/io/Logger.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/service/TargetedBedrock.h>

#include <mc/client/game/ClientInstance.h>
#include <mc/deps/core/resource/PackIdVersion.h>
#include <mc/deps/core/resource/PackType.h>
#include <mc/deps/core/utility/NonOwnerPointer.h>
#include <mc/platform/UUID.h>
#include <mc/resources/IResourcePackRepository.h>
#include <mc/resources/Pack.h>
#include <mc/resources/PackInstance.h>
#include <mc/resources/PackSettingsFactory.h>
#include <mc/resources/ResourcePack.h>
#include <mc/resources/ResourcePackManager.h>
#include <mc/resources/ResourcePackStack.h>
#include <mc/scripting/modules/PackManifest.h>

namespace javachat {
namespace {

std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool readEntryUuid(PackInstance const& pi, std::string& uuidOut) {
    try {
        ::ResourcePack const* rp = pi.mPack.get().get();
        if (!rp) return false;
        auto const& impl = rp->mImpl.get();
        if (!impl || !impl->mPack.get()) return false;
        auto const* manifest = impl->mPack.get()->mManifest.get();
        if (!manifest) return false;
        uuidOut = toLowerAscii(manifest->mIdentity->mId->asString());
        return true;
    } catch (...) {
        return false;
    }
}

LL_TYPE_INSTANCE_HOOK(
    JavaChatComposeHook,
    ::ll::memory::HookPriority::High,
    ResourcePackManager,
    &ResourcePackManager::composeFullStack,
    int,
    ResourcePackStack&       output,
    ResourcePackStack const& globalStack,
    ResourcePackStack const& levelStack
) {
    thread_local bool reentered = false;
    bool              outer     = !reentered;
    if (outer) reentered = true;

    int result = 0;
    try {
        result = origin(output, globalStack, levelStack);
    } catch (...) {
        if (outer) reentered = false;
        throw;
    }

    if (outer) {
        PackPinner::instance().onAfterCompose(this, &output);
        reentered = false;
    }
    return result;
}

} // namespace

PackPinner& PackPinner::instance() noexcept {
    static PackPinner inst;
    return inst;
}

bool PackPinner::install(ll::io::Logger* logger, std::string uiPackUuid, bool enabled, bool verbose) noexcept {
    mLogger          = logger;
    mTargetUuidLower = toLowerAscii(std::move(uiPackUuid));
    mShutdown.store(false, std::memory_order_relaxed);
    mEnabled.store(enabled, std::memory_order_relaxed);
    mVerbose.store(verbose, std::memory_order_relaxed);
    mFailedLatch.store(false, std::memory_order_relaxed);

    if (mHookInstalled.load(std::memory_order_relaxed)) return true;
    try {
        bool ok = (JavaChatComposeHook::hook() == 0);
        mHookInstalled.store(ok, std::memory_order_relaxed);
        return ok;
    } catch (...) {
        mHookInstalled.store(false, std::memory_order_relaxed);
        return false;
    }
}

void PackPinner::uninstall() noexcept {
    mShutdown.store(true, std::memory_order_relaxed);
    if (mHookInstalled.exchange(false, std::memory_order_relaxed)) {
        try {
            JavaChatComposeHook::unhook();
        } catch (...) {
        }
    }
}

void PackPinner::setEnabled(bool enabled) noexcept {
    mEnabled.store(enabled, std::memory_order_relaxed);
}

void PackPinner::setVerbose(bool verbose) noexcept {
    mVerbose.store(verbose, std::memory_order_relaxed);
}

std::string PackPinner::statusSummary() const {
    if (!mHookInstalled.load(std::memory_order_relaxed)) return "hook=off";
    if (mFailedLatch.load(std::memory_order_relaxed)) return "hook=latched_failed";
    if (!mEnabled.load(std::memory_order_relaxed)) return "hook=bypassed";
    if (mPinnedLast.load(std::memory_order_relaxed)) return "pinned=true";
    if (!mPackFoundLast.load(std::memory_order_relaxed)) return "pinned=false (UI pack not yet in repo)";
    return "pinned=false";
}

void PackPinner::onAfterCompose(void const* mgrVoid, void* outputStackVoid) noexcept {
    try {
        if (mShutdown.load(std::memory_order_relaxed)) return;
        if (!mEnabled.load(std::memory_order_relaxed) || mFailedLatch.load(std::memory_order_relaxed)) return;
        if (mTargetUuidLower.empty()) return;

        auto clientRef = ll::service::getClientInstance();
        if (!clientRef) return;
        auto& client = clientRef.value();

        // Only touch the client's own ResourcePackManager.
        if (static_cast<void const*>(&client.getResourcePackManager()) != mgrVoid) return;

        auto& output = *static_cast<ResourcePackStack*>(outputStackVoid);
        auto& repo   = client.getResourcePackRepository();
        auto& vec    = output.mStack.get();

        // Already pinned at index 0? Nothing to do.
        if (!vec.empty()) {
            std::string firstUuid;
            if (readEntryUuid(vec.front(), firstUuid) && firstUuid == mTargetUuidLower) {
                mPackFoundLast.store(true, std::memory_order_relaxed);
                mPinnedLast.store(true, std::memory_order_relaxed);
                return;
            }
        }

        // Resolve JavaChatUI from the repository.
        auto parsedUuid = ::mce::UUID::tryFromString(mTargetUuidLower);
        if (!parsedUuid.has_value()) return;

        auto            found  = repo.getResourcePacksByUUID(parsedUuid.value());
        ::ResourcePack* chosen = nullptr;
        for (auto* rp : found) {
            if (!rp) continue;
            auto const* impl = rp->mImpl.get().get();
            if (!impl || !impl->mPack.get()) continue;
            auto const* mp = impl->mPack.get()->mManifest.get();
            if (mp && mp->mPackType == ::PackType::Resources) {
                chosen = rp;
                break;
            }
        }
        if (!chosen && !found.empty()) chosen = found.front();
        if (!chosen) {
            mPackFoundLast.store(false, std::memory_order_relaxed);
            mPinnedLast.store(false, std::memory_order_relaxed);
            return;
        }
        mPackFoundLast.store(true, std::memory_order_relaxed);

        auto const* manifest = chosen->mImpl.get()->mPack.get()->mManifest.get();
        if (!manifest) return;
        auto targetSp = repo.getResourcePackForPackId(manifest->mIdentity.get());
        if (!targetSp) return;

        // Snapshot existing entries without copying/moving PackInstance (PackReport copy ops are MCNAPI).
        struct KeptEntry {
            std::shared_ptr<::ResourcePack> pack;
            int                             subpackIndex = -1;
            ::PackSettings*                 settings     = nullptr;
            std::string                     uuid;
            bool                            hasUuid      = false;
        };
        std::vector<KeptEntry> kept;
        kept.reserve(vec.size());
        for (PackInstance const& pi : vec) {
            std::string uuid;
            bool        known = readEntryUuid(pi, uuid);
            if (known && uuid == mTargetUuidLower) {
                continue; // Will be placed at index 0.
            }
            KeptEntry k;
            k.pack         = pi.mPack.get();
            k.subpackIndex = pi.mSubpackIndex;
            k.settings     = pi.mPackSettings;
            k.uuid         = std::move(uuid);
            k.hasUuid      = known;
            kept.push_back(std::move(k));
        }

        vec.clear();

        ::Bedrock::NonOwnerPointer<::IResourcePackRepository const>    nnp(repo);
        ::Bedrock::NotNullNonOwnerPtr<::IResourcePackRepository const> nn(nnp);

        ::PackSettings* targetSettings = repo.getPackSettingsFactory().getPackSettings(*manifest, std::nullopt);
        output.add(::PackInstance(targetSp, -1, false, targetSettings), nn, false);

        for (auto& k : kept) {
            if (!k.pack) continue;
            try {
                output.add(::PackInstance(k.pack, k.subpackIndex, false, k.settings), nn, false);
            } catch (...) {
            }
        }

        // Verify zero loss of existing packs.
        size_t            restored = 0;
        std::vector<char> used(vec.size(), 0);
        for (auto const& k : kept) {
            for (size_t i = 0; i < vec.size(); ++i) {
                if (used[i]) continue;
                bool match = false;
                if (k.hasUuid) {
                    std::string u;
                    if (readEntryUuid(vec[i], u) && u == k.uuid) match = true;
                } else if (vec[i].mPack.get() == k.pack) {
                    match = true;
                }
                if (match) {
                    used[i] = 1;
                    ++restored;
                    break;
                }
            }
        }

        bool pinnedNow = false;
        if (!vec.empty()) {
            std::string topUuid;
            pinnedNow = readEntryUuid(vec.front(), topUuid) && topUuid == mTargetUuidLower;
        }
        mPinnedLast.store(pinnedNow, std::memory_order_relaxed);

        if (restored < kept.size()) {
            mFailedLatch.store(true, std::memory_order_relaxed);
            if (!mShutdown.load(std::memory_order_relaxed) && mLogger) {
                mLogger->error(
                    "JavaChat: UI pack pin lost {} stack entries; disabling pin for safety",
                    kept.size() - restored
                );
            }
        } else if (!mShutdown.load(std::memory_order_relaxed) && mVerbose.load(std::memory_order_relaxed) && mLogger) {
            mLogger->info("JavaChat: pinned JavaChatUI at stack[0] (total entries={})", vec.size());
        }
    } catch (...) {
        mFailedLatch.store(true, std::memory_order_relaxed);
    }
}

} // namespace javachat
