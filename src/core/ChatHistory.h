#pragma once

#include "core/ChatConfig.h"
#include "core/ChatFormatter.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace javachat {

struct Rect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    float width() const noexcept { return x1 - x0; }
    float height() const noexcept { return y1 - y0; }
    bool  contains(float x, float y) const noexcept { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

struct GuiMessageSnapshot {
    std::string fullText;
    float       duration    = 0.0f;
    int         messageType = 0;
};

struct CachedWrappedLine {
    std::string                text;
    std::optional<std::string> lineUrl;
};

struct ChatEntry {
    uint64_t                   id            = 0;
    std::string                rawText;        // Original formatted text (with § codes)
    std::string                plainText;      // Clean text without § codes (for clipboard copy)
    std::string                timestampText;  // e.g. "§8[02:55] §r" when captured
    std::optional<std::string> firstUrl;
    double                     receivedAtSec = 0.0;
    int                        repeatCount   = 1;
    int                        messageType   = 0;

    // Per-entry wrapped line cache (invalidated when layout/display/font parameters change).
    mutable std::vector<CachedWrappedLine> cachedWrappedLines;
    mutable float                          cachedMaxWidth      = -1.0f;
    mutable float                          cachedFontScale     = -1.0f;
    mutable int                            cachedRepeatCount   = -1;
    mutable uint32_t                       cachedFontRevision  = 0;
    mutable bool                           cachedTimestamps    = false;
    mutable bool                           cachedCompact       = false;
};

struct RenderedChatLine {
    uint64_t                   entryId     = 0;
    int                        messageType = 0;
    std::string                text;
    std::optional<std::string> lineUrl;
    Rect                       rect;
    float                      bgAlpha     = 0.0f;
    float                      textAlpha   = 0.0f;
    bool                       hasUrl      = false;
    bool                       isFirstLine = true;
};

struct ChatViewLayout {
    Rect                          panelRect;
    Rect                          scrollbarTrackRect;
    Rect                          scrollbarThumbRect;
    bool                          showScrollbar     = false;
    int                           totalWrappedLines = 0;
    int                           maxVisibleLines   = 0;
    int                           clampedScroll     = 0;
    std::vector<RenderedChatLine> lines; // Top-to-bottom for drawing
};

class ChatHistory {
public:
    // O(1) fast-path check to skip full GuiMessage copying on unchanged frames.
    bool needsSync(
        size_t           currentCount,
        std::string_view firstFull,
        int              firstType,
        std::string_view lastFull,
        int              lastType,
        float            lastDuration,
        double           nowSec
    ) const noexcept;

    // True if at least one entry would still be visible in closed HUD mode at `nowSec`.
    bool hasVisibleClosedMessages(double nowSec, ChatConfig const& cfg) const noexcept;

    // Synchronize incoming snapshots from GuiData::mGuiMessages.
    // Returns the number of newly ingested messages.
    size_t syncFromGuiMessages(
        std::span<GuiMessageSnapshot const> current,
        double                              nowSec,
        ChatConfig const&                   cfg,
        std::string_view                    timestampPrefix = {}
    );

    // Push a single message directly (also used by tests / local notices).
    void pushMessage(
        std::string_view  fullText,
        double            nowSec,
        ChatConfig const& cfg,
        int               messageType     = 1,
        std::string_view  timestampPrefix = {}
    );

    // Clear all stored messages and reset scroll.
    void clear() noexcept;

    // Immediately trim stored entries down to `maxHistoryMessages`.
    void trimToMax(int maxHistoryMessages) noexcept;

    // Reset only the GuiData mirror (e.g., on world switch when keeping history).
    void resetSyncCursor() noexcept;

    // Scroll by deltaLines (>0 scrolls up into older messages, <0 scrolls down toward latest).
    void scrollBy(int deltaLines) noexcept;
    void scrollToBottom() noexcept;
    void scrollToTop() noexcept;
    void setScrollFromTrackY(ChatViewLayout const& view, float my) noexcept;
    int  scrollOffset() const noexcept { return mScrollLines; }

    size_t                  size() const noexcept { return mEntries.size(); }
    std::deque<ChatEntry> const& entries() const noexcept { return mEntries; }
    ChatEntry const*        findById(uint64_t id) const noexcept;

    // Compute visible Java-style chat layout for the current frame.
    // `bottomOffsetOverride`: when > 0, acts as a minimum bottom offset floor
    // `max(cfg.bottomOffset, bottomOffsetOverride)` (e.g. to sit cleanly above
    // a tall command autocomplete popup without ever pushing chat lower than cfg.bottomOffset).
    ChatViewLayout buildView(
        float                screenW,
        float                screenH,
        bool                 chatOpen,
        double               nowSec,
        ChatConfig const&    cfg,
        TextMeasureFn const& measure,
        float                bottomOffsetOverride = -1.0f,
        uint32_t             fontRevision         = 0
    );

    // Return the index in `view.lines` containing (mx, my), or std::nullopt.
    static std::optional<size_t> hitTestLine(ChatViewLayout const& view, float mx, float my) noexcept;

    // True when (mx, my) falls inside or right next to the scrollbar track.
    static bool hitTestScrollbar(ChatViewLayout const& view, float mx, float my) noexcept;

    // Compute Java-style message alpha (0.0 .. 1.0) for a given age.
    static float computeFadeAlpha(double ageSec, bool chatOpen, ChatConfig const& cfg) noexcept;

private:
    std::deque<ChatEntry>           mEntries;
    std::vector<GuiMessageSnapshot> mLastGuiSnapshot;
    double                          mLastSyncSec = 0.0;
    uint64_t                        mNextId      = 1;
    int                             mScrollLines = 0;
    bool                            mScrollToTopPending = false;
    uint64_t                        mAnchoredEntryId    = 0;
    size_t                          mAnchoredLineIndex  = 0;
    bool                            mAnchorPending      = false;
};

} // namespace javachat
