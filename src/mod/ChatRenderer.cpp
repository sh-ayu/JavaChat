#include "mod/ChatRenderer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

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
inline constexpr mce::Color kSuggestionSelected(255, 255, 85);
inline constexpr mce::Color kSuggestionNormal(170, 170, 170);
inline constexpr mce::Color kCaretWhite(224, 224, 224);
inline constexpr mce::Color kSelectionBlue(51, 128, 255);

RectangleArea toArea(Rect const& r, float ox, float oy) noexcept {
    RectangleArea a;
    a._x0 = r.x0 + ox;
    a._y0 = r.y0 + oy;
    a._x1 = r.x1 + ox;
    a._y1 = r.y1 + oy;
    return a;
}

Font* getFallbackDebugFont(MinecraftUIRenderContext& ctx) noexcept {
    try {
        return ctx.mDebugTextFontHandle.get().mDefaultFont.get();
    } catch (...) {
        return nullptr;
    }
}

void drawStyledText(
    MinecraftUIRenderContext& ctx,
    Font*                     font,
    RectangleArea const&      area,
    std::string_view          text,
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
            ctx.drawText(*font, area, text, color, alpha, align, measure, caret);
        } else {
            ctx.drawDebugText(area, std::string(text), color, alpha, align, measure, caret);
        }
    } catch (...) {
    }
}

} // namespace

TextMeasureFn ChatRenderer::makeMeasureFn(
    MinecraftUIRenderContext& ctx,
    FontRenderSet const&      fonts,
    float                     fontScale
) noexcept {
    FontRenderSet effective = fonts;
    if (!effective.primary) {
        effective.primary = getFallbackDebugFont(ctx);
    }
    return [effective, fontScale](std::string_view sv) -> float {
        if (sv.empty()) return 0.0f;
        Font* font = effective.selectForText(sv);
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
    FontRenderSet const&      fonts,
    bool                      chatOpen,
    std::optional<size_t>     hoveredLine,
    ToastNotice const*        toast,
    bool                      scrollbarActive
) noexcept {
    try {
        bool const hasToast = (toast && toast->alpha > 0.01f && !toast->text.empty());
        if (view.lines.empty() && !hasToast) return;

        FontRenderSet effective = fonts;
        if (!effective.primary) {
            effective.primary = getFallbackDebugFont(ctx);
        }
        float const padX = 4.0f;

        if (!view.lines.empty()) {
            // Background strips, hover highlights, and scrollbar are already bounded inside panelRect;
            // drawing them unclipped avoids Bedrock's 1px bottom/right scissor trim on panelRect.
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

            ctx.saveCurrentClippingRectangle();
            Rect textClip{
                view.panelRect.x0,
                view.panelRect.y0 - 2.0f,
                view.panelRect.x1 + 1.0f,
                view.panelRect.y1 + 2.0f
            };
            ctx.setClippingRectangle(toArea(textClip, originX, originY));

            for (auto const& line : view.lines) {
                if (line.textAlpha <= 0.001f || line.text.empty()) continue;
                // Bedrock Font::drawText adds ~1px top bearing (notably on CJK SmoothFont);
                // center against a 10px glyph+shadow box so 9px lines never protrude below panelBottom.
                float const effectiveGlyphH = 10.0f * cfg.fontScale;
                float const textYOffset     = std::floor((line.rect.height() - effectiveGlyphH) * 0.5f * 2.0f) * 0.5f;
                Rect        textRect{
                    line.rect.x0 + padX,
                    line.rect.y0 + textYOffset,
                    line.rect.x1 - 2.0f,
                    line.rect.y1 + 2.0f
                };
                Font* lineFont = effective.selectForText(line.text);
                drawStyledText(
                    ctx,
                    lineFont,
                    toArea(textRect, originX, originY),
                    line.text,
                    kWhite,
                    line.textAlpha,
                    cfg.fontScale,
                    cfg.textShadow
                );
            }

            ctx.flushText(0.016f, std::nullopt);
            ctx.restoreSavedClippingRectangle();
        }

        // Minimal feedback toast only after copy/URL click.
        if (hasToast) {
            float baseTop = view.lines.empty() ? 160.0f : view.panelRect.y0;
            float toastW  = std::max(64.0f, estimateTextWidth(toast->text, 0.9f) + 8.0f);
            float toastH  = 11.0f;
            float x0      = cfg.leftOffset;
            float y0      = std::max(4.0f, baseTop - toastH - 2.0f);
            Rect  box{x0, y0, x0 + toastW, y0 + toastH};
            ctx.fillRectangle(toArea(box, originX, originY), kBlack, 0.50f * toast->alpha);
            Rect textBox{x0 + 4.0f, y0 + 1.0f, x0 + toastW - 2.0f, y0 + toastH};
            Font* toastFont = effective.fallback ? effective.fallback : effective.primary;
            drawStyledText(ctx, toastFont, toArea(textBox, originX, originY), toast->text, kWhite, toast->alpha, 0.9f, true);
            ctx.flushText(0.016f, std::nullopt);
        }
    } catch (...) {
    }
}

void ChatRenderer::renderSuggestions(
    MinecraftUIRenderContext&    ctx,
    float                        originX,
    float                        originY,
    SuggestionPopupLayout const& layout,
    ChatConfig const&            cfg,
    FontRenderSet const&         fonts,
    Rect const*                  midCaretRect,
    Rect const*                  selectionRect
) noexcept {
    try {
        bool const hasSelection =
            (selectionRect != nullptr && selectionRect->width() > 0.0f && selectionRect->height() > 0.0f);
        if (hasSelection) {
            ctx.fillRectangle(toArea(*selectionRect, originX, originY), kSelectionBlue, 0.42f);
        }

        bool const hasMidCaret =
            (midCaretRect != nullptr && midCaretRect->width() > 0.0f && midCaretRect->height() > 0.0f);
        if (hasMidCaret) {
            ctx.fillRectangle(toArea(*midCaretRect, originX, originY), kCaretWhite, 0.95f);
        }

        if (!layout.visible || layout.rows.empty()) return;

        FontRenderSet effective = fonts;
        if (!effective.primary) {
            effective.primary = getFallbackDebugFont(ctx);
        }

        ctx.fillRectangle(toArea(layout.popupRect, originX, originY), kBlack, kPopupBgAlpha);

        // Java SuggestionsList dotted scroll indicators along top/bottom borders when scrollable.
        auto drawDottedEdge = [&](float edgeY) {
            for (float x = layout.popupRect.x0; x + 0.5f < layout.popupRect.x1; x += 2.0f) {
                Rect dot{x, edgeY, std::min(x + 1.0f, layout.popupRect.x1), edgeY + 1.0f};
                ctx.fillRectangle(toArea(dot, originX, originY), kWhite, 0.65f);
            }
        };
        if (layout.hasMoreAbove) {
            drawDottedEdge(layout.popupRect.y0);
        }
        if (layout.hasMoreBelow) {
            drawDottedEdge(std::max(layout.popupRect.y0, layout.popupRect.y1 - 1.0f));
        }

        float const rowScale = std::clamp(layout.rows.front().rect.height() / kSuggestionRowHeight, 0.5f, 2.0f);
        float const glyphH   = 8.0f * rowScale;

        ctx.saveCurrentClippingRectangle();
        Rect popupClip{
            layout.popupRect.x0,
            layout.popupRect.y0 - 1.0f,
            layout.popupRect.x1 + 1.0f,
            layout.popupRect.y1 + 1.0f
        };
        ctx.setClippingRectangle(toArea(popupClip, originX, originY));

        for (auto const& row : layout.rows) {
            if (row.text.empty()) continue;
            mce::Color const& color = row.selected ? kSuggestionSelected : kSuggestionNormal;
            float const       yOff  = std::max(1.0f, std::round((row.rect.height() - glyphH) * 0.5f));
            Rect              textRect{
                row.rect.x0 + kPopupPadLeft,
                row.rect.y0 + yOff,
                row.rect.x1,
                row.rect.y1
            };
            Font* rowFont = effective.selectForText(row.text);
            drawStyledText(
                ctx,
                rowFont,
                toArea(textRect, originX, originY),
                row.text,
                color,
                1.0f,
                rowScale,
                cfg.textShadow
            );
        }

        ctx.flushText(0.016f, std::nullopt);
        ctx.restoreSavedClippingRectangle();
    } catch (...) {
    }
}

} // namespace javachat
