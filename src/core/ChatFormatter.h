#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace javachat {

using TextMeasureFn = std::function<float(std::string_view)>;

// Decode one UTF-8 codepoint starting at `pos` and advance `pos`.
// Invalid byte sequences consume 1 byte and return U+FFFD.
uint32_t nextUtf8Codepoint(std::string_view s, size_t& pos) noexcept;

// True if `s` starts with a Bedrock section-sign (§, UTF-8 0xC2 0xA7) followed by a code char.
// When true, `seqLen` is set to the byte length of the formatting token (typically 3 bytes).
bool isSectionCodeAt(std::string_view s, size_t pos, size_t& seqLen, char& codeOut) noexcept;

// Remove all Bedrock § formatting codes and unsafe control characters (converting tabs to spaces).
std::string stripFormatting(std::string_view text);

// Case-insensitive (ASCII A-Z) UTF-8 substring check. Empty `needle` returns true.
bool containsIgnoreCaseAscii(std::string_view haystack, std::string_view needle) noexcept;

// Highlight every case-insensitive match of `query` inside `formattedText` with `§<highlightColorCode>`,
// restoring the original formatting state after each match so glyph metrics and `stripFormatting()` stay intact.
std::string highlightFormattedMatches(
    std::string_view formattedText,
    std::string_view query,
    char             highlightColorCode = 'e'
);

// Extract the first safe http:// or https:// URL from a chat line (after stripping § codes).
std::optional<std::string> extractFirstUrl(std::string_view text);

// Fallback text width estimation in GUI pixels (ignores § codes, handles ASCII + CJK/wide chars).
float estimateTextWidth(std::string_view text, float fontScale = 1.0f) noexcept;

// Track active Bedrock color/style state across lines so wrapped lines preserve formatting.
struct FormatState {
    char colorCode = 0; // '0'-'9', 'a'-'v', or 0 if default
    bool bold      = false;
    bool italic    = false;
    bool obf       = false;

    void        apply(char code) noexcept;
    std::string toPrefix() const;
};

// Wrap `text` into visual lines whose measured width does not exceed `maxWidth`.
// Preserves active § color/style codes across wrapped lines and honors explicit '\n'.
std::vector<std::string>
wrapFormattedText(std::string_view text, float maxWidth, TextMeasureFn const& measure);

} // namespace javachat
