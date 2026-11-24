#pragma once

#include "core/ChatConfig.h"
#include "core/ChatHistory.h"

#include <optional>
#include <string>

class MinecraftUIRenderContext;

namespace javachat {

struct ToastNotice {
    std::string text;
    float       alpha = 0.0f;
};

class ChatRenderer {
public:
    // Build a TextMeasureFn backed by the live Minecraft Font when available,
    // falling back to estimateTextWidth() otherwise.
    static TextMeasureFn makeMeasureFn(MinecraftUIRenderContext& ctx, float fontScale) noexcept;

    // Render the Java-style chat panel, hover highlight, scrollbar, and optional toast.
    static void render(
        MinecraftUIRenderContext& ctx,
        float                     originX,
        float                     originY,
        ChatViewLayout const&     view,
        ChatConfig const&         cfg,
        bool                      chatOpen,
        std::optional<size_t>     hoveredLine,
        ToastNotice const*        toast,
        bool                      scrollbarActive = false
    ) noexcept;
};

} // namespace javachat
