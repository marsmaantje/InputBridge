#include "OpenFolder.h"

#include "App/Log.h"

#include <SDL3/SDL.h>
#include <filesystem>

static constexpr const char* kTag = "OpenFolder";

namespace {

// Percent-encodes everything except the small set of characters that are
// always safe unescaped in a file:// URI path (RFC 3986 unreserved set,
// plus '/' as the path separator and ':' for a Windows drive letter).
// Without this, paths containing spaces (very common — "Program Files",
// "John Doe", OneDrive-synced folders, …) would produce a malformed URI.
std::string PercentEncodePathForUri(const std::string& path) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') ||
                           c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':';
        if (safe) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

void SetError(std::string* outError, const std::string& message) {
    LOG_ERROR(kTag, "%s", message.c_str());
    if (outError) *outError = message;
}

} // namespace

bool OpenFolderInFileBrowser(const std::string& path, std::string* outError) {
    // Best-effort: a folder that doesn't exist yet (e.g. no mapping profile
    // saved so far) should still open to *somewhere* useful rather than fail.
    std::error_code ec;
    std::filesystem::create_directories(path, ec);

    std::filesystem::path absPath = std::filesystem::absolute(path, ec);
    if (ec) {
        SetError(outError, "Couldn't resolve path '" + path + "': " + ec.message());
        return false;
    }

    // generic_string() always uses forward slashes, which is what a file://
    // URI needs on every platform — including Windows, which accepts forward
    // slashes in file:// URIs even though native paths use backslashes.
    const std::string generic = absPath.generic_string();

#ifdef _WIN32
    // Windows absolute paths start with a drive letter ("C:/Users/...")
    // rather than a leading slash, so the URI needs one more slash to reach
    // the standard "file:///C:/Users/..." form.
    const std::string uri = "file:///" + PercentEncodePathForUri(generic);
#else
    // POSIX absolute paths already start with '/', so "file://" + "/home/…"
    // naturally produces the standard "file:///home/…" form.
    const std::string uri = "file://" + PercentEncodePathForUri(generic);
#endif

    if (!SDL_OpenURL(uri.c_str())) {
        SetError(outError, std::string("Couldn't open '") + generic + "': " + SDL_GetError());
        return false;
    }
    return true;
}