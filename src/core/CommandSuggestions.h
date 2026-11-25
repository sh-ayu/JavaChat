#pragma once

#include "core/ChatFormatter.h"
#include "core/ChatHistory.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace javachat {

inline constexpr int   kMaxVisibleSuggestions = 10;
inline constexpr float kSuggestionRowHeight   = 12.0f;
inline constexpr float kInputBarHeight        = 12.0f;
inline constexpr float kInputBarBottomMargin  = 2.0f;
inline constexpr float kPopupGapAboveInput    = 1.0f;
inline constexpr float kPopupPadLeft          = 1.0f;
inline constexpr float kPopupPadRight         = 2.0f;
inline constexpr float kPopupBgAlpha          = 0.82f;

struct ActiveTokenSpan {
    size_t startByte      = 0;
    size_t endByte        = 0;
    int    argIndex       = 0;
    bool   isSlashCommand = false;
};

struct SuggestionPopupRow {
    int         itemIndex = 0;
    std::string text;
    Rect        rect;
    bool        selected  = false;
};

struct SuggestionPopupLayout {
    bool                            visible      = false;
    Rect                            popupRect;
    bool                            hasMoreAbove = false;
    bool                            hasMoreBelow = false;
    std::vector<SuggestionPopupRow> rows;
};

struct SuggestionApplyResult {
    std::string newText;
    bool        refreshIntellisense = true;
};

// Extract pure command/argument token from Bedrock's autocomplete string
// (trims leading item-icon spaces and strips " - <description>" suffix).
std::string extractSuggestionToken(std::string_view rawBedrockText);

// Find the byte span of the token currently being typed/completed at the end of `input`.
ActiveTokenSpan findActiveTokenSpan(std::string_view input) noexcept;

// Find the byte offset where `suggestionToken` replaces into `input` within `span`
// (handles selector attributes like `@e[type=z` and namespaces like `minecraft:dia`).
size_t findSubTokenStart(
    std::string_view       input,
    ActiveTokenSpan const& span,
    std::string_view       suggestionToken
) noexcept;

// Apply `suggestionToken` onto the active token in `input`.
std::string applySuggestionToInput(std::string_view input, std::string_view suggestionToken);

// Extract the newly inserted UTF-8 text when `currentAfter` resulted from inserting into `selectedBefore`
// while all text was selected. Returns std::nullopt if `selectedBefore` is empty, `currentAfter` did not
// strictly grow, or text outside the single inserted span does not match.
std::optional<std::string>
extractInsertedTextAfterSelectAll(std::string_view selectedBefore, std::string_view currentAfter);

// Sanitize raw clipboard UTF-8 for single-line chat input: trims trailing CR/LF, normalizes internal
// newlines/tabs to spaces, skips other control characters, and caps output at `maxCodepoints`.
std::string sanitizeClipboardForChat(std::string_view raw, size_t maxCodepoints = 512);

// Compute pale-gray inline completion suffix shown immediately after `input` when the
// selected suggestion extends the active prefix.
std::string computeInlineGhostSuffix(std::string_view input, std::string_view selectedToken);

// Format a slash command with Java Edition Brigadier syntax colors using Bedrock § codes.
// Preserves `stripFormatting(result) == input`.
std::string formatJavaCommandSyntax(
    std::string_view                input,
    std::vector<std::string> const& suggestionTokens,
    bool                            hasIntellisenseUsage,
    bool                            knownCommandValid = false
);

// Stateful controller for Java-style command suggestions (selection, scrolling, Tab cycling,
// click acceptance, and layout geometry).
class CommandSuggestionState {
public:
    void reset() noexcept;
    void clearKnownCommands() noexcept;

    void syncFromBedrock(
        std::string_view                currentInput,
        std::vector<std::string> const& rawAutoCompleteTexts,
        bool                            intellisensePending = false
    );

    bool isKnownCommand(std::string_view input) const noexcept;

    bool                            active() const noexcept { return !mItems.empty(); }
    std::vector<std::string> const& items() const noexcept { return mItems; }
    int                             selectedIndex() const noexcept { return mSelectedIndex; }
    int                             scrollOffset() const noexcept { return mScrollOffset; }

    std::string_view selectedToken() const noexcept;

    void moveSelection(int delta) noexcept;
    bool setSelectedIndex(int idx) noexcept;
    void scrollByRows(int deltaRows) noexcept;

    std::optional<SuggestionApplyResult> applyByTab(int direction);
    std::optional<SuggestionApplyResult> applyByClick(int itemIndex);

    SuggestionPopupLayout buildLayout(
        float                screenW,
        float                screenH,
        float                inputTextOriginX,
        float                fontScale,
        TextMeasureFn const& measure
    ) const;

    static std::optional<int> hitTestRow(SuggestionPopupLayout const& layout, float mx, float my) noexcept;

private:
    void ensureSelectedVisible() noexcept;
    void clampState() noexcept;

    std::string                     mLastInput;
    std::vector<std::string>        mItems;
    int                             mSelectedIndex = 0;
    int                             mScrollOffset  = 0;

    std::vector<std::string>        mLastRawItems;
    int                             mLastRawArgIndex   = -1;
    size_t                          mLastRawTokenStart = 0;
    bool                            mLastRawIsSlash    = false;
    std::unordered_set<std::string> mKnownCommands;

    bool                     mTabCycling = false;
    std::string              mTabCycleBaseInput;
    std::vector<std::string> mTabCycleItems;
    int                      mTabCycleIndex = 0;
    std::string              mLastTabAppliedInput;
};

} // namespace javachat
