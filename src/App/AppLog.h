#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <cstdint>

/// Log level associated with a captured message.
enum class AppLogLevel : uint8_t {
    Verbose,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};

/// A single captured log line.
struct AppLogEntry {
    AppLogLevel level;
    std::string text; ///< Formatted "[LEVEL] message\n"
};

/**
 * @brief Thread-safe application log sink.
 *
 * AppLog installs itself as SDL's log output function so every SDL_Log*()
 * call in the codebase is captured automatically.  The captured entries are
 * stored in a fixed-size ring buffer and exposed to the UI without any SDL
 * or ImGui dependency in this header.
 *
 * Usage:
 * @code
 *   AppLog::Install();          // once, early in Application::Init()
 *   AppLog::Get().Clear();      // optional - clear startup noise
 *   // ... later in the UI:
 *   const auto& entries = AppLog::Get().Entries();
 * @endcode
 */
class AppLog {
public:
    static constexpr size_t kMaxEntries = 2048;

    /// Singleton accessor.
    static AppLog& Get();

    /// Install as SDL's log output function.  Safe to call multiple times.
    static void Install();

    /// Append a line manually (also used by the SDL callback).
    void Push(AppLogLevel level, const char* message);

    /// Remove all stored entries and reset the scroll-to-bottom flag.
    void Clear();

    /// Returns true if new entries arrived since the last call to
    /// ConsumeScrollRequest().  Used by the panel to auto-scroll.
    bool HasNewEntries() const;

    /// Acknowledge a scroll request.
    void ConsumeScrollRequest();

    /// Read-only access to the ring buffer (caller must hold no lock;
    /// safe to call from the main/render thread only).
    const std::deque<AppLogEntry>& Entries() const { return m_entries; }

    AppLogLevel DisplayLevel = AppLogLevel::Verbose; ///< Minimum level to show

private:
    AppLog() = default;

    mutable std::mutex     m_mutex;
    std::deque<AppLogEntry> m_entries; ///< Ring buffer (capped at kMaxEntries)
    bool                   m_scrollToBottom = false;
};
