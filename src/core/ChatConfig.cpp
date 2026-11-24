#include "core/ChatConfig.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace javachat {
namespace {

float clampFinite(float v, float lo, float hi, float fallback) noexcept {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, lo, hi);
}

bool parseHex4(std::string_view s, size_t& pos, unsigned& out) noexcept {
    if (pos + 4 > s.size()) return false;
    unsigned code = 0;
    for (int i = 0; i < 4; ++i) {
        char h = s[pos++];
        code <<= 4;
        if (h >= '0' && h <= '9') code |= unsigned(h - '0');
        else if (h >= 'a' && h <= 'f') code |= unsigned(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') code |= unsigned(h - 'A' + 10);
        else return false;
    }
    out = code;
    return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

struct JsonParser {
    std::string_view s;
    size_t           pos      = 0;
    bool             hasError = false;

    void skipWs() noexcept {
        while (pos < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[pos]);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
            } else if (c == '/' && pos + 1 < s.size() && s[pos + 1] == '/') {
                pos += 2;
                while (pos < s.size() && s[pos] != '\n') ++pos;
            } else if (c == '/' && pos + 1 < s.size() && s[pos + 1] == '*') {
                pos += 2;
                bool closed = false;
                while (pos + 1 < s.size()) {
                    if (s[pos] == '*' && s[pos + 1] == '/') {
                        pos += 2;
                        closed = true;
                        break;
                    }
                    ++pos;
                }
                if (!closed) {
                    hasError = true;
                    pos      = s.size();
                    return;
                }
            } else {
                break;
            }
        }
    }

    bool consume(char ch) noexcept {
        skipWs();
        if (hasError) return false;
        if (pos < s.size() && s[pos] == ch) {
            ++pos;
            return true;
        }
        return false;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (hasError || pos >= s.size() || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= s.size()) return false;
                char esc = s[pos++];
                switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    unsigned code = 0;
                    if (!parseHex4(s, pos, code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        // Decode UTF-16 surrogate pair when followed by a low surrogate.
                        if (pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                            size_t   savedPos = pos;
                            unsigned low      = 0;
                            pos += 2;
                            if (parseHex4(s, pos, low) && low >= 0xDC00 && low <= 0xDFFF) {
                                uint32_t cp = 0x10000u + (((code - 0xD800u) << 10) | (low - 0xDC00u));
                                appendUtf8(out, cp);
                            } else {
                                pos = savedPos;
                                appendUtf8(out, 0xFFFD);
                            }
                        } else {
                            appendUtf8(out, 0xFFFD);
                        }
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        appendUtf8(out, 0xFFFD);
                    } else {
                        appendUtf8(out, code);
                    }
                    break;
                }
                default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parseRawValue(std::string& rawOut, char& kindOut, int depth = 0) {
        if (depth > 32) return false;
        skipWs();
        if (hasError || pos >= s.size()) return false;
        char c = s[pos];
        if (c == '"') {
            kindOut = 's';
            return parseString(rawOut);
        }
        if (c == '{') {
            kindOut = 'o';
            size_t start = pos++;
            if (consume('}')) {
                rawOut.assign(s.substr(start, pos - start));
                return true;
            }
            while (true) {
                std::string k, v;
                char        vk = 0;
                if (!parseString(k)) return false;
                if (!consume(':')) return false;
                if (!parseRawValue(v, vk, depth + 1)) return false;
                if (consume(',')) {
                    if (consume('}')) {
                        rawOut.assign(s.substr(start, pos - start));
                        return true;
                    }
                    continue;
                }
                if (consume('}')) {
                    rawOut.assign(s.substr(start, pos - start));
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            kindOut = 'a';
            size_t start = pos++;
            if (consume(']')) {
                rawOut.assign(s.substr(start, pos - start));
                return true;
            }
            while (true) {
                std::string v;
                char        vk = 0;
                if (!parseRawValue(v, vk, depth + 1)) return false;
                if (consume(',')) {
                    if (consume(']')) {
                        rawOut.assign(s.substr(start, pos - start));
                        return true;
                    }
                    continue;
                }
                if (consume(']')) {
                    rawOut.assign(s.substr(start, pos - start));
                    return true;
                }
                return false;
            }
        }
        if (s.substr(pos, 4) == "true") {
            pos += 4;
            kindOut = 'b';
            rawOut  = "true";
            return true;
        }
        if (s.substr(pos, 5) == "false") {
            pos += 5;
            kindOut = 'b';
            rawOut  = "false";
            return true;
        }
        if (s.substr(pos, 4) == "null") {
            pos += 4;
            kindOut = 'u';
            rawOut  = "null";
            return true;
        }
        // Number
        size_t start = pos;
        if (s[pos] == '-') ++pos;
        if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        kindOut = 'n';
        rawOut.assign(s.substr(start, pos - start));
        return true;
    }
};

std::string formatFloat(float v) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    std::string out(buf);
    // Keep at least one decimal place (e.g., 1.0)
    while (out.size() > 3 && out.back() == '0' && out[out.size() - 2] != '.') {
        out.pop_back();
    }
    return out;
}

} // namespace

void sanitizeConfig(ChatConfig& cfg) noexcept {
    ChatConfig def;
    cfg.version            = 1;
    cfg.width              = clampFinite(cfg.width, 120.0f, 640.0f, def.width);
    cfg.closedHeight       = clampFinite(cfg.closedHeight, 24.0f, 360.0f, def.closedHeight);
    cfg.openHeight         = clampFinite(cfg.openHeight, 40.0f, 480.0f, def.openHeight);
    if (cfg.openHeight < cfg.closedHeight) cfg.openHeight = cfg.closedHeight;
    cfg.bottomOffset       = clampFinite(cfg.bottomOffset, 18.0f, 240.0f, def.bottomOffset);
    cfg.leftOffset         = clampFinite(cfg.leftOffset, 0.0f, 240.0f, def.leftOffset);
    cfg.fontScale          = clampFinite(cfg.fontScale, 0.5f, 2.0f, def.fontScale);
    cfg.lineSpacing        = clampFinite(cfg.lineSpacing, 0.0f, 8.0f, def.lineSpacing);
    cfg.backgroundOpacity  = clampFinite(cfg.backgroundOpacity, 0.0f, 1.0f, def.backgroundOpacity);
    cfg.textOpacity        = clampFinite(cfg.textOpacity, 0.1f, 1.0f, def.textOpacity);
    cfg.fadeDelaySec       = clampFinite(cfg.fadeDelaySec, 1.0f, 120.0f, def.fadeDelaySec);
    cfg.fadeDurationSec    = clampFinite(cfg.fadeDurationSec, 0.1f, 10.0f, def.fadeDurationSec);
    cfg.maxHistoryMessages = std::clamp(cfg.maxHistoryMessages, 20, 5000);
    cfg.scrollStepLines    = std::clamp(cfg.scrollStepLines, 1, 20);
}

bool parseConfigJson(std::string_view jsonText, ChatConfig& outConfig, std::string* outWarning) {
    // Strip UTF-8 BOM if present.
    if (jsonText.size() >= 3 && static_cast<unsigned char>(jsonText[0]) == 0xEF
        && static_cast<unsigned char>(jsonText[1]) == 0xBB
        && static_cast<unsigned char>(jsonText[2]) == 0xBF) {
        jsonText.remove_prefix(3);
    }

    JsonParser p{jsonText, 0};
    if (!p.consume('{')) {
        if (outWarning) *outWarning = "config root is not a JSON object";
        return false;
    }

    ChatConfig cfg;
    std::vector<std::string> warnings;
    p.skipWs();
    if (!p.consume('}')) {
        while (true) {
            std::string key;
            if (!p.parseString(key)) {
                if (outWarning) *outWarning = "expected string key in config object";
                return false;
            }
            if (!p.consume(':')) {
                if (outWarning) *outWarning = "expected ':' after key '" + key + "'";
                return false;
            }
            std::string val;
            char        kind = 0;
            if (!p.parseRawValue(val, kind)) {
                if (outWarning) *outWarning = "invalid value for key '" + key + "'";
                return false;
            }

            auto readBool = [&](bool& dst) {
                if (kind == 'b') dst = (val == "true");
                else warnings.push_back(key + " expects bool");
            };
            auto readFloat = [&](float& dst) {
                if (kind == 'n') {
                    try {
                        dst = std::stof(val);
                    } catch (...) {
                        warnings.push_back(key + " out of float range");
                    }
                } else {
                    warnings.push_back(key + " expects number");
                }
            };
            auto readInt = [&](int& dst) {
                if (kind == 'n') {
                    try {
                        dst = static_cast<int>(std::lround(std::stod(val)));
                    } catch (...) {
                        warnings.push_back(key + " out of int range");
                    }
                } else {
                    warnings.push_back(key + " expects integer");
                }
            };

            if (key == "version") readInt(cfg.version);
            else if (key == "enabled") readBool(cfg.enabled);
            else if (key == "overrideServerChatUi") readBool(cfg.overrideServerChatUi);
            else if (key == "width") readFloat(cfg.width);
            else if (key == "closedHeight") readFloat(cfg.closedHeight);
            else if (key == "openHeight") readFloat(cfg.openHeight);
            else if (key == "bottomOffset") readFloat(cfg.bottomOffset);
            else if (key == "leftOffset") readFloat(cfg.leftOffset);
            else if (key == "fontScale") readFloat(cfg.fontScale);
            else if (key == "lineSpacing") readFloat(cfg.lineSpacing);
            else if (key == "backgroundOpacity") readFloat(cfg.backgroundOpacity);
            else if (key == "textOpacity") readFloat(cfg.textOpacity);
            else if (key == "textShadow") readBool(cfg.textShadow);
            else if (key == "fadeDelaySec") readFloat(cfg.fadeDelaySec);
            else if (key == "fadeDurationSec") readFloat(cfg.fadeDurationSec);
            else if (key == "maxHistoryMessages") readInt(cfg.maxHistoryMessages);
            else if (key == "scrollStepLines") readInt(cfg.scrollStepLines);
            else if (key == "compactDuplicates") readBool(cfg.compactDuplicates);
            else if (key == "showTimestamps") readBool(cfg.showTimestamps);
            else if (key == "clickToCopy") readBool(cfg.clickToCopy);
            else if (key == "clickToOpenUrl") readBool(cfg.clickToOpenUrl);
            else if (key == "clearHistoryOnWorldSwitch") readBool(cfg.clearHistoryOnWorldSwitch);
            else if (key == "verboseLog") readBool(cfg.verboseLog);

            if (p.consume(',')) {
                if (p.consume('}')) break;
                continue;
            }
            if (p.consume('}')) break;
            if (outWarning) *outWarning = p.hasError ? "unterminated comment in config JSON" : "expected ',' or '}' in config object";
            return false;
        }
    }

    p.skipWs();
    if (p.hasError || p.pos != p.s.size()) {
        if (outWarning) *outWarning = p.hasError ? "unterminated comment in config JSON" : "unexpected trailing data in config JSON";
        return false;
    }

    sanitizeConfig(cfg);
    outConfig = cfg;
    if (outWarning && !warnings.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < warnings.size(); ++i) {
            if (i > 0) oss << "; ";
            oss << warnings[i];
        }
        *outWarning = oss.str();
    }
    return true;
}

std::string serializeConfigJson(ChatConfig const& rawCfg) {
    ChatConfig cfg = rawCfg;
    sanitizeConfig(cfg);
    auto b = [](bool v) { return v ? "true" : "false"; };

    std::ostringstream oss;
    oss << "{\n"
        << "  \"version\": " << cfg.version << ",\n"
        << "  \"enabled\": " << b(cfg.enabled) << ",\n"
        << "  \"overrideServerChatUi\": " << b(cfg.overrideServerChatUi) << ",\n"
        << "  \"width\": " << formatFloat(cfg.width) << ",\n"
        << "  \"closedHeight\": " << formatFloat(cfg.closedHeight) << ",\n"
        << "  \"openHeight\": " << formatFloat(cfg.openHeight) << ",\n"
        << "  \"bottomOffset\": " << formatFloat(cfg.bottomOffset) << ",\n"
        << "  \"leftOffset\": " << formatFloat(cfg.leftOffset) << ",\n"
        << "  \"fontScale\": " << formatFloat(cfg.fontScale) << ",\n"
        << "  \"lineSpacing\": " << formatFloat(cfg.lineSpacing) << ",\n"
        << "  \"backgroundOpacity\": " << formatFloat(cfg.backgroundOpacity) << ",\n"
        << "  \"textOpacity\": " << formatFloat(cfg.textOpacity) << ",\n"
        << "  \"textShadow\": " << b(cfg.textShadow) << ",\n"
        << "  \"fadeDelaySec\": " << formatFloat(cfg.fadeDelaySec) << ",\n"
        << "  \"fadeDurationSec\": " << formatFloat(cfg.fadeDurationSec) << ",\n"
        << "  \"maxHistoryMessages\": " << cfg.maxHistoryMessages << ",\n"
        << "  \"scrollStepLines\": " << cfg.scrollStepLines << ",\n"
        << "  \"compactDuplicates\": " << b(cfg.compactDuplicates) << ",\n"
        << "  \"showTimestamps\": " << b(cfg.showTimestamps) << ",\n"
        << "  \"clickToCopy\": " << b(cfg.clickToCopy) << ",\n"
        << "  \"clickToOpenUrl\": " << b(cfg.clickToOpenUrl) << ",\n"
        << "  \"clearHistoryOnWorldSwitch\": " << b(cfg.clearHistoryOnWorldSwitch) << ",\n"
        << "  \"verboseLog\": " << b(cfg.verboseLog) << "\n"
        << "}\n";
    return oss.str();
}

ConfigLoadResult loadConfigFile(std::filesystem::path const& path) {
    ConfigLoadResult res;
    std::error_code  ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return res;
    }
    res.existed = true;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        res.warning = "failed to open config file for reading";
        return res;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    ChatConfig  parsed;
    std::string warn;
    if (!parseConfigJson(content, parsed, &warn)) {
        res.corrupted = true;
        res.warning   = warn.empty() ? "malformed JSON" : warn;
        auto stamp    = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch()
        )
                         .count();
        auto backup = path;
        backup += ".corrupt-" + std::to_string(stamp);
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
        return res;
    }
    res.config  = parsed;
    res.warning = std::move(warn);
    return res;
}

bool saveConfigFile(std::filesystem::path const& path, ChatConfig const& cfg) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    std::string text = serializeConfigJson(cfg);
    auto        tmp  = path;
    tmp += ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
        ofs.flush();
        if (!ofs.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Fallback for Windows when target already exists.
        std::error_code ec2;
        std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec2);
        std::filesystem::remove(tmp, ec);
        return !ec2;
    }
    return true;
}

} // namespace javachat
