# Changelog

All notable changes to **JavaChat** will be documented in this file.

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
