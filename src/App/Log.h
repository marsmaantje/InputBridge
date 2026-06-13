#pragma once
/**
 * @file Log.h
 * @brief Levelled logging macros for InputBridge.
 *
 * Replace all bare SDL_Log(), fprintf(stderr,...), printf(), std::cerr and
 * std::cout logging with these macros.  Every call is routed through SDL's
 * logging system so AppLog captures it automatically with the correct level.
 *
 * Usage — tag may be a string literal or any const char* (e.g. kTag):
 *   static constexpr const char* kTag = "OSCServer";
 *   LOG_INFO (kTag, "Listening on port %d", port);
 *   LOG_WARN (kTag, "Rumble init failed: %s", SDL_GetError());
 *   LOG_ERROR(kTag, "Cannot write %s", path.c_str());
 *
 * The category string appears as a "[tag] " prefix inside the message so the
 * AppLog UI text-filter can match on it (e.g. filter "OSCServer").
 *
 * SDL log categories used:
 *   SDL_LOG_CATEGORY_APPLICATION  — general / UI / protocol / mapper
 *   SDL_LOG_CATEGORY_INPUT        — devices, haptics, sensors
 *   SDL_LOG_CATEGORY_SYSTEM       — network (OSC / WebSocket)
 *
 * If you need finer per-category SDL filtering you can switch the category
 * argument of SDL_LogMessage to any SDL_LOG_CATEGORY_* constant; the text
 * prefix makes it identifiable in the AppLog UI regardless.
 */

#include <SDL3/SDL_log.h>

// clang-format off

// ── Macros ───────────────────────────────────────────────────────────────────
// tag may be a string literal or a const char* variable (e.g. a file-scope
// static constexpr const char* kTag).  SDL_LogMessage is used instead of the
// SDL_LogXxx helpers so the format string can embed the tag at runtime via
// "[%s] " without requiring compile-time string concatenation.

/// Noisy per-frame or repeated diagnostic messages — hidden by default in the UI.
#define LOG_VERBOSE(tag, fmt, ...) \
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE,  "[%s] " fmt, tag, ##__VA_ARGS__)

/// Internal state tracing useful during development.
#define LOG_DEBUG(tag, fmt, ...) \
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG,    "[%s] " fmt, tag, ##__VA_ARGS__)

/// Normal operational events (startup, device connect/disconnect, etc.).
#define LOG_INFO(tag, fmt, ...) \
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO,     "[%s] " fmt, tag, ##__VA_ARGS__)

/// Unexpected but recoverable situations.
#define LOG_WARN(tag, fmt, ...) \
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN,     "[%s] " fmt, tag, ##__VA_ARGS__)

/// Failures that affect functionality but don't require a shutdown.
#define LOG_ERROR(tag, fmt, ...) \
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR,    "[%s] " fmt, tag, ##__VA_ARGS__)

/// Unrecoverable errors — application integrity may be compromised.
#define LOG_CRITICAL(tag, fmt, ...) \
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_CRITICAL, "[%s] " fmt, tag, ##__VA_ARGS__)

// clang-format on
