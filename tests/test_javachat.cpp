#include "core/ChatConfig.h"
#include "core/ChatFormatter.h"
#include "core/ChatHistory.h"

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
    cfg.width             = 280.0f;
    cfg.closedHeight      = 80.0f;
    cfg.openHeight        = 200.0f;
    cfg.backgroundOpacity = 0.45f;
    cfg.fontScale         = 1.15f;
    cfg.showTimestamps    = true;
    cfg.compactDuplicates = false;
    cfg.maxHistoryMessages = 750;

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
    CHECK(parsed.showTimestamps == true);
    CHECK(parsed.compactDuplicates == false);
    CHECK(parsed.maxHistoryMessages == 750);

    // Clamping & unknown keys & type mismatch handling
    ChatConfig clamped;
    warn.clear();
    CHECK(parseConfigJson(
        R"({"width": 9999, "fontScale": -5, "backgroundOpacity": 2.5, "enabled": "bad", "unknown": [1,2,3]})",
        clamped,
        &warn
    ));
    CHECK_NEAR(clamped.width, 640.0f, 0.01f);
    CHECK_NEAR(clamped.fontScale, 0.5f, 0.01f);
    CHECK_NEAR(clamped.backgroundOpacity, 1.0f, 0.01f);
    CHECK(clamped.enabled == true); // fallback default
    CHECK(!warn.empty());

    // Malformed JSON rejected
    ChatConfig bad;
    CHECK(!parseConfigJson("{not valid json", bad, &warn));
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
}

} // namespace

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

    std::cout << "==== " << gPassed << " passed, " << gFailed << " failed ====\n";
    return gFailed == 0 ? 0 : 1;
}
