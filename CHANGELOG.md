# Changelog

All notable changes to **JavaChat** will be documented in this file.

## [1.1.1] - 2026-11-27

- Fix `/jchat toggle` so the chat input and HUD return to the default Bedrock UI when JavaChat is disabled, without restarting or rejoining.
- Add in-game screenshots of both chat layouts and command autocomplete to the English and Japanese READMEs.

## [1.1.0] - 2026-11-26

Authentic Java Edition chat UI overhaul, Brigadier-style command suggestions & syntax highlighting, `Ctrl+A` input editing, frame-time optimizations, and custom/built-in font support.

### Features & Parity
- **Modern `lip` / LeviMC Mods catalog support**: Added [`lip.toml`](lip.toml) (format 4) alongside legacy [`tooth.json`](tooth.json) for automated LeviMC Mods discovery and `lip` v4+ installation.
- **Authentic Java Edition chat & input bar layout**:
  - Updated default `lineSpacing` to `0.0` (crisp 9px/line spacing) and `backgroundOpacity` to `0.50`, with config v3 auto-migration for existing v1/v2 defaults.
  - Shrunk `chat_screen.json` to an authentic **12px Java-style bottom input bar** (`offset: [2, -2]`) with fixed `g` descender clipping and `bottomOffset: 40.0` chat alignment so chat history never jumps when opening chat or showing command suggestions.
- **Java Edition command suggestions & syntax coloring**:
  - Replaced Bedrock's full-width autocomplete panel with a compact Java-style token-only popup (`#FFFF55` yellow selected, `#AAAAAA` light gray, aligned above the active token, supporting `↑`/`↓`, `Tab`/`Shift+Tab` cycling, mouse hover/click, and wheel scrolling).
  - Hooked `TextComponent::$render` (`MCAPI`) to render Java Brigadier syntax colors (gray valid command names, red unknown/incomplete command names, and Java's 5-color argument cycle `§b` / `§e` / `§a` / `§d` / `§6`), dark-gray inline ghost completion, and `_` / `|` carets.
  - Added instant client-side stale-list filtering, known-command caching (cleared on world switch), and 1-frame lag fallback rendering to prevent flickering while typing commands quickly.
- **Input editing improvements (`Ctrl+A` & Tab/click caret sync)**:
  - Synchronized `AppPlatform::setTextBoxBackend` and `AppPlatform::setCaretPosition(-1)` after Tab and click completion so the cursor immediately jumps to the end of the completed token.
  - Added full `Ctrl+A` select-all workflow in the chat input bar (blue selection highlight, `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Backspace`/`Delete`, direct typing & IME commit replacement, arrow/click deselection).
- **Auto-close on send (Java Edition parity)**: Sending a chat message or command automatically closes the chat screen without interfering with IME composition `Enter` (`autoCloseOnSend`, toggleable via `/jchat autoclose <true|false>`).
- **Instant open/close with zero screen animations**: Removed screen push/pop slide and fade transitions in both `chat_screen.json` and `UIScene::$onScreenEntrance` / `UIScene::$onScreenExit` (`disableChatAnimations`, toggleable via `/jchat animations <true|false>`). Opening chat no longer fades out the HUD.
- **Keep HUD & third-party HUD mods visible in chat**: Hooked `UIScene::ignoreAsTop` for `chat_screen` so `hud_screen` (and vanilla HUD / other HUD mods' `AfterUIRenderEvent` overlays) stays rendered behind the open chat screen (`keepHudVisibleInChat`, default `true`, toggleable via `/jchat keephud <true|false>`).
- **World/server switch history management**: Added `clearHistoryOnWorldSwitch` (default `true`, toggleable via `/jchat clearonswitch <true|false>`) to automatically clear buffered history when switching worlds or servers.
- **Instant HUD message vanish option**: `fadeDurationSec` now supports `0.0` (e.g. `/jchat fade 9 0`) to hide expired HUD messages immediately without a fade curve.
- **Font customization (overlay + input bar + autocomplete)**:
  - Instant live switching among Bedrock built-in fonts: `default`, `smooth`, `unicode`, `MinecraftTen`, `rune`, `SmoothSmall` (`/jchat font <name>`).
  - Custom `.ttf` file support via path, `<configDir>/fonts/`, or Windows font filename (`/jchat font <path_or_filename>`), automatically staged into `JavaChatUI/font/` with `font_metadata.json` (applies after game restart, with automatic per-line `default` fallback when a glyph such as CJK is missing in the `.ttf`).

### Fixes & Performance Improvements
- Fixed CJK/descender vertical clipping at the bottom of the chat history panel by expanding text scissor margins and centering glyphs accurately against a 10px line height box.
- Fixed Bedrock `§7§f` formatting wrappers leaking into suggestion tokens and ghost previews.
- Added O(1) `ChatHistory::needsSync` fast-path to skip per-frame `GuiMessage` copying and timestamp formatting on unchanged frames.
- Skip layout calculation, font measurement, and `flushText` completely on closed HUD frames when no message or toast is visible.
- Replaced full-history string copying in open-chat `ChatHistory::buildView` with lightweight `EntrySpan` indexing (allocating rendered lines only for the visible viewport) and cached per-line URL extraction.
- Switched `ChatRenderer` to `MinecraftUIRenderContext::drawText` `std::string_view` overload.

## [1.0.0] - 2026-11-24

Initial public release for Minecraft Bedrock **1.26.60.5** / LeviLamina **26.60.8** (Windows x64 client).

### Features
- **Lag-free hybrid chat architecture**:
  - Minimal `JavaChatUI` resource pack replaces Bedrock's heavy `chat_screen.json` (`messages_scrolling_panel`, `chat_header`, `host_main_panel`, `popup_factory`, and server sidebars) with a lightweight 14px Java-style bottom input box and clipped command autocomplete popup.
  - Native C++ renderer draws chat history via `MinecraftUIRenderContext` with per-entry line-wrap caching (`Font::getLineLength`), preserving native IME input, clipboard paste, Tab completion, and sent-message history.
- **Authentic Java Edition styling & behavior**:
  - Bottom-left chat overlay with translucent pure-black message strips, drop-shadowed text, and 9s display + 1s fade-out curve.
  - Consistent vertical alignment (`bottomOffset: 40.0`) across both open and closed chat states (no vertical jump when opening/closing).
  - Transparent chat backdrop (no full-screen dimming when opening chat).
  - Single `Esc` press closes the chat screen cleanly without black-screen issues.
  - Automatic collision avoidance with tall command autocomplete popups (chat history slides above active suggestions).
- **Enhanced history & interactions**:
  - Configurable history buffer (default `500`, up to `5000` messages).
  - Smooth mouse wheel scrolling (`Shift + Wheel` for 1-line steps), `PageUp`/`PageDown`, `Ctrl+Home`/`Ctrl+End`, plus clickable & draggable scrollbar.
  - Exact viewport anchor preservation when new messages arrive while scrolled up.
  - Consecutive duplicate compaction (`[xN]`), optional `[HH:MM]` timestamps, click-to-copy (formatted codes stripped), and per-line `http://`/`https://` URL click-to-open.
- **Server UI override**:
  - `ResourcePackManager::composeFullStack` hook at `HookPriority::High` pins `JavaChatUI` at stack index 0 (can be disabled via `overrideServerChatUi`).
- **Full `/jchat` client command suite**:
  - Added `/jchat` / `/jchat help`, `status`, `toggle`, `clear`, `reload`, `reset`, `width`, `height`, `position`, `scale`, `spacing`, `opacity`, `textopacity`, `fade`, `shadow`, `compact`, `timestamps`, `clickcopy`, and `clickurl`.
- **Resilient JSON/JSONC configuration**:
  - Supports `//` line comments, `/* ... */` block comments, trailing commas, UTF-8 BOM, UTF-16 surrogate escapes, strict clamping, and automatic backup on malformed files.
- **Packaging & distribution**:
  - Added [`tooth.json`](tooth.json) for `lip` / LeviLauncher installation (`github.com/sh-ayu/JavaChat`, preserving `mods/JavaChat/config/config.json` across upgrades).
  - Standardized release asset naming to `JavaChat-client-windows-x64.zip` with stripped local PDB paths (`/PDBALTPATH:%_PDB%`).
