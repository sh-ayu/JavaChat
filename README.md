# JavaChat

[日本語版 README はこちら (README_JA.md)](README_JA.md)

A lightweight **LeviLamina client mod** for **Minecraft Bedrock Edition** (Windows x64) that replaces Bedrock's heavy chat UI with an authentic **Minecraft: Java Edition** style chat overlay and lag-free single-line input bar.

> **Compatibility & Verification**: Built against **LeviLamina 26.60.8** (Minecraft Bedrock **1.26.60.5**). Core logic is verified with the standalone `jchat_tests` unit suite, and in-game rendering, single-Esc close, autocomplete avoidance, and authentic Java styling were verified in Minecraft 1.26.60.05.

---

## Why JavaChat? (Hybrid Architecture)

Opening chat in Bedrock Edition often stutters because `chat_screen.json` rebuilds a large scrollable message stack, header bar, host command panels, chat settings popups, and sometimes heavy server-provided sidebars every time you press `T` or `/`.

JavaChat splits the work cleanly:
1. **Ultra-light JSON-UI (`JavaChatUI` bundled pack)**: Strips `messages_scrolling_panel`, `chat_header`, `host_main_panel`, and `popup_factory`, leaving only a clean 14px Java-style bottom input bar and a clipped command autocomplete popup. It also hides vanilla's top-left HUD chat (`hud_screen.json`) without affecting coordinates or the paper doll.
2. **Native C++ Overlay (`MinecraftUIRenderContext`)**: Captures incoming messages from `GuiData::mGuiMessages` and renders the bottom-left chat history with cached UTF-8 word-wrapping (`Font::getLineLength`), smooth fade-out, and zero-lag scrolling—while keeping Bedrock's native **IME input, clipboard, command Tab completion, and arrow-key input history** completely intact.

---

## Features

- **Authentic Java Edition look & feel**:
  - Bottom-left translucent pure-black message strips (`backgroundOpacity: 0.38` by default) with drop-shadowed text—no distracting borders or extra sidebars.
  - Consistent vertical alignment (`bottomOffset: 40.0` above the hotbar/status bar) whether chat is open or closed, with zero jump when toggling chat.
  - Transparent backdrop when chat is open (no full-screen dimming).
  - **Single-`Esc` close**: Pressing `Esc` once closes chat immediately instead of only deselecting the text box.
  - **Autocomplete collision avoidance**: Chat history layers behind JSON-UI (`BeforeUIRenderEvent`) and automatically slides above tall active command suggestions so text never overlaps.
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
This repository ships a [`tooth.json`](tooth.json) manifest, so you can install it where `lip` / LeviLauncher package installation is available:

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
| `/jchat opacity <0..1>` | Set message background opacity |
| `/jchat textopacity <0.1..1>` | Set text opacity |
| `/jchat fade <1..120>` | Set HUD fade delay in seconds |
| `/jchat shadow <true\|false>` | Toggle text drop shadow |
| `/jchat compact <true\|false>` | Toggle consecutive duplicate `[xN]` compaction |
| `/jchat timestamps <true\|false>` | Toggle `[HH:MM]` timestamps |
| `/jchat clickcopy <true\|false>` | Toggle click-to-copy |
| `/jchat clickurl <true\|false>` | Toggle click-to-open `http(s)` URLs |

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
| `lineSpacing` | `1.5` | Extra vertical spacing per line (`0.0`–`8.0`) |
| `backgroundOpacity` | `0.38` | Background alpha for message strips (`0.0`–`1.0`) |
| `textOpacity` | `1.0` | Text alpha (`0.1`–`1.0`) |
| `textShadow` | `true` | Render drop shadows under chat text |
| `fadeDelaySec` | `9.0` | Seconds before closed HUD messages begin fading (`1`–`120`) |
| `fadeDurationSec` | `1.0` | Fade-out duration in seconds (`0.1`–`10`) |
| `maxHistoryMessages` | `500` | Maximum messages kept in history (`20`–`5000`) |
| `scrollStepLines` | `3` | Lines scrolled per mouse wheel notch (`1`–`20`) |
| `compactDuplicates` | `true` | Merge consecutive duplicate messages into `[xN]` |
| `showTimestamps` | `false` | Prefix messages with local `[HH:MM]` timestamp |
| `clickToCopy` | `true` | Copy message text on click |
| `clickToOpenUrl` | `true` | Open `http://` / `https://` links on left-click |
| `clearHistoryOnWorldSwitch` | `false` | Clear history when joining/leaving a world or server |
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
