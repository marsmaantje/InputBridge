#pragma once
/**
 * @file Log.h
 * @brief Levelled logging macros for InputBridge.
 *
 * Replace all bare SDL_Log(), fprintf(stderr,...), printf(), std::cerr and
 * std::cout logging with these macros.  Every call is routed through SDL's
 * logging system so AppLog captures it automatically with the correct level.
 *
 * Usage with a string literal tag (zero runtime overhead):
 *   LOG_INFO ("OSCServer", "Listening on port %d", port);
 *   LOG_WARN ("HapticDevice", "Rumble init failed: %s", SDL_GetError());
 *   LOG_ERROR("ProtocolRegistry", "Cannot write %s", path.c_str());
 *
 * Usage with a const std::string tag (avoids duplicate string storage):
 *   static const std::string kTag = "OSCServer";
 *   LOG_INFO_S (kTag, "Listening on port %d", port);
 *   LOG_WARN_S (kTag, "Rumble init failed: %s", SDL_GetError());
 *   LOG_ERROR_S(kTag, "Cannot write %s", path.c_str());
 *
 * The category string appears as a prefix inside the message so the existing
 * AppLog UI text-filter can match on it (e.g. filter "OSCServer").
 *
 * SDL log categories used:
 *   SDL_LOG_CATEGORY_APPLICATION  — general / UI / protocol / mapper
 *   SDL_LOG_CATEGORY_INPUT        — devices, haptics, sensors
 *   SDL_LOG_CATEGORY_SYSTEM       — network (OSC / WebSocket)
 *
 * If you need finer per-category SDL filtering you can switch the second
 * argument of SDL_LogMessage to any SDL_LOG_CATEGORY_* constant; the text
 * prefix makes it identifiable in the AppLog UI regardless.
 */

#include <SDL3/SDL_log.h>
#include <string>

// ── Helper: pick an SDL category from the subsystem tag ──────────────────────
// This keeps SDL's own category-level filtering meaningful while still letting
// every message land in AppLog via the single shared callback.

// clang-format off
#define _LOG_SDL_CAT(tag) SDL_LOG_CATEGORY_APPLICATION

// ── Public macros — literal tag variants ─────────────────────────────────────
// tag must be a string literal; the "[tag] " prefix is composed at compile time.

/// Noisy per-frame or repeated diagnostic messages — hidden by default in the UI.
#define LOG_VERBOSE(tag, fmt, ...) \
    SDL_LogVerbose(_LOG_SDL_CAT(tag), "[" tag "] " fmt, ##__VA_ARGS__)

/// Internal state tracing useful during development.
#define LOG_DEBUG(tag, fmt, ...) \
    SDL_LogDebug(_LOG_SDL_CAT(tag), "[" tag "] " fmt, ##__VA_ARGS__)

/// Normal operational events (startup, device connect/disconnect, etc.).
#define LOG_INFO(tag, fmt, ...) \
    SDL_LogInfo(_LOG_SDL_CAT(tag), "[" tag "] " fmt, ##__VA_ARGS__)

/// Unexpected but recoverable situations.
#define LOG_WARN(tag, fmt, ...) \
    SDL_LogWarn(_LOG_SDL_CAT(tag), "[" tag "] " fmt, ##__VA_ARGS__)

/// Failures that affect functionality but don't require a shutdown.
#define LOG_ERROR(tag, fmt, ...) \
    SDL_LogError(_LOG_SDL_CAT(tag), "[" tag "] " fmt, ##__VA_ARGS__)

/// Unrecoverable errors — application integrity may be compromised.
#define LOG_CRITICAL(tag, fmt, ...) \
    SDL_LogCritical(_LOG_SDL_CAT(tag), "[" tag "] " fmt, ##__VA_ARGS__)

// ── Public macros — std::string tag variants ─────────────────────────────────
// Use these when the tag is stored as a const std::string (e.g. a class member
// or static constant) to avoid duplicating the string as a literal.
// The tag value is prepended at runtime; all other behaviour is identical.

#define LOG_VERBOSE_S(stag, fmt, ...) \
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, ("[" + (stag) + "] " fmt).c_str(), ##__VA_ARGS__)

#define LOG_DEBUG_S(stag, fmt, ...) \
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, ("[" + (stag) + "] " fmt).c_str(), ##__VA_ARGS__)

#define LOG_INFO_S(stag, fmt, ...) \
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, ("[" + (stag) + "] " fmt).c_str(), ##__VA_ARGS__)

#define LOG_WARN_S(stag, fmt, ...) \
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, ("[" + (stag) + "] " fmt).c_str(), ##__VA_ARGS__)

#define LOG_ERROR_S(stag, fmt, ...) \
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, ("[" + (stag) + "] " fmt).c_str(), ##__VA_ARGS__)

#define LOG_CRITICAL_S(stag, fmt, ...) \
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, ("[" + (stag) + "] " fmt).c_str(), ##__VA_ARGS__)
// clang-format on
