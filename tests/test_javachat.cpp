#include "core/ChatConfig.h"
#include "core/ChatFormatter.h"
#include "core/ChatHistory.h"
#include "core/CommandSuggestions.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int gPassed = 0;
int gFailed = 0;

#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (expr) {                                                                                                    \
            ++gPassed;                                                                                                 \
        } else {                                                                                                       \
            ++gFailed;                                                                                                 \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #expr << "\n";                               \
        }                                                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= double(eps))

void testConfigParseAndRoundTrip() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width                 = 280.0f;
    cfg.closedHeight          = 80.0f;
    cfg.openHeight            = 200.0f;
    cfg.backgroundOpacity     = 0.45f;
    cfg.fontScale             = 1.15f;
    cfg.fadeDurationSec       = 0.0f;
    cfg.showTimestamps        = true;
    cfg.compactDuplicates     = false;
    cfg.autoCloseOnSend       = false;
    cfg.disableChatAnimations = false;
    cfg.keepHudVisibleInChat  = false;
    cfg.font                  = R"(C:\Windows\Fonts\BIZUDPGothic-Regular.ttf)";
    cfg.maxHistoryMessages    = 750;

    std::string json = serializeConfigJson(cfg);
    ChatConfig  parsed;
    std::string warn;
    CHECK(parseConfigJson(json, parsed, &warn));
    CHECK(warn.empty());
    CHECK_NEAR(parsed.width, 280.0f, 0.01f);
    CHECK_NEAR(parsed.closedHeight, 80.0f, 0.01f);
    CHECK_NEAR(parsed.openHeight, 200.0f, 0.01f);
    CHECK_NEAR(parsed.backgroundOpacity, 0.45f, 0.01f);
    CHECK_NEAR(parsed.fontScale, 1.15f, 0.01f);
    CHECK_NEAR(parsed.fadeDurationSec, 0.0f, 0.001f);
    CHECK(parsed.showTimestamps == true);
    CHECK(parsed.compactDuplicates == false);
    CHECK(parsed.autoCloseOnSend == false);
    CHECK(parsed.disableChatAnimations == false);
    CHECK(parsed.keepHudVisibleInChat == false);
    CHECK(parsed.font == R"(C:\Windows\Fonts\BIZUDPGothic-Regular.ttf)");
    CHECK(parsed.maxHistoryMessages == 750);

    // Built-in font name normalization
    std::string canonical;
    CHECK(isBuiltinFontName("Mojangles", &canonical) && canonical == "default");
    CHECK(isBuiltinFontName("ui", &canonical) && canonical == "ui");
    CHECK(isBuiltinFontName("UI", &canonical) && canonical == "ui");
    CHECK(isBuiltinFontName(" SMOOTH ", &canonical) && canonical == "smooth");
    CHECK(isBuiltinFontName("mc10", &canonical) && canonical == "MinecraftTen");
    CHECK(isBuiltinFontName("unicode", &canonical) && canonical == "unicode");
    CHECK(isBuiltinFontName("rune", &canonical) && canonical == "rune");
    CHECK(isBuiltinFontName("smoothsmall", &canonical) && canonical == "SmoothSmall");
    CHECK(!isBuiltinFontName("arial.ttf", &canonical));

    // Clamping & unknown keys & type mismatch handling
    ChatConfig clamped;
    warn.clear();
    CHECK(parseConfigJson(
        R"({"width": 9999, "fontScale": -5, "fadeDurationSec": -1.0, "backgroundOpacity": 2.5, "font": "  mojangles ", "enabled": "bad", "unknown": [1,2,3]})",
        clamped,
        &warn
    ));
    CHECK_NEAR(clamped.width, 640.0f, 0.01f);
    CHECK_NEAR(clamped.fontScale, 0.5f, 0.01f);
    CHECK_NEAR(clamped.fadeDurationSec, 0.0f, 0.001f);
    CHECK_NEAR(clamped.backgroundOpacity, 1.0f, 0.01f);
    CHECK(clamped.font == "default");
    CHECK(clamped.enabled == true); // fallback default
    CHECK(!warn.empty());

    // Defaults: version 3, lineSpacing 0.0, backgroundOpacity 0.50, and clearHistoryOnWorldSwitch / keepHudVisibleInChat enabled.
    ChatConfig defaultsCheck;
    CHECK(defaultsCheck.version == 3);
    CHECK_NEAR(defaultsCheck.lineSpacing, 0.0f, 0.001f);
    CHECK_NEAR(defaultsCheck.backgroundOpacity, 0.50f, 0.001f);
    CHECK(defaultsCheck.keepHudVisibleInChat == true);
    CHECK(defaultsCheck.clearHistoryOnWorldSwitch == true);

    // Backward compatibility & v1->v3 / v2->v3 migrations.
    ChatConfig v10;
    warn.clear();
    bool migratedV1 = false;
    CHECK(parseConfigJson(R"({"version":1,"width":300.0,"fadeDurationSec":1.0})", v10, &warn, &migratedV1));
    CHECK(warn.empty());
    CHECK(migratedV1 == true);
    CHECK(v10.version == 3);
    CHECK_NEAR(v10.lineSpacing, 0.0f, 0.001f);
    CHECK_NEAR(v10.backgroundOpacity, 0.50f, 0.001f);
    CHECK(v10.autoCloseOnSend == true);
    CHECK(v10.disableChatAnimations == true);
    CHECK(v10.keepHudVisibleInChat == true);
    CHECK(v10.clearHistoryOnWorldSwitch == true);
    CHECK(v10.font == "default");

    ChatConfig v1ExplicitFalse;
    bool       migratedV1False = false;
    warn.clear();
    CHECK(parseConfigJson(R"({"version":1,"clearHistoryOnWorldSwitch":false})", v1ExplicitFalse, &warn, &migratedV1False));
    CHECK(warn.empty());
    CHECK(migratedV1False == true);
    CHECK(v1ExplicitFalse.version == 3);
    CHECK(v1ExplicitFalse.clearHistoryOnWorldSwitch == true);

    // v2 with old defaults (1.5 / 0.38) migrates visuals to 0.0 / 0.50 while keeping explicit clearHistoryOnWorldSwitch.
    ChatConfig v2Defaults;
    bool       migratedV2Def = false;
    warn.clear();
    CHECK(parseConfigJson(
        R"({"version":2,"lineSpacing":1.5,"backgroundOpacity":0.38,"clearHistoryOnWorldSwitch":false})",
        v2Defaults,
        &warn,
        &migratedV2Def
    ));
    CHECK(warn.empty());
    CHECK(migratedV2Def == true);
    CHECK(v2Defaults.version == 3);
    CHECK_NEAR(v2Defaults.lineSpacing, 0.0f, 0.001f);
    CHECK_NEAR(v2Defaults.backgroundOpacity, 0.50f, 0.001f);
    CHECK(v2Defaults.clearHistoryOnWorldSwitch == false);

    // v2 with custom lineSpacing / backgroundOpacity preserves customized values during v3 upgrade.
    ChatConfig v2Custom;
    bool       migratedV2Cust = false;
    warn.clear();
    CHECK(parseConfigJson(
        R"({"version":2,"lineSpacing":2.5,"backgroundOpacity":0.70,"clearHistoryOnWorldSwitch":false})",
        v2Custom,
        &warn,
        &migratedV2Cust
    ));
    CHECK(warn.empty());
    CHECK(migratedV2Cust == true);
    CHECK(v2Custom.version == 3);
    CHECK_NEAR(v2Custom.lineSpacing, 2.5f, 0.001f);
    CHECK_NEAR(v2Custom.backgroundOpacity, 0.70f, 0.001f);
    CHECK(v2Custom.clearHistoryOnWorldSwitch == false);

    // v3 explicit 1.5 / 0.38 stays untouched (no re-migration).
    ChatConfig v3Explicit;
    bool       migratedV3 = true;
    warn.clear();
    CHECK(parseConfigJson(
        R"({"version":3,"lineSpacing":1.5,"backgroundOpacity":0.38,"clearHistoryOnWorldSwitch":false})",
        v3Explicit,
        &warn,
        &migratedV3
    ));
    CHECK(warn.empty());
    CHECK(migratedV3 == false);
    CHECK(v3Explicit.version == 3);
    CHECK_NEAR(v3Explicit.lineSpacing, 1.5f, 0.001f);
    CHECK_NEAR(v3Explicit.backgroundOpacity, 0.38f, 0.001f);
    CHECK(v3Explicit.clearHistoryOnWorldSwitch == false);

    ChatConfig v3RoundTrip;
    bool       migratedV3Rt = true;
    warn.clear();
    CHECK(parseConfigJson(serializeConfigJson(v3Explicit), v3RoundTrip, &warn, &migratedV3Rt));
    CHECK(warn.empty());
    CHECK(migratedV3Rt == false);
    CHECK(v3RoundTrip.version == 3);
    CHECK_NEAR(v3RoundTrip.lineSpacing, 1.5f, 0.001f);
    CHECK_NEAR(v3RoundTrip.backgroundOpacity, 0.38f, 0.001f);

    // Malformed JSON rejected
    ChatConfig bad;
    CHECK(!parseConfigJson("{not valid json", bad, &warn));

    // Out-of-range integer value safely warned without lround UB
    ChatConfig bigInt;
    warn.clear();
    CHECK(parseConfigJson(R"({"maxHistoryMessages": 1e30, "width": 300.0})", bigInt, &warn));
    CHECK(!warn.empty());
    CHECK(bigInt.maxHistoryMessages == 500);
    CHECK_NEAR(bigInt.width, 300.0f, 0.01f);
}

void testConfigFileSaveLoadAndCorruptBackup() {
    using namespace javachat;
    auto tmpDir = std::filesystem::temp_directory_path() / "javachat_test_cfg";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    auto cfgPath = tmpDir / "config.json";

    // Missing file -> defaults
    auto missing = loadConfigFile(cfgPath);
    CHECK(!missing.existed);
    CHECK(!missing.corrupted);

    ChatConfig orig;
    orig.width          = 350.0f;
    orig.showTimestamps = true;
    CHECK(saveConfigFile(cfgPath, orig));

    auto loaded = loadConfigFile(cfgPath);
    CHECK(loaded.existed);
    CHECK(!loaded.corrupted);
    CHECK_NEAR(loaded.config.width, 350.0f, 0.01f);
    CHECK(loaded.config.showTimestamps == true);

    // Corrupt file -> marked corrupted + backup written
    {
        std::ofstream ofs(cfgPath, std::ios::binary | std::ios::trunc);
        ofs << "{broken";
    }
    auto corrupt = loadConfigFile(cfgPath);
    CHECK(corrupt.existed);
    CHECK(corrupt.corrupted);
    bool foundBackup = false;
    for (auto const& e : std::filesystem::directory_iterator(tmpDir)) {
        if (e.path().filename().string().find(".corrupt-") != std::string::npos) {
            foundBackup = true;
        }
    }
    CHECK(foundBackup);
    std::filesystem::remove_all(tmpDir, ec);
}

void testFormatterStripAndUrls() {
    using namespace javachat;
    std::string colored = "\xC2\xA7" "a<Steve> \xC2\xA7" "lHello \xC2\xA7" "rworld!";
    CHECK(stripFormatting(colored) == "<Steve> Hello world!");

    // URL extraction (strips formatting & trailing punctuation)
    auto u1 = extractFirstUrl("\xC2\xA7" "bCheck https://example.com/path_(1).");
    CHECK(u1.has_value());
    CHECK(*u1 == "https://example.com/path_(1)");

    auto u2 = extractFirstUrl("Visit (https://minecraft.net/ja-jp)!");
    CHECK(u2.has_value());
    CHECK(*u2 == "https://minecraft.net/ja-jp");

    // Unsafe schemes ignored
    CHECK(!extractFirstUrl("file:///C:/Windows/calc.exe").has_value());
    CHECK(!extractFirstUrl("javascript:alert(1)").has_value());
    CHECK(!extractFirstUrl("just plain text without link").has_value());

    // Spoofed userinfo (@) URLs and overlong (>2048) URLs rejected, while path @ (e.g. YouTube) is allowed.
    CHECK(!extractFirstUrl("https://trusted.example.com@evil.example.com/login").has_value());
    auto yt = extractFirstUrl("https://youtube.com/@minecraft");
    CHECK(yt.has_value() && *yt == "https://youtube.com/@minecraft");
    std::string hugeUrl = "https://example.com/" + std::string(2050, 'a');
    CHECK(!extractFirstUrl(hugeUrl).has_value());
}

void testFormatterWrapAndFormattingCarryOver() {
    using namespace javachat;
    // Fixed-width measure: 10px per visible ASCII char, ignoring § codes.
    TextMeasureFn mono = [](std::string_view sv) -> float {
        std::string clean = stripFormatting(sv);
        float       w     = 0.0f;
        size_t      pos   = 0;
        while (pos < clean.size()) {
            uint32_t cp = nextUtf8Codepoint(clean, pos);
            w += (cp >= 0x80) ? 16.0f : 10.0f;
        }
        return w;
    };

    // Word wrap preserves active §c§l across wrapped lines.
    std::string msg   = "\xC2\xA7" "c\xC2\xA7" "lhello world again";
    auto        lines = wrapFormattedText(msg, 70.0f, mono);
    CHECK(lines.size() == 3);
    CHECK(stripFormatting(lines[0]) == "hello");
    CHECK(stripFormatting(lines[1]) == "world");
    CHECK(stripFormatting(lines[2]) == "again");
    CHECK(lines[1].starts_with("\xC2\xA7" "c\xC2\xA7" "l"));
    CHECK(lines[2].starts_with("\xC2\xA7" "c\xC2\xA7" "l"));

    // Japanese UTF-8 wrapping without spaces (each kanji = 16px, maxWidth = 48px -> 3 chars per line)
    std::string jp      = "\xC2\xA7" "eあいうえおかき";
    auto        jpLines = wrapFormattedText(jp, 48.0f, mono);
    CHECK(jpLines.size() == 3);
    CHECK(stripFormatting(jpLines[0]) == "あいう");
    CHECK(stripFormatting(jpLines[1]) == "えおか");
    CHECK(stripFormatting(jpLines[2]) == "き");
    CHECK(jpLines[1].starts_with("\xC2\xA7" "e"));

    // Explicit newline resets line cleanly while preserving active color.
    auto nlLines = wrapFormattedText("\xC2\xA7" "bline1\nline2", 200.0f, mono);
    CHECK(nlLines.size() == 2);
    CHECK(stripFormatting(nlLines[0]) == "line1");
    CHECK(nlLines[1] == "\xC2\xA7" "bline2");
}

void testHistorySyncOverlapAndCompact() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.compactDuplicates  = true;
    cfg.maxHistoryMessages = 100;

    ChatHistory h;

    // Initial sync: 2 messages
    std::vector<GuiMessageSnapshot> s1 = {
        {"<A> hi", 10.0f, 1},
        {"<B> hello", 10.0f, 1},
    };
    CHECK(h.syncFromGuiMessages(s1, 1.0, cfg) == 2);
    CHECK(h.size() == 2);

    // Same snapshot -> 0 new
    CHECK(h.syncFromGuiMessages(s1, 1.5, cfg) == 0);
    CHECK(h.size() == 2);

    // Append 1 duplicate message -> compacted into repeatCount=2
    std::vector<GuiMessageSnapshot> s2 = {
        {"<A> hi", 9.0f, 1},
        {"<B> hello", 9.0f, 1},
        {"<B> hello", 10.0f, 1},
    };
    CHECK(h.syncFromGuiMessages(s2, 2.0, cfg) == 1);
    CHECK(h.size() == 2);
    CHECK(h.entries().back().repeatCount == 2);
    CHECK_NEAR(h.entries().back().receivedAtSec, 2.0, 0.001);

    // Ring-buffer shift (oldest evicted, new item appended)
    std::vector<GuiMessageSnapshot> s3 = {
        {"<B> hello", 8.0f, 1},
        {"<B> hello", 9.0f, 1},
        {"<C> shifted", 10.0f, 1},
    };
    CHECK(h.syncFromGuiMessages(s3, 3.0, cfg) == 1);
    CHECK(h.size() == 3);
    CHECK(h.entries().back().plainText == "<C> shifted");

    // Full-capacity identical message detected via duration reset jump
    std::vector<GuiMessageSnapshot> s4 = {
        {"<C> shifted", 5.0f, 1},
        {"<C> shifted", 5.0f, 1},
        {"<C> shifted", 5.0f, 1},
    };
    h.syncFromGuiMessages(s4, 4.0, cfg);
    int beforeRepeat = h.entries().back().repeatCount;
    std::vector<GuiMessageSnapshot> s5 = {
        {"<C> shifted", 4.9f, 1},
        {"<C> shifted", 4.9f, 1},
        {"<C> shifted", 10.0f, 1},
    };
    CHECK(h.syncFromGuiMessages(s5, 5.0, cfg) == 1);
    CHECK(h.entries().back().repeatCount == beforeRepeat + 1);
}

void testHistoryFadeScrollAndHitTest() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width             = 200.0f;
    cfg.closedHeight      = 30.0f; // 3 lines at lineH=10
    cfg.openHeight        = 50.0f; // 5 lines at lineH=10
    cfg.bottomOffset      = 40.0f;
    cfg.leftOffset        = 2.0f;
    cfg.fontScale         = 1.0f;
    cfg.lineSpacing       = 1.0f; // lineH = 10
    cfg.fadeDelaySec      = 9.0f;
    cfg.fadeDurationSec   = 1.0f;
    cfg.compactDuplicates = false;

    // Fade curve checks
    CHECK_NEAR(ChatHistory::computeFadeAlpha(0.0, false, cfg), 1.0f, 0.001f);
    CHECK_NEAR(ChatHistory::computeFadeAlpha(9.0, false, cfg), 1.0f, 0.001f);
    CHECK(ChatHistory::computeFadeAlpha(9.5, false, cfg) > 0.1f);
    CHECK(ChatHistory::computeFadeAlpha(9.5, false, cfg) < 0.9f);
    CHECK_NEAR(ChatHistory::computeFadeAlpha(10.5, false, cfg), 0.0f, 0.001f);
    CHECK_NEAR(ChatHistory::computeFadeAlpha(999.0, true, cfg), 1.0f, 0.001f);

    ChatHistory h;
    for (int i = 0; i < 10; ++i) {
        std::string m = "msg-" + std::to_string(i);
        if (i == 9) m += " https://example.com";
        h.pushMessage(m, static_cast<double>(i), cfg);
    }

    TextMeasureFn measure = [](std::string_view sv) { return estimateTextWidth(sv, 1.0f); };

    // Closed at t=11.0 -> msg-0 (age 11) & msg-1 (age 10) faded out; closedHeight shows at most 3 latest
    auto closedView = h.buildView(640.0f, 360.0f, false, 11.0, cfg, measure);
    CHECK(closedView.lines.size() == 3);
    CHECK(!closedView.showScrollbar);
    CHECK(closedView.lines.back().text.find("msg-9") != std::string::npos);
    CHECK(closedView.lines.back().hasUrl);

    // Open at t=100.0 -> all 10 messages available, openHeight shows 5, scrollbar enabled
    auto openView = h.buildView(640.0f, 360.0f, true, 100.0, cfg, measure);
    CHECK(openView.lines.size() == 5);
    CHECK(openView.showScrollbar);
    CHECK(openView.lines.front().text == "msg-5");
    CHECK(openView.lines.back().text.find("msg-9") != std::string::npos);
    CHECK_NEAR(openView.panelRect.y1, closedView.panelRect.y1, 0.01f);
    CHECK_NEAR(openView.panelRect.y1, 360.0f - cfg.bottomOffset, 0.5f);

    // Scroll up 3 lines -> shows msg-2 .. msg-6
    h.scrollBy(3);
    auto scrolled = h.buildView(640.0f, 360.0f, true, 100.0, cfg, measure);
    CHECK(scrolled.clampedScroll == 3);
    CHECK(scrolled.lines.front().text == "msg-2");
    CHECK(scrolled.lines.back().text == "msg-6");

    // Scroll way past top -> clamped to maxScroll = 5 (msg-0 .. msg-4)
    h.scrollBy(100);
    auto atTop = h.buildView(640.0f, 360.0f, true, 100.0, cfg, measure);
    CHECK(atTop.clampedScroll == 5);
    CHECK(atTop.lines.front().text == "msg-0");
    CHECK(atTop.lines.back().text == "msg-4");

    // Hit test inside line 2
    auto const& r2  = atTop.lines[2].rect;
    auto        hit = ChatHistory::hitTestLine(atTop, (r2.x0 + r2.x1) * 0.5f, (r2.y0 + r2.y1) * 0.5f);
    CHECK(hit.has_value());
    CHECK(*hit == 2);
    CHECK(!ChatHistory::hitTestLine(atTop, 500.0f, 10.0f).has_value());

    // Bottom offset override acts as a floor above tall autocomplete popups without dropping below bottomOffset
    auto notLowered = h.buildView(640.0f, 360.0f, true, 100.0, cfg, measure, 33.0f);
    CHECK_NEAR(notLowered.panelRect.y1, 360.0f - cfg.bottomOffset, 0.5f);

    auto shifted = h.buildView(640.0f, 360.0f, true, 100.0, cfg, measure, 132.0f);
    CHECK_NEAR(shifted.panelRect.y1, 360.0f - 132.0f, 0.5f);
    CHECK(shifted.panelRect.y0 >= 0.0f);

    // Authentic Java width stays consistent at configured width
    ChatHistory shortHist;
    ChatConfig  wideCfg = cfg;
    wideCfg.width       = 320.0f;
    shortHist.pushMessage("hi", 10.0, wideCfg);
    auto shortClosed = shortHist.buildView(640.0f, 360.0f, false, 10.5, wideCfg, measure);
    CHECK_NEAR(shortClosed.panelRect.width(), 320.0f, 0.5f);
}

void testWrappedLineCacheInvalidation() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width             = 200.0f;
    cfg.openHeight        = 120.0f;
    cfg.compactDuplicates = true;
    cfg.showTimestamps    = false;

    ChatHistory h;
    h.pushMessage("Hello cached world", 1.0, cfg, 1, "\xC2\xA7" "8[03:00] \xC2\xA7" "r");
    h.pushMessage("Second line", 2.0, cfg, 1, "\xC2\xA7" "8[03:00] \xC2\xA7" "r");

    int           measureCalls = 0;
    TextMeasureFn counting     = [&](std::string_view sv) -> float {
        ++measureCalls;
        return estimateTextWidth(sv, cfg.fontScale);
    };

    // First build computes wrap
    (void)h.buildView(640.0f, 360.0f, true, 5.0, cfg, counting);
    CHECK(measureCalls > 0);

    // Second build with identical parameters hits cache (0 measure calls)
    int afterFirst = measureCalls;
    (void)h.buildView(640.0f, 360.0f, true, 5.1, cfg, counting);
    CHECK(measureCalls == afterFirst);

    // Changing width invalidates cache
    cfg.width = 140.0f;
    (void)h.buildView(640.0f, 360.0f, true, 5.2, cfg, counting);
    CHECK(measureCalls > afterFirst);

    // Toggling timestamps invalidates cache and prepends timestamp text
    int afterWidth   = measureCalls;
    cfg.showTimestamps = true;
    auto tsView      = h.buildView(640.0f, 360.0f, true, 5.3, cfg, counting);
    CHECK(measureCalls > afterWidth);
    CHECK(!tsView.lines.empty());
    CHECK(tsView.lines.front().text.find("[03:00]") != std::string::npos);
}

void testBedrockColorCodesAndCorruptUtf8() {
    using namespace javachat;
    TextMeasureFn mono = [](std::string_view sv) -> float {
        std::string clean = stripFormatting(sv);
        float       w     = 0.0f;
        size_t      pos   = 0;
        while (pos < clean.size()) {
            uint32_t cp = nextUtf8Codepoint(clean, pos);
            w += (cp >= 0x80) ? 16.0f : 10.0f;
        }
        return w;
    };

    // Bedrock §m (Redstone) & §n (Copper) are color codes that carry over and reset §l when changed.
    std::string bedrockMsg = "\xC2\xA7" "m\xC2\xA7" "lalpha beta \xC2\xA7" "ngamma delta";
    auto        lines      = wrapFormattedText(bedrockMsg, 60.0f, mono);
    CHECK(lines.size() == 4);
    CHECK(lines[1].starts_with("\xC2\xA7" "m\xC2\xA7" "l"));
    CHECK(lines[3].starts_with("\xC2\xA7" "n"));
    CHECK(!lines[3].starts_with("\xC2\xA7" "n\xC2\xA7" "l")); // §n reset bold

    // §g and §v also behave as color codes and reset prior bold.
    std::string matMsg   = "\xC2\xA7" "lbold\xC2\xA7" "g gold \xC2\xA7" "vamethyst";
    auto        matLines = wrapFormattedText(matMsg, 70.0f, mono);
    CHECK(matLines.size() >= 2);
    CHECK(matLines.back().starts_with("\xC2\xA7" "v"));

    // Corrupt UTF-8 sequences never hang or overrun and decode safely as U+FFFD.
    std::string badUtf8 = "ok\x80\xE3\x81" "end";
    size_t      pos     = 0;
    std::vector<uint32_t> cps;
    while (pos < badUtf8.size()) {
        cps.push_back(nextUtf8Codepoint(badUtf8, pos));
    }
    CHECK(pos == badUtf8.size());
    CHECK(cps.size() >= 6);
    CHECK(cps[2] == 0xFFFD);

    // stripFormatting handles control chars & tabs safely while preserving UTF-8.
    std::string dirty = "\xC2\xA7" "aHi\x01\x02\tthere\r\nあ";
    CHECK(stripFormatting(dirty) == "Hi there\nあ");
}

void testJsoncCommentsTrailingCommasAndSurrogates() {
    using namespace javachat;
    std::string jsonc = "\xEF\xBB\xBF" R"({
        // line comment
        /* block comment
           spanning lines */
        "width": 360.0,
        "textShadow": false,
        "unknownArr": [1, 2, 3,],
        "unknownStr": "emoji \uD83D\uDE00 ok",
    })";
    ChatConfig  cfg;
    std::string warn;
    CHECK(parseConfigJson(jsonc, cfg, &warn));
    CHECK(warn.empty());
    CHECK_NEAR(cfg.width, 360.0f, 0.01f);
    CHECK(cfg.textShadow == false);

    // Unterminated block comment is rejected cleanly.
    ChatConfig bad;
    CHECK(!parseConfigJson(R"({"width": 300 /* unclosed })", bad, &warn));
    CHECK(!warn.empty());
}

void testScrollAnchorAndScrollbarDrag() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width             = 120.0f;
    cfg.openHeight        = 40.0f; // 4 lines at lineH=10
    cfg.closedHeight      = 20.0f;
    cfg.bottomOffset      = 20.0f;
    cfg.leftOffset        = 2.0f;
    cfg.fontScale         = 1.0f;
    cfg.lineSpacing       = 1.0f; // lineH = 10
    cfg.compactDuplicates = false;
    cfg.maxHistoryMessages = 10;

    TextMeasureFn mono = [](std::string_view sv) -> float {
        return static_cast<float>(stripFormatting(sv).size()) * 10.0f;
    };

    ChatHistory h;
    for (int i = 0; i < 8; ++i) {
        h.pushMessage("line" + std::to_string(i), static_cast<double>(i), cfg);
    }

    // Open and scroll up by 2 lines -> viewing line2..line5 (bottom line is line5).
    (void)h.buildView(640.0f, 360.0f, true, 10.0, cfg, mono);
    h.scrollBy(2);
    auto beforePush = h.buildView(640.0f, 360.0f, true, 10.0, cfg, mono);
    CHECK(beforePush.lines.size() == 4);
    CHECK(beforePush.lines.back().text == "line5");
    CHECK(beforePush.showScrollbar);

    // Push a long message that wraps into 3 lines while scrolled up.
    // Anchor must keep line2..line5 locked in view!
    h.pushMessage("aaaaaaaaaa bbbbbbbbbb cccccccccc", 11.0, cfg);
    auto afterPush = h.buildView(640.0f, 360.0f, true, 11.0, cfg, mono);
    CHECK(afterPush.lines.size() == 4);
    CHECK(afterPush.lines.front().text == "line2");
    CHECK(afterPush.lines.back().text == "line5");

    // Evict oldest entry while scrolled up -> anchor STILL holds line5 at bottom.
    h.pushMessage("tail1", 12.0, cfg);
    h.pushMessage("tail2", 13.0, cfg); // 11 pushed total, cap=10 -> line0 evicted
    auto afterEvict = h.buildView(640.0f, 360.0f, true, 13.0, cfg, mono);
    CHECK(h.size() == 10);
    CHECK(afterEvict.lines.back().text == "line5");

    // Scrollbar hit-testing & drag calculation
    float sbMidX = (afterEvict.scrollbarTrackRect.x0 + afterEvict.scrollbarTrackRect.x1) * 0.5f;
    float sbTopY = afterEvict.scrollbarTrackRect.y0 + 1.0f;
    float sbBotY = afterEvict.scrollbarTrackRect.y1 - 1.0f;
    CHECK(ChatHistory::hitTestScrollbar(afterEvict, sbMidX, sbTopY));
    CHECK(ChatHistory::hitTestScrollbar(afterEvict, afterEvict.scrollbarTrackRect.x0 - 2.0f, sbTopY));
    CHECK(!ChatHistory::hitTestScrollbar(afterEvict, 10.0f, sbTopY));

    h.setScrollFromTrackY(afterEvict, sbTopY);
    auto topDragged = h.buildView(640.0f, 360.0f, true, 13.0, cfg, mono);
    CHECK(topDragged.lines.front().text == "line1");

    h.setScrollFromTrackY(afterEvict, sbBotY);
    auto botDragged = h.buildView(640.0f, 360.0f, true, 13.0, cfg, mono);
    CHECK(botDragged.clampedScroll == 0);
    CHECK(botDragged.lines.back().text == "tail2");

    // Immediate trimToMax
    h.trimToMax(3);
    CHECK(h.size() == 3);
    CHECK(h.entries().front().plainText == "aaaaaaaaaa bbbbbbbbbb cccccccccc");
}

void testMultiUrlPerLineExtraction() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width      = 120.0f;
    cfg.openHeight = 100.0f;
    cfg.leftOffset = 2.0f;
    cfg.fontScale  = 1.0f;

    TextMeasureFn mono = [](std::string_view sv) -> float {
        return static_cast<float>(stripFormatting(sv).size()) * 5.0f;
    };

    ChatHistory h;
    h.pushMessage("first https://a.example second https://b.example", 1.0, cfg);
    auto view = h.buildView(640.0f, 360.0f, true, 2.0, cfg, mono);
    CHECK(view.lines.size() >= 2);

    std::vector<std::string> foundUrls;
    for (auto const& l : view.lines) {
        if (l.lineUrl.has_value()) foundUrls.push_back(*l.lineUrl);
    }
    CHECK(foundUrls.size() == 2);
    if (foundUrls.size() == 2) {
        CHECK(foundUrls[0] == "https://a.example");
        CHECK(foundUrls[1] == "https://b.example");
    }

    // Wrapped line without a URL must NOT set hasUrl or lineUrl even if another line of the entry has a URL.
    ChatHistory h2;
    h2.pushMessage("prefix words without any link here https://only-second.example", 1.0, cfg);
    auto v2 = h2.buildView(640.0f, 360.0f, true, 2.0, cfg, mono);
    CHECK(v2.lines.size() >= 2);
    CHECK(!v2.lines.front().lineUrl.has_value());
    CHECK(!v2.lines.front().hasUrl);
    bool anyHasUrl = false;
    for (auto const& l : v2.lines) {
        CHECK(l.hasUrl == l.lineUrl.has_value());
        if (l.hasUrl) anyHasUrl = true;
    }
    CHECK(anyHasUrl);
}

void testOptimizationNeedsSyncFontRevisionAndZeroFade() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width             = 220.0f;
    cfg.openHeight        = 60.0f;
    cfg.closedHeight      = 30.0f;
    cfg.lineSpacing       = 1.0f;
    cfg.fadeDelaySec      = 9.0f;
    cfg.fadeDurationSec   = 0.0f;
    cfg.compactDuplicates = false;
    cfg.maxHistoryMessages = 500;

    // Instant fade when fadeDurationSec == 0.0
    CHECK_NEAR(ChatHistory::computeFadeAlpha(8.9, false, cfg), 1.0f, 0.001f);
    CHECK_NEAR(ChatHistory::computeFadeAlpha(9.0, false, cfg), 1.0f, 0.001f);
    CHECK_NEAR(ChatHistory::computeFadeAlpha(9.01, false, cfg), 0.0f, 0.001f);

    ChatHistory h;
    CHECK(!h.needsSync(0, "", 0, "", 0, 0.0f, 1.0));
    CHECK(!h.hasVisibleClosedMessages(1.0, cfg));

    std::vector<GuiMessageSnapshot> s1 = {
        {"msg-1", 10.0f, 1},
        {"msg-2", 10.0f, 1},
    };
    CHECK(h.needsSync(2, "msg-1", 1, "msg-2", 1, 10.0f, 1.0));
    CHECK(h.syncFromGuiMessages(s1, 1.0, cfg) == 2);
    CHECK(!h.needsSync(2, "msg-1", 1, "msg-2", 1, 9.8f, 1.2)); // normal aging -> O(1) skip
    CHECK(h.needsSync(3, "msg-1", 1, "msg-3", 1, 10.0f, 1.5)); // count changed
    CHECK(h.needsSync(2, "msg-0", 1, "msg-2", 1, 9.5f, 1.5)); // head changed
    CHECK(h.needsSync(2, "msg-1", 2, "msg-2", 1, 9.5f, 1.5)); // type changed
    CHECK(h.needsSync(2, "msg-1", 1, "msg-2", 1, 2.0f, 1.1)); // duration jump at same count
    CHECK(!h.needsSync(2, "msg-1", 1, "msg-2", 1, 10.0f, 1.1));
    std::vector<GuiMessageSnapshot> sSame = {
        {"msg-2", 3.0f, 1},
        {"msg-2", 3.0f, 1},
    };
    h.syncFromGuiMessages(sSame, 5.0, cfg);
    CHECK(h.needsSync(2, "msg-2", 1, "msg-2", 1, 10.0f, 5.1)); // identical ring jump detected

    // Regression: full ring buffer with identical messages aging down, then a fresh identical message resets duration back to 10.0
    std::vector<GuiMessageSnapshot> sFull = {
        {"dup", 10.0f, 1},
        {"dup", 10.0f, 1},
        {"dup", 10.0f, 1},
    };
    ChatHistory hFull;
    ChatConfig  dupCfg = cfg;
    dupCfg.compactDuplicates = true;
    CHECK(hFull.syncFromGuiMessages(sFull, 10.0, dupCfg) == 3);
    CHECK(hFull.entries().back().repeatCount == 3);
    // Simulate several frames of normal countdown aging (10.0 -> 9.0 -> 5.0)
    CHECK(!hFull.needsSync(3, "dup", 1, "dup", 1, 9.0f, 11.0));
    CHECK(!hFull.needsSync(3, "dup", 1, "dup", 1, 5.0f, 15.0));
    // Now a fresh identical message arrives at t=15.05, resetting back duration to 10.0 (same as last synced!)
    CHECK(hFull.needsSync(3, "dup", 1, "dup", 1, 10.0f, 15.05));
    std::vector<GuiMessageSnapshot> sFullReset = {
        {"dup", 5.0f, 1},
        {"dup", 5.0f, 1},
        {"dup", 10.0f, 1},
    };
    CHECK(hFull.syncFromGuiMessages(sFullReset, 15.05, dupCfg) == 1);
    CHECK(hFull.entries().back().repeatCount == 4);

    // hasVisibleClosedMessages respects fadeDelaySec + fadeDurationSec
    CHECK(h.hasVisibleClosedMessages(13.9, cfg));
    CHECK(!h.hasVisibleClosedMessages(14.1, cfg));

    // fontRevision invalidates wrap cache cleanly
    int           measureCalls = 0;
    TextMeasureFn counting     = [&](std::string_view sv) -> float {
        ++measureCalls;
        return estimateTextWidth(sv, cfg.fontScale);
    };
    (void)h.buildView(640.0f, 360.0f, true, 6.0, cfg, counting, -1.0f, 1);
    int afterRev1 = measureCalls;
    (void)h.buildView(640.0f, 360.0f, true, 6.1, cfg, counting, -1.0f, 1);
    CHECK(measureCalls == afterRev1); // cache hit
    (void)h.buildView(640.0f, 360.0f, true, 6.2, cfg, counting, -1.0f, 2);
    CHECK(measureCalls > afterRev1); // invalidated on font revision change

    // 500-message open chat view slice & anchor preservation
    ChatHistory bulk;
    for (int i = 0; i < 500; ++i) {
        bulk.pushMessage("line-" + std::to_string(i), static_cast<double>(i), cfg);
    }
    TextMeasureFn mono = [](std::string_view sv) { return static_cast<float>(stripFormatting(sv).size()) * 6.0f; };
    auto vBottom = bulk.buildView(640.0f, 360.0f, true, 501.0, cfg, mono, -1.0f, 1);
    CHECK(vBottom.lines.size() == 6);
    CHECK(vBottom.lines.back().text == "line-499");
    bulk.scrollBy(10);
    auto vMid = bulk.buildView(640.0f, 360.0f, true, 501.0, cfg, mono, -1.0f, 1);
    CHECK(vMid.lines.back().text == "line-489");
    bulk.pushMessage("line-500", 502.0, cfg); // evicts line-0, keeps anchor on line-489
    auto vAnchored = bulk.buildView(640.0f, 360.0f, true, 502.0, cfg, mono, -1.0f, 1);
    CHECK(vAnchored.lines.back().text == "line-489");
}

void testCommandSuggestions() {
    using namespace javachat;

    // extractSuggestionToken strips Bedrock § color wrappers, description suffixes, & item-icon padding cleanly.
    CHECK(extractSuggestionToken("gamemode - プレイヤーのゲームモードを設定します。") == "gamemode");
    CHECK(
        extractSuggestionToken(
            "\xC2\xA7" "7\xC2\xA7" "fgamemode\xC2\xA7" "7\xC2\xA7" "f\xC2\xA7" "7 - プレイヤーのゲームモードを設定します。"
        )
        == "gamemode"
    );
    CHECK(extractSuggestionToken("   \xC2\xA7" "7\xC2\xA7" "fstone\xC2\xA7" "7") == "stone");
    CHECK(extractSuggestionToken("   stone - Solid block") == "stone");
    CHECK(extractSuggestionToken("dark-oak - Hyphenated name") == "dark-oak");
    CHECK(extractSuggestionToken("\"Player - 1\" - quoted selector") == "\"Player - 1\"");
    CHECK(extractSuggestionToken("...").empty());
    CHECK(extractSuggestionToken("   ").empty());

    // findActiveTokenSpan + applySuggestionToInput.
    CHECK(findActiveTokenSpan("/").startByte == 1);
    CHECK(findActiveTokenSpan("/game").startByte == 1);
    CHECK(findActiveTokenSpan("/game").argIndex == 0);
    CHECK(findActiveTokenSpan("/gamemode ").startByte == 10);
    CHECK(findActiveTokenSpan("/gamemode ").argIndex == 1);

    CHECK(applySuggestionToInput("/", "gamemode") == "/gamemode");
    CHECK(applySuggestionToInput("/", "/gamemode") == "/gamemode"); // no double slash
    CHECK(applySuggestionToInput("/game", "gamemode") == "/gamemode");
    CHECK(applySuggestionToInput("/gamemode ", "creative") == "/gamemode creative");
    CHECK(applySuggestionToInput("/gamemode c", "creative") == "/gamemode creative");
    CHECK(applySuggestionToInput("/tp @e[type=z", "zombie") == "/tp @e[type=zombie");
    CHECK(applySuggestionToInput("/give @s minecraft:dia", "diamond") == "/give @s minecraft:diamond");
    CHECK(applySuggestionToInput("/give @s minecraft:dia", "minecraft:diamond") == "/give @s minecraft:diamond");
    CHECK(applySuggestionToInput("hello @Al", "Alice") == "hello Alice");

    // computeInlineGhostSuffix.
    CHECK(computeInlineGhostSuffix("/game", "gamemode") == "mode");
    CHECK(computeInlineGhostSuffix("/GAME", "gamemode") == "mode");
    CHECK(computeInlineGhostSuffix("/gamemode ", "creative") == "creative");
    CHECK(computeInlineGhostSuffix("/", "gamemode") == "gamemode");
    CHECK(computeInlineGhostSuffix("/gamemode", "gamemode").empty());
    CHECK(computeInlineGhostSuffix("/gm", "gamemode").empty());

    // formatJavaCommandSyntax + invariant stripFormatting(out) == input.
    std::vector<std::string> cmds{"gamemode", "gamerule"};
    CHECK(formatJavaCommandSyntax("/", cmds, false) == "\xC2\xA7" "7/");
    CHECK(formatJavaCommandSyntax("/game", cmds, false) == "\xC2\xA7" "7/\xC2\xA7" "cgame");
    CHECK(formatJavaCommandSyntax("/game", cmds, true) == "\xC2\xA7" "7/\xC2\xA7" "cgame");
    CHECK(formatJavaCommandSyntax("/gamemode", cmds, false) == "\xC2\xA7" "7/gamemode");

    std::string sixArgs = "/cmd a b c d e f";
    std::string colored = formatJavaCommandSyntax(sixArgs, {}, true);
    CHECK(
        colored == "\xC2\xA7" "7/cmd \xC2\xA7" "ba \xC2\xA7" "eb \xC2\xA7" "ac \xC2\xA7" "dd \xC2\xA7" "6e \xC2\xA7" "bf"
    );
    CHECK(stripFormatting(colored) == sixArgs);

    std::string badWithArgs = "/badcmd arg1 arg2";
    std::string coloredBad  = formatJavaCommandSyntax(badWithArgs, {}, false);
    CHECK(coloredBad == "\xC2\xA7" "7/\xC2\xA7" "cbadcmd arg1 arg2");
    CHECK(stripFormatting(coloredBad) == badWithArgs);

    CHECK(formatJavaCommandSyntax("plain text", cmds, false) == "plain text");
    CHECK(formatJavaCommandSyntax("/say \xC2\xA7" "ahi", cmds, true) == "/say \xC2\xA7" "ahi");

    // CommandSuggestionState navigation, scrolling, Tab cycling, click completion, and layout geometry.
    CommandSuggestionState state;
    std::vector<std::string> fifteen;
    for (int i = 0; i < 15; ++i) {
        fifteen.push_back("cmd" + std::to_string(i) + " - desc");
    }
    state.syncFromBedrock("/", fifteen);
    CHECK(state.active());
    CHECK(state.items().size() == 15);
    CHECK(state.selectedIndex() == 0);

    // Move Up from 0 wraps to last item (14) and scrolls window down.
    state.moveSelection(-1);
    CHECK(state.selectedIndex() == 14);
    CHECK(state.scrollOffset() == 5);
    // Move Down wraps back to 0 and scrolls window to top.
    state.moveSelection(+1);
    CHECK(state.selectedIndex() == 0);
    CHECK(state.scrollOffset() == 0);

    // Wheel scroll clamping.
    state.scrollByRows(100);
    CHECK(state.scrollOffset() == 5);
    state.scrollByRows(-100);
    CHECK(state.scrollOffset() == 0);

    // Regression: Bedrock §7§f...§7§f§7 wrapped suggestions yield clean ghost preview & Tab completion.
    state.syncFromBedrock(
        "/game",
        {
            "\xC2\xA7" "7\xC2\xA7" "fgamemode\xC2\xA7" "7\xC2\xA7" "f\xC2\xA7" "7 - desc",
            "\xC2\xA7" "7\xC2\xA7" "fgamerule\xC2\xA7" "7\xC2\xA7" "f\xC2\xA7" "7 - desc2",
        }
    );
    CHECK(state.items().size() == 2);
    CHECK(state.selectedToken() == "gamemode");
    CHECK(computeInlineGhostSuffix("/game", state.selectedToken()) == "mode");

    // Tab cycling through multiple candidates without losing the list.
    state.syncFromBedrock("/game", {"gamemode - d1", "gamerule - d2"});
    auto t1 = state.applyByTab(+1);
    CHECK(t1.has_value() && t1->newText == "/gamemode" && !t1->refreshIntellisense);
    // Simulate Bedrock sync at /gamemode while Tab cycling is active -> keeps both candidates.
    state.syncFromBedrock("/gamemode", {"creative - c", "survival - s"});
    auto t2 = state.applyByTab(+1);
    CHECK(t2.has_value() && t2->newText == "/gamerule" && !t2->refreshIntellisense);
    auto t3 = state.applyByTab(+1);
    CHECK(t3.has_value() && t3->newText == "/gamemode" && !t3->refreshIntellisense);
    // User typing next char exits Tab cycle mode.
    state.syncFromBedrock("/gamemode ", {"creative - c", "survival - s"});
    CHECK(state.items().size() == 2);
    CHECK(state.items()[0] == "creative");

    // Popup layout: narrow width, aligned X after "/", flush 1px above 12px input bar (y1 = screenH - 15).
    state.syncFromBedrock("/game", {"gamemode - desc", "gamerule - long description here"});
    TextMeasureFn mono6 = [](std::string_view sv) -> float {
        return static_cast<float>(stripFormatting(sv).size()) * 6.0f;
    };
    auto layout = state.buildLayout(360.0f, 180.0f, 4.0f, 1.0f, mono6);
    CHECK(layout.visible);
    CHECK(layout.rows.size() == 2);
    CHECK_NEAR(layout.popupRect.y1, 180.0f - 15.0f, 0.01f);
    CHECK_NEAR(layout.popupRect.height(), 24.0f, 0.01f);
    // "/" is 6px at 4.0 -> token starts at 10.0, padLeft 1.0 -> x0 == 9.0, width == 8*6 + 1 + 2 = 51.0.
    CHECK_NEAR(layout.popupRect.x0, 9.0f, 0.01f);
    CHECK_NEAR(layout.popupRect.width(), 51.0f, 0.01f);
    CHECK(layout.rows[0].selected);
    CHECK(!layout.rows[1].selected);

    auto hitRow1 = CommandSuggestionState::hitTestRow(
        layout,
        layout.popupRect.x0 + 5.0f,
        layout.rows[1].rect.y0 + 3.0f
    );
    CHECK(hitRow1.has_value() && *hitRow1 == 1);
    auto clicked = state.applyByClick(*hitRow1);
    CHECK(clicked.has_value() && clicked->newText == "/gamerule" && clicked->refreshIntellisense);
}

void testSelectAllAndClipboardHelpers() {
    using namespace javachat;

    // End append
    auto r1 = extractInsertedTextAfterSelectAll("/gamemode spectator", "/gamemode spectator/");
    CHECK(r1.has_value() && *r1 == "/");

    // Mid-string insertion
    auto r2 = extractInsertedTextAfterSelectAll("/gamemode spectator", "/gameamode spectator");
    CHECK(r2.has_value() && *r2 == "a");

    // Start insertion with multibyte UTF-8 sharing lead bytes ("あ" E3 81 82 vs "い" E3 81 84)
    auto r3 = extractInsertedTextAfterSelectAll("あ", "いあ");
    CHECK(r3.has_value() && *r3 == "い");

    // Multi-char Japanese IME commit
    auto r4 = extractInsertedTextAfterSelectAll("hello", "helloこんにちは");
    CHECK(r4.has_value() && *r4 == "こんにちは");

    // Repeated chars
    auto r5 = extractInsertedTextAfterSelectAll("aaa", "aaaa");
    CHECK(r5.has_value() && *r5 == "a");

    // Empty / non-growing / mismatched edits
    CHECK(!extractInsertedTextAfterSelectAll("", "a").has_value());
    CHECK(!extractInsertedTextAfterSelectAll("abc", "abc").has_value());
    CHECK(!extractInsertedTextAfterSelectAll("abc", "ab").has_value());
    CHECK(!extractInsertedTextAfterSelectAll("abc", "acbd").has_value());

    // sanitizeClipboardForChat
    CHECK(sanitizeClipboardForChat("/gamemode spectator\r\n") == "/gamemode spectator");
    CHECK(sanitizeClipboardForChat("line1\r\nline2\n\ttabbed\r") == "line1 line2  tabbed");
    CHECK(sanitizeClipboardForChat(std::string_view("a\x01\x1F" "b\x7F" "c", 6)) == "abc");
    CHECK(sanitizeClipboardForChat("あいうえお", 3) == "あいう");
}

void testFastTypingSuggestionSyncAndKnownCommands() {
    using namespace javachat;

    CommandSuggestionState state;
    std::vector<std::string> rootCmds{"ability - d0", "gamemode - d1", "gamerule - d2", "give - d3"};
    state.syncFromBedrock("/", rootCmds, false);
    CHECK(state.items().size() == 4);
    CHECK(state.isKnownCommand("/gamemode"));
    CHECK(state.isKnownCommand("/GIVE"));
    // Partial prefix must NOT be treated as a complete known command!
    CHECK(!state.isKnownCommand("/game"));

    // Fast typing "/g" before Bedrock ticks (same stale rootCmds, intellisensePending == true)
    state.syncFromBedrock("/g", rootCmds, true);
    CHECK(state.items().size() == 3);
    CHECK(state.selectedToken() == "gamemode");
    CHECK(computeInlineGhostSuffix("/g", state.selectedToken()) == "amemode");

    // Fast typing "/gi" -> narrows immediately to "give"
    state.syncFromBedrock("/gi", rootCmds, true);
    CHECK(state.items().size() == 1);
    CHECK(state.selectedToken() == "give");
    CHECK(computeInlineGhostSuffix("/gi", state.selectedToken()) == "ve");

    // Known complete command "/give" stays gray even when pending intellisense hasn't sent usage yet
    state.syncFromBedrock("/give", rootCmds, true);
    CHECK(state.isKnownCommand("/give"));
    CHECK(formatJavaCommandSyntax("/give", state.items(), false, state.isKnownCommand("/give")) == "\xC2\xA7" "7/give");

    // Partial command "/game" stays red even with knownCommandValid == false
    state.syncFromBedrock("/game", rootCmds, true);
    CHECK(!state.isKnownCommand("/game"));
    CHECK(
        formatJavaCommandSyntax("/game", state.items(), false, state.isKnownCommand("/game"))
        == "\xC2\xA7" "7/\xC2\xA7" "cgame"
    );

    // Crossing space into arg 1 while Bedrock still holds stale arg-0 list -> clear items immediately
    std::vector<std::string> gmOnly{"gamemode - d1"};
    state.syncFromBedrock("/gamemode", gmOnly, false);
    state.syncFromBedrock("/gamemode ", gmOnly, true);
    CHECK(state.items().empty());
    CHECK(state.isKnownCommand("/gamemode "));
    CHECK(
        formatJavaCommandSyntax("/gamemode ", state.items(), false, state.isKnownCommand("/gamemode "))
        == "\xC2\xA7" "7/gamemode "
    );

    // Bedrock tick delivers arg-1 suggestions
    std::vector<std::string> gmModes{"adventure", "creative", "spectator", "survival"};
    state.syncFromBedrock("/gamemode ", gmModes, false);
    CHECK(state.items().size() == 4);
    CHECK(state.items()[0] == "adventure");

    // World switch clears known commands
    state.clearKnownCommands();
    CHECK(!state.isKnownCommand("/gamemode"));
}

void testWordDeletionUtf8() {
    using namespace javachat;

    {
        auto r = deletePreviousWordUtf8("", -1);
        CHECK(!r.changed && r.newText.empty() && r.newCaretCodepoint == 0);
        auto r0 = deletePreviousWordUtf8("hello", 0);
        CHECK(!r0.changed && r0.newText == "hello" && r0.newCaretCodepoint == 0);
    }

    // Command chaining: "/gamemode creative" -> "/gamemode " -> "/" -> ""
    {
        auto r1 = deletePreviousWordUtf8("/gamemode creative", -1);
        CHECK(r1.changed && r1.newText == "/gamemode " && r1.newCaretCodepoint == 10);
        auto r2 = deletePreviousWordUtf8(r1.newText, r1.newCaretCodepoint);
        CHECK(r2.changed && r2.newText == "/" && r2.newCaretCodepoint == 1);
        auto r3 = deletePreviousWordUtf8(r2.newText, r2.newCaretCodepoint);
        CHECK(r3.changed && r3.newText.empty() && r3.newCaretCodepoint == 0);
        auto r4 = deletePreviousWordUtf8(r3.newText, r3.newCaretCodepoint);
        CHECK(!r4.changed && r4.newText.empty() && r4.newCaretCodepoint == 0);
    }

    // Trailing spaces & namespaces & selectors
    {
        auto ws = deletePreviousWordUtf8("hello   ", -1);
        CHECK(ws.changed && ws.newText.empty() && ws.newCaretCodepoint == 0);

        auto ns = deletePreviousWordUtf8("minecraft:diamond_sword", -1);
        CHECK(ns.changed && ns.newText == "minecraft:" && ns.newCaretCodepoint == 10);

        auto sel = deletePreviousWordUtf8("@e[type=zombie", -1);
        CHECK(sel.changed && sel.newText == "@e[type=" && sel.newCaretCodepoint == 8);
    }

    // Mid-string caret positions
    {
        auto mid1 = deletePreviousWordUtf8("foo bar baz", 7);
        CHECK(mid1.changed && mid1.newText == "foo  baz" && mid1.newCaretCodepoint == 4);

        auto mid2 = deletePreviousWordUtf8("hello world", 5);
        CHECK(mid2.changed && mid2.newText == " world" && mid2.newCaretCodepoint == 0);

        auto mid3 = deletePreviousWordUtf8("   hello", 5);
        CHECK(mid3.changed && mid3.newText == "   llo" && mid3.newCaretCodepoint == 3);
    }

    // Japanese UTF-8 character classes (Hiragana, Katakana, Kanji, prolonged mark, U+3000, punctuation)
    {
        auto j1 = deletePreviousWordUtf8("こんにちは世界です", -1);
        CHECK(j1.changed && j1.newText == "こんにちは世界" && j1.newCaretCodepoint == 7);
        auto j2 = deletePreviousWordUtf8(j1.newText, j1.newCaretCodepoint);
        CHECK(j2.changed && j2.newText == "こんにちは" && j2.newCaretCodepoint == 5);
        auto j3 = deletePreviousWordUtf8(j2.newText, j2.newCaretCodepoint);
        CHECK(j3.changed && j3.newText.empty() && j3.newCaretCodepoint == 0);

        auto kata = deletePreviousWordUtf8("ゲームスタート", -1);
        CHECK(kata.changed && kata.newText.empty() && kata.newCaretCodepoint == 0);

        auto hiraLong = deletePreviousWordUtf8("テスト　らーめん", -1);
        CHECK(hiraLong.changed && hiraLong.newText == "テスト　" && hiraLong.newCaretCodepoint == 4);
        auto hiraLong2 = deletePreviousWordUtf8("らー", -1);
        CHECK(hiraLong2.changed && hiraLong2.newText.empty() && hiraLong2.newCaretCodepoint == 0);

        auto dotSep = deletePreviousWordUtf8("カタカナ・テスト", -1);
        CHECK(dotSep.changed && dotSep.newText == "カタカナ・" && dotSep.newCaretCodepoint == 5);
    }

    // Invalid UTF-8 bytes do not hang or crash
    {
        std::string corrupt("\xFF\xFE" "abc", 5);
        auto        rc = deletePreviousWordUtf8(corrupt, -1);
        CHECK(rc.changed && rc.newText == std::string("\xFF\xFE", 2) && rc.newCaretCodepoint == 2);
    }
}

void testSearchFilterAndHighlight() {
    using namespace javachat;

    // ASCII case folding & UTF-8 matching
    CHECK(containsIgnoreCaseAscii("Hello World", ""));
    CHECK(containsIgnoreCaseAscii("Hello World", "wOrLd"));
    CHECK(!containsIgnoreCaseAscii("Hello", "Hello World"));
    CHECK(containsIgnoreCaseAscii("<Alice> ダイヤモンド発見！", "ダイヤ"));
    CHECK(!containsIgnoreCaseAscii("<Alice> ダイヤモンド発見！", "エメラルド"));

    // Highlight formatting preservation
    {
        std::string const s1 = "\xC2\xA7" "aHello World";
        std::string const h1 = highlightFormattedMatches(s1, "world", 'e');
        CHECK(stripFormatting(h1) == stripFormatting(s1));
        CHECK(h1.find("\xC2\xA7" "eWorld") != std::string::npos);

        // Bold + color restored after highlighted word before trailing text
        std::string const s2 = "\xC2\xA7" "b\xC2\xA7" "lHello World Test";
        std::string const h2 = highlightFormattedMatches(s2, "world", 'e');
        CHECK(stripFormatting(h2) == "Hello World Test");
        CHECK(h2.find("\xC2\xA7" "e\xC2\xA7" "lWorld\xC2\xA7" "r\xC2\xA7" "b\xC2\xA7" "l") != std::string::npos);

        // Inline § code inside match boundary is overridden while active state after match restores §c
        std::string const s3 = "\xC2\xA7" "aHel\xC2\xA7" "clo ok";
        std::string const h3 = highlightFormattedMatches(s3, "hello", 'e');
        CHECK(stripFormatting(h3) == "Hello ok");
        CHECK(h3.find("\xC2\xA7" "r\xC2\xA7" "c ok") != std::string::npos);

        // Multiple matches & no match pass-through
        std::string const s4 = "foo bar FOO";
        std::string const h4 = highlightFormattedMatches(s4, "FOO", 'e');
        CHECK(stripFormatting(h4) == "foo bar FOO");
        CHECK(h4 != s4);
        CHECK(highlightFormattedMatches(s4, "missing", 'e') == s4);
    }

    // ChatHistory::buildView filtering & highlights
    {
        ChatConfig cfg;
        cfg.compactDuplicates = false;
        cfg.width             = 320.0f;
        cfg.bottomOffset      = 40.0f;
        auto measure          = [](std::string_view sv) { return estimateTextWidth(sv, 1.0f); };

        ChatHistory hist;
        hist.pushMessage("\xC2\xA7" "a[Alice] Diamond", 10.0, cfg, 1, "\xC2\xA7" "8[12:34] \xC2\xA7" "r");
        hist.pushMessage("[Bob] Iron", 11.0, cfg, 1, "\xC2\xA7" "8[12:35] \xC2\xA7" "r");
        hist.pushMessage("[Alice] \xC2\xA7" "bEmerald", 12.0, cfg, 1, "\xC2\xA7" "8[12:36] \xC2\xA7" "r");
        hist.pushMessage("[System] ダイヤ発見", 13.0, cfg, 1, "\xC2\xA7" "8[12:37] \xC2\xA7" "r");

        auto allView = hist.buildView(640.0f, 360.0f, true, 14.0, cfg, measure, -1.0f, 1);
        CHECK(!allView.isFiltered);
        CHECK(allView.matchedEntryCount == 4);
        CHECK(allView.totalEntryCount == 4);
        CHECK(allView.lines.size() == 4);

        auto aliceView = hist.buildView(640.0f, 360.0f, true, 14.0, cfg, measure, -1.0f, 1, "alice");
        CHECK(aliceView.isFiltered);
        CHECK(aliceView.matchedEntryCount == 2);
        CHECK(aliceView.totalEntryCount == 4);
        CHECK(aliceView.lines.size() == 2);
        CHECK(aliceView.lines[0].text.find("\xC2\xA7" "eAlice") != std::string::npos);
        CHECK(stripFormatting(aliceView.lines[1].text) == "[Alice] Emerald");
        // Click hit-test on filtered view still resolves underlying ChatEntry by entryId
        float const midX = (aliceView.lines[0].rect.x0 + aliceView.lines[0].rect.x1) * 0.5f;
        float const midY = (aliceView.lines[0].rect.y0 + aliceView.lines[0].rect.y1) * 0.5f;
        auto const  hit  = ChatHistory::hitTestLine(aliceView, midX, midY);
        CHECK(hit.has_value() && *hit == 0);
        auto const* entry = hist.findById(aliceView.lines[*hit].entryId);
        CHECK(entry != nullptr && entry->plainText == "[Alice] Diamond");

        auto jpView = hist.buildView(640.0f, 360.0f, true, 14.0, cfg, measure, -1.0f, 1, "ダイヤ");
        CHECK(jpView.isFiltered && jpView.matchedEntryCount == 1 && jpView.lines.size() == 1);

        // Zero matches preserves panel width and bottom anchor for search bar placement
        auto zeroView = hist.buildView(640.0f, 360.0f, true, 14.0, cfg, measure, -1.0f, 1, "notfound");
        CHECK(zeroView.isFiltered);
        CHECK(zeroView.matchedEntryCount == 0);
        CHECK(zeroView.totalEntryCount == 4);
        CHECK(zeroView.lines.empty());
        CHECK_NEAR(zeroView.panelRect.width(), 320.0f, 0.5f);
        CHECK_NEAR(zeroView.panelRect.y1, 360.0f - cfg.bottomOffset, 0.5f);

        // Closed HUD does NOT filter
        auto closedView = hist.buildView(640.0f, 360.0f, false, 14.0, cfg, measure, -1.0f, 1, "alice");
        CHECK(!closedView.isFiltered);
        CHECK(closedView.lines.size() == 4);

        // Timestamp matching follows showTimestamps flag
        auto tsOff = hist.buildView(640.0f, 360.0f, true, 14.0, cfg, measure, -1.0f, 1, "12:35");
        CHECK(tsOff.matchedEntryCount == 0);
        cfg.showTimestamps = true;
        auto tsOn = hist.buildView(640.0f, 360.0f, true, 14.0, cfg, measure, -1.0f, 1, "12:35");
        CHECK(tsOn.matchedEntryCount == 1 && tsOn.lines.size() == 1);

        // Clearing history resets visible entries while keeping sync cursor intact
        hist.clear();
        CHECK(hist.size() == 0);
        CHECK(!hist.needsSync(0, "", 0, "", 0, 0.0f, 15.0));
    }
}

} // namespace

void testChatHeadsIndentAndResolver() {
    using namespace javachat;
    ChatConfig cfg;
    cfg.width             = 120.0f;
    cfg.openHeight        = 200.0f;
    cfg.compactDuplicates = true;
    TextMeasureFn mono    = [](std::string_view sv) -> float { return 6.0f * float(stripFormatting(sv).size()); };

    // Resolver is consulted once per newly ingested message, with sender metadata.
    ChatHistory                     h;
    int                             resolverCalls = 0;
    std::vector<GuiMessageSnapshot> snaps(2);
    snaps[0].fullText    = "<Steve> aaaa bbbb"; // 102px: fits 108px, wraps at 98px
    snaps[0].messageType = 1;
    snaps[0].username    = "Steve";
    snaps[1].fullText    = "Server notice";
    snaps[1].messageType = 4;
    HeadKeyResolver resolver = [&](GuiMessageSnapshot const& s) {
        ++resolverCalls;
        return s.username == "Steve" ? std::string("steve") : std::string{};
    };
    CHECK(h.syncFromGuiMessages(snaps, 1.0, cfg, {}, resolver) == 2);
    CHECK(resolverCalls == 2);
    CHECK(h.syncFromGuiMessages(snaps, 1.5, cfg, {}, resolver) == 0);
    CHECK(resolverCalls == 2);
    CHECK(h.entries()[0].headKey == "steve");
    CHECK(h.entries()[1].headKey.empty());

    // Without heads: no indent, no head key on lines.
    auto plain = h.buildView(640.0f, 360.0f, true, 2.0, cfg, mono);
    CHECK(!plain.lines.empty());
    for (auto const& l : plain.lines) {
        CHECK(l.textIndent == 0.0f);
        CHECK(l.headKey.empty());
    }
    size_t const plainLines = plain.lines.size();

    // With heads: only the attributed entry is indented, head key only on its first line,
    // and the narrower width makes it wrap into more lines.
    auto headed = h.buildView(640.0f, 360.0f, true, 2.0, cfg, mono, -1.0f, 0, {}, 10.0f);
    CHECK(headed.lines.size() > plainLines);
    int headLines = 0;
    for (auto const& l : headed.lines) {
        bool const steveLine = (l.entryId == h.entries()[0].id);
        CHECK(l.textIndent == (steveLine ? 10.0f : 0.0f));
        if (!l.headKey.empty()) {
            ++headLines;
            CHECK(l.isFirstLine && steveLine);
        }
    }
    CHECK(headLines == 1);

    // Closed HUD view carries the same metadata.
    auto closed = h.buildView(640.0f, 360.0f, false, 2.0, cfg, mono, -1.0f, 0, {}, 10.0f);
    bool sawHead = false;
    for (auto const& l : closed.lines) sawHead |= !l.headKey.empty();
    CHECK(sawHead);

    // Duplicate compaction never merges lines from different senders.
    ChatHistory d;
    d.pushMessage("gg", 1.0, cfg, 1, {}, "steve");
    d.pushMessage("gg", 1.1, cfg, 1, {}, "alex");
    d.pushMessage("gg", 1.2, cfg, 1, {}, "alex");
    CHECK(d.size() == 2);
    CHECK(d.entries().back().repeatCount == 2);

    // Config round-trip.
    ChatConfig off;
    off.chatHeads = false;
    ChatConfig parsed;
    CHECK(parseConfigJson(serializeConfigJson(off), parsed));
    CHECK(parsed.chatHeads == false);
    ChatConfig defaults;
    CHECK(parseConfigJson("{}", defaults));
    CHECK(defaults.chatHeads == true);
}

int main() {
    testConfigParseAndRoundTrip();
    testConfigFileSaveLoadAndCorruptBackup();
    testFormatterStripAndUrls();
    testFormatterWrapAndFormattingCarryOver();
    testHistorySyncOverlapAndCompact();
    testHistoryFadeScrollAndHitTest();
    testWrappedLineCacheInvalidation();
    testBedrockColorCodesAndCorruptUtf8();
    testJsoncCommentsTrailingCommasAndSurrogates();
    testScrollAnchorAndScrollbarDrag();
    testMultiUrlPerLineExtraction();
    testOptimizationNeedsSyncFontRevisionAndZeroFade();
    testCommandSuggestions();
    testSelectAllAndClipboardHelpers();
    testFastTypingSuggestionSyncAndKnownCommands();
    testWordDeletionUtf8();
    testSearchFilterAndHighlight();
    testChatHeadsIndentAndResolver();

    std::cout << "==== " << gPassed << " passed, " << gFailed << " failed ====\n";
    return gFailed == 0 ? 0 : 1;
}
