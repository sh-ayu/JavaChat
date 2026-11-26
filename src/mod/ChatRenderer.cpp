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

class ClippingRectGuard {
public:
    explicit ClippingRectGuard(MinecraftUIRenderContext& ctx) : mCtx(ctx) {
        mCtx.saveCurrentClippingRectangle();
    }
    ~ClippingRectGuard() noexcept {
        try {
            mCtx.restoreSavedClippingRectangle();
        } catch (...) {
        }
    }
    ClippingRectGuard(ClippingRectGuard const&)            = delete;
    ClippingRectGuard& operator=(ClippingRectGuard const&) = delete;

private:
    MinecraftUIRenderContext& mCtx;
};

std::shared_ptr<Font> getFallbackDebugFont(MinecraftUIRenderContext& ctx) noexcept {
    try {
        return ctx.mDebugTextFontHandle.get().mDefaultFont;
    } catch (...) {
        return {};
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

SearchBarLayout ChatRenderer::computeSearchBarLayout(
    ChatViewLayout const& view,
    float                 screenW,
    float                 screenH,
    ChatConfig const&     cfg
) noexcept {
    SearchBarLayout layout;
    if (!(screenW > 0.0f && screenH > 0.0f)) return layout;

    float const panelLeft  = (view.panelRect.width() > 1.0f) ? view.panelRect.x0 : cfg.leftOffset;
    float const panelRight = (view.panelRect.width() > 1.0f)
                               ? view.panelRect.x1
                               : (panelLeft + std::min(cfg.width, std::max(80.0f, screenW - cfg.leftOffset - 4.0f)));
    if (panelRight <= panelLeft + 16.0f) return layout;

    float const barH       = 12.0f;
    float const baseBottom = (view.panelRect.y1 > 1.0f) ? view.panelRect.y1 : (screenH - cfg.bottomOffset);
    float const barY1      = view.lines.empty() ? baseBottom : (view.panelRect.y0 - 1.0f);
    float const barY0      = std::max(4.0f, barY1 - barH);
    if (barY0 + barH > screenH) return layout;

    layout.visible      = true;
    layout.barRect      = Rect{panelLeft, barY0, panelRight, barY0 + barH};
    layout.closeBtnRect = Rect{std::max(panelLeft, panelRight - 14.0f), barY0, panelRight, barY0 + barH};
    return layout;
}

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
        std::shared_ptr<Font> font = effective.selectForText(sv);
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
    bool                      scrollbarActive,
    SearchBarNotice const*    searchBar,
    SearchBarLayout const*    searchLayout,
    bool                      searchCloseHovered
) noexcept {
    try {
        bool const hasToast = (toast && toast->alpha > 0.01f && !toast->text.empty());
        bool const hasSearchBar =
            (chatOpen && searchBar && searchBar->active && searchLayout && searchLayout->visible);
        if (view.lines.empty() && !hasToast && !hasSearchBar) return;

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

            {
                ClippingRectGuard clipGuard(ctx);
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
                    std::shared_ptr<Font> lineFont = effective.selectForText(line.text);
                    drawStyledText(
                        ctx,
                        lineFont.get(),
                        toArea(textRect, originX, originY),
                        line.text,
                        kWhite,
                        line.textAlpha,
                        cfg.fontScale,
                        cfg.textShadow
                    );
                }

                ctx.flushText(0.016f, std::nullopt);
            }
        }

        // Slim Java-style search header bar above the filtered chat panel.
        if (hasSearchBar) {
            ctx.fillRectangle(toArea(searchLayout->barRect, originX, originY), kBlack, 0.72f);
            Rect accent{
                searchLayout->barRect.x0,
                searchLayout->barRect.y0,
                searchLayout->barRect.x0 + 2.0f,
                searchLayout->barRect.y1
            };
            ctx.fillRectangle(toArea(accent, originX, originY), kUrlUnderline, 0.90f);
            if (searchCloseHovered) {
                ctx.fillRectangle(toArea(searchLayout->closeBtnRect, originX, originY), kWhite, 0.14f);
            }

            std::string label;
            if (searchBar->query.empty()) {
                label = "\xC2\xA7" "bSearch\xC2\xA7" "7: type to filter ("
                      + std::to_string(searchBar->totalCount) + " msgs)";
            } else {
                label = "\xC2\xA7" "bSearch\xC2\xA7" "f: " + searchBar->query
                      + (searchBar->matchedCount == 0 ? " \xC2\xA7" "c(" : " \xC2\xA7" "7(")
                      + std::to_string(searchBar->matchedCount) + "/"
                      + std::to_string(searchBar->totalCount) + ")";
            }

            std::shared_ptr<Font> uiFont = effective.fallback ? effective.fallback : effective.primary;
            {
                ClippingRectGuard clipGuard(ctx);
                Rect labelClip{
                    searchLayout->barRect.x0 + 4.0f,
                    searchLayout->barRect.y0,
                    std::max(searchLayout->barRect.x0 + 4.0f, searchLayout->closeBtnRect.x0 - 2.0f),
                    searchLayout->barRect.y1
                };
                ctx.setClippingRectangle(toArea(labelClip, originX, originY));
                Rect labelRect{labelClip.x0, searchLayout->barRect.y0 + 2.0f, labelClip.x1, searchLayout->barRect.y1};
                drawStyledText(ctx, uiFont.get(), toArea(labelRect, originX, originY), label, kWhite, 1.0f, 0.9f, true);
                ctx.flushText(0.016f, std::nullopt);
            }

            Rect closeTextRect{
                searchLayout->closeBtnRect.x0,
                searchLayout->barRect.y0 + 2.0f,
                searchLayout->closeBtnRect.x1 - 2.0f,
                searchLayout->barRect.y1
            };
            drawStyledText(
                ctx,
                uiFont.get(),
                toArea(closeTextRect, originX, originY),
                "[x]",
                searchCloseHovered ? kUrlUnderline : kSuggestionNormal,
                1.0f,
                0.9f,
                true,
                ::ui::TextAlignment::Right
            );
            ctx.flushText(0.016f, std::nullopt);
        }

        // Minimal feedback toast only after copy/URL click/clear.
        if (hasToast) {
            float baseTop = hasSearchBar
                              ? searchLayout->barRect.y0
                              : (view.lines.empty()
                                     ? std::max(32.0f, (view.panelRect.y1 > 1.0f ? view.panelRect.y1 : 160.0f))
                                     : view.panelRect.y0);
            float toastW  = std::max(64.0f, estimateTextWidth(toast->text, 0.9f) + 8.0f);
            float toastH  = 11.0f;
            float x0      = cfg.leftOffset;
            float y0      = std::max(4.0f, baseTop - toastH - 2.0f);
            Rect  box{x0, y0, x0 + toastW, y0 + toastH};
            ctx.fillRectangle(toArea(box, originX, originY), kBlack, 0.50f * toast->alpha);
            Rect textBox{x0 + 4.0f, y0 + 1.0f, x0 + toastW - 2.0f, y0 + toastH};
            std::shared_ptr<Font> toastFont = effective.fallback ? effective.fallback : effective.primary;
            drawStyledText(ctx, toastFont.get(), toArea(textBox, originX, originY), toast->text, kWhite, toast->alpha, 0.9f, true);
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
    Rect const*                  selectionRect,
    bool                         searchModeActive,
    float                        screenH
) noexcept {
    try {
        if (searchModeActive && screenH > 16.0f) {
            Rect accent{2.0f, screenH - 14.0f, 4.0f, screenH - 2.0f};
            ctx.fillRectangle(toArea(accent, originX, originY), kUrlUnderline, 0.90f);
        }

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

        {
            ClippingRectGuard clipGuard(ctx);
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
                std::shared_ptr<Font> rowFont = effective.selectForText(row.text);
                drawStyledText(
                    ctx,
                    rowFont.get(),
                    toArea(textRect, originX, originY),
                    row.text,
                    color,
                    1.0f,
                    rowScale,
                    cfg.textShadow
                );
            }

            ctx.flushText(0.016f, std::nullopt);
        }
    } catch (...) {
    }
}

} // namespace javachat
