#include "core/ChatFormatter.h"

#include <algorithm>
#include <cctype>

namespace javachat {
namespace {

bool isColorCode(char c) noexcept {
    char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lo >= '0' && lo <= '9') return true;
    // Bedrock supports a-g plus material colors h, i, j, m, n, p, q, s, t, u, v.
    switch (lo) {
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'm': case 'n': case 'p': case 'q':
    case 's': case 't': case 'u': case 'v':
        return true;
    default:
        return false;
    }
}

bool isWideCodepoint(uint32_t cp) noexcept {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF)
        || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0xFE10 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60)
        || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF)
        || (cp >= 0x20000 && cp <= 0x3FFFF);
}

bool isUrlChar(unsigned char c) noexcept {
    if (c <= 0x20 || c >= 0x7F) return false;
    switch (c) {
    case '"': case '\'': case '<': case '>': case '`': case '{': case '}': case '|': case '\\': case '^':
        return false;
    default:
        return true;
    }
}

void appendSection(std::string& out, char code) {
    out.push_back(static_cast<char>(0xC2));
    out.push_back(static_cast<char>(0xA7));
    out.push_back(code);
}

} // namespace

uint32_t nextUtf8Codepoint(std::string_view s, size_t& pos) noexcept {
    if (pos >= s.size()) return 0;
    unsigned char b0 = static_cast<unsigned char>(s[pos]);
    if (b0 < 0x80) {
        ++pos;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && pos + 1 < s.size()) {
        unsigned char b1 = static_cast<unsigned char>(s[pos + 1]);
        if ((b1 & 0xC0) == 0x80) {
            pos += 2;
            return ((uint32_t(b0 & 0x1F) << 6) | uint32_t(b1 & 0x3F));
        }
    } else if ((b0 & 0xF0) == 0xE0 && pos + 2 < s.size()) {
        unsigned char b1 = static_cast<unsigned char>(s[pos + 1]);
        unsigned char b2 = static_cast<unsigned char>(s[pos + 2]);
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
            pos += 3;
            return ((uint32_t(b0 & 0x0F) << 12) | (uint32_t(b1 & 0x3F) << 6) | uint32_t(b2 & 0x3F));
        }
    } else if ((b0 & 0xF8) == 0xF0 && pos + 3 < s.size()) {
        unsigned char b1 = static_cast<unsigned char>(s[pos + 1]);
        unsigned char b2 = static_cast<unsigned char>(s[pos + 2]);
        unsigned char b3 = static_cast<unsigned char>(s[pos + 3]);
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
            pos += 4;
            return ((uint32_t(b0 & 0x07) << 18) | (uint32_t(b1 & 0x3F) << 12) | (uint32_t(b2 & 0x3F) << 6)
                    | uint32_t(b3 & 0x3F));
        }
    }
    ++pos;
    return 0xFFFD;
}

bool isSectionCodeAt(std::string_view s, size_t pos, size_t& seqLen, char& codeOut) noexcept {
    if (pos + 2 < s.size() && static_cast<unsigned char>(s[pos]) == 0xC2
        && static_cast<unsigned char>(s[pos + 1]) == 0xA7) {
        seqLen  = 3;
        codeOut = s[pos + 2];
        return true;
    }
    return false;
}

void FormatState::apply(char code) noexcept {
    char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(code)));
    if (lo == 'r') {
        *this = FormatState{};
    } else if (lo == 'k') {
        obf = true;
    } else if (lo == 'l') {
        bold = true;
    } else if (lo == 'o') {
        italic = true;
    } else if (isColorCode(lo)) {
        colorCode = lo;
        // In Bedrock, setting a new color resets active style modifiers.
        bold = italic = obf = false;
    }
}

std::string FormatState::toPrefix() const {
    std::string out;
    if (colorCode != 0) appendSection(out, colorCode);
    if (obf) appendSection(out, 'k');
    if (bold) appendSection(out, 'l');
    if (italic) appendSection(out, 'o');
    return out;
}

std::string stripFormatting(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        size_t seqLen = 0;
        char   code   = 0;
        if (isSectionCodeAt(text, i, seqLen, code)) {
            i += seqLen;
            continue;
        }
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\r') {
            ++i;
            continue;
        }
        if (c == '\t') {
            out.push_back(' ');
            ++i;
            continue;
        }
        if (c < 0x20 && c != '\n') {
            ++i;
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

std::optional<std::string> extractFirstUrl(std::string_view text) {
    std::string clean = stripFormatting(text);
    size_t      pos   = 0;
    while (pos < clean.size()) {
        size_t h1 = clean.find("https://", pos);
        size_t h2 = clean.find("http://", pos);
        size_t p  = std::min(h1, h2);
        if (p == std::string::npos) return std::nullopt;

        size_t end = p;
        while (end < clean.size() && isUrlChar(static_cast<unsigned char>(clean[end]))) {
            ++end;
        }
        std::string url = clean.substr(p, end - p);
        // Trim trailing punctuation commonly attached to URLs in prose.
        while (!url.empty()) {
            char back = url.back();
            bool trim = (back == '.' || back == ',' || back == ';' || back == ':' || back == '!' || back == '?');
            if (!trim && back == ')') {
                size_t opens  = static_cast<size_t>(std::count(url.begin(), url.end(), '('));
                size_t closes = static_cast<size_t>(std::count(url.begin(), url.end(), ')'));
                trim          = closes > opens;
            }
            if (!trim && back == ']') {
                size_t opens  = static_cast<size_t>(std::count(url.begin(), url.end(), '['));
                size_t closes = static_cast<size_t>(std::count(url.begin(), url.end(), ']'));
                trim          = closes > opens;
            }
            if (!trim) break;
            url.pop_back();
        }
        size_t schemeEnd = url.find("://");
        if (schemeEnd != std::string::npos && url.size() > schemeEnd + 3) {
            std::string_view hostPart = std::string_view(url).substr(schemeEnd + 3);
            if (!hostPart.empty() && hostPart.find('.') != std::string_view::npos) {
                return url;
            }
        }
        pos = std::max(end, p + 7);
    }
    return std::nullopt;
}

float estimateTextWidth(std::string_view text, float fontScale) noexcept {
    float  w = 0.0f;
    size_t i = 0;
    while (i < text.size()) {
        size_t seqLen = 0;
        char   code   = 0;
        if (isSectionCodeAt(text, i, seqLen, code)) {
            i += seqLen;
            continue;
        }
        uint32_t cp = nextUtf8Codepoint(text, i);
        if (cp == '\n' || cp == '\r') continue;
        if (cp == ' ' || cp == 'i' || cp == 'l' || cp == '!' || cp == '.' || cp == ',' || cp == ':' || cp == ';'
            || cp == '|') {
            w += 3.0f;
        } else if (cp == 't' || cp == 'I' || cp == '[' || cp == ']' || cp == '(' || cp == ')') {
            w += 4.0f;
        } else if (cp < 0x80) {
            w += 6.0f;
        } else if (isWideCodepoint(cp)) {
            w += 9.5f;
        } else {
            w += 6.5f;
        }
    }
    return w * fontScale;
}

std::vector<std::string>
wrapFormattedText(std::string_view text, float maxWidth, TextMeasureFn const& measure) {
    std::vector<std::string> lines;
    float const              limit = std::max(maxWidth, 24.0f);
    FormatState              state;

    // Split on explicit '\n' first.
    size_t segStart = 0;
    while (segStart <= text.size()) {
        size_t nl  = text.find('\n', segStart);
        size_t end = (nl == std::string_view::npos) ? text.size() : nl;
        std::string_view seg = text.substr(segStart, end - segStart);
        if (!seg.empty() && seg.back() == '\r') seg.remove_suffix(1);

        if (seg.empty()) {
            lines.push_back(state.toPrefix());
        } else {
            // Tokenize into indivisible pieces: formatting codes attach to the following glyph,
            // ASCII spaces act as preferred wrap breaks, CJK/wide glyphs can break individually.
            struct Atom {
                size_t      start;
                size_t      len;
                bool        isSpace;
                FormatState stateAfter;
            };
            std::vector<Atom> atoms;
            FormatState       scanState = state;
            size_t            pos       = 0;
            while (pos < seg.size()) {
                size_t atomStart = pos;
                // Consume leading section codes so they stay glued to this atom.
                while (pos < seg.size()) {
                    size_t seqLen = 0;
                    char   code   = 0;
                    if (!isSectionCodeAt(seg, pos, seqLen, code)) break;
                    scanState.apply(code);
                    pos += seqLen;
                }
                if (pos >= seg.size()) {
                    if (!atoms.empty()) {
                        atoms.back().len        = pos - atoms.back().start;
                        atoms.back().stateAfter = scanState;
                    } else {
                        atoms.push_back({atomStart, pos - atomStart, false, scanState});
                    }
                    break;
                }
                size_t   charStart = pos;
                uint32_t cp        = nextUtf8Codepoint(seg, pos);
                bool     isSp      = (cp == ' ' || cp == '\t');
                atoms.push_back({atomStart, pos - atomStart, isSp, scanState});
                (void)charStart;
            }

            std::string currentLine       = state.toPrefix();
            FormatState currentLineState  = state;
            size_t      lastSpaceLineLen  = std::string::npos;
            size_t      lastSpaceAtomIdx  = std::string::npos;
            FormatState stateAtLastSpace  = state;
            size_t      lineStartAtomIdx  = 0;

            size_t idx = 0;
            while (idx < atoms.size()) {
                auto const&      a = atoms[idx];
                std::string_view chunk = seg.substr(a.start, a.len);

                // Skip leading spaces on continuation lines (not on the very first line).
                if (idx == lineStartAtomIdx && lineStartAtomIdx > 0 && a.isSpace) {
                    currentLineState = a.stateAfter;
                    currentLine      = currentLineState.toPrefix();
                    ++idx;
                    lineStartAtomIdx = idx;
                    continue;
                }

                std::string candidate = currentLine;
                candidate.append(chunk);
                float w = measure ? measure(candidate) : estimateTextWidth(candidate, 1.0f);

                if (w <= limit || idx == lineStartAtomIdx) {
                    currentLine      = std::move(candidate);
                    currentLineState = a.stateAfter;
                    if (a.isSpace) {
                        lastSpaceLineLen = currentLine.size() - chunk.size();
                        lastSpaceAtomIdx = idx;
                        stateAtLastSpace = a.stateAfter;
                    }
                    ++idx;
                } else {
                    if (lastSpaceLineLen != std::string::npos && lastSpaceAtomIdx >= lineStartAtomIdx) {
                        // Break cleanly at the last space in this line.
                        lines.push_back(currentLine.substr(0, lastSpaceLineLen));
                        currentLineState = stateAtLastSpace;
                        idx              = lastSpaceAtomIdx + 1;
                    } else {
                        // Hard wrap before atom `idx`.
                        lines.push_back(currentLine);
                    }
                    currentLine      = currentLineState.toPrefix();
                    lineStartAtomIdx = idx;
                    lastSpaceLineLen = std::string::npos;
                    lastSpaceAtomIdx = std::string::npos;
                }
            }
            if (!currentLine.empty() || lines.empty()) {
                lines.push_back(currentLine);
            }
            state = currentLineState;
        }

        if (nl == std::string_view::npos) break;
        segStart = nl + 1;
    }

    if (lines.empty()) lines.emplace_back();
    return lines;
}

} // namespace javachat
