#include "AppLog.h"
#include <SDL3/SDL_log.h>
#include <cstdio>

// ── Singleton ─────────────────────────────────────────────────────────────────

AppLog& AppLog::Get() {
    static AppLog instance;
    return instance;
}

// ── SDL callback ──────────────────────────────────────────────────────────────

static void SDLLogCallback(void* /*userdata*/,
                            int             category,
                            SDL_LogPriority  priority,
                            const char*      message)
{
    // Also forward to the default output so the terminal still works.
    SDL_LogPriority currentPriority = SDL_GetLogPriority(category);
    if (priority >= currentPriority) {
        // Print to stderr the same way SDL's default callback would.
        const char* prefix = "INFO";
        switch (priority) {
            case SDL_LOG_PRIORITY_VERBOSE:  prefix = "VERBOSE";  break;
            case SDL_LOG_PRIORITY_DEBUG:    prefix = "DEBUG";    break;
            case SDL_LOG_PRIORITY_INFO:     prefix = "INFO";     break;
            case SDL_LOG_PRIORITY_WARN:     prefix = "WARN";     break;
            case SDL_LOG_PRIORITY_ERROR:    prefix = "ERROR";    break;
            case SDL_LOG_PRIORITY_CRITICAL: prefix = "CRITICAL"; break;
            default: break;
        }
        std::fprintf(stderr, "%s: %s\n", prefix, message);
    }

    AppLogLevel level = AppLogLevel::Info;
    const char* tag   = "INFO";
    switch (priority) {
        case SDL_LOG_PRIORITY_VERBOSE:  level = AppLogLevel::Verbose;  tag = "VERBOSE";  break;
        case SDL_LOG_PRIORITY_DEBUG:    level = AppLogLevel::Debug;    tag = "DEBUG";    break;
        case SDL_LOG_PRIORITY_INFO:     level = AppLogLevel::Info;     tag = "INFO";     break;
        case SDL_LOG_PRIORITY_WARN:     level = AppLogLevel::Warn;     tag = "WARN";     break;
        case SDL_LOG_PRIORITY_ERROR:    level = AppLogLevel::Error;    tag = "ERROR";    break;
        case SDL_LOG_PRIORITY_CRITICAL: level = AppLogLevel::Critical; tag = "CRITICAL"; break;
        default: break;
    }

    // Format as "[TAG] message"
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "[%s] %s", tag, message);

    AppLog::Get().Push(level, buf);
}

// ── Install ───────────────────────────────────────────────────────────────────

void AppLog::Install() {
    SDL_SetLogOutputFunction(SDLLogCallback, nullptr);
}

// ── Push ──────────────────────────────────────────────────────────────────────

void AppLog::Push(AppLogLevel level, const char* message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_entries.size() >= kMaxEntries)
        m_entries.pop_front(); // O(1) performance

    m_entries.push_back({ level, std::string(message) });
    m_scrollToBottom = true;
}

// ── Clear ─────────────────────────────────────────────────────────────────────

void AppLog::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_scrollToBottom = false;
}

// ── Scroll state ──────────────────────────────────────────────────────────────

bool AppLog::HasNewEntries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scrollToBottom;
}

void AppLog::ConsumeScrollRequest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scrollToBottom = false;
}
