#include "mod/ChatCommands.h"

#include <string>

#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "mod/JavaChatMod.h"

namespace javachat {

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
            out.success("JavaChat: disabled (overlay hidden; rejoin world or restart to restore vanilla chat UI)");
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
}

} // namespace javachat
