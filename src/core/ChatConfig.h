#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace javachat {

inline constexpr char const* kUiPackUuid = "a7c4e912-3f68-4b8d-9e21-6c5d8a1f40b3";

struct ChatConfig {
    int   version                   = 3;
    bool  enabled                   = true;
    bool  overrideServerChatUi      = true;
    float width                     = 320.0f;
    float closedHeight              = 90.0f;
    float openHeight                = 180.0f;
    float bottomOffset              = 40.0f;
    float leftOffset                = 2.0f;
    float fontScale                 = 1.0f;
    float lineSpacing               = 0.0f;
    float backgroundOpacity         = 0.50f;
    float textOpacity               = 1.0f;
    bool  textShadow                = true;
    float fadeDelaySec              = 9.0f;
    float fadeDurationSec           = 1.0f;
    int   maxHistoryMessages        = 500;
    int   scrollStepLines           = 3;
    bool  compactDuplicates         = true;
    bool  showTimestamps            = false;
    bool        clickToCopy               = true;
    bool        clickToOpenUrl            = true;
    bool        autoCloseOnSend           = true;
    bool        disableChatAnimations     = true;
    bool        keepHudVisibleInChat      = true;
    std::string font                      = "default";
    bool        clearHistoryOnWorldSwitch = true;
    bool        chatHeads                 = true; // draw player heads when the ChatHeads mod is installed
    bool        verboseLog                = false;
};

struct ConfigLoadResult {
    ChatConfig  config;
    bool        existed   = false;
    bool        corrupted = false;
    bool        migrated  = false;
    std::string warning;
};

// Return true if `name` matches one of Bedrock's built-in font aliases/types (case-insensitive).
bool isBuiltinFontName(std::string_view name, std::string* canonicalOut = nullptr) noexcept;

// Clamp every numeric field into a safe, finite operating range.
void sanitizeConfig(ChatConfig& cfg) noexcept;

// Parse JSON text into ChatConfig. Unknown keys are ignored; invalid field types fall back to defaults.
bool parseConfigJson(
    std::string_view jsonText,
    ChatConfig&      outConfig,
    std::string*     outWarning  = nullptr,
    bool*            outMigrated = nullptr
);

// Serialize ChatConfig to pretty-printed JSON.
std::string serializeConfigJson(ChatConfig const& cfg);

// Load from disk. Missing file returns defaults (existed=false). Corrupt file is backed up.
ConfigLoadResult loadConfigFile(std::filesystem::path const& path);

// Crash-safe atomic write (.tmp + replace).
bool saveConfigFile(std::filesystem::path const& path, ChatConfig const& cfg);

} // namespace javachat
