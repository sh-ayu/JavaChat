#include "mod/ChatFontManager.h"

#include "core/ChatConfig.h"
#include "core/ChatFormatter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <ll/api/io/Logger.h>

#include <mc/client/game/ClientInstance.h>
#include <mc/client/game/IMinecraftGame.h>
#include <mc/client/gui/Font.h>
#include <mc/client/gui/FontHandle.h>
#include <mc/client/gui/FontRepository.h>
#include <mc/client/gui/controls/TextComponent.h>
#include <mc/client/gui/controls/UIControl.h>
#include <mc/client/gui/controls/VisualTree.h>
#include <mc/client/gui/screens/ScreenView.h>
#include <mc/client/renderer/screen/MinecraftUIRenderContext.h>

namespace javachat {
namespace {

std::string toLowerAsciiStr(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string trimQuotesAndSpace(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"') {
        sv.remove_prefix(1);
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::filesystem::path findFileCaseInsensitive(std::filesystem::path const& dir, std::string_view targetName) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return {};
    std::string targetLo = toLowerAsciiStr(targetName);
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        std::string fname = entry.path().filename().string();
        if (toLowerAsciiStr(fname) == targetLo) {
            return entry.path();
        }
    }
    return {};
}

std::filesystem::path resolveTtfFile(std::string const& rawSpec, std::filesystem::path const& userFontsDir) {
    std::string cleaned = trimQuotesAndSpace(rawSpec);
    if (cleaned.empty()) return {};

    std::error_code ec;
    auto checkCandidate = [&](std::filesystem::path const& p) -> std::filesystem::path {
        if (p.empty()) return {};
        if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec)) {
            return p;
        }
        return {};
    };

    std::filesystem::path direct(cleaned);
    if (auto found = checkCandidate(direct); !found.empty()) return found;
    if (!direct.has_extension()) {
        if (auto found = checkCandidate(std::filesystem::path(cleaned + ".ttf")); !found.empty()) return found;
    }

    std::vector<std::filesystem::path> searchDirs;
    if (!userFontsDir.empty()) searchDirs.push_back(userFontsDir);
    char localAppDataBuf[MAX_PATH]{};
    if (DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", localAppDataBuf, MAX_PATH);
        len > 0 && len < MAX_PATH) {
        searchDirs.push_back(std::filesystem::path(localAppDataBuf) / "Microsoft" / "Windows" / "Fonts");
    }
    searchDirs.push_back(std::filesystem::path("C:\\Windows\\Fonts"));

    std::vector<std::string> namesToTry{cleaned};
    if (!direct.has_extension()) {
        namesToTry.push_back(cleaned + ".ttf");
    }

    for (auto const& dir : searchDirs) {
        for (auto const& name : namesToTry) {
            if (auto found = checkCandidate(dir / name); !found.empty()) return found;
            if (auto found = findFileCaseInsensitive(dir, name); !found.empty()) return found;
        }
    }
    return {};
}

bool validateTrueTypeHeader(std::filesystem::path const& path, std::string& errorOut) {
    std::error_code ec;
    auto            sz = std::filesystem::file_size(path, ec);
    if (ec || sz < 12) {
        errorOut = "font file is too small or unreadable";
        return false;
    }
    if (sz > 64ull * 1024ull * 1024ull) {
        errorOut = "font file exceeds 64 MB safety limit";
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        errorOut = "failed to open font file";
        return false;
    }
    std::array<unsigned char, 4> hdr{};
    ifs.read(reinterpret_cast<char*>(hdr.data()), 4);
    if (!ifs) {
        errorOut = "failed to read font header";
        return false;
    }

    bool const isSfntTrueType = (hdr[0] == 0x00 && hdr[1] == 0x01 && hdr[2] == 0x00 && hdr[3] == 0x00)
                             || (hdr[0] == 't' && hdr[1] == 'r' && hdr[2] == 'u' && hdr[3] == 'e');
    if (isSfntTrueType) return true;

    if (hdr[0] == 't' && hdr[1] == 't' && hdr[2] == 'c' && hdr[3] == 'f') {
        errorOut = "TrueType collection (.ttc) is not supported; extract a standalone .ttf file";
        return false;
    }
    if (hdr[0] == 'O' && hdr[1] == 'T' && hdr[2] == 'T' && hdr[3] == 'O') {
        errorOut = "OpenType CFF (.otf) is not supported by Bedrock TrueTypeFont; please use a .ttf font";
        return false;
    }
    errorOut = "unsupported font format (expected a TrueType .ttf file)";
    return false;
}

std::string computeFileHash8(std::filesystem::path const& path) {
    std::error_code ec;
    auto            sz    = std::filesystem::file_size(path, ec);
    auto            mtime = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
    std::string     key   = path.lexically_normal().string() + "|" + std::to_string(sz) + "|" + std::to_string(mtime);

    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setw(8) << std::setfill('0')
        << static_cast<uint32_t>((h >> 32) ^ h);
    return oss.str();
}

bool writeTextAtomic(std::filesystem::path const& path, std::string_view content) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.flush();
        if (!ofs.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec2);
        std::filesystem::remove(tmp, ec);
        return !ec2;
    }
    return true;
}

// Preserves all Bedrock 1.26.60.5 vanilla font_metadata.json entries and appends the custom TTF.
std::string buildFontMetadataJson(std::string_view regName, std::string_view fontFileRelNoExt) {
    std::ostringstream oss;
    oss << R"({"version":1,"fonts":[)"
        << R"({"font_format":"ttf","font_name":"MinecraftTen","version":1,"font_file":"font/minecraft-ten","lowPerformanceCompatible":false},)"
        << R"({"font_format":"msdf","font_name":"OldSmoothFont","font_file":"font/smooth/smooth_"},)"
        << R"({"font_format":"ttf","font_name":")" << regName << R"(","version":1,"font_file":")" << fontFileRelNoExt
        << R"(","lowPerformanceCompatible":true})"
        << R"(],"font_aliases":[)"
        << R"({"alias":"SmoothFontLatin","fonts":[{"font_reference":"OldSmoothFont"},{"font_reference":"UnicodeFont","font_scale":1.0,"font_ranges":[{"first":0,"last":44397},{"first":54620,"last":54620},{"first":50612,"last":50612},{"first":45824,"last":45824},{"first":48124,"last":48124},{"first":57344,"last":57599},{"first":57856,"last":63743}]}]},)"
        << R"({"alias":"SmoothFontWithHangulFallback","fonts":[{"font_reference":"OldSmoothFont"},{"font_reference":"UnicodeFont","font_scale":1.0,"font_ranges":[{"first":44032,"last":55203}]},{"font_reference":"OldSmoothFont"},{"font_reference":"UnicodeFont","font_scale":1.0,"font_ranges":[{"first":57344,"last":57599},{"first":57856,"last":63743}]}]},)"
        << R"({"alias":"SmoothWithEmoticons","fonts":[{"font_reference":"OldSmoothFont"},{"font_reference":"UnicodeFont","font_scale":1.0,"font_ranges":[{"first":44032,"last":55203}]},{"font_reference":"OldSmoothFont"},{"font_reference":"UnicodeFont","font_scale":0.65,"font_ranges":[{"first":57600,"last":57855}]}]},)"
        << R"({"alias":"MinecraftTenEmoticon","fonts":[{"font_reference":"MinecraftTen"},{"font_reference":"unicode","font_ranges":[{"first":57344,"last":63743}]}]},)"
        << R"({"alias":"SmoothSmall","font_scale_factor_override":1,"fonts":[{"font_reference":"OldSmoothFont"},{"font_reference":"UnicodeFont","font_ranges":[{"first":54620,"last":54620},{"first":44397,"last":44397},{"first":50612,"last":50612},{"first":45824,"last":45824},{"first":48124,"last":48124},{"first":57344,"last":57599},{"first":57856,"last":63743}]}]})"
        << R"(]})";
    return oss.str();
}

bool applyFontToLabelControl(
    UIControl*                   ctrl,
    FontRepository const*        repoPtr,
    std::shared_ptr<Font> const& primary,
    uint64_t                     primaryId,
    std::shared_ptr<Font> const& fallback,
    uint64_t                     fallbackId
) noexcept {
    if (!ctrl || !primary) return false;
    auto const& lookup = ctrl->mRenderableComponentLookup.get();
    if (lookup.size() != 1 || !lookup[0]) return false;

    auto* tc = reinterpret_cast<TextComponent*>(lookup[0]);
    // Safety check without RTTI: TextComponent always holds mFontRepository == repoPtr.
    if (tc->mFontRepository.get().get() != repoPtr) return false;

    auto setHandle = [](FontHandle& h, std::shared_ptr<Font> const& f, uint64_t id) {
        h.mDefaultFont   = f;
        h.mFontId        = id;
        h.mIsDummyHandle = false;
    };
    setHandle(tc->mFontHandle.get(), primary, primaryId);
    setHandle(tc->mPrimaryFontHandle.get(), primary, primaryId);
    if (fallback) {
        setHandle(tc->mBackupFontHandle.get(), fallback, fallbackId);
    }
    tc->mCachedFontSources.get().clear();
    tc->mCachedTextDirty = true;
    return true;
}

bool controlNeedsFontSync(
    UIControl const*      ctrl,
    FontRepository const* repoPtr,
    Font const*           primaryPtr,
    uint64_t              primaryId
) noexcept {
    if (!ctrl || !primaryPtr) return false;
    auto const& lookup = ctrl->mRenderableComponentLookup.get();
    if (lookup.size() != 1 || !lookup[0]) return false;
    auto const* tc = reinterpret_cast<TextComponent const*>(lookup[0]);
    if (tc->mFontRepository.get().get() != repoPtr) return false;
    auto const& h = tc->mFontHandle.get();
    return h.mFontId != primaryId || h.mDefaultFont.get() != primaryPtr;
}

struct ResolvedFont {
    std::shared_ptr<Font> font;
    uint64_t              id = ~0ull;
};

std::shared_ptr<Font> fontFromLoadedTable(FontRepository const* repo, uint64_t id) noexcept {
    if (!repo || id == FontRepository::INVALID_FONT()) return {};
    auto const& loaded = repo->mLoadedFonts.get();
    if (id < loaded.size()) {
        return loaded[id].get();
    }
    return {};
}

ResolvedFont resolveHandle(
    FontRepository const* repo,
    FontHandle const&     h,
    uint64_t              preferredId = ~0ull
) noexcept {
    uint64_t const invalid = FontRepository::INVALID_FONT();
    uint64_t const id      = (preferredId != invalid) ? preferredId : h.mFontId;
    if (auto live = fontFromLoadedTable(repo, id)) {
        return {std::move(live), id};
    }
    return {h.mDefaultFont, id};
}

ResolvedFont resolveByName(FontRepository const* repo, std::string const& name) noexcept {
    if (!repo) return {};
    auto const& idMap = repo->mFontNameToIdentifier.get();
    if (auto it = idMap.find(name); it != idMap.end()) {
        if (auto live = fontFromLoadedTable(repo, it->second)) {
            return {std::move(live), it->second};
        }
    }
    FontHandle h = repo->getFontFromFontType(name);
    return resolveHandle(repo, h, h.mFontId);
}

} // namespace

Font* FontRenderSet::selectForText(std::string_view sv) const noexcept {
    if (!primary) return fallback;
    if (!checkFallbackPerLine || !fallback || fallback == primary) return primary;

    bool hasNonAscii = false;
    for (unsigned char c : sv) {
        if (c >= 0x80) {
            hasNonAscii = true;
            break;
        }
    }
    if (!hasNonAscii) return primary;

    // Skip § formatting codes and check if primary supports all non-ASCII codepoints.
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t seqLen = 0;
        char   code   = 0;
        if (isSectionCodeAt(sv, pos, seqLen, code)) {
            pos += seqLen;
            continue;
        }
        uint32_t cp = nextUtf8Codepoint(sv, pos);
        if (cp < 0x80 || cp == 0xFFFD) continue;
        try {
            if (!primary->supportsChar(static_cast<int>(cp))) {
                return fallback;
            }
        } catch (...) {
            return fallback;
        }
    }
    return primary;
}

ChatFontManager& ChatFontManager::instance() noexcept {
    static ChatFontManager inst;
    return inst;
}

void ChatFontManager::initialize(
    std::filesystem::path modDir,
    std::filesystem::path configDir,
    ll::io::Logger*       logger
) {
    std::lock_guard lock(mMutex);
    mLogger        = logger;
    mUiPackFontDir = std::move(modDir) / "resource_packs" / "JavaChatUI" / "font";
    mUserFontsDir  = std::move(configDir) / "fonts";
    mDirty.store(true, std::memory_order_release);
    invalidateUiSync();
}

void ChatFontManager::cleanupCustomFontStaging() noexcept {
    std::error_code ec;
    if (mUiPackFontDir.empty() || !std::filesystem::exists(mUiPackFontDir, ec)) return;
    std::filesystem::remove(mUiPackFontDir / "font_metadata.json", ec);
    for (auto const& entry : std::filesystem::directory_iterator(mUiPackFontDir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        std::string fn = entry.path().filename().string();
        if (fn.starts_with("jchat_") && fn.ends_with(".ttf")) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

bool ChatFontManager::prepareFontSpec(std::string const& spec, std::string& statusOut, bool& isCustomOut) {
    std::lock_guard lock(mMutex);
    std::string     canonical;
    if (isBuiltinFontName(spec, &canonical)) {
        mConfiguredSpec = canonical;
        mCustomSourceFile.clear();
        mCustomHash.clear();
        mCustomRegName.clear();
        mIsCustomSpec = false;
        mCustomPendingRestart.store(false, std::memory_order_relaxed);
        mDirty.store(true, std::memory_order_release);
        isCustomOut = false;
        cleanupCustomFontStaging();
        statusOut = "Using built-in font '" + canonical + "'";
        return true;
    }

    std::filesystem::path resolved = resolveTtfFile(spec, mUserFontsDir);
    if (resolved.empty()) {
        statusOut = "Font '" + spec + "' not found. Use built-in (default/smooth/ui/unicode/MinecraftTen/rune/SmoothSmall) or place a .ttf in "
                  + mUserFontsDir.string();
        return false;
    }

    std::string headerErr;
    if (!validateTrueTypeHeader(resolved, headerErr)) {
        statusOut = "Invalid font '" + resolved.filename().string() + "': " + headerErr;
        return false;
    }

    std::string hash8      = computeFileHash8(resolved);
    std::string regName    = "JavaChatTTF_" + hash8;
    std::string fileStem   = "jchat_" + hash8;
    std::string stagedName = fileStem + ".ttf";

    std::error_code ec;
    std::filesystem::create_directories(mUiPackFontDir, ec);
    if (ec) {
        statusOut = "Failed to create UI font staging directory: " + mUiPackFontDir.string();
        return false;
    }

    for (auto const& entry : std::filesystem::directory_iterator(mUiPackFontDir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        std::string fn = entry.path().filename().string();
        if (fn.starts_with("jchat_") && fn.ends_with(".ttf") && fn != stagedName) {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    std::filesystem::path stagedTtf = mUiPackFontDir / stagedName;
    auto                  srcSize   = std::filesystem::file_size(resolved, ec);
    bool                  needCopy  = !std::filesystem::exists(stagedTtf, ec)
                                   || std::filesystem::file_size(stagedTtf, ec) != srcSize;
    if (needCopy) {
        std::filesystem::copy_file(resolved, stagedTtf, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            statusOut = "Failed to copy font to " + stagedTtf.string();
            return false;
        }
    }

    std::string metaJson = buildFontMetadataJson(regName, "font/" + fileStem);
    if (!writeTextAtomic(mUiPackFontDir / "font_metadata.json", metaJson)) {
        statusOut = "Failed to write font_metadata.json";
        return false;
    }

    mConfiguredSpec   = trimQuotesAndSpace(spec);
    mCustomSourceFile = resolved.filename().string();
    mCustomHash       = hash8;
    mCustomRegName    = regName;
    mIsCustomSpec     = true;
    mDirty.store(true, std::memory_order_release);
    isCustomOut = true;
    statusOut   = "Staged custom TTF '" + mCustomSourceFile + "' (" + regName + ")";
    return true;
}

void ChatFontManager::ensureResolved(ClientInstance& client, MinecraftUIRenderContext& ctx) noexcept {
    if (!mDirty.load(std::memory_order_acquire) && mHasPrimaryFont.load(std::memory_order_relaxed)) return;

    try {
        std::lock_guard lock(mMutex);
        if (!mDirty.load(std::memory_order_relaxed) && mPrimaryFont) return;

        Font* const    prevPtr = mPrimaryFont.get();
        uint64_t const prevId  = mPrimaryFontId;
        auto           repoPtr = client.getMinecraftGame().getFontRepository().get();

        // Fallback & default font: true Mojangles pixel font from mFontHandle / mDefaultFontId.
        ResolvedFont fb;
        if (repoPtr) {
            fb = resolveHandle(repoPtr, repoPtr->mFontHandle.get(), repoPtr->mDefaultFontId);
        }
        if (!fb.font) {
            FontHandle ch = client.getFontHandle();
            fb            = resolveHandle(repoPtr, ch, ch.mFontId);
        }
        if (!fb.font) {
            auto const& dbg = ctx.mDebugTextFontHandle.get();
            fb              = resolveHandle(repoPtr, dbg, dbg.mFontId);
        }

        mFallbackFont   = fb.font;
        mFallbackFontId = fb.id;
        mUsingCustomTtf.store(false, std::memory_order_relaxed);
        mCustomPendingRestart.store(false, std::memory_order_relaxed);

        ResolvedFont chosen;
        if (mIsCustomSpec && repoPtr) {
            auto const& idMap = repoPtr->mFontNameToIdentifier.get();
            if (auto it = idMap.find(mCustomRegName); it != idMap.end()) {
                if (auto live = fontFromLoadedTable(repoPtr, it->second)) {
                    chosen.font = std::move(live);
                    chosen.id   = it->second;
                    mUsingCustomTtf.store(true, std::memory_order_relaxed);
                } else {
                    FontHandle customH = repoPtr->getFontFromFontType(mCustomRegName);
                    chosen             = resolveHandle(repoPtr, customH, it->second);
                    if (chosen.font) {
                        mUsingCustomTtf.store(true, std::memory_order_relaxed);
                    }
                }
            }
            if (!chosen.font) {
                mCustomPendingRestart.store(true, std::memory_order_relaxed);
                if (mLogger) {
                    mLogger->info(
                        "JavaChat: custom font '{}' staged as '{}'; restart game to apply",
                        mCustomSourceFile,
                        mCustomRegName
                    );
                }
            }
        } else if (repoPtr) {
            if (mConfiguredSpec == "default") {
                chosen = fb;
            } else if (mConfiguredSpec == "ui") {
                chosen = resolveHandle(repoPtr, repoPtr->mUIFontHandle.get(), repoPtr->mUIFontId);
            } else if (mConfiguredSpec == "smooth") {
                chosen = resolveHandle(repoPtr, repoPtr->mSmoothFontLatinHandle.get(), repoPtr->mSmoothFontLatinFontId);
                if (!chosen.font) chosen = resolveByName(repoPtr, "SmoothFontLatin");
                if (!chosen.font) chosen = resolveByName(repoPtr, "smooth");
            } else if (mConfiguredSpec == "unicode") {
                chosen = resolveHandle(repoPtr, repoPtr->mUnicodeFontHandle.get(), repoPtr->mUnicodeFontId);
                if (!chosen.font) chosen = resolveByName(repoPtr, "unicode");
                if (!chosen.font) chosen = resolveByName(repoPtr, "UnicodeFont");
            } else if (mConfiguredSpec == "rune") {
                chosen = resolveHandle(repoPtr, repoPtr->mRuneFontHandle.get(), repoPtr->mRuneFontId);
            } else if (mConfiguredSpec == "SmoothSmall") {
                if (auto live = fontFromLoadedTable(repoPtr, repoPtr->mSmoothSmallFontID)) {
                    chosen = {std::move(live), repoPtr->mSmoothSmallFontID};
                } else {
                    chosen = resolveByName(repoPtr, "SmoothSmall");
                }
            } else {
                chosen = resolveByName(repoPtr, mConfiguredSpec);
            }
        }

        mPrimaryFont   = chosen.font ? chosen.font : fb.font;
        mPrimaryFontId = chosen.font ? chosen.id : fb.id;
        mHasPrimaryFont.store(mPrimaryFont != nullptr, std::memory_order_relaxed);
        mDirty.store(false, std::memory_order_release);

        if (mPrimaryFont.get() != prevPtr || mPrimaryFontId != prevId) {
            mFontRevision.fetch_add(1, std::memory_order_relaxed);
            invalidateUiSync();
        }
    } catch (...) {
    }
}

void ChatFontManager::syncChatScreenFonts(ScreenView const& screenView, ClientInstance& client) noexcept {
    try {
        std::lock_guard lock(mMutex);
        if (!mPrimaryFont) return;
        auto const& tree = static_cast<std::unique_ptr<VisualTree> const&>(screenView.mVisualTree);
        if (!tree) return;
        auto repoPtr = client.getMinecraftGame().getFontRepository().get();
        if (!repoPtr) return;

        uint32_t const curRev       = mFontRevision.load(std::memory_order_relaxed);
        bool const     sameScreen   = (mLastSyncedScreen == &screenView);
        bool const     needFullSync = !sameScreen || (mLastSyncedRev != curRev);

        auto displayText = sameScreen ? mLastDisplayText.lock() : std::shared_ptr<UIControl>{};
        if (!displayText) {
            displayText      = tree->getControlByName("display_text", false);
            mLastDisplayText = displayText;
        }
        bool const displayDrift = displayText
                               && controlNeedsFontSync(displayText.get(), repoPtr, mPrimaryFont.get(), mPrimaryFontId);

        if (!needFullSync && !displayDrift) return;

        if ((needFullSync || displayDrift) && displayText) {
            applyFontToLabelControl(
                displayText.get(),
                repoPtr,
                mPrimaryFont,
                mPrimaryFontId,
                mFallbackFont,
                mFallbackFontId
            );
        }
        if (needFullSync) {
            if (auto placeholder = tree->getControlByName("place_holder_control", false)) {
                applyFontToLabelControl(
                    placeholder.get(),
                    repoPtr,
                    mPrimaryFont,
                    mPrimaryFontId,
                    mFallbackFont,
                    mFallbackFontId
                );
            }
        }

        mLastSyncedScreen = &screenView;
        mLastSyncedRev    = curRev;
    } catch (...) {
    }
}

void ChatFontManager::invalidateUiSync() noexcept {
    mLastSyncedScreen = nullptr;
    mLastSyncedRev    = 0;
    mLastDisplayText.reset();
}

void ChatFontManager::shutdown() noexcept {
    std::lock_guard lock(mMutex);
    mPrimaryFont.reset();
    mFallbackFont.reset();
    mPrimaryFontId  = ~0ull;
    mFallbackFontId = ~0ull;
    mHasPrimaryFont.store(false, std::memory_order_relaxed);
    invalidateUiSync();
    mDirty.store(true, std::memory_order_release);
}

FontRenderSet ChatFontManager::renderSet() const noexcept {
    std::lock_guard lock(mMutex);
    FontRenderSet   set;
    set.primary              = mPrimaryFont.get();
    set.fallback             = mFallbackFont.get();
    set.checkFallbackPerLine = mUsingCustomTtf.load(std::memory_order_relaxed) && mPrimaryFont && mFallbackFont
                            && (mPrimaryFont.get() != mFallbackFont.get());
    return set;
}

std::string ChatFontManager::statusSummary() const {
    std::lock_guard lock(mMutex);
    if (!mIsCustomSpec) return mConfiguredSpec;
    if (mCustomPendingRestart.load(std::memory_order_relaxed)) {
        return "default (pending restart for " + mCustomSourceFile + ")";
    }
    if (mUsingCustomTtf.load(std::memory_order_relaxed)) {
        return mCustomRegName + " (" + mCustomSourceFile + ")";
    }
    return mConfiguredSpec;
}

std::string ChatFontManager::listAvailableFonts() const {
    std::filesystem::path userDir;
    std::string           curStatus;
    {
        std::lock_guard lock(mMutex);
        userDir = mUserFontsDir;
        if (!mIsCustomSpec) {
            curStatus = mConfiguredSpec;
        } else if (mCustomPendingRestart.load(std::memory_order_relaxed)) {
            curStatus = "default (pending restart for " + mCustomSourceFile + ")";
        } else if (mUsingCustomTtf.load(std::memory_order_relaxed)) {
            curStatus = mCustomRegName + " (" + mCustomSourceFile + ")";
        } else {
            curStatus = mConfiguredSpec;
        }
    }
    std::ostringstream oss;
    oss << "JavaChat fonts (current: " << curStatus << ")\n"
        << "  Built-in (instant): default (Mojangles), smooth, ui, unicode, MinecraftTen, rune, SmoothSmall\n"
        << "  Custom .ttf (restart to apply): /jchat font <filename_or_path>";

    std::error_code ec;
    if (!userDir.empty() && std::filesystem::exists(userDir, ec)
        && std::filesystem::is_directory(userDir, ec)) {
        std::vector<std::string> files;
        for (auto const& entry : std::filesystem::directory_iterator(userDir, ec)) {
            if (ec || !entry.is_regular_file(ec)) continue;
            auto ext = toLowerAsciiStr(entry.path().extension().string());
            if (ext == ".ttf") files.push_back(entry.path().filename().string());
        }
        std::sort(files.begin(), files.end());
        oss << "\n  config/fonts/ (" << userDir.string() << "): ";
        if (files.empty()) {
            oss << "(none)";
        } else {
            for (size_t i = 0; i < files.size() && i < 12; ++i) {
                if (i > 0) oss << ", ";
                oss << files[i];
            }
            if (files.size() > 12) oss << " ... (+" << (files.size() - 12) << ")";
        }
    } else {
        oss << "\n  Tip: place .ttf files in " << userDir.string()
            << " or specify a Windows font filename (e.g. arial.ttf, BIZUDPGothic-Regular.ttf).";
    }
    return oss.str();
}

std::vector<std::string> ChatFontManager::completionCandidates() const {
    std::vector<std::string> out{
        "list",
        "default",
        "mojangles",
        "smooth",
        "ui",
        "unicode",
        "MinecraftTen",
        "rune",
        "SmoothSmall",
    };

    std::filesystem::path userDir;
    {
        std::lock_guard lock(mMutex);
        userDir = mUserFontsDir;
    }
    std::error_code ec;
    if (!userDir.empty() && std::filesystem::exists(userDir, ec)
        && std::filesystem::is_directory(userDir, ec)) {
        std::vector<std::string> files;
        for (auto const& entry : std::filesystem::directory_iterator(userDir, ec)) {
            if (ec || !entry.is_regular_file(ec)) continue;
            if (toLowerAsciiStr(entry.path().extension().string()) == ".ttf") {
                files.push_back(entry.path().filename().string());
            }
        }
        std::sort(files.begin(), files.end());
        out.insert(out.end(), files.begin(), files.end());
    }
    return out;
}

} // namespace javachat
