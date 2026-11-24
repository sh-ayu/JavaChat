#include "core/ChatHistory.h"

#include <algorithm>
#include <cmath>

namespace javachat {
namespace {

bool sameMessageIdentity(GuiMessageSnapshot const& a, GuiMessageSnapshot const& b) noexcept {
    return a.messageType == b.messageType && a.fullText == b.fullText;
}

std::string buildDisplayText(ChatEntry const& e, ChatConfig const& cfg) {
    std::string out;
    if (cfg.showTimestamps && !e.timestampText.empty()) {
        out += e.timestampText;
    }
    out += e.rawText;
    if (cfg.compactDuplicates && e.repeatCount > 1) {
        out += " \xC2\xA7" "7[x" + std::to_string(e.repeatCount) + "]";
    }
    return out;
}

std::vector<std::string> const&
getWrappedLinesCached(ChatEntry const& e, float textMaxW, ChatConfig const& cfg, TextMeasureFn const& measure) {
    bool const hit = !e.cachedWrappedLines.empty()
                  && std::fabs(e.cachedMaxWidth - textMaxW) < 0.05f
                  && std::fabs(e.cachedFontScale - cfg.fontScale) < 0.001f
                  && e.cachedRepeatCount == e.repeatCount
                  && e.cachedTimestamps == cfg.showTimestamps
                  && e.cachedCompact == cfg.compactDuplicates;
    if (!hit) {
        std::string display   = buildDisplayText(e, cfg);
        e.cachedWrappedLines  = wrapFormattedText(display, textMaxW, measure);
        e.cachedMaxWidth      = textMaxW;
        e.cachedFontScale     = cfg.fontScale;
        e.cachedRepeatCount   = e.repeatCount;
        e.cachedTimestamps    = cfg.showTimestamps;
        e.cachedCompact       = cfg.compactDuplicates;
    }
    return e.cachedWrappedLines;
}

} // namespace

float ChatHistory::computeFadeAlpha(double ageSec, bool chatOpen, ChatConfig const& cfg) noexcept {
    if (chatOpen) return 1.0f;
    if (ageSec < 0.0) return 1.0f;
    double delay = std::max(0.0, static_cast<double>(cfg.fadeDelaySec));
    double dur   = std::max(0.05, static_cast<double>(cfg.fadeDurationSec));
    if (ageSec <= delay) return 1.0f;
    double t = (ageSec - delay) / dur;
    if (t >= 1.0) return 0.0f;
    // Smooth Java-like quadratic fade at the tail end.
    double rem = 1.0 - t;
    return static_cast<float>(std::clamp(rem * rem, 0.0, 1.0));
}

void ChatHistory::pushMessage(
    std::string_view  fullText,
    double            nowSec,
    ChatConfig const& cfg,
    int               messageType,
    std::string_view  timestampPrefix
) {
    if (fullText.empty()) return;

    if (cfg.compactDuplicates && !mEntries.empty() && mEntries.back().rawText == fullText) {
        auto& back = mEntries.back();
        ++back.repeatCount;
        back.receivedAtSec = nowSec;
        if (!timestampPrefix.empty()) back.timestampText.assign(timestampPrefix);
        return;
    }

    ChatEntry entry;
    entry.id            = mNextId++;
    entry.rawText.assign(fullText);
    entry.plainText     = stripFormatting(fullText);
    entry.timestampText.assign(timestampPrefix);
    entry.firstUrl      = extractFirstUrl(fullText);
    entry.receivedAtSec = nowSec;
    entry.repeatCount   = 1;
    entry.messageType   = messageType;
    mEntries.push_back(std::move(entry));

    size_t cap     = static_cast<size_t>(std::max(cfg.maxHistoryMessages, 1));
    bool   evicted = false;
    while (mEntries.size() > cap) {
        mEntries.pop_front();
        evicted = true;
    }
    // Preserve exact viewport anchor when browsing scrolled-up history (including eviction).
    if (mScrollLines > 0 && mAnchoredEntryId != 0) {
        mAnchorPending = true;
        (void)evicted;
    } else if (mScrollLines > 0) {
        ++mScrollLines;
    }
}

size_t ChatHistory::syncFromGuiMessages(
    std::span<GuiMessageSnapshot const> current,
    double                              nowSec,
    ChatConfig const&                   cfg,
    std::string_view                    timestampPrefix
) {
    if (current.empty()) {
        mLastGuiSnapshot.clear();
        return 0;
    }

    size_t const prevN = mLastGuiSnapshot.size();
    size_t const currN = current.size();
    size_t       startNew = currN;

    if (prevN == 0) {
        // First observation in this session: ingest everything currently present.
        startNew = 0;
    } else if (currN > prevN) {
        // Fast path: vector grew by appending at the end.
        bool prefixMatch = true;
        for (size_t i = 0; i < prevN; ++i) {
            if (!sameMessageIdentity(mLastGuiSnapshot[i], current[i])) {
                prefixMatch = false;
                break;
            }
        }
        if (prefixMatch) {
            startNew = prevN;
        }
    } else if (currN == prevN) {
        bool allMatch = true;
        for (size_t i = 0; i < currN; ++i) {
            if (!sameMessageIdentity(mLastGuiSnapshot[i], current[i])) {
                allMatch = false;
                break;
            }
        }
        if (allMatch) {
            // Same list at capacity. Check if a brand-new identical message pushed the oldest one out:
            // whether mDuration counts down or up, a fresh message causes a discontinuity far larger
            // than elapsed wall time since the last frame.
            float  prevDur = mLastGuiSnapshot.back().duration;
            float  currDur = current.back().duration;
            double dt      = std::max(0.0, nowSec - mLastSyncSec);
            double jump    = std::fabs(static_cast<double>(currDur) - static_cast<double>(prevDur));
            if (std::isfinite(prevDur) && std::isfinite(currDur) && jump > std::max(0.5, dt * 2.5)) {
                startNew = currN - 1;
            } else {
                startNew = currN; // No change
            }
        }
    }

    // General overlap search (handles ring-buffer eviction when GuiData shifts old items out).
    if (startNew == currN) {
        bool alreadyDecidedUnchanged = (currN == prevN);
        if (alreadyDecidedUnchanged) {
            for (size_t i = 0; i < currN; ++i) {
                if (!sameMessageIdentity(mLastGuiSnapshot[i], current[i])) {
                    alreadyDecidedUnchanged = false;
                    break;
                }
            }
        }
        if (!alreadyDecidedUnchanged) {
            size_t maxOverlap = std::min(prevN, currN);
            size_t matched    = 0;
            // Find the longest suffix of `mLastGuiSnapshot` that matches a prefix of `current`.
            for (size_t k = maxOverlap; k >= 1; --k) {
                bool ok = true;
                for (size_t j = 0; j < k; ++j) {
                    if (!sameMessageIdentity(mLastGuiSnapshot[prevN - k + j], current[j])) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    matched = k;
                    break;
                }
            }
            startNew = matched;
        }
    }

    size_t added = 0;
    for (size_t i = startNew; i < currN; ++i) {
        pushMessage(current[i].fullText, nowSec, cfg, current[i].messageType, timestampPrefix);
        ++added;
    }

    mLastGuiSnapshot.assign(current.begin(), current.end());
    mLastSyncSec = nowSec;
    return added;
}

void ChatHistory::clear() noexcept {
    mEntries.clear();
    mScrollLines        = 0;
    mScrollToTopPending = false;
    mAnchoredEntryId    = 0;
    mAnchoredLineIndex  = 0;
    mAnchorPending      = false;
}

void ChatHistory::trimToMax(int maxHistoryMessages) noexcept {
    size_t cap     = static_cast<size_t>(std::max(maxHistoryMessages, 1));
    bool   evicted = false;
    while (mEntries.size() > cap) {
        mEntries.pop_front();
        evicted = true;
    }
    if (mEntries.empty()) {
        mScrollLines        = 0;
        mScrollToTopPending = false;
        mAnchoredEntryId    = 0;
        mAnchoredLineIndex  = 0;
        mAnchorPending      = false;
    } else if (evicted && mScrollLines > 0 && mAnchoredEntryId != 0) {
        mAnchorPending = true;
    }
}

void ChatHistory::resetSyncCursor() noexcept {
    mLastGuiSnapshot.clear();
}

void ChatHistory::scrollBy(int deltaLines) noexcept {
    mScrollToTopPending = false;
    mAnchorPending      = false;
    mScrollLines        = std::max(0, mScrollLines + deltaLines);
}

void ChatHistory::scrollToBottom() noexcept {
    mScrollToTopPending = false;
    mAnchorPending      = false;
    mScrollLines        = 0;
    mAnchoredEntryId    = 0;
    mAnchoredLineIndex  = 0;
}

void ChatHistory::scrollToTop() noexcept {
    mScrollToTopPending = true;
    mAnchorPending      = false;
}

void ChatHistory::setScrollFromTrackY(ChatViewLayout const& view, float my) noexcept {
    if (!view.showScrollbar || view.totalWrappedLines <= view.maxVisibleLines) return;
    float trackH    = view.scrollbarTrackRect.height();
    float thumbH    = view.scrollbarThumbRect.height();
    float usable    = trackH - thumbH;
    float t         = (usable > 0.5f)
        ? std::clamp((my - (view.scrollbarTrackRect.y0 + thumbH * 0.5f)) / usable, 0.0f, 1.0f)
        : 0.0f;
    int   maxScroll = std::max(0, view.totalWrappedLines - view.maxVisibleLines);
    mScrollLines = std::clamp(static_cast<int>(std::lround((1.0f - t) * static_cast<float>(maxScroll))), 0, maxScroll);
    mScrollToTopPending = false;
    mAnchorPending      = false;
}

ChatEntry const* ChatHistory::findById(uint64_t id) const noexcept {
    for (auto it = mEntries.rbegin(); it != mEntries.rend(); ++it) {
        if (it->id == id) return &(*it);
    }
    return nullptr;
}

ChatViewLayout ChatHistory::buildView(
    float                screenW,
    float                screenH,
    bool                 chatOpen,
    double               nowSec,
    ChatConfig const&    cfg,
    TextMeasureFn const& measure,
    float                bottomOffsetOverride
) {
    ChatViewLayout view;
    if (!(screenW > 0.0f && screenH > 0.0f)) return view;

    float const padX            = 4.0f;
    float const padY            = 0.0f;
    float const scrollBarW      = chatOpen ? 2.0f : 0.0f;
    float const scrollGap       = chatOpen ? 2.0f : 0.0f;
    float const effectiveBottom = (bottomOffsetOverride > 0.0f)
        ? std::max(cfg.bottomOffset, bottomOffsetOverride)
        : cfg.bottomOffset;

    float maxPanelW = std::min(cfg.width, std::max(80.0f, screenW - cfg.leftOffset - 4.0f));
    float availH    = screenH - effectiveBottom - 8.0f;
    if (availH < 12.0f) return view;

    float maxH = chatOpen ? cfg.openHeight : cfg.closedHeight;
    maxH       = std::min(maxH, availH);

    float lineH      = std::max(9.0f, std::round(9.0f * cfg.fontScale + cfg.lineSpacing));
    int   maxVisible = std::max(1, static_cast<int>(std::floor(maxH / lineH)));
    view.maxVisibleLines = maxVisible;

    float textMaxW = std::max(24.0f, maxPanelW - padX * 2.0f - scrollBarW - scrollGap);

    struct WrappedRow {
        uint64_t    entryId;
        size_t      lineIndex;
        int         messageType;
        std::string text;
        float       fadeAlpha;
        bool        hasUrl;
        bool        isFirstLine;
    };

    std::vector<WrappedRow> allRows;
    if (!chatOpen) {
        mScrollLines        = 0;
        mScrollToTopPending = false;
        mAnchorPending      = false;
        mAnchoredEntryId    = 0;
        mAnchoredLineIndex  = 0;
        std::vector<WrappedRow> rev;
        for (auto it = mEntries.rbegin(); it != mEntries.rend(); ++it) {
            float fade = computeFadeAlpha(nowSec - it->receivedAtSec, false, cfg);
            if (fade <= 0.001f) break;
            auto const& wrapped = getWrappedLinesCached(*it, textMaxW, cfg, measure);
            for (size_t wi = wrapped.size(); wi-- > 0;) {
                rev.push_back({it->id, wi, it->messageType, wrapped[wi], fade, it->firstUrl.has_value(), wi == 0});
                if (static_cast<int>(rev.size()) >= maxVisible) break;
            }
            if (static_cast<int>(rev.size()) >= maxVisible) break;
        }
        allRows.assign(std::make_move_iterator(rev.rbegin()), std::make_move_iterator(rev.rend()));
    } else {
        for (auto const& e : mEntries) {
            auto const& wrapped = getWrappedLinesCached(e, textMaxW, cfg, measure);
            for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                allRows.push_back({e.id, wi, e.messageType, wrapped[wi], 1.0f, e.firstUrl.has_value(), wi == 0});
            }
        }
    }

    int const totalLines   = static_cast<int>(allRows.size());
    view.totalWrappedLines = totalLines;
    if (totalLines == 0) {
        mScrollLines        = 0;
        mScrollToTopPending = false;
        mAnchorPending      = false;
        mAnchoredEntryId    = 0;
        mAnchoredLineIndex  = 0;
        return view;
    }

    int const maxScroll = std::max(0, totalLines - maxVisible);
    if (mScrollToTopPending) {
        mScrollLines        = maxScroll;
        mScrollToTopPending = false;
        mAnchorPending      = false;
    } else if (chatOpen && mAnchorPending && mAnchoredEntryId != 0) {
        int foundExact    = -1;
        int foundFallback = -1;
        for (int idx = totalLines - 1; idx >= 0; --idx) {
            auto const& r = allRows[static_cast<size_t>(idx)];
            if (r.entryId == mAnchoredEntryId) {
                if (foundFallback < 0) foundFallback = idx;
                if (r.lineIndex == mAnchoredLineIndex) {
                    foundExact = idx;
                    break;
                }
            }
        }
        int targetIdx = (foundExact >= 0) ? foundExact : foundFallback;
        if (targetIdx >= 0) {
            mScrollLines = totalLines - 1 - targetIdx;
        } else {
            mScrollLines = maxScroll;
        }
        mAnchorPending = false;
    }
    mScrollLines       = std::clamp(mScrollLines, 0, maxScroll);
    view.clampedScroll = mScrollLines;

    int const endIdx   = totalLines - mScrollLines;
    int const startIdx = std::max(0, endIdx - maxVisible);
    int const visCount = endIdx - startIdx;

    if (chatOpen && mScrollLines > 0 && visCount > 0) {
        auto const& bottomRow = allRows[static_cast<size_t>(endIdx - 1)];
        mAnchoredEntryId      = bottomRow.entryId;
        mAnchoredLineIndex    = bottomRow.lineIndex;
    } else if (!chatOpen || mScrollLines == 0) {
        mAnchoredEntryId   = 0;
        mAnchoredLineIndex = 0;
    }

    // Authentic Java Edition uses a consistent chat box width (default 320px).
    float const panelW      = maxPanelW;
    float const panelBottom = screenH - effectiveBottom;
    float const panelTop    = panelBottom - static_cast<float>(visCount) * lineH;
    float const panelLeft   = cfg.leftOffset;
    float const panelRight  = panelLeft + panelW;
    view.panelRect          = Rect{panelLeft, panelTop, panelRight, panelBottom};

    float const contentRight = panelRight - (chatOpen ? (scrollBarW + scrollGap) : 0.0f);
    view.lines.reserve(static_cast<size_t>(visCount));
    for (int i = 0; i < visCount; ++i) {
        auto const& row = allRows[static_cast<size_t>(startIdx + i)];
        float       y0  = panelTop + static_cast<float>(i) * lineH;
        float       y1  = y0 + lineH;
        RenderedChatLine rl;
        rl.entryId     = row.entryId;
        rl.messageType = row.messageType;
        rl.text        = row.text;
        rl.lineUrl     = extractFirstUrl(row.text);
        rl.rect        = Rect{panelLeft, y0, contentRight, y1};
        rl.bgAlpha     = cfg.backgroundOpacity * row.fadeAlpha;
        rl.textAlpha   = cfg.textOpacity * row.fadeAlpha;
        rl.hasUrl      = rl.lineUrl.has_value() || row.hasUrl;
        rl.isFirstLine = row.isFirstLine;
        view.lines.push_back(std::move(rl));
    }

    if (chatOpen && totalLines > maxVisible) {
        view.showScrollbar      = true;
        float trackH            = std::max(8.0f, panelBottom - panelTop);
        float sbRight           = panelRight;
        float sbLeft            = sbRight - scrollBarW;
        view.scrollbarTrackRect = Rect{sbLeft, panelTop, sbRight, panelBottom};
        float thumbRatio        = static_cast<float>(visCount) / static_cast<float>(totalLines);
        float thumbH            = std::clamp(trackH * thumbRatio, 10.0f, trackH);
        float scrollRatio = maxScroll > 0 ? (static_cast<float>(mScrollLines) / static_cast<float>(maxScroll)) : 0.0f;
        float thumbY0     = panelTop + (trackH - thumbH) * (1.0f - scrollRatio);
        view.scrollbarThumbRect = Rect{sbLeft, thumbY0, sbRight, thumbY0 + thumbH};
    }

    return view;
}

std::optional<size_t> ChatHistory::hitTestLine(ChatViewLayout const& view, float mx, float my) noexcept {
    for (size_t i = 0; i < view.lines.size(); ++i) {
        if (view.lines[i].rect.contains(mx, my)) {
            return i;
        }
    }
    return std::nullopt;
}

bool ChatHistory::hitTestScrollbar(ChatViewLayout const& view, float mx, float my) noexcept {
    if (!view.showScrollbar) return false;
    Rect hitRect{
        view.scrollbarTrackRect.x0 - 3.0f,
        view.scrollbarTrackRect.y0,
        view.scrollbarTrackRect.x1 + 2.0f,
        view.scrollbarTrackRect.y1
    };
    return hitRect.contains(mx, my);
}

} // namespace javachat
