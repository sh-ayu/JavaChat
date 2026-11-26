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

bool PackPinner::install(ll::io::Logger* logger, std::string uiPackUuid, PackPinMode mode, bool verbose) noexcept {
    {
        std::lock_guard lock(mConfigMutex);
        mLogger          = logger;
        mTargetUuidLower = toLowerAscii(std::move(uiPackUuid));
    }
    mShutdown.store(false, std::memory_order_release);
    mMode.store(static_cast<int>(mode), std::memory_order_release);
    mVerbose.store(verbose, std::memory_order_relaxed);
    mFailedLatch.store(false, std::memory_order_relaxed);
    mComposeObserved.store(false, std::memory_order_relaxed);
    mPinnedLast.store(false, std::memory_order_relaxed);
    mStrippedLast.store(false, std::memory_order_relaxed);
    mPackFoundLast.store(false, std::memory_order_relaxed);

    if (mHookInstalled.load(std::memory_order_relaxed)) return true;
    try {
        bool ok = (JavaChatComposeHook::hook() == 0);
        mHookInstalled.store(ok, std::memory_order_release);
        return ok;
    } catch (...) {
        mHookInstalled.store(false, std::memory_order_release);
        return false;
    }
}

void PackPinner::uninstall() noexcept {
    mShutdown.store(true, std::memory_order_release);
    if (mHookInstalled.exchange(false, std::memory_order_acq_rel)) {
        try {
            JavaChatComposeHook::unhook();
        } catch (...) {
        }
    }
    std::lock_guard lock(mConfigMutex);
    mLogger = nullptr;
}

void PackPinner::setMode(PackPinMode mode) noexcept {
    mMode.store(static_cast<int>(mode), std::memory_order_release);
}

void PackPinner::setVerbose(bool verbose) noexcept {
    mVerbose.store(verbose, std::memory_order_relaxed);
}

std::string PackPinner::statusSummary() const {
    if (!mHookInstalled.load(std::memory_order_acquire)) return "hook=off";
    if (mFailedLatch.load(std::memory_order_relaxed)) return "hook=latched_failed";
    auto const mode = static_cast<PackPinMode>(mMode.load(std::memory_order_acquire));
    if (mode == PackPinMode::Passthrough) return "hook=passthrough";
    if (!mComposeObserved.load(std::memory_order_acquire)) return "pending";
    if (mode == PackPinMode::Strip) {
        return mStrippedLast.load(std::memory_order_relaxed) ? "stripped=true" : "stripped=false";
    }
    if (mPinnedLast.load(std::memory_order_relaxed)) return "pinned=true";
    if (!mPackFoundLast.load(std::memory_order_relaxed)) return "pinned=false (UI pack not yet in repo)";
    return "pinned=false";
}

void PackPinner::onAfterCompose(void const* mgrVoid, void* outputStackVoid) noexcept {
    try {
        if (mShutdown.load(std::memory_order_acquire)) return;
        auto const mode = static_cast<PackPinMode>(mMode.load(std::memory_order_acquire));
        if (mode == PackPinMode::Passthrough || mFailedLatch.load(std::memory_order_relaxed)) return;
        if (!outputStackVoid) return;

        std::string     targetUuidLower;
        ll::io::Logger* logger = nullptr;
        {
            std::lock_guard lock(mConfigMutex);
            targetUuidLower = mTargetUuidLower;
            logger          = mLogger;
        }
        if (targetUuidLower.empty()) return;

        auto clientRef = ll::service::getClientInstance();
        if (!clientRef) return;
        auto& client = clientRef.value();

        // Only touch the client's own ResourcePackManager.
        if (static_cast<void const*>(&client.getResourcePackManager()) != mgrVoid) return;
        mComposeObserved.store(true, std::memory_order_release);

        auto& output = *static_cast<ResourcePackStack*>(outputStackVoid);
        auto& repo   = client.getResourcePackRepository();
        auto& vec    = output.mStack.get();

        bool const stripOnly = (mode == PackPinMode::Strip);
        if (stripOnly) {
            bool foundInStack = false;
            for (PackInstance const& pi : vec) {
                std::string uuid;
                if (readEntryUuid(pi, uuid) && uuid == targetUuidLower) {
                    foundInStack = true;
                    break;
                }
            }
            if (!foundInStack) {
                mPackFoundLast.store(false, std::memory_order_relaxed);
                mPinnedLast.store(false, std::memory_order_relaxed);
                mStrippedLast.store(true, std::memory_order_relaxed);
                return;
            }
            mPackFoundLast.store(true, std::memory_order_relaxed);
        } else {
            // PinTop: already pinned at index 0? Nothing to do.
            if (!vec.empty()) {
                std::string firstUuid;
                if (readEntryUuid(vec.front(), firstUuid) && firstUuid == targetUuidLower) {
                    mPackFoundLast.store(true, std::memory_order_relaxed);
                    mPinnedLast.store(true, std::memory_order_relaxed);
                    mStrippedLast.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        }

        ::PackManifest const*           manifest = nullptr;
        std::shared_ptr<::ResourcePack> targetSp;
        if (!stripOnly) {
            auto parsedUuid = ::mce::UUID::tryFromString(targetUuidLower);
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
                mStrippedLast.store(false, std::memory_order_relaxed);
                return;
            }
            auto const* chosenImpl = chosen->mImpl.get().get();
            if (!chosenImpl || !chosenImpl->mPack.get()) {
                mPackFoundLast.store(false, std::memory_order_relaxed);
                mPinnedLast.store(false, std::memory_order_relaxed);
                mStrippedLast.store(false, std::memory_order_relaxed);
                return;
            }
            manifest = chosenImpl->mPack.get()->mManifest.get();
            if (!manifest) {
                mPackFoundLast.store(false, std::memory_order_relaxed);
                mPinnedLast.store(false, std::memory_order_relaxed);
                mStrippedLast.store(false, std::memory_order_relaxed);
                return;
            }
            mPackFoundLast.store(true, std::memory_order_relaxed);

            targetSp = repo.getResourcePackForPackId(manifest->mIdentity.get());
            if (!targetSp) return;
        }

        // Snapshot existing non-target entries without copying/moving PackInstance (PackReport copy ops are MCNAPI).
        struct KeptEntry {
            std::shared_ptr<::ResourcePack> pack;
            int                             subpackIndex = -1;
            ::PackSettings*                 settings     = nullptr;
            ::PackStats                     stats{};
            uchar                           revision     = 0;
            bool                            isDependent  = false;
            std::string                     uuid;
            bool                            hasUuid      = false;
        };
        std::vector<KeptEntry> kept;
        kept.reserve(vec.size());
        for (PackInstance const& pi : vec) {
            std::string uuid;
            bool        known = readEntryUuid(pi, uuid);
            if (known && uuid == targetUuidLower) {
                continue; // Either placed at index 0 (PinTop) or stripped cleanly (Strip).
            }
            KeptEntry k;
            k.pack         = pi.mPack.get();
            k.subpackIndex = pi.mSubpackIndex;
            k.settings     = pi.mPackSettings;
            k.stats        = pi.mStats.get();
            k.revision     = pi.mRevision;
            k.isDependent  = false;
            k.uuid         = std::move(uuid);
            k.hasUuid      = known;
            kept.push_back(std::move(k));
        }

        // Exception-safe staging: swap the original stack into a local backup vector (pointer swap only,
        // zero PackReport copy/move). If rebuilding fails or loses any entry, swap back to restore intact.
        std::vector<::PackInstance> backup;
        backup.swap(vec);

        bool   rebuiltOk = false;
        size_t restored  = 0;
        bool   pinnedNow = false;
        try {
            ::Bedrock::NonOwnerPointer<::IResourcePackRepository const>    nnp(repo);
            ::Bedrock::NotNullNonOwnerPtr<::IResourcePackRepository const> nn(nnp);

            if (!stripOnly) {
                ::PackSettings* targetSettings =
                    repo.getPackSettingsFactory().getPackSettings(*manifest, std::nullopt);
                output.add(::PackInstance(targetSp, -1, false, targetSettings), nn, false);
            }

            for (auto& k : kept) {
                if (!k.pack) continue;
                size_t const prevSize = vec.size();
                output.add(::PackInstance(k.pack, k.subpackIndex, k.isDependent, k.settings), nn, k.isDependent);
                if (vec.size() > prevSize && vec.back().mPack.get() == k.pack) {
                    vec.back().mStats.get() = k.stats;
                    vec.back().mRevision    = k.revision;
                }
            }

            // Verify zero loss of existing packs.
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

            if (stripOnly) {
                bool stillPresent = false;
                for (PackInstance const& pi : vec) {
                    std::string u;
                    if (readEntryUuid(pi, u) && u == targetUuidLower) {
                        stillPresent = true;
                        break;
                    }
                }
                rebuiltOk = (restored == kept.size()) && !stillPresent;
            } else {
                if (!vec.empty()) {
                    std::string topUuid;
                    pinnedNow = readEntryUuid(vec.front(), topUuid) && topUuid == targetUuidLower;
                }
                rebuiltOk = (restored == kept.size()) && pinnedNow;
            }
        } catch (...) {
            rebuiltOk = false;
        }

        if (!rebuiltOk) {
            vec.swap(backup);
            mPinnedLast.store(false, std::memory_order_relaxed);
            mStrippedLast.store(false, std::memory_order_relaxed);
            mFailedLatch.store(true, std::memory_order_relaxed);
            if (!mShutdown.load(std::memory_order_acquire) && logger) {
                logger->error(
                    "JavaChat: UI pack {} failed (restored={}/{}, pinned={}); rolled back original stack",
                    stripOnly ? "strip" : "pin",
                    restored,
                    kept.size(),
                    pinnedNow
                );
            }
            return;
        }

        mPinnedLast.store(!stripOnly, std::memory_order_relaxed);
        mStrippedLast.store(stripOnly, std::memory_order_relaxed);
        if (!mShutdown.load(std::memory_order_acquire) && (stripOnly || mVerbose.load(std::memory_order_relaxed))
            && logger) {
            logger->info(
                "JavaChat: {} JavaChatUI (total entries={})",
                stripOnly ? "stripped" : "pinned",
                vec.size()
            );
        }
    } catch (...) {
        mFailedLatch.store(true, std::memory_order_relaxed);
    }
}

} // namespace javachat
