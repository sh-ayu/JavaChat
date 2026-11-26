#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class ClientInstance;
class Font;
class MinecraftUIRenderContext;
class ScreenView;
class UIControl;

namespace ll::io {
class Logger;
}

namespace javachat {

struct FontRenderSet {
    std::shared_ptr<Font> primary;
    std::shared_ptr<Font> fallback;
    bool                  checkFallbackPerLine = false;

    std::shared_ptr<Font> selectForText(std::string_view sv) const noexcept;
};

// Manages built-in Bedrock fonts and custom .ttf fonts staged into JavaChatUI/font/.
// Uses only MCAPI / virtual / public TypedStorage APIs (zero MCNAPI imports).
class ChatFontManager {
public:
    static ChatFontManager& instance() noexcept;

    void initialize(
        std::filesystem::path modDir,
        std::filesystem::path configDir,
        ll::io::Logger*       logger
    );

    // Prepare a font spec ("default", "smooth", "unicode", "MinecraftTen", "rune", "SmoothSmall",
    // or a .ttf file path / name). Stages custom .ttf + font_metadata.json into JavaChatUI/font/.
    bool prepareFontSpec(std::string const& spec, std::string& statusOut, bool& isCustomOut);

    // Resolve live Font* pointers on the UI render thread. Fast O(1) when not dirty.
    void ensureResolved(ClientInstance& client, MinecraftUIRenderContext& ctx) noexcept;

    // Sync the chat input box (display_text) and autocomplete labels on chat_screen to the active font.
    void syncChatScreenFonts(ScreenView const& screenView, ClientInstance& client) noexcept;

    void invalidateUiSync() noexcept;
    void shutdown() noexcept;

    FontRenderSet renderSet() const noexcept;
    uint32_t      revision() const noexcept { return mFontRevision.load(std::memory_order_relaxed); }
    bool          isUsingCustomTtf() const noexcept { return mUsingCustomTtf.load(std::memory_order_relaxed); }
    bool          isCustomPendingRestart() const noexcept { return mCustomPendingRestart.load(std::memory_order_relaxed); }
    std::string              statusSummary() const;
    std::string              listAvailableFonts() const;
    std::vector<std::string> completionCandidates() const;

private:
    ChatFontManager() = default;

    void cleanupCustomFontStaging() noexcept;

    mutable std::mutex    mMutex;
    ll::io::Logger*       mLogger = nullptr;
    std::filesystem::path mUiPackFontDir;
    std::filesystem::path mUserFontsDir;

    std::string        mConfiguredSpec = "default";
    std::string        mCustomSourceFile;
    std::string        mCustomHash;
    std::string        mCustomRegName;
    bool               mIsCustomSpec = false;
    std::atomic<bool>  mUsingCustomTtf{false};
    std::atomic<bool>  mCustomPendingRestart{false};
    std::atomic<bool>  mDirty{true};
    std::atomic<bool>  mHasPrimaryFont{false};
    std::atomic<uint32_t> mFontRevision{1};

    std::shared_ptr<Font> mPrimaryFont;
    std::shared_ptr<Font> mFallbackFont;
    uint64_t              mPrimaryFontId  = ~0ull;
    uint64_t              mFallbackFontId = ~0ull;

    ScreenView const*        mLastSyncedScreen = nullptr;
    uint32_t                 mLastSyncedRev    = 0;
    std::weak_ptr<UIControl> mLastDisplayText;
};

} // namespace javachat
