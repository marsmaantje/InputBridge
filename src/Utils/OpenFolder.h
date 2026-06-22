#pragma once

#include <string>

/**
 * OpenFolderInFileBrowser
 *
 * Opens `path` in the OS's native file browser (Explorer on Windows, Finder
 * on macOS, whatever the user's default file manager is on Linux), via
 * SDL_OpenURL() and a "file://" URI. The directory is created first if it
 * doesn't exist yet (e.g. a profiles/backups/themes folder that hasn't been
 * written to on this machine yet), so the button always opens *something*
 * useful instead of silently failing on a fresh install.
 *
 * @param path      Any path (relative or absolute) to a directory.
 * @param outError  If non-null and this call fails, set to a human-readable
 *                  error message (from SDL_GetError() or std::filesystem).
 * @return true on success (SDL believes it launched a file browser — this is
 *         not a guarantee the window became visible), false on failure.
 */
bool OpenFolderInFileBrowser(const std::string& path, std::string* outError = nullptr);