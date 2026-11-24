#include "mod/ChatRenderer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <mc/client/gui/CaretMeasureData.h>
#include <mc/client/gui/Font.h>
#include <mc/client/gui/FontHandle.h>
#include <mc/client/gui/TextAlignment.h>
#include <mc/client/gui/TextMeasureData.h>
#include <mc/client/renderer/screen/MinecraftUIRenderContext.h>
#include <mc/deps/core/math/Color.h>
#include <mc/deps/input/RectangleArea.h>

namespace javachat {
namespace {

// Authentic vanilla Java Edition palette: pure black translucent strips + white shadowed text.
inline constexpr mce::Color kBlack(0, 0, 0);
inline constexpr mce::Color kWhite(255, 255, 255);
inline constexpr mce::Color kScrollbarThumb(204, 204, 204);
inline constexpr mce::Color kUrlUnderline(85, 255, 255);

RectangleArea toArea(Rect const& r, float ox, float oy) noexcept {
    RectangleArea a;
    a._x0 = r.x0 + ox;
    a._y0 = r.y0 + oy;
    a._x1 = r.x1 + ox;
    a._y1 = r.y1 + oy;
    return a;
}

Font* getLiveFont(MinecraftUIRenderContext& ctx) noexcept {
    try {
        auto const& handle = ctx.mDebugTextFontHandle.get();
        return handle.mDefaultFont.get();
    } catch (...) {
        return nullptr;
    }
}

void drawStyledText(
    MinecraftUIRenderContext& ctx,
    Font*                     font,
    RectangleArea const&      area,
    std::string               text,
    mce::Color const&         color,
    float                     alpha,
    float                     fontScale,
    bool                      shadow,
    ::ui::TextAlignment       align = ::ui::TextAlignment::Left
) noexcept {
    try {
        TextMeasureData  measure{fontScale, 0.0f, shadow, false, false, align};
        CaretMeasureData caret{0, false};
        if (font) {
            ctx.drawText(*font, area, std::move(text), color, alpha, align, measure, caret);
        } else {
            ctx.drawDebugText(area, std::move(text), color, alpha, align, measure, caret);
        }
    } catch (...) {
    }
}

} // namespace

TextMeasureFn ChatRenderer::makeMeasureFn(MinecraftUIRenderContext& ctx, float fontScale) noexcept {
    Font* font = getLiveFont(ctx);
    return [font, fontScale](std::string_view sv) -> float {
        if (sv.empty()) return 0.0f;
        if (font) {
            try {
                int px = font->getLineLength(sv, fontScale, false);
                if (px > 0) return static_cast<float>(px);
            } catch (...) {
            }
        }
        return estimateTextWidth(sv, fontScale);
    };
}

void ChatRenderer::render(
    MinecraftUIRenderContext& ctx,
    float                     originX,
    float                     originY,
    ChatViewLayout const&     view,
    ChatConfig const&         cfg,
    bool                      chatOpen,
    std::optional<size_t>     hoveredLine,
    ToastNotice const*        toast,
    bool                      scrollbarActive
) noexcept {
    try {
        Font* const font = getLiveFont(ctx);
        float const padX = 4.0f;

        if (!view.lines.empty()) {
            ctx.saveCurrentClippingRectangle();
            ctx.setClippingRectangle(toArea(view.panelRect, originX, originY));

            for (size_t i = 0; i < view.lines.size(); ++i) {
                auto const& line = view.lines[i];
                if (line.textAlpha <= 0.001f && line.bgAlpha <= 0.001f) continue;

                // Vanilla Java style: plain translucent black strip per message line, no borders.
                if (line.bgAlpha > 0.001f) {
                    Rect strip{view.panelRect.x0, line.rect.y0, view.panelRect.x1, line.rect.y1};
                    ctx.fillRectangle(toArea(strip, originX, originY), kBlack, line.bgAlpha);
                }

                // Subtle hover highlight while chat is open.
                bool isHovered = chatOpen && hoveredLine.has_value() && *hoveredLine == i;
                if (isHovered) {
                    ctx.fillRectangle(toArea(line.rect, originX, originY), kWhite, 0.08f);
                    if (line.hasUrl) {
                        Rect under{line.rect.x0 + padX, line.rect.y1 - 1.0f, line.rect.x1 - 2.0f, line.rect.y1};
                        ctx.fillRectangle(toArea(under, originX, originY), kUrlUnderline, 0.65f);
                    }
                }

                if (line.textAlpha > 0.001f && !line.text.empty()) {
                    float glyphH      = 8.5f * cfg.fontScale;
                    float textYOffset = std::max(0.5f, std::floor((line.rect.height() - glyphH) * 0.5f));
                    Rect  textRect{
                        line.rect.x0 + padX,
                        line.rect.y0 + textYOffset,
                        line.rect.x1 - 2.0f,
                        line.rect.y1
                    };
                    drawStyledText(
                        ctx,
                        font,
                        toArea(textRect, originX, originY),
                        line.text,
                        kWhite,
                        line.textAlpha,
                        cfg.fontScale,
                        cfg.textShadow
                    );
                }
            }

            // Classic Java-style slim gray scrollbar when scrolled/overflowed.
            if (view.showScrollbar) {
                ctx.fillRectangle(toArea(view.scrollbarTrackRect, originX, originY), kBlack, 0.25f);
                ctx.fillRectangle(
                    toArea(view.scrollbarThumbRect, originX, originY),
                    kScrollbarThumb,
                    scrollbarActive ? 0.85f : 0.60f
                );
            }

            ctx.restoreSavedClippingRectangle();
        }

        // Minimal feedback toast only after copy/URL click.
        if (toast && toast->alpha > 0.01f && !toast->text.empty()) {
            float baseTop = view.lines.empty() ? 160.0f : view.panelRect.y0;
            float toastW  = std::max(64.0f, estimateTextWidth(toast->text, 0.9f) + 8.0f);
            float toastH  = 11.0f;
            float x0      = cfg.leftOffset;
            float y0      = std::max(4.0f, baseTop - toastH - 2.0f);
            Rect  box{x0, y0, x0 + toastW, y0 + toastH};
            ctx.fillRectangle(toArea(box, originX, originY), kBlack, 0.50f * toast->alpha);
            Rect textBox{x0 + 4.0f, y0 + 1.0f, x0 + toastW - 2.0f, y0 + toastH};
            drawStyledText(ctx, font, toArea(textBox, originX, originY), toast->text, kWhite, toast->alpha, 0.9f, true);
        }

        ctx.flushText(0.016f, std::nullopt);
    } catch (...) {
    }
}

} // namespace javachat
