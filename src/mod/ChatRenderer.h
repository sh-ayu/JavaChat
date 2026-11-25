#pragma once

#include "core/ChatConfig.h"
#include "core/ChatHistory.h"
#include "core/CommandSuggestions.h"
#include "mod/ChatFontManager.h"

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
    // Build a TextMeasureFn backed by the live Minecraft FontSet when available,
    // falling back to estimateTextWidth() otherwise.
    static TextMeasureFn makeMeasureFn(
        MinecraftUIRenderContext& ctx,
        FontRenderSet const&      fonts,
        float                     fontScale
    ) noexcept;

    // Render the Java-style chat panel, hover highlight, scrollbar, and optional toast.
    static void render(
        MinecraftUIRenderContext& ctx,
        float                     originX,
        float                     originY,
        ChatViewLayout const&     view,
        ChatConfig const&         cfg,
        FontRenderSet const&      fonts,
        bool                      chatOpen,
        std::optional<size_t>     hoveredLine,
        ToastNotice const*        toast,
        bool                      scrollbarActive = false
    ) noexcept;

    // Render authentic Java Edition command suggestions popup (and optional mid-string caret bar /
    // Ctrl+A selection highlight) in front of chat history during AfterUIRenderEvent.
    static void renderSuggestions(
        MinecraftUIRenderContext&    ctx,
        float                        originX,
        float                        originY,
        SuggestionPopupLayout const& layout,
        ChatConfig const&            cfg,
        FontRenderSet const&         fonts,
        Rect const*                  midCaretRect  = nullptr,
        Rect const*                  selectionRect = nullptr
    ) noexcept;
};

} // namespace javachat
