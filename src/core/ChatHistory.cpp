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

std::vector<CachedWrappedLine> const& getWrappedLinesCached(
    ChatEntry const&     e,
    float                textMaxW,
    ChatConfig const&    cfg,
    TextMeasureFn const& measure,
    uint32_t             fontRevision,
    float                headIndent
) {
    bool const hit = !e.cachedWrappedLines.empty()
                  && std::fabs(e.cachedMaxWidth - textMaxW) < 0.05f
                  && std::fabs(e.cachedFontScale - cfg.fontScale) < 0.001f
                  && e.cachedRepeatCount == e.repeatCount
                  && e.cachedFontRevision == fontRevision
                  && e.cachedTimestamps == cfg.showTimestamps
                  && e.cachedCompact == cfg.compactDuplicates
                  && std::fabs(e.cachedHeadIndent - headIndent) < 0.05f;
    if (!hit) {
        std::string display = buildDisplayText(e, cfg);
        auto        rawLines = wrapFormattedText(display, std::max(24.0f, textMaxW - headIndent), measure);
        e.cachedWrappedLines.clear();
        e.cachedWrappedLines.reserve(rawLines.size());
        bool const mightHaveUrl = e.firstUrl.has_value();
        for (auto& line : rawLines) {
            CachedWrappedLine cached;
            cached.text = std::move(line);
            if (mightHaveUrl) {
                cached.lineUrl = extractFirstUrl(cached.text);
            }
            e.cachedWrappedLines.push_back(std::move(cached));
        }
        e.cachedMaxWidth     = textMaxW;
        e.cachedFontScale    = cfg.fontScale;
        e.cachedRepeatCount  = e.repeatCount;
        e.cachedFontRevision = fontRevision;
        e.cachedTimestamps   = cfg.showTimestamps;
        e.cachedCompact      = cfg.compactDuplicates;
        e.cachedHeadIndent   = headIndent;
    }
    return e.cachedWrappedLines;
}

} // namespace

float ChatHistory::computeFadeAlpha(double ageSec, bool chatOpen, ChatConfig const& cfg) noexcept {
    if (chatOpen) return 1.0f;
    if (ageSec < 0.0) return 1.0f;
    double delay = std::max(0.0, static_cast<double>(cfg.fadeDelaySec));
    if (ageSec <= delay) return 1.0f;
    if (cfg.fadeDurationSec <= 0.001f) return 0.0f;
    double dur = static_cast<double>(cfg.fadeDurationSec);
    double t   = (ageSec - delay) / dur;
    if (t >= 1.0) return 0.0f;
    // Smooth Java-like quadratic fade at the tail end.
    double rem = 1.0 - t;
    return static_cast<float>(std::clamp(rem * rem, 0.0, 1.0));
}

bool ChatHistory::isDurationDiscontinuity(float currDuration, double nowSec) const noexcept {
    if (mLastGuiSnapshot.empty() || !std::isfinite(currDuration)) return false;

    float const  snapDur = mLastGuiSnapshot.back().duration;
    double const dtSnap  = std::max(0.0, nowSec - mLastSyncSec);
    if (std::isfinite(snapDur)) {
        double const jumpSnap = std::fabs(static_cast<double>(currDuration) - static_cast<double>(snapDur));
        if (jumpSnap > std::max(0.5, dtSnap * 2.5)) {
            return true;
        }
    }

    if (mHasObservedBack && std::isfinite(mLastObservedBackDuration) && nowSec >= mLastObservedSec) {
        double const dtObs = nowSec - mLastObservedSec;
        double const delta = static_cast<double>(currDuration) - static_cast<double>(mLastObservedBackDuration);
        // Direction reversal (e.g. countdown jumped back up on a fresh identical message, or count-up reset down).
        if ((mObservedDurationDir < 0 && delta > 0.03) || (mObservedDurationDir > 0 && delta < -0.03)) {
            return true;
        }
        // Sudden jump relative to the previous frame's observation (short dt even long after last sync).
        if (std::fabs(delta) > std::max(0.5, dtObs * 2.5)) {
            return true;
        }
    }
    return false;
}

bool ChatHistory::needsSync(
    size_t           currentCount,
    std::string_view firstFull,
    int              firstType,
    std::string_view lastFull,
    int              lastType,
    float            lastDuration,
    double           nowSec
) const noexcept {
    if (currentCount == 0) {
        return !mLastGuiSnapshot.empty();
    }
    if (currentCount != mLastGuiSnapshot.size()) {
        return true;
    }
    auto const& front = mLastGuiSnapshot.front();
    auto const& back  = mLastGuiSnapshot.back();
    if (firstType != front.messageType || lastType != back.messageType
        || firstFull != front.fullText || lastFull != back.fullText) {
        return true;
    }
    if (isDurationDiscontinuity(lastDuration, nowSec)) {
        return true;
    }

    // Track per-frame duration progression so a later reset back to the initial duration is caught.
    if (std::isfinite(lastDuration)) {
        if (mHasObservedBack && std::isfinite(mLastObservedBackDuration) && mObservedDurationDir == 0) {
            double const delta = static_cast<double>(lastDuration) - static_cast<double>(mLastObservedBackDuration);
            if (delta < -0.01) {
                mObservedDurationDir = -1;
            } else if (delta > 0.01) {
                mObservedDurationDir = +1;
            }
        }
        mLastObservedBackDuration = lastDuration;
        mLastObservedSec          = nowSec;
        mHasObservedBack          = true;
    }
    return false;
}

bool ChatHistory::hasVisibleClosedMessages(double nowSec, ChatConfig const& cfg) const noexcept {
    if (mEntries.empty()) return false;
    return computeFadeAlpha(nowSec - mEntries.back().receivedAtSec, false, cfg) > 0.001f;
}

void ChatHistory::pushMessage(
    std::string_view  fullText,
    double            nowSec,
    ChatConfig const& cfg,
    int               messageType,
    std::string_view  timestampPrefix,
    std::string_view  headKey
) {
    if (fullText.empty()) return;

    if (cfg.compactDuplicates && !mEntries.empty() && mEntries.back().rawText == fullText
        && mEntries.back().headKey == headKey) {
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
    entry.headKey.assign(headKey);
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
    std::string_view                    timestampPrefix,
    HeadKeyResolver const&              headResolver
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
            // catches both large jumps from last sync and direction reversals / jumps vs last frame observation.
            if (isDurationDiscontinuity(current.back().duration, nowSec)) {
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
        std::string headKey = headResolver ? headResolver(current[i]) : std::string{};
        pushMessage(current[i].fullText, nowSec, cfg, current[i].messageType, timestampPrefix, headKey);
        ++added;
    }

    mLastGuiSnapshot.assign(current.begin(), current.end());
    mLastSyncSec              = nowSec;
    mLastObservedBackDuration = current.back().duration;
    mLastObservedSec          = nowSec;
    mObservedDurationDir      = 0;
    mHasObservedBack          = std::isfinite(current.back().duration);
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
    mLastObservedBackDuration = 0.0f;
    mLastObservedSec          = 0.0;
    mObservedDurationDir      = 0;
    mHasObservedBack          = false;
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
    float                bottomOffsetOverride,
    uint32_t             fontRevision,
    std::string_view     searchQuery,
    float                headIndent
) {
    ChatViewLayout view;
    if (!(headIndent > 0.0f) || !std::isfinite(headIndent)) headIndent = 0.0f;
    auto indentFor = [headIndent](ChatEntry const& e) { return e.headKey.empty() ? 0.0f : headIndent; };
    if (!(screenW > 0.0f && screenH > 0.0f)) return view;

    float const padX            = 4.0f;
    float const scrollBarW      = chatOpen ? 2.0f : 0.0f;
    float const scrollGap       = chatOpen ? 2.0f : 0.0f;
    float const effectiveBottom = (bottomOffsetOverride > 0.0f)
        ? std::max(cfg.bottomOffset, bottomOffsetOverride)
        : cfg.bottomOffset;

    float maxPanelW = std::min(cfg.width, std::max(80.0f, screenW - cfg.leftOffset - 4.0f));
    float availH    = screenH - effectiveBottom - 8.0f;

    float const panelW      = maxPanelW;
    float const panelBottom = screenH - effectiveBottom;
    float const panelLeft   = cfg.leftOffset;
    float const panelRight  = panelLeft + panelW;
    view.panelRect          = Rect{panelLeft, panelBottom, panelRight, panelBottom};
    view.totalEntryCount    = static_cast<int>(mEntries.size());
    view.matchedEntryCount  = view.totalEntryCount;

    if (availH < 12.0f) return view;

    float maxH = chatOpen ? cfg.openHeight : cfg.closedHeight;
    maxH       = std::min(maxH, availH);

    float lineH      = std::max(9.0f, std::round(9.0f * cfg.fontScale + cfg.lineSpacing));
    int   maxVisible = std::max(1, static_cast<int>(std::floor(maxH / lineH)));
    view.maxVisibleLines = maxVisible;

    float textMaxW = std::max(24.0f, maxPanelW - padX * 2.0f - scrollBarW - scrollGap);

    if (!chatOpen) {
        mScrollLines        = 0;
        mScrollToTopPending = false;
        mAnchorPending      = false;
        mAnchoredEntryId    = 0;
        mAnchoredLineIndex  = 0;

        struct ClosedRef {
            size_t entryIdx;
            size_t lineIdx;
            float  fadeAlpha;
        };
        std::vector<ClosedRef> rev;
        rev.reserve(static_cast<size_t>(maxVisible));
        for (size_t ei = mEntries.size(); ei-- > 0;) {
            auto const& e    = mEntries[ei];
            float       fade = computeFadeAlpha(nowSec - e.receivedAtSec, false, cfg);
            if (fade <= 0.001f) break;
            auto const& wrapped = getWrappedLinesCached(e, textMaxW, cfg, measure, fontRevision, indentFor(e));
            for (size_t wi = wrapped.size(); wi-- > 0;) {
                rev.push_back({ei, wi, fade});
                if (static_cast<int>(rev.size()) >= maxVisible) break;
            }
            if (static_cast<int>(rev.size()) >= maxVisible) break;
        }

        int const visCount = static_cast<int>(rev.size());
        view.totalWrappedLines = visCount;
        view.clampedScroll     = 0;
        if (visCount == 0) return view;

        float const panelTop = panelBottom - static_cast<float>(visCount) * lineH;
        view.panelRect       = Rect{panelLeft, panelTop, panelRight, panelBottom};

        view.lines.reserve(static_cast<size_t>(visCount));
        for (int i = 0; i < visCount; ++i) {
            auto const& ref = rev[static_cast<size_t>(visCount - 1 - i)];
            auto const& e   = mEntries[ref.entryIdx];
            auto const& wl  = e.cachedWrappedLines[ref.lineIdx];
            float       y0  = panelTop + static_cast<float>(i) * lineH;
            RenderedChatLine rl;
            rl.entryId     = e.id;
            rl.messageType = e.messageType;
            rl.text        = wl.text;
            rl.lineUrl     = wl.lineUrl;
            rl.rect        = Rect{panelLeft, y0, panelRight, y0 + lineH};
            rl.bgAlpha     = cfg.backgroundOpacity * ref.fadeAlpha;
            rl.textAlpha   = cfg.textOpacity * ref.fadeAlpha;
            rl.hasUrl      = wl.lineUrl.has_value();
            rl.isFirstLine = (ref.lineIdx == 0);
            rl.textIndent  = indentFor(e);
            if (rl.isFirstLine && rl.textIndent > 0.0f) rl.headKey = e.headKey;
            view.lines.push_back(std::move(rl));
        }
        return view;
    }

    // Open chat: compute lightweight per-entry line spans without copying all 500 entries' strings.
    bool const filtering = !searchQuery.empty();
    view.isFiltered      = filtering;
    if (filtering) {
        view.matchedEntryCount = 0;
    }

    struct EntrySpan {
        size_t entryIdx;
        int    startLine;
        int    lineCount;
    };
    std::vector<EntrySpan> spans;
    spans.reserve(mEntries.size());
    int totalLines = 0;
    for (size_t ei = 0; ei < mEntries.size(); ++ei) {
        auto const& e = mEntries[ei];
        if (filtering) {
            bool const matched = containsIgnoreCaseAscii(e.plainText, searchQuery)
                              || (cfg.showTimestamps && !e.timestampText.empty()
                                  && containsIgnoreCaseAscii(stripFormatting(e.timestampText), searchQuery));
            if (!matched) continue;
            ++view.matchedEntryCount;
        }
        auto const& wrapped = getWrappedLinesCached(e, textMaxW, cfg, measure, fontRevision, indentFor(e));
        int const   lc      = static_cast<int>(wrapped.size());
        spans.push_back({ei, totalLines, lc});
        totalLines += lc;
    }

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
    } else if (mAnchorPending && mAnchoredEntryId != 0) {
        int targetIdx = -1;
        for (size_t si = spans.size(); si-- > 0;) {
            auto const& sp = spans[si];
            if (mEntries[sp.entryIdx].id == mAnchoredEntryId && sp.lineCount > 0) {
                int local = std::min(static_cast<int>(mAnchoredLineIndex), sp.lineCount - 1);
                targetIdx = sp.startLine + local;
                break;
            }
        }
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

    if (mScrollLines > 0 && visCount > 0) {
        int const bottomLineIdx = endIdx - 1;
        for (size_t si = spans.size(); si-- > 0;) {
            auto const& sp = spans[si];
            if (bottomLineIdx >= sp.startLine && bottomLineIdx < sp.startLine + sp.lineCount) {
                mAnchoredEntryId   = mEntries[sp.entryIdx].id;
                mAnchoredLineIndex = static_cast<size_t>(bottomLineIdx - sp.startLine);
                break;
            }
        }
    } else if (mScrollLines == 0) {
        mAnchoredEntryId   = 0;
        mAnchoredLineIndex = 0;
    }

    // Authentic Java Edition uses a consistent chat box width (default 320px).
    float const panelTop = panelBottom - static_cast<float>(visCount) * lineH;
    view.panelRect       = Rect{panelLeft, panelTop, panelRight, panelBottom};

    float const contentRight = panelRight - (scrollBarW + scrollGap);
    view.lines.reserve(static_cast<size_t>(visCount));
    int outIdx = 0;
    for (auto const& sp : spans) {
        int const spEnd = sp.startLine + sp.lineCount;
        if (spEnd <= startIdx) continue;
        if (sp.startLine >= endIdx) break;

        auto const& e  = mEntries[sp.entryIdx];
        int const   lo = std::max(sp.startLine, startIdx) - sp.startLine;
        int const   hi = std::min(spEnd, endIdx) - sp.startLine;
        for (int wi = lo; wi < hi; ++wi) {
            auto const& wl = e.cachedWrappedLines[static_cast<size_t>(wi)];
            float       y0 = panelTop + static_cast<float>(outIdx++) * lineH;
            RenderedChatLine rl;
            rl.entryId     = e.id;
            rl.messageType = e.messageType;
            rl.text        = filtering ? highlightFormattedMatches(wl.text, searchQuery, 'e') : wl.text;
            rl.lineUrl     = wl.lineUrl;
            rl.rect        = Rect{panelLeft, y0, contentRight, y0 + lineH};
            rl.bgAlpha     = cfg.backgroundOpacity;
            rl.textAlpha   = cfg.textOpacity;
            rl.hasUrl      = wl.lineUrl.has_value();
            rl.isFirstLine = (wi == 0);
            rl.textIndent  = indentFor(e);
            if (rl.isFirstLine && rl.textIndent > 0.0f) rl.headKey = e.headKey;
            view.lines.push_back(std::move(rl));
        }
    }

    if (totalLines > maxVisible) {
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
