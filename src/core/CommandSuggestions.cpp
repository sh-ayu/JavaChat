#include "core/CommandSuggestions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace javachat {
namespace {

bool isAsciiWs(char c) noexcept {
    return c == ' ' || c == '\t';
}

char toLowerAsciiChar(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string toLowerAsciiString(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        out.push_back(toLowerAsciiChar(c));
    }
    return out;
}

bool equalsIgnoreCaseAscii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (toLowerAsciiChar(a[i]) != toLowerAsciiChar(b[i])) return false;
    }
    return true;
}

bool startsWithIgnoreCaseAscii(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) return false;
    return equalsIgnoreCaseAscii(text.substr(0, prefix.size()), prefix);
}

std::string_view trimAsciiWs(std::string_view sv) noexcept {
    while (!sv.empty() && isAsciiWs(sv.front())) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && isAsciiWs(sv.back())) {
        sv.remove_suffix(1);
    }
    return sv;
}

struct SplitSegment {
    size_t start = 0;
    size_t len   = 0;
    bool   isWs  = false;
};

std::vector<SplitSegment> splitCommandBody(std::string_view body) {
    std::vector<SplitSegment> segs;
    size_t                    pos = 0;
    while (pos < body.size()) {
        if (isAsciiWs(body[pos])) {
            size_t start = pos;
            while (pos < body.size() && isAsciiWs(body[pos])) {
                ++pos;
            }
            segs.push_back({start, pos - start, true});
        } else {
            size_t start    = pos;
            bool   inQuotes = false;
            while (pos < body.size()) {
                char c = body[pos];
                if (inQuotes) {
                    if (c == '\\' && pos + 1 < body.size()) {
                        pos += 2;
                        continue;
                    }
                    if (c == '"') {
                        inQuotes = false;
                    }
                    ++pos;
                } else {
                    if (isAsciiWs(c)) break;
                    if (c == '"') inQuotes = true;
                    ++pos;
                }
            }
            segs.push_back({start, pos - start, false});
        }
    }
    return segs;
}

std::string_view normalizeSuggestionForActiveSpan(
    ActiveTokenSpan const& span,
    std::string_view       suggestionToken
) noexcept {
    if (span.isSlashCommand && span.argIndex == 0 && span.startByte == 1 && suggestionToken.starts_with('/')) {
        suggestionToken.remove_prefix(1);
    }
    return suggestionToken;
}

} // namespace

std::string extractSuggestionToken(std::string_view rawBedrockText) {
    std::string      stripped = stripFormatting(rawBedrockText);
    std::string_view sv       = trimAsciiWs(stripped);
    if (sv.empty()) return {};

    // Find the first " - " outside quotes (Bedrock appends " - <command description>").
    bool inQuotes = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (inQuotes) {
            if (c == '\\' && i + 1 < sv.size()) {
                ++i;
                continue;
            }
            if (c == '"') {
                inQuotes = false;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (i + 3 <= sv.size() && sv.substr(i, 3) == " - ") {
                sv = sv.substr(0, i);
                break;
            }
        }
    }

    sv = trimAsciiWs(sv);
    if (sv.empty() || sv == "...") return {};
    return std::string(sv);
}

ActiveTokenSpan findActiveTokenSpan(std::string_view input) noexcept {
    if (input.empty()) return {};

    if (input.front() == '/') {
        size_t pos          = 1;
        int    argIndex     = 0;
        size_t lastTokStart = 1;
        int    lastArgIdx   = 0;
        bool   hasTok       = false;

        while (pos < input.size()) {
            while (pos < input.size() && isAsciiWs(input[pos])) {
                ++pos;
            }
            if (pos >= input.size()) {
                return ActiveTokenSpan{input.size(), input.size(), argIndex, true};
            }
            lastTokStart  = pos;
            lastArgIdx    = argIndex;
            hasTok        = true;
            bool inQuotes = false;
            while (pos < input.size()) {
                char c = input[pos];
                if (inQuotes) {
                    if (c == '\\' && pos + 1 < input.size()) {
                        pos += 2;
                        continue;
                    }
                    if (c == '"') inQuotes = false;
                    ++pos;
                } else {
                    if (isAsciiWs(c)) break;
                    if (c == '"') inQuotes = true;
                    ++pos;
                }
            }
            ++argIndex;
        }

        if (!hasTok) {
            return ActiveTokenSpan{1, 1, 0, true};
        }
        return ActiveTokenSpan{lastTokStart, input.size(), lastArgIdx, true};
    }

    // Plain message (e.g. @player mention): span starts after the last space/tab.
    size_t lastSpace = input.find_last_of(" \t");
    size_t start     = (lastSpace == std::string_view::npos) ? 0 : (lastSpace + 1);
    return ActiveTokenSpan{start, input.size(), 0, false};
}

size_t findSubTokenStart(
    std::string_view       input,
    ActiveTokenSpan const& span,
    std::string_view       suggestionToken
) noexcept {
    if (span.startByte >= span.endByte || span.endByte > input.size()) {
        return span.startByte;
    }
    std::string_view cur = input.substr(span.startByte, span.endByte - span.startByte);

    auto hasSelectorDelim = [](std::string_view s) noexcept {
        return s.find_first_of("[{,=") != std::string_view::npos;
    };

    if (hasSelectorDelim(cur) && !hasSelectorDelim(suggestionToken)) {
        size_t pos = cur.find_last_of("[{,=");
        if (pos != std::string_view::npos) {
            return span.startByte + pos + 1;
        }
    }

    size_t colonPos = cur.rfind(':');
    if (colonPos != std::string_view::npos && suggestionToken.find(':') == std::string_view::npos) {
        return span.startByte + colonPos + 1;
    }

    return span.startByte;
}

std::string applySuggestionToInput(std::string_view input, std::string_view suggestionToken) {
    ActiveTokenSpan  span     = findActiveTokenSpan(input);
    std::string_view tok      = normalizeSuggestionForActiveSpan(span, suggestionToken);
    size_t           repStart = findSubTokenStart(input, span, tok);

    std::string out;
    out.reserve(repStart + tok.size() + (input.size() - span.endByte));
    out.append(input.substr(0, repStart));
    out.append(tok);
    out.append(input.substr(span.endByte));
    return out;
}

std::optional<std::string>
extractInsertedTextAfterSelectAll(std::string_view selectedBefore, std::string_view currentAfter) {
    if (selectedBefore.empty() || currentAfter.size() <= selectedBefore.size()) {
        return std::nullopt;
    }

    size_t posBefore = 0;
    size_t posAfter  = 0;
    while (posBefore < selectedBefore.size() && posAfter < currentAfter.size()) {
        size_t         nextBefore = posBefore;
        size_t         nextAfter  = posAfter;
        uint32_t const cpBefore   = nextUtf8Codepoint(selectedBefore, nextBefore);
        uint32_t const cpAfter    = nextUtf8Codepoint(currentAfter, nextAfter);
        if (cpBefore != cpAfter) {
            break;
        }
        if (selectedBefore.substr(posBefore, nextBefore - posBefore)
            != currentAfter.substr(posAfter, nextAfter - posAfter)) {
            break;
        }
        posBefore = nextBefore;
        posAfter  = nextAfter;
    }

    size_t const prefix = posBefore;
    size_t const delta  = currentAfter.size() - selectedBefore.size();
    if (prefix + delta > currentAfter.size()) {
        return std::nullopt;
    }
    if (currentAfter.substr(prefix + delta) != selectedBefore.substr(prefix)) {
        return std::nullopt;
    }
    return std::string(currentAfter.substr(prefix, delta));
}

std::string sanitizeClipboardForChat(std::string_view raw, size_t maxCodepoints) {
    while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }

    std::string out;
    out.reserve(std::min<size_t>(raw.size(), maxCodepoints * 4));
    size_t pos        = 0;
    size_t emittedCps = 0;
    while (pos < raw.size() && emittedCps < maxCodepoints) {
        size_t const   start = pos;
        uint32_t const cp    = nextUtf8Codepoint(raw, pos);
        if (cp == '\r') {
            if (pos < raw.size() && raw[pos] == '\n') {
                ++pos;
            }
            out.push_back(' ');
            ++emittedCps;
        } else if (cp == '\n' || cp == '\t') {
            out.push_back(' ');
            ++emittedCps;
        } else if (cp < 0x20 || cp == 0x7F) {
            continue;
        } else {
            out.append(raw.substr(start, pos - start));
            ++emittedCps;
        }
    }
    return out;
}

std::string computeInlineGhostSuffix(std::string_view input, std::string_view selectedToken) {
    if (input.empty() || selectedToken.empty()) return {};

    ActiveTokenSpan  span     = findActiveTokenSpan(input);
    std::string_view tok      = normalizeSuggestionForActiveSpan(span, selectedToken);
    if (tok.empty()) return {};

    size_t           repStart = findSubTokenStart(input, span, tok);
    std::string_view prefix   = input.substr(repStart, span.endByte - repStart);
    if (prefix.empty()) {
        return std::string(tok);
    }
    if (tok.size() > prefix.size() && startsWithIgnoreCaseAscii(tok, prefix)) {
        return std::string(tok.substr(prefix.size()));
    }
    return {};
}

std::string formatJavaCommandSyntax(
    std::string_view                input,
    std::vector<std::string> const& suggestionTokens,
    bool                            hasIntellisenseUsage,
    bool                            knownCommandValid
) {
    if (input.empty() || input.front() != '/' || input.find("\xC2\xA7") != std::string_view::npos) {
        return std::string(input);
    }

    std::string out;
    out.reserve(input.size() + 32);
    out.append("\xC2\xA7" "7/");

    std::string_view body = input.substr(1);
    if (body.empty()) {
        return out;
    }

    auto segs = splitCommandBody(body);
    int  cmdSegIdx = -1;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (!segs[i].isWs) {
            cmdSegIdx = static_cast<int>(i);
            break;
        }
    }
    if (cmdSegIdx < 0) {
        out.append(body);
        return out;
    }

    std::string_view cmdName = body.substr(segs[cmdSegIdx].start, segs[cmdSegIdx].len);
    bool const       hasArgsOrTrailingSpace = (static_cast<size_t>(cmdSegIdx + 1) < segs.size());

    bool cmdValid = false;
    if (!hasArgsOrTrailingSpace) {
        if (!suggestionTokens.empty()) {
            for (std::string const& s : suggestionTokens) {
                std::string_view sv = s;
                if (sv.starts_with('/')) sv.remove_prefix(1);
                if (equalsIgnoreCaseAscii(cmdName, sv)) {
                    cmdValid = true;
                    break;
                }
            }
        }
        if (!cmdValid) {
            if (knownCommandValid) {
                cmdValid = true;
            } else if (suggestionTokens.empty()) {
                cmdValid = hasIntellisenseUsage;
            }
        }
    } else {
        cmdValid = hasIntellisenseUsage || !suggestionTokens.empty() || knownCommandValid;
    }

    if (!cmdValid) {
        for (int i = 0; i < cmdSegIdx; ++i) {
            out.append(body.substr(segs[i].start, segs[i].len));
        }
        out.append("\xC2\xA7" "c");
        out.append(body.substr(segs[cmdSegIdx].start));
        return out;
    }

    static constexpr std::array<char, 5> kArgColors{'b', 'e', 'a', 'd', '6'};
    int                                  argOrdinal = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        std::string_view segSv = body.substr(segs[i].start, segs[i].len);
        if (segs[i].isWs || static_cast<int>(i) <= cmdSegIdx) {
            out.append(segSv);
        } else {
            char code = kArgColors[static_cast<size_t>(argOrdinal++) % kArgColors.size()];
            out.append("\xC2\xA7");
            out.push_back(code);
            out.append(segSv);
        }
    }
    return out;
}

void CommandSuggestionState::reset() noexcept {
    mLastInput.clear();
    mItems.clear();
    mSelectedIndex = 0;
    mScrollOffset  = 0;
    mLastRawItems.clear();
    mLastRawArgIndex   = -1;
    mLastRawTokenStart = 0;
    mLastRawIsSlash    = false;
    mTabCycling        = false;
    mTabCycleBaseInput.clear();
    mTabCycleItems.clear();
    mTabCycleIndex = 0;
    mLastTabAppliedInput.clear();
}

void CommandSuggestionState::clearKnownCommands() noexcept {
    mKnownCommands.clear();
}

bool CommandSuggestionState::isKnownCommand(std::string_view input) const noexcept {
    if (input.empty() || input.front() != '/' || mKnownCommands.empty()) {
        return false;
    }
    try {
        std::string_view body = input.substr(1);
        auto             segs = splitCommandBody(body);
        for (auto const& seg : segs) {
            if (!seg.isWs) {
                std::string_view cmdName = body.substr(seg.start, seg.len);
                return mKnownCommands.contains(toLowerAsciiString(cmdName));
            }
        }
    } catch (...) {
    }
    return false;
}

void CommandSuggestionState::syncFromBedrock(
    std::string_view                currentInput,
    std::vector<std::string> const& rawAutoCompleteTexts,
    bool                            intellisensePending
) {
    if (mTabCycling && currentInput == mLastTabAppliedInput && !mTabCycleItems.empty()) {
        return;
    }

    mTabCycling = false;
    mTabCycleBaseInput.clear();
    mTabCycleItems.clear();
    mTabCycleIndex = 0;
    mLastTabAppliedInput.clear();

    std::vector<std::string> rawItems;
    if (!currentInput.empty() && !rawAutoCompleteTexts.empty()) {
        rawItems.reserve(rawAutoCompleteTexts.size());
        std::unordered_set<std::string> seen;
        seen.reserve(rawAutoCompleteTexts.size());
        for (std::string const& raw : rawAutoCompleteTexts) {
            std::string tok = extractSuggestionToken(raw);
            if (tok.empty()) continue;
            if (seen.insert(tok).second) {
                rawItems.push_back(std::move(tok));
            }
        }
    }

    ActiveTokenSpan const    curSpan        = findActiveTokenSpan(currentInput);
    bool const               rawChanged     = (rawItems != mLastRawItems);
    bool const               isStalePending = intellisensePending && !rawChanged && !mLastRawItems.empty();
    std::vector<std::string> newItems;

    if (!isStalePending) {
        mLastRawItems      = rawItems;
        mLastRawArgIndex   = curSpan.argIndex;
        mLastRawTokenStart = curSpan.startByte;
        mLastRawIsSlash    = curSpan.isSlashCommand;

        if (curSpan.isSlashCommand && curSpan.argIndex == 0 && mKnownCommands.size() < 2048) {
            for (std::string const& tok : mLastRawItems) {
                if (mKnownCommands.size() >= 2048) break;
                std::string_view sv = tok;
                if (sv.starts_with('/')) sv.remove_prefix(1);
                if (!sv.empty()) {
                    mKnownCommands.insert(toLowerAsciiString(sv));
                }
            }
        }

        newItems = std::move(rawItems);
    } else {
        bool const sameSlot = (curSpan.isSlashCommand == mLastRawIsSlash)
                           && (curSpan.argIndex == mLastRawArgIndex)
                           && (curSpan.startByte == mLastRawTokenStart);
        if (sameSlot) {
            newItems.reserve(mLastRawItems.size());
            for (std::string const& item : mLastRawItems) {
                std::string_view norm     = normalizeSuggestionForActiveSpan(curSpan, item);
                size_t const     repStart = findSubTokenStart(currentInput, curSpan, norm);
                std::string_view prefix   = currentInput.substr(repStart, curSpan.endByte - repStart);
                if (prefix.empty() || startsWithIgnoreCaseAscii(norm, prefix)) {
                    newItems.push_back(item);
                }
            }
        }
    }

    bool const sameInput = (currentInput == mLastInput);
    if (!sameInput || newItems != mItems) {
        std::string prevSelected;
        if (mSelectedIndex >= 0 && mSelectedIndex < static_cast<int>(mItems.size())) {
            prevSelected = mItems[static_cast<size_t>(mSelectedIndex)];
        }

        mLastInput.assign(currentInput);
        mItems = std::move(newItems);

        if (mItems.empty()) {
            mSelectedIndex = 0;
            mScrollOffset  = 0;
        } else if (!prevSelected.empty()) {
            int found = 0;
            for (size_t i = 0; i < mItems.size(); ++i) {
                if (mItems[i] == prevSelected) {
                    found = static_cast<int>(i);
                    break;
                }
            }
            mSelectedIndex = found;
            clampState();
            ensureSelectedVisible();
        } else {
            mSelectedIndex = 0;
            mScrollOffset  = 0;
        }
    } else {
        clampState();
    }
}

std::string_view CommandSuggestionState::selectedToken() const noexcept {
    if (mSelectedIndex < 0 || mSelectedIndex >= static_cast<int>(mItems.size())) return {};
    return mItems[static_cast<size_t>(mSelectedIndex)];
}

void CommandSuggestionState::ensureSelectedVisible() noexcept {
    if (mItems.empty()) {
        mScrollOffset = 0;
        return;
    }
    int const count    = static_cast<int>(mItems.size());
    int const visCount = std::min(count, kMaxVisibleSuggestions);
    int const maxOff   = std::max(0, count - visCount);
    if (mSelectedIndex < mScrollOffset) {
        mScrollOffset = mSelectedIndex;
    } else if (mSelectedIndex >= mScrollOffset + visCount) {
        mScrollOffset = mSelectedIndex - visCount + 1;
    }
    mScrollOffset = std::clamp(mScrollOffset, 0, maxOff);
}

void CommandSuggestionState::clampState() noexcept {
    if (mItems.empty()) {
        mSelectedIndex = 0;
        mScrollOffset  = 0;
        return;
    }
    int const count    = static_cast<int>(mItems.size());
    int const visCount = std::min(count, kMaxVisibleSuggestions);
    int const maxOff   = std::max(0, count - visCount);
    mSelectedIndex     = std::clamp(mSelectedIndex, 0, count - 1);
    mScrollOffset      = std::clamp(mScrollOffset, 0, maxOff);
}

void CommandSuggestionState::moveSelection(int delta) noexcept {
    if (mItems.empty()) return;
    mTabCycling = false;
    int const n = static_cast<int>(mItems.size());
    int       r = (mSelectedIndex + delta) % n;
    if (r < 0) r += n;
    mSelectedIndex = r;
    ensureSelectedVisible();
}

bool CommandSuggestionState::setSelectedIndex(int idx) noexcept {
    if (idx < 0 || idx >= static_cast<int>(mItems.size())) return false;
    mTabCycling = false;
    if (mSelectedIndex == idx) return false;
    mSelectedIndex = idx;
    ensureSelectedVisible();
    return true;
}

void CommandSuggestionState::scrollByRows(int deltaRows) noexcept {
    if (mItems.empty()) return;
    int const count    = static_cast<int>(mItems.size());
    int const visCount = std::min(count, kMaxVisibleSuggestions);
    int const maxOff   = std::max(0, count - visCount);
    mScrollOffset      = std::clamp(mScrollOffset + deltaRows, 0, maxOff);
}

std::optional<SuggestionApplyResult> CommandSuggestionState::applyByTab(int direction) {
    int const dir = (direction < 0) ? -1 : 1;
    if (!mTabCycling) {
        if (mItems.empty()) return std::nullopt;
        int const n = static_cast<int>(mItems.size());
        mTabCycling        = true;
        mTabCycleBaseInput = mLastInput;
        mTabCycleItems     = mItems;
        mTabCycleIndex     = std::clamp(mSelectedIndex, 0, n - 1);

        std::string applied = applySuggestionToInput(
            mTabCycleBaseInput, mTabCycleItems[static_cast<size_t>(mTabCycleIndex)]
        );
        if (applied == mTabCycleBaseInput && n > 1) {
            mTabCycleIndex = (mTabCycleIndex + dir + n) % n;
            applied = applySuggestionToInput(
                mTabCycleBaseInput, mTabCycleItems[static_cast<size_t>(mTabCycleIndex)]
            );
        }
        mSelectedIndex = mTabCycleIndex;
        ensureSelectedVisible();
        mLastTabAppliedInput = applied;
        mLastInput           = applied;
        return SuggestionApplyResult{std::move(applied), n <= 1};
    }

    if (mTabCycleItems.empty()) return std::nullopt;
    int const n    = static_cast<int>(mTabCycleItems.size());
    mTabCycleIndex = (mTabCycleIndex + dir + n) % n;
    mSelectedIndex = std::clamp(mTabCycleIndex, 0, static_cast<int>(mItems.size()) - 1);
    ensureSelectedVisible();
    std::string applied = applySuggestionToInput(
        mTabCycleBaseInput, mTabCycleItems[static_cast<size_t>(mTabCycleIndex)]
    );
    mLastTabAppliedInput = applied;
    mLastInput           = applied;
    return SuggestionApplyResult{std::move(applied), n <= 1};
}

std::optional<SuggestionApplyResult> CommandSuggestionState::applyByClick(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(mItems.size())) return std::nullopt;
    mTabCycling = false;
    mTabCycleBaseInput.clear();
    mTabCycleItems.clear();
    mTabCycleIndex = 0;
    mLastTabAppliedInput.clear();

    mSelectedIndex = itemIndex;
    ensureSelectedVisible();
    std::string applied = applySuggestionToInput(mLastInput, mItems[static_cast<size_t>(itemIndex)]);
    return SuggestionApplyResult{std::move(applied), true};
}

SuggestionPopupLayout CommandSuggestionState::buildLayout(
    float                screenW,
    float                screenH,
    float                inputTextOriginX,
    float                fontScale,
    TextMeasureFn const& measure
) const {
    SuggestionPopupLayout layout;
    if (mItems.empty() || !(screenW > 0.0f && screenH > 0.0f)) return layout;

    float const safeScale = (std::isfinite(fontScale) && fontScale >= 0.25f) ? fontScale : 1.0f;
    auto measureText = [&](std::string_view sv) -> float {
        if (sv.empty()) return 0.0f;
        return measure ? measure(sv) : estimateTextWidth(sv, safeScale);
    };

    ActiveTokenSpan const span     = findActiveTokenSpan(mLastInput);
    size_t const          repStart = findSubTokenStart(mLastInput, span, selectedToken());
    float const           tokOffX  = (repStart > 0 && repStart <= mLastInput.size())
                                       ? measureText(std::string_view(mLastInput).substr(0, repStart))
                                       : 0.0f;
    float const           tokenStartX = inputTextOriginX + tokOffX;

    size_t const measureLimit = std::min<size_t>(mItems.size(), 200);
    float        maxItemW     = 0.0f;
    for (size_t i = 0; i < measureLimit; ++i) {
        maxItemW = std::max(maxItemW, measureText(mItems[i]));
    }
    float const maxAllowedW = std::max(16.0f, std::min(screenW - 4.0f, 320.0f));
    float const popupW      = std::clamp(maxItemW + kPopupPadLeft + kPopupPadRight, 16.0f, maxAllowedW);

    int const   total    = static_cast<int>(mItems.size());
    int const   visCount = std::min(total, kMaxVisibleSuggestions);
    int const   maxOff   = std::max(0, total - visCount);
    int const   scroll   = std::clamp(mScrollOffset, 0, maxOff);
    float const rowH     = std::max(kSuggestionRowHeight, std::round(12.0f * safeScale));
    float const popupH   = static_cast<float>(visCount) * rowH;

    float const y1 = screenH - (kInputBarBottomMargin + kInputBarHeight + kPopupGapAboveInput);
    float const y0 = std::max(2.0f, y1 - popupH);
    float const x0 = std::clamp(tokenStartX - kPopupPadLeft, 2.0f, std::max(2.0f, screenW - popupW - 2.0f));
    float const x1 = std::min(screenW - 2.0f, x0 + popupW);

    layout.visible      = (visCount > 0 && y1 > y0 && x1 > x0);
    layout.popupRect    = Rect{x0, y0, x1, y1};
    layout.hasMoreAbove = (scroll > 0);
    layout.hasMoreBelow = (scroll + visCount < total);
    layout.rows.reserve(static_cast<size_t>(visCount));

    for (int i = 0; i < visCount; ++i) {
        int const          idx  = scroll + i;
        float const        ry0  = y0 + static_cast<float>(i) * rowH;
        SuggestionPopupRow row;
        row.itemIndex = idx;
        row.text      = mItems[static_cast<size_t>(idx)];
        row.rect      = Rect{x0, ry0, x1, ry0 + rowH};
        row.selected  = (idx == mSelectedIndex);
        layout.rows.push_back(std::move(row));
    }
    return layout;
}

std::optional<int> CommandSuggestionState::hitTestRow(
    SuggestionPopupLayout const& layout,
    float                        mx,
    float                        my
) noexcept {
    if (!layout.visible || !layout.popupRect.contains(mx, my)) return std::nullopt;
    for (auto const& row : layout.rows) {
        if (row.rect.contains(mx, my)) {
            return row.itemIndex;
        }
    }
    return std::nullopt;
}

} // namespace javachat
