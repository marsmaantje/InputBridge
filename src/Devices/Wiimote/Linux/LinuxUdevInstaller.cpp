#ifdef __linux__
#include "LinuxUdevInstaller.h"
#include "App/Log.h"

#include <SDL3/SDL_filesystem.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "LinuxUdevInstaller";
constexpr const char *kScriptRelativeToBinDir = "share/inputbridge/udev/install-udev-rules.sh";
// Kept in sync with WiimoteLinuxDiagnostics.cpp's kUdevRulesPath basename -
// this file only needs the bare filename since it copies it alongside the
// script (see StageScriptForHostSpawn below), not the full install
// destination.
constexpr const char *kUdevRulesFilename = "71-inputbridge-wiimote.rules";

bool FileExists(const std::string &path) {
    return ::access(path.c_str(), F_OK) == 0;
}

// True when this process is itself running inside a Flatpak sandbox
// (standard detection: Flatpak bind-mounts this file into every sandboxed
// app's /). Needed because pkexec and plain filesystem paths behave very
// differently in here than on a normal host - see RunScript() below.
bool IsRunningInFlatpak() {
    return FileExists("/.flatpak-info");
}

bool CopyFile(const std::string &src, const std::string &dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return static_cast<bool>(out);
}

// Inside a Flatpak sandbox, pkexec can't reach the host's PolicyKit
// authority, so RunScript() below shells out via `flatpak-spawn --host
// pkexec ...` instead - that runs pkexec (and the script it's given) on
// the real host rather than in here. But flatpak-spawn's argv is resolved
// on the HOST's filesystem, and install-udev-rules.sh (plus the .rules
// file it looks for next to itself - see that script's SOURCE_RULES_PATH
// logic) normally lives under /app/share/..., a path that only exists in
// this sandbox's own private mount namespace and is invisible to the
// host entirely.
//
// $XDG_RUNTIME_DIR is one of the few locations Flatpak bind-mounts
// through to the sandbox at the *same* path the host uses (it's how
// Wayland sockets and portals work), so staging a copy of both files
// there - preserving the same "script next to its .rules file" layout -
// gives flatpak-spawn a path that resolves correctly on both sides.
//
// Returns the host-visible path to the staged script, or empty if
// staging failed for any reason (missing XDG_RUNTIME_DIR, couldn't
// create the directory, couldn't find/copy either file).
std::string StageScriptForHostSpawn(const std::string &sandboxed_script_path) {
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        LOG_WARN(kTag, "XDG_RUNTIME_DIR not set - can't stage install-udev-rules.sh "
                  "somewhere the host can see it for flatpak-spawn.");
        return {};
    }

    const size_t slash = sandboxed_script_path.find_last_of('/');
    if (slash == std::string::npos) return {};
    const std::string sandboxed_dir = sandboxed_script_path.substr(0, slash);
    const std::string sandboxed_rules_path = sandboxed_dir + "/" + kUdevRulesFilename;

    const std::string stage_dir = std::string(runtime_dir) + "/inputbridge-udev-install";
    if (::mkdir(stage_dir.c_str(), 0700) != 0 && errno != EEXIST) {
        LOG_WARN(kTag, "mkdir('%s') failed: %s", stage_dir.c_str(), std::strerror(errno));
        return {};
    }

    const std::string staged_script_path = stage_dir + "/install-udev-rules.sh";
    const std::string staged_rules_path = stage_dir + "/" + kUdevRulesFilename;

    if (!CopyFile(sandboxed_script_path, staged_script_path)) {
        LOG_WARN(kTag, "Failed to stage '%s' -> '%s' for flatpak-spawn.",
                  sandboxed_script_path.c_str(), staged_script_path.c_str());
        return {};
    }
    if (!CopyFile(sandboxed_rules_path, staged_rules_path)) {
        LOG_WARN(kTag, "Failed to stage '%s' -> '%s' for flatpak-spawn.",
                  sandboxed_rules_path.c_str(), staged_rules_path.c_str());
        return {};
    }
    ::chmod(staged_script_path.c_str(), 0700);
    ::chmod(staged_rules_path.c_str(), 0644);

    return staged_script_path;
}

// Runs argv via posix_spawn (no shell involved - argv entries are passed
// exactly as given, so a path containing spaces or other characters that
// would need shell-escaping is still handled correctly), waits for it,
// and captures a tail of its stdout and stderr for the UI.
//
// stdout matters as much as stderr here: on success, install-udev-rules.sh
// prints a multi-step "Next steps" block (replug the device, log out/in if
// it added the user to plugdev, relaunch InputBridge) to stdout, and that
// text is the only place those steps are written down - the UI needs to
// show it to the user directly rather than just logging that install
// succeeded.
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

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
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
    // Child's stdout/stderr -> write end of the respective pipe; child
    // doesn't need the read ends.
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    pid_t pid = -1;
    const int spawn_rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    if (spawn_rc != 0) {
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        LOG_ERROR(kTag, "posix_spawnp('%s') failed: %s", argv[0], std::strerror(spawn_rc));
        outcome.result = (spawn_rc == ENOENT) ? LinuxUdevInstaller::Result::PkexecNotFound
                                               : LinuxUdevInstaller::Result::Failed;
        return outcome;
    }

    // Drain both pipes as we go (rather than after waitpid()) so the child
    // can't block forever writing to a full pipe while we're not reading -
    // and use poll() to read whichever pipe has data rather than reading
    // stderr to EOF first, which would deadlock if the child fills the
    // stdout pipe (e.g. the "Next steps" block) before it closes stderr.
    std::string stdout_all;
    std::string stderr_all;
    {
        std::array<pollfd, 2> fds{{
            {stdout_pipe[0], POLLIN, 0},
            {stderr_pipe[0], POLLIN, 0},
        }};
        int open_fds = 2;
        std::array<char, 512> buf{};
        while (open_fds > 0) {
            const int ready = ::poll(fds.data(), fds.size(), -1);
            if (ready < 0) {
                if (errno == EINTR) continue;
                break;
            }
            for (auto &pfd : fds) {
                if (pfd.fd == -1 || !(pfd.revents & (POLLIN | POLLHUP | POLLERR))) continue;
                ssize_t n = ::read(pfd.fd, buf.data(), buf.size());
                if (n > 0) {
                    (pfd.fd == stdout_pipe[0] ? stdout_all : stderr_all)
                        .append(buf.data(), static_cast<size_t>(n));
                } else {
                    // EOF or error on this fd - stop polling it.
                    ::close(pfd.fd);
                    pfd.fd = -1;
                    --open_fds;
                }
            }
        }
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        LOG_ERROR(kTag, "waitpid() failed: %s", std::strerror(errno));
        outcome.result = LinuxUdevInstaller::Result::Failed;
        return outcome;
    }

    outcome.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    // Keep only the last ~4KB of stdout / ~1KB of stderr for the caller -
    // stdout gets more room since a successful run's "Next steps" block is
    // meant to be shown to the user in full, while stderr is only ever
    // used for a one-line error message.
    constexpr size_t kStdoutTailLen = 4096;
    constexpr size_t kStderrTailLen = 1024;
    outcome.stdout_tail = stdout_all.size() > kStdoutTailLen
        ? stdout_all.substr(stdout_all.size() - kStdoutTailLen)
        : stdout_all;
    outcome.stderr_tail = stderr_all.size() > kStderrTailLen
        ? stderr_all.substr(stderr_all.size() - kStderrTailLen)
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
    // Under Flatpak, RunScript() below never calls the sandboxed pkexec
    // directly (it wouldn't be able to reach the host's PolicyKit
    // authority anyway) - it shells out via flatpak-spawn --host instead.
    // So the binary that actually needs to be on PATH here is
    // flatpak-spawn, not pkexec; checking for pkexec would leave the
    // Install/Remove buttons permanently disabled in the Flatpak build
    // even though the flatpak-spawn path works fine.
    const char *bin_to_check = IsRunningInFlatpak() ? "flatpak-spawn" : "pkexec";

    const char *path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::string path_copy(path_env);
    size_t start = 0;
    while (start <= path_copy.size()) {
        size_t end = path_copy.find(':', start);
        if (end == std::string::npos) end = path_copy.size();
        std::string dir = path_copy.substr(start, end - start);
        if (!dir.empty() && FileExists(dir + "/" + bin_to_check)) return true;
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

    std::vector<std::string> argv;
    if (IsRunningInFlatpak()) {
        // pkexec inside the sandbox can't reach the host's PolicyKit
        // authority, so run it on the host via flatpak-spawn instead -
        // which needs a copy of the script (and its .rules file)
        // somewhere the host can actually resolve. See
        // StageScriptForHostSpawn's comment for why a plain sandboxed
        // path can't just be passed through as-is.
        const std::string staged_script_path = StageScriptForHostSpawn(script_path);
        if (staged_script_path.empty()) {
            outcome.result = Result::ScriptNotFound;
            return outcome;
        }
        argv = {"flatpak-spawn", "--host", "pkexec", staged_script_path};
    } else {
        argv = {"pkexec", script_path};
    }
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
