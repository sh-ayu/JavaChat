#include "mod/ChatCommands.h"

#include <cctype>
#include <string>

#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/EnumName.h>
#include <ll/api/command/SoftEnum.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "mod/ChatFontManager.h"
#include "mod/JavaChatMod.h"

namespace javachat {

enum class JchatFontSpec : int {};

struct FontSpecParam {
    ll::command::SoftEnum<JchatFontSpec> value;
};

struct FloatValueParam {
    float value;
};

struct BoolValueParam {
    bool value;
};

struct HeightParam {
    float closed;
    float open;
};

struct PositionParam {
    float left;
    float bottom;
};

struct FadeParam {
    float delay;
    float duration;
};

void registerChatCommands(std::weak_ptr<ll::mod::Mod> weakMod) {
    auto& registrar = ll::command::CommandRegistrar::getClientInstance();

    auto& cmd = registrar.getOrCreateCommand(
        "jchat",
        "JavaChat: Java-style chat overlay controls",
        CommandPermissionLevel::Any,
        CommandFlagValue::NotCheat,
        weakMod
    );

    cmd.overload(weakMod).execute([](CommandOrigin const&, CommandOutput& out) {
        out.success("{}", JavaChatMod::instance().describeHelp());
    });

    cmd.overload(weakMod).text("help").execute([](CommandOrigin const&, CommandOutput& out) {
        out.success("{}", JavaChatMod::instance().describeHelp());
    });

    cmd.overload(weakMod).text("status").execute([](CommandOrigin const&, CommandOutput& out) {
        out.success("{}", JavaChatMod::instance().describeStatus());
    });

    cmd.overload(weakMod).text("toggle").execute([](CommandOrigin const&, CommandOutput& out) {
        bool nowEnabled = JavaChatMod::instance().toggleEnabled();
        if (nowEnabled) {
            out.success("JavaChat: enabled");
        } else {
            out.success("JavaChat: disabled (vanilla chat restored)");
        }
    });

    cmd.overload(weakMod).text("clear").execute([](CommandOrigin const&, CommandOutput& out) {
        JavaChatMod::instance().clearHistory();
        out.success("JavaChat: chat history cleared");
    });

    cmd.overload(weakMod).text("reload").execute([](CommandOrigin const&, CommandOutput& out) {
        std::string msg;
        if (JavaChatMod::instance().reloadConfig(msg)) out.success("{}", msg);
        else out.error("{}", msg);
    });

    cmd.overload(weakMod).text("reset").execute([](CommandOrigin const&, CommandOutput& out) {
        JavaChatMod::instance().resetConfig();
        out.success("JavaChat: config reset to defaults");
    });

    cmd.overload<FloatValueParam>(weakMod).text("width").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FloatValueParam const& p) {
            float v = JavaChatMod::instance().setWidth(p.value);
            out.success("JavaChat: width = {:.1f}", v);
        }
    );

    cmd.overload<HeightParam>(weakMod).text("height").required("closed").required("open").execute(
        [](CommandOrigin const&, CommandOutput& out, HeightParam const& p) {
            auto [ch, oh] = JavaChatMod::instance().setHeight(p.closed, p.open);
            out.success("JavaChat: closedHeight = {:.1f}, openHeight = {:.1f}", ch, oh);
        }
    );

    cmd.overload<PositionParam>(weakMod).text("position").required("left").required("bottom").execute(
        [](CommandOrigin const&, CommandOutput& out, PositionParam const& p) {
            auto [lo, bo] = JavaChatMod::instance().setPosition(p.left, p.bottom);
            out.success("JavaChat: leftOffset = {:.1f}, bottomOffset = {:.1f}", lo, bo);
        }
    );

    cmd.overload<FloatValueParam>(weakMod).text("scale").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FloatValueParam const& p) {
            float v = JavaChatMod::instance().setFontScale(p.value);
            out.success("JavaChat: fontScale = {:.2f}", v);
        }
    );

    cmd.overload<FloatValueParam>(weakMod).text("spacing").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FloatValueParam const& p) {
            float v = JavaChatMod::instance().setLineSpacing(p.value);
            out.success("JavaChat: lineSpacing = {:.2f}", v);
        }
    );

    cmd.overload<FloatValueParam>(weakMod).text("opacity").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FloatValueParam const& p) {
            float v = JavaChatMod::instance().setBackgroundOpacity(p.value);
            out.success("JavaChat: backgroundOpacity = {:.2f}", v);
        }
    );

    cmd.overload<FloatValueParam>(weakMod).text("textopacity").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FloatValueParam const& p) {
            float v = JavaChatMod::instance().setTextOpacity(p.value);
            out.success("JavaChat: textOpacity = {:.2f}", v);
        }
    );

    cmd.overload<FloatValueParam>(weakMod).text("fade").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FloatValueParam const& p) {
            float v = JavaChatMod::instance().setFadeDelay(p.value);
            out.success("JavaChat: fadeDelaySec = {:.1f}", v);
        }
    );

    cmd.overload<FadeParam>(weakMod).text("fade").required("delay").required("duration").execute(
        [](CommandOrigin const&, CommandOutput& out, FadeParam const& p) {
            auto [d, dur] = JavaChatMod::instance().setFade(p.delay, p.duration);
            out.success("JavaChat: fadeDelaySec = {:.1f}, fadeDurationSec = {:.2f}", d, dur);
        }
    );

    cmd.overload(weakMod).text("font").execute([](CommandOrigin const&, CommandOutput& out) {
        out.success("{}", JavaChatMod::instance().describeFonts());
    });

    std::string const fontEnumName = ll::command::enum_name_v<JchatFontSpec>;
    auto              fontVals     = ChatFontManager::instance().completionCandidates();
    if (registrar.hasSoftEnum(fontEnumName)) registrar.setSoftEnumValues(fontEnumName, fontVals);
    else registrar.tryRegisterSoftEnum(fontEnumName, fontVals);

    cmd.overload<FontSpecParam>(weakMod).text("font").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, FontSpecParam const& p) {
            std::string raw(p.value);
            while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.erase(raw.begin());
            while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
            std::string lo = raw;
            for (char& c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lo.empty() || lo == "list") {
                out.success("{}", JavaChatMod::instance().describeFonts());
                return;
            }
            std::string msg;
            if (JavaChatMod::instance().setFont(raw, msg)) out.success("{}", msg);
            else out.error("{}", msg);
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("autoclose").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setAutoCloseOnSend(p.value);
            out.success("JavaChat: autoCloseOnSend = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("animations").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setDisableChatAnimations(!p.value);
            out.success("JavaChat: chat animations = {}", p.value ? "on" : "off (Java Edition style)");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("keephud").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setKeepHudVisibleInChat(p.value);
            out.success("JavaChat: keepHudVisibleInChat = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("shadow").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setTextShadow(p.value);
            out.success("JavaChat: textShadow = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("compact").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setCompactDuplicates(p.value);
            out.success("JavaChat: compactDuplicates = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("timestamps").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setShowTimestamps(p.value);
            out.success("JavaChat: showTimestamps = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("clickcopy").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setClickToCopy(p.value);
            out.success("JavaChat: clickToCopy = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("clickurl").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setClickToOpenUrl(p.value);
            out.success("JavaChat: clickToOpenUrl = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("clearonswitch").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setClearHistoryOnWorldSwitch(p.value);
            out.success("JavaChat: clearHistoryOnWorldSwitch = {}", p.value ? "true" : "false");
        }
    );

    cmd.overload<BoolValueParam>(weakMod).text("heads").required("value").execute(
        [](CommandOrigin const&, CommandOutput& out, BoolValueParam const& p) {
            JavaChatMod::instance().setChatHeads(p.value);
            out.success(
                "JavaChat: chatHeads = {}{}",
                p.value ? "true" : "false",
                p.value ? " (requires the ChatHeads mod: lip install github.com/sh-ayu/ChatHeads)" : ""
            );
        }
    );
}

} // namespace javachat
