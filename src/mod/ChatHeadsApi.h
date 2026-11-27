// Vendored from github.com/sh-ayu/ChatHeads (include/ChatHeadsApi.h). Keep in sync.
// ChatHeads public C ABI (v1).
//
// Other client mods (e.g. JavaChat) can draw player heads next to chat lines without linking
// against ChatHeads: look the module up at runtime and resolve these exports with GetProcAddress.
//
//   HMODULE mod = GetModuleHandleW(L"ChatHeads.dll");
//   auto ver    = (ChatHeads_ApiVersion_t)GetProcAddress(mod, "ChatHeads_ApiVersion");
//   if (ver && ver() >= CHATHEADS_API_VERSION) { ... }
//
// All functions must be called on the client render/UI thread (e.g. inside a LeviLamina
// UIRenderEvent listener). They never throw across the ABI boundary.
#pragma once

#include <stddef.h>

#define CHATHEADS_API_VERSION 1

#ifdef CHATHEADS_EXPORTS
#define CHATHEADS_API extern "C" __declspec(dllexport)
#else
#define CHATHEADS_API extern "C" __declspec(dllimport)
#endif

// Returns CHATHEADS_API_VERSION of the loaded ChatHeads build.
typedef int (*ChatHeads_ApiVersion_t)(void);

// Returns non-zero when ChatHeads is enabled by the user (`/chatheads toggle`).
typedef int (*ChatHeads_IsEnabled_t)(void);

// Resolves the sender of a chat message into a stable head key.
//  - messageType: GuiMessage::MessageType as int (1 = chat, 4 = system, ...)
//  - username / authorXuid: GuiMessage::mUsername / mAuthorXuid (may be empty)
//  - text: full formatted line (with § codes)
// Writes a NUL-terminated key into outKey and returns its length, or 0 when no known player
// was found (then no head should be drawn and no indent reserved).
typedef size_t (*ChatHeads_ResolveSender_t)(
    int         messageType,
    char const* username,
    size_t      usernameLen,
    char const* authorXuid,
    size_t      authorXuidLen,
    char const* text,
    size_t      textLen,
    char*       outKey,
    size_t      outKeyCapacity
);

// Draws the head for `key` at (x, y) with the given square size (GUI pixels, absolute screen
// coordinates) and opacity. `uiRenderContext` is a `MinecraftUIRenderContext*`.
// Returns non-zero if a head was drawn.
typedef int (*ChatHeads_DrawHead_t)(
    void*       uiRenderContext,
    char const* key,
    size_t      keyLen,
    float       x,
    float       y,
    float       size,
    float       alpha
);

// Recommended head size / gap (GUI pixels at font scale 1.0) from the user's ChatHeads config.
typedef float (*ChatHeads_HeadSize_t)(void);
typedef float (*ChatHeads_HeadGap_t)(void);
