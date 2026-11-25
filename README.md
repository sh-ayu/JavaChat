# JavaChat

[日本語版 README はこちら (README_JA.md)](README_JA.md)

A lightweight **LeviLamina client mod** for **Minecraft Bedrock Edition** (Windows x64) that replaces Bedrock's heavy chat UI with an authentic **Minecraft: Java Edition** style chat overlay and lag-free single-line input bar.

> **Compatibility & Verification**: Built against **LeviLamina 26.60.8** (Minecraft Bedrock **1.26.60.5**). Core logic is verified with the standalone `jchat_tests` unit suite, and in-game rendering, single-Esc close, autocomplete avoidance, and authentic Java styling were verified in Minecraft 1.26.60.05.

---

## Why JavaChat? (Hybrid Architecture)

Opening chat in Bedrock Edition often stutters because `chat_screen.json` rebuilds a large scrollable message stack, header bar, host command panels, chat settings popups, and sometimes heavy server-provided sidebars every time you press `T` or `/`.

JavaChat splits the work cleanly:
1. **Ultra-light JSON-UI (`JavaChatUI` bundled pack)**: Strips `messages_scrolling_panel`, `chat_header`, `host_main_panel`, `auto_complete_panel`, and `popup_factory`, leaving only a slim **12px Java-style bottom input bar** while keeping vanilla's IME, clipboard, and intellisense data intact. It also hides vanilla's top-left HUD chat (`hud_screen.json`) without affecting coordinates or the paper doll.
2. **Native C++ Overlay (`MinecraftUIRenderContext` & `TextComponent`)**: Captures incoming messages from `GuiData::mGuiMessages` and renders the bottom-left chat history with cached UTF-8 word-wrapping (`Font::getLineLength`), smooth fade-out, and zero-lag scrolling, plus an authentic **Java Edition command suggestions popup** (`#FFFF55` yellow selection, token-only narrow box aligned to the active argument, `↑`/`↓` + wheel/click/Tab cycling), **pale-gray inline ghost completion**, and **Brigadier command syntax coloring** inside the input bar.

---

## Features

- **Authentic Java Edition look & feel**:
  - Bottom-left translucent pure-black message strips (`backgroundOpacity: 0.50`, `lineSpacing: 0.0` for crisp 9px rows at 1.0x scale) with drop-shadowed text—no distracting borders or extra sidebars.
  - Consistent vertical alignment (`bottomOffset: 40.0` above the hotbar/status bar) whether chat is open or closed, with zero jump when opening chat or showing command suggestions (suggestions render cleanly in front of chat history just like Java Edition).
  - Authentic 12px command/chat input bar with Brigadier syntax coloring (`/` and valid command names gray, incomplete/unknown command names red, subsequent arguments cycling Java's 5-color palette: aqua/yellow/green/light-purple/gold), pale-gray inline ghost completion, `_` / `|` caret, and full **`Ctrl+A` select-all editing** (`Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Backspace`/`Delete`, direct typing & IME replacement).
  - Narrow content-fitted command suggestions popup aligned directly above the active token, displaying tokens only (`#FFFF55` yellow selected, `#AAAAAA` gray unselected) with `↑`/`↓`, `Tab`/`Shift+Tab`, mouse hover, click, and wheel scrolling.
  - Transparent backdrop when chat is open (no full-screen dimming).
  - **Auto-clear on world/server switch**: Clears stale chat history from previous servers when joining or leaving a world (`clearHistoryOnWorldSwitch`).
  - **Single-`Esc` close**: Pressing `Esc` once closes chat immediately instead of only deselecting the text box.
  - **Auto-close on send**: Sending a chat message or command automatically closes chat just like Java Edition, without misfiring on IME composition `Enter` (`autoCloseOnSend`).
  - **Zero open/close animations**: Removes Bedrock's screen slide/fade animations and loading bars so chat opens and closes instantaneously (`disableChatAnimations`).
  - **Keep HUD visible in chat**: Keeps vanilla HUD and other client mods' HUD overlays visible while typing in chat (`keepHudVisibleInChat`).
- **Custom & built-in font switching**:
  - Switch live between Bedrock built-in fonts (`default`, `smooth`, `unicode`, `MinecraftTen`, `rune`, `SmoothSmall`) across the chat overlay, input bar, and autocomplete suggestions.
  - Load custom `.ttf` files by path, from `config/fonts/`, or from Windows Fonts (`C:\Windows\Fonts`), with automatic fallback for missing glyphs (restart game to apply newly staged `.ttf` files).
- **Expanded history & smooth navigation**:
  - Keeps up to **500** messages by default (configurable `20`–`5000`).
  - Scroll with **Mouse Wheel** (`Shift + Wheel` for 1-line steps), **`PageUp` / `PageDown`**, **`Ctrl+Home` / `Ctrl+End`**, or by **clicking / dragging the scrollbar**.
  - Locks your exact scroll anchor when new messages arrive while browsing older chat.
  - Properly carries over Bedrock `§` formatting codes (including Bedrock Material colors like `§m` and `§n`) across wrapped lines.
- **Quality-of-life extras**:
  - **Duplicate compaction**: Collapses consecutive identical messages into `[xN]` (`compactDuplicates`).
  - **Optional timestamps**: `[HH:MM]` prefix (`showTimestamps`).
  - **Click actions** (without unfocusing the input bar):
    - Left-click a line containing `http://` or `https://` to open it in your browser.
    - Left-click (non-URL) or right-click any line to copy its clean text (`§` codes stripped) to the clipboard.
- **Server UI override**:
  - Pins the bundled `JavaChatUI` pack to stack index 0 via `ResourcePackManager::composeFullStack` (`HookPriority::High`), overriding custom server chat UIs while preserving server packs below it (`overrideServerChatUi`).

---

## Installation

### Option 1: Using `lip` / LeviLauncher
This repository ships both [`lip.toml`](lip.toml) (format 4, for modern `lip` and LeviMC Mods discovery) and [`tooth.json`](tooth.json) (legacy compatibility), so you can install it where `lip` / LeviLauncher package installation is available:

```powershell
lip install github.com/sh-ayu/JavaChat
```

Upgrades preserve your existing `mods/JavaChat/config/config.json`.

### Option 2: Manual Release Package (`JavaChat-client-windows-x64.zip`)
1. Download `JavaChat-client-windows-x64.zip` from [Releases](https://github.com/sh-ayu/JavaChat/releases) and extract it so the `JavaChat/` folder sits inside your LeviLauncher instance's `mods/` directory:
   ```text
   %LOCALAPPDATA%\LeviLauncher\Instances\<InstanceName>\mods\JavaChat\
   ├── JavaChat.dll
   ├── manifest.json
   ├── config.sample.json
   ├── LICENSE
   ├── README.md
   ├── README_JA.md
   └── resource_packs\
       └── JavaChatUI\
   ```
2. Launch Minecraft via LeviLauncher. LeviLamina automatically registers `mods/JavaChat/resource_packs/JavaChatUI/`.
3. On first run, `config/config.json` is created under the mod's config directory (logged as `JavaChat loaded (config: ...)`).

---

## Client Commands (`/jchat`)

All settings can be adjusted live in-game and are saved automatically to `config.json`:

| Command | Description |
|---|---|
| `/jchat` or `/jchat help` | Show all `/jchat` subcommands |
| `/jchat status` | Print current state, history count, layout settings, and UI pack pin status |
| `/jchat toggle` | Toggle the JavaChat overlay ON/OFF |
| `/jchat clear` | Clear the buffered chat history |
| `/jchat reload` | Reload `config.json` from disk |
| `/jchat reset` | Reset all settings to defaults |
| `/jchat width <120..640>` | Set chat panel width |
| `/jchat height <closed> <open>` | Set max heights when closed (`24..360`) and open (`40..480`) |
| `/jchat position <left> <bottom>` | Set `leftOffset` (`0..240`) and `bottomOffset` (`18..240`) |
| `/jchat scale <0.5..2.0>` | Set text font scale |
| `/jchat spacing <0..8>` | Set extra spacing between lines |
| `/jchat font` / `/jchat font list` | Show current font, built-in presets, and `config/fonts/*.ttf` |
| `/jchat font <nameOrPath>` | Switch to a built-in font (instant) or stage a `.ttf` font (restart to apply) |
| `/jchat opacity <0..1>` | Set message background opacity |
| `/jchat textopacity <0.1..1>` | Set text opacity |
| `/jchat fade <1..120>` | Set HUD fade delay in seconds |
| `/jchat fade <delay> <duration>` | Set HUD fade delay (`1..120`s) and fade duration (`0..10`s, `0` = instant) |
| `/jchat autoclose <true\|false>` | Toggle auto-closing chat after sending a message |
| `/jchat animations <true\|false>` | Toggle chat open/close animations (`false` = Java-style instant) |
| `/jchat keephud <true\|false>` | Keep vanilla HUD & other mods' HUD visible while chat is open |
| `/jchat shadow <true\|false>` | Toggle text drop shadow |
| `/jchat compact <true\|false>` | Toggle consecutive duplicate `[xN]` compaction |
| `/jchat timestamps <true\|false>` | Toggle `[HH:MM]` timestamps |
| `/jchat clickcopy <true\|false>` | Toggle click-to-copy |
| `/jchat clickurl <true\|false>` | Toggle click-to-open `http(s)` URLs |
| `/jchat clearonswitch <true\|false>` | Toggle clearing chat history when switching worlds/servers |

---

## Configuration (`config.json`)

Supports standard JSON as well as `//` line comments, `/* ... */` block comments, and trailing commas. Invalid or out-of-range fields fall back safely; malformed files are backed up as `config.json.corrupt-<timestamp>`.

| Key | Default | Description |
|---|---|---|
| `enabled` | `true` | Enable/disable the mod overlay |
| `overrideServerChatUi` | `true` | Pin `JavaChatUI` at the top of the resource pack stack |
| `width` | `320.0` | Chat box width in GUI pixels (`120`–`640`) |
| `closedHeight` | `90.0` | Max height when chat is closed (`24`–`360`) |
| `openHeight` | `180.0` | Max height when chat is open (`40`–`480`) |
| `bottomOffset` | `40.0` | Distance from bottom edge (`18`–`240`) |
| `leftOffset` | `2.0` | Distance from left edge (`0`–`240`) |
| `fontScale` | `1.0` | Chat text scale (`0.5`–`2.0`) |
| `lineSpacing` | `0.0` | Extra vertical spacing per line (`0.0`–`8.0`) |
| `backgroundOpacity` | `0.50` | Background alpha for message strips (`0.0`–`1.0`) |
| `textOpacity` | `1.0` | Text alpha (`0.1`–`1.0`) |
| `textShadow` | `true` | Render drop shadows under chat text |
| `fadeDelaySec` | `9.0` | Seconds before closed HUD messages begin fading (`1`–`120`) |
| `fadeDurationSec` | `1.0` | Fade-out duration in seconds (`0.0`–`10`, `0` = instant vanish) |
| `maxHistoryMessages` | `500` | Maximum messages kept in history (`20`–`5000`) |
| `scrollStepLines` | `3` | Lines scrolled per mouse wheel notch (`1`–`20`) |
| `compactDuplicates` | `true` | Merge consecutive duplicate messages into `[xN]` |
| `showTimestamps` | `false` | Prefix messages with local `[HH:MM]` timestamp |
| `clickToCopy` | `true` | Copy message text on click |
| `clickToOpenUrl` | `true` | Open `http://` / `https://` links on left-click |
| `autoCloseOnSend` | `true` | Close chat automatically after sending a message or command |
| `disableChatAnimations` | `true` | Disable screen transition animations when opening/closing chat |
| `keepHudVisibleInChat` | `true` | Keep `hud_screen` & other mods' HUD overlays visible while chat is open |
| `font` | `"default"` | Built-in font (`default`/`smooth`/`unicode`/`MinecraftTen`/`rune`/`SmoothSmall`) or `.ttf` path/filename |
| `clearHistoryOnWorldSwitch` | `true` | Clear history when joining/leaving a world or server |
| `verboseLog` | `false` | Emit extra diagnostic log lines |

---

## Building from Source

Requires Windows x64, Visual Studio Build Tools, LLVM (`clang-cl`), and [xmake](https://xmake.io/) 3.0+.

```bat
:: 1. Run standalone unit tests (no LeviLamina dependency)
xmake f -m release --with_mod=n -y
xmake build jchat_tests
xmake run jchat_tests

:: 2. Build the LeviLamina 26.60.8 client mod DLL + bundled JavaChatUI pack
xmake f -m release --with_mod=y -y
xmake build JavaChat

:: 3. (Optional) Create distributable release ZIP in dist/
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

---

## Safety

- Strictly **client-side**: JavaChat reads `GuiData::mGuiMessages` and renders the chat overlay locally; it does **not** send, forge, intercept, or modify network packets.
- URL click actions strictly validate the `http://` or `https://` scheme before passing links to the OS shell (`ShellExecuteW`).

---

## Known Limitations

- **Java `hoverEvent` / `clickEvent`**: Minecraft Bedrock's network protocol (`TextPacket`) does not transmit Java Edition hover tooltips or command-run click actions, so only URL links present in message text and clipboard copy are supported.
- **Hot-unloading**: Like most LeviLamina client UI mods, updating or removing `JavaChat.dll` requires restarting the game (live unload is disabled).
- **Toggling OFF mid-session**: `/jchat toggle` immediately hides the C++ overlay, while restoring Bedrock's vanilla JSON-UI screen requires rejoining the world or restarting so the resource pack stack recomposes.
- **Future OreUI migration**: If a future Bedrock update fully migrates in-game chat from JSON-UI (`chat_screen.json`) to OreUI, the input bar portion will need an update.

---

## License

Released under the [MIT License](LICENSE).
