#ifdef __linux__
#include "LinuxUdevInstaller.h"
#include "App/Log.h"

#include <SDL3/SDL_filesystem.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "LinuxUdevInstaller";
constexpr const char *kScriptRelativeToBinDir = "share/inputbridge/udev/install-udev-rules.sh";

bool FileExists(const std::string &path) {
    return ::access(path.c_str(), F_OK) == 0;
}

// Runs argv via posix_spawn (no shell involved - argv entries are passed
// exactly as given, so a path containing spaces or other characters that
// would need shell-escaping is still handled correctly), waits for it,
// and captures a tail of its stderr for error reporting.
//
// posix_spawn rather than fork()+exec(): this process is a multi-threaded
// GUI app (ImGui/SDL render loop, background D-Bus thread for Bluetooth
// pairing, etc.), and fork() in a multi-threaded process only duplicates
// the calling thread - anything the runtime/other threads held locked at
// the moment of fork() (malloc arenas, etc.) can deadlock the child before
// it even gets to exec(). posix_spawn avoids that whole class of problem
// since the child never runs arbitrary C++ before exec().
LinuxUdevInstaller::RunOutcome SpawnAndWait(const std::vector<std::string> &argv_strings) {
    LinuxUdevInstaller::RunOutcome outcome;

    int stderr_pipe[2] = {-1, -1};
    if (::pipe(stderr_pipe) != 0) {
        LOG_ERROR(kTag, "pipe() failed: %s", std::strerror(errno));
        outcome.result = LinuxUdevInstaller::Result::Failed;
        return outcome;
    }

    std::vector<char *> argv;
    argv.reserve(argv_strings.size() + 1);
    for (const auto &s : argv_strings) argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // Child's stderr -> write end of the pipe; child doesn't need the read end.
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    pid_t pid = -1;
    const int spawn_rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(stderr_pipe[1]);

    if (spawn_rc != 0) {
        ::close(stderr_pipe[0]);
        LOG_ERROR(kTag, "posix_spawnp('%s') failed: %s", argv[0], std::strerror(spawn_rc));
        outcome.result = (spawn_rc == ENOENT) ? LinuxUdevInstaller::Result::PkexecNotFound
                                               : LinuxUdevInstaller::Result::Failed;
        return outcome;
    }

    // Drain stderr as we go (rather than after waitpid()) so the child
    // can't block forever writing to a full pipe while we're not reading.
    std::string stderr_all;
    std::array<char, 512> buf{};
    ssize_t n;
    while ((n = ::read(stderr_pipe[0], buf.data(), buf.size())) > 0)
        stderr_all.append(buf.data(), static_cast<size_t>(n));
    ::close(stderr_pipe[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        LOG_ERROR(kTag, "waitpid() failed: %s", std::strerror(errno));
        outcome.result = LinuxUdevInstaller::Result::Failed;
        return outcome;
    }

    outcome.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    // Keep only the last ~1KB of stderr for the caller - plenty for a
    // one-line error message, without the UI needing to handle arbitrarily
    // long text from a misbehaving script.
    constexpr size_t kTailLen = 1024;
    outcome.stderr_tail = stderr_all.size() > kTailLen
        ? stderr_all.substr(stderr_all.size() - kTailLen)
        : stderr_all;

    if (!WIFEXITED(status)) {
        outcome.result = LinuxUdevInstaller::Result::Failed;
    } else if (outcome.exit_code == 0) {
        outcome.result = LinuxUdevInstaller::Result::Success;
    } else if (outcome.exit_code == 126 || outcome.exit_code == 127) {
        // pkexec's own documented exit codes: 126 = auth dialog dismissed/
        // declined, 127 = the requested command itself couldn't be run.
        // We only distinguish "the user said no" here since that's the
        // one the UI should word differently from a generic failure.
        outcome.result = LinuxUdevInstaller::Result::UserCancelled;
    } else {
        outcome.result = LinuxUdevInstaller::Result::Failed;
    }
    return outcome;
}
} // namespace

bool LinuxUdevInstaller::IsPkexecAvailable() {
    const char *path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::string path_copy(path_env);
    size_t start = 0;
    while (start <= path_copy.size()) {
        size_t end = path_copy.find(':', start);
        if (end == std::string::npos) end = path_copy.size();
        std::string dir = path_copy.substr(start, end - start);
        if (!dir.empty() && FileExists(dir + "/pkexec")) return true;
        start = end + 1;
    }
    return false;
}

std::string LinuxUdevInstaller::ResolveScriptPath() {
    const char *base = SDL_GetBasePath();
    if (!base) {
        LOG_WARN(kTag, "SDL_GetBasePath() returned null - can't locate install-udev-rules.sh");
        return {};
    }
    const std::string bin_dir(base);

    // Dev-build layout: share/inputbridge/udev/ copied directly next to
    // the binary by CMakeLists.txt's POST_BUILD step.
    std::string candidate = bin_dir + kScriptRelativeToBinDir;
    if (FileExists(candidate)) return candidate;

    // Installed-package layout: binary is in <prefix>/bin/, script is in
    // <prefix>/share/inputbridge/udev/ - one directory up from bin_dir.
    candidate = bin_dir + "../" + kScriptRelativeToBinDir;
    if (FileExists(candidate)) return candidate;

    LOG_WARN(kTag, "install-udev-rules.sh not found next to the binary (checked '%s' "
              "and the installed-package location one level up) - was InputBridge built "
              "without the Linux packaging step, or run from somewhere unexpected?",
             (bin_dir + kScriptRelativeToBinDir).c_str());
    return {};
}

LinuxUdevInstaller::RunOutcome LinuxUdevInstaller::RunScript(const std::string &script_path,
                                                              const char *arg) {
    RunOutcome outcome;
    outcome.script_path = script_path;

    if (script_path.empty()) {
        outcome.result = Result::ScriptNotFound;
        return outcome;
    }
    if (!IsPkexecAvailable()) {
        outcome.result = Result::PkexecNotFound;
        return outcome;
    }

    std::vector<std::string> argv = {"pkexec", script_path};
    if (arg) argv.emplace_back(arg);

    outcome = SpawnAndWait(argv);
    outcome.script_path = script_path;
    return outcome;
}

LinuxUdevInstaller::RunOutcome LinuxUdevInstaller::InstallRules() {
    const std::string script_path = ResolveScriptPath();
    LOG_INFO(kTag, "Requesting elevated privileges via pkexec to install the Wiimote/"
              "Balance Board udev rule from '%s'", script_path.c_str());
    return RunScript(script_path, nullptr);
}

LinuxUdevInstaller::RunOutcome LinuxUdevInstaller::UninstallRules() {
    const std::string script_path = ResolveScriptPath();
    LOG_INFO(kTag, "Requesting elevated privileges via pkexec to remove the Wiimote/"
              "Balance Board udev rule from '%s'", script_path.c_str());
    return RunScript(script_path, "--uninstall");
}

} // namespace InputBridge::Wiimote

#endif // __linux__
