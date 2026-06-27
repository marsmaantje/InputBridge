#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// @file Log.h 
// @brief provides a unified logging interface for the application.
//
// Features:
// • Consistent logging macros for verbose, debug, info, warning,
//   error, and critical messages.
// • Automatic routing through SDL's logging system for centralized
//   capture and display.
// • Tag-based message categorization for easy filtering and debugging.
// • Runtime-compatible tags using string literals or const char*
//   variables.
// • Standardized message formatting across all subsystems.
// • Integration with AppLog and SDL log-priority filtering.
//
// All log messages are emitted through SDL_LogMessage, ensuring
// consistent formatting, severity handling, and log capture
// throughout the application.
// ─────────────────────────────────────────────────────────────────────────────

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
