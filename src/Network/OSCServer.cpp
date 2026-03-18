#include "Network/OSCServer.h"
#include "Network/OSCSubchannel.h"
#include "Mappers/OutputMapper.h"
#include "Mappers/InputMapper.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "Protocols/OSCBaseProtocol.h"
#include "Haptics/HapticDevice.h"
#include <SDL3/SDL_timer.h>
#include <iostream>
#include <string>
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <string_view>

namespace {
    // Preference Keys
    const char* const kOSCSection = "OSC";
    const char* const kSendHostKey = "SendHost";
    const char* const kSendPortKey = "SendPort";
    const char* const kRecvPortKey = "RecvPort";
    const char* const kProtocolKey = "Protocol";
    const char* const kInputProtocolKey = "InputProtocol";
    const char* const kOutputDefIdKey = "OutputDefinitionId";
    const char* const kInputDefIdKey = "InputDefinitionId";
    const char* const kEnabledKey = "Enabled";
    const char* const kOutputEnabledKey = "OutputEnabled";
    const char* const kInputEnabledKey  = "InputEnabled";

    // Default values
    const char* const kDefaultHost = "127.0.0.1";
    const char* const kDefaultProtocol = "OSC Back Ally Racing";

    // OSC Paths
    const char* const kHapticRumblePath = "/haptic/rumble";
    const char* const kHapticConstantPath = "/haptic/constant";
    const char* const kHapticPeriodicPath = "/haptic/periodic";
    const char* const kHapticConditionPath = "/haptic/condition";
    const char* const kHapticGainPath = "/haptic/gain";

    const char* const kWheelSteerPath = "/wheel/steer";
    const char* const kWheelBrakePath = "/wheel/brake";
    const char* const kWheelThrottlePath = "/wheel/throttle";
    const char* const kWheelPitchPath = "/wheel/pitch";
    const char* const kWheelRollPath = "/wheel/roll";

    const char* const kWheelButtons0Path = "/wheel/buttons/0";
    const char* const kWheelButtons1Path = "/wheel/buttons/1";
    const char* const kWheelButtons2Path = "/wheel/buttons/2";
    const char* const kWheelButtons3Path = "/wheel/buttons/3";
}

static std::atomic<bool> s_isDestroyed{false};

OSCServer& OSCServer::GetInstance() {
    static OSCServer instance;
    return instance;
}

OSCServer::OSCServer() {
    s_isDestroyed = false;
    SetProtocol(kDefaultProtocol);
    strncpy(m_send_host, kDefaultHost, sizeof(m_send_host) - 1);
    m_send_host[sizeof(m_send_host) - 1] = '\0';
}

OSCServer::~OSCServer() {
    Stop();
    // Join the cleanup thread (if any) so it finishes accessing our members
    // before the destructor body returns and the members are destroyed.
    if (m_cleanupThread.joinable())
        m_cleanupThread.join();
    s_isDestroyed = true;
}

int OSCServer::haptic_rumble_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    // All static handlers are called from the liblo background thread (a plain C thread).
    // Any uncaught C++ exception propagating into that thread is undefined behaviour and
    // will typically terminate the process.  Guard every handler with try/catch.
    // Also check s_isDestroyed to avoid touching the OSCServer object after it has been
    // destroyed (belt-and-suspenders alongside lo_server_thread_stop in Stop()).
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 5) {
            int id = argv[0]->i;
            int slot = argv[1]->i;
            float low = argv[2]->f;
            float high = argv[3]->f;
            int duration = argv[4]->i;
            server->m_OutputMapper->QueueRumble(id, slot, low, high, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_constant_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 4) {
            int id = argv[0]->i;
            int slot = argv[1]->i;
            float strength = argv[2]->f;
            int duration = argv[3]->i;
            server->m_OutputMapper->QueueConstantForce(id, slot, strength, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_periodic_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 9) {
            // New format: i i i f i f f i i  (id, slot, wave_type, strength, period, magnitude, offset, phase, duration)
            int id       = argv[0]->i;
            int slot     = argv[1]->i;
            HapticPeriodicType wave_type = PeriodicTypeFromIndex(argv[2]->i);
            float strength  = argv[3]->f;
            int period      = argv[4]->i;
            float magnitude = argv[5]->f;
            float offset    = argv[6]->f;
            int phase       = argv[7]->i;
            int duration    = argv[8]->i;
            server->m_OutputMapper->QueuePeriodic(id, slot, wave_type, strength, period, magnitude, offset, phase, duration);
        } else if (argc >= 8) {
            // Legacy format: i i f i f f i i  (id, slot, strength, period, magnitude, offset, phase, duration) — defaults to Sine
            int id       = argv[0]->i;
            int slot     = argv[1]->i;
            float strength  = argv[2]->f;
            int period      = argv[3]->i;
            float magnitude = argv[4]->f;
            float offset    = argv[5]->f;
            int phase       = argv[6]->i;
            int duration    = argv[7]->i;
            server->m_OutputMapper->QueuePeriodic(id, slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_condition_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 10) {
            int id = argv[0]->i;
            int slot = argv[1]->i;
            HapticConditionType ctype = ConditionTypeFromIndex(argv[2]->i);
            float rsat = argv[3]->f;
            float lsat = argv[4]->f;
            float rcoeff = argv[5]->f;
            float lcoeff = argv[6]->f;
            float db = argv[7]->f;
            float center = argv[8]->f;
            int duration = argv[9]->i;
            server->m_OutputMapper->QueueCondition(id, slot, ctype, rsat, lsat, rcoeff, lcoeff, db, center, duration);
        }
    } catch (...) {}
    return 0;
}


int OSCServer::haptic_gain_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 2) {
            int id = argv[0]->i;
            int gain = argv[1]->i;
            server->m_OutputMapper->QueueSetGain(id, gain);
        }
    } catch (...) {}
    return 0;
}

// Handles subchannel paths of the form /haptic/<effect>/<slot>
// where the slot integer is encoded as the final path component instead of
// being passed as a message argument.  This allows a host that can only send
// one OSC message per frame per path (e.g. Resonite) to send multiple effects
// of the same type simultaneously by addressing different subchannels.
//
// Path parsing is delegated to ParseSubchannelPath() (OSCSubchannel.h) so that
// the logic can be unit-tested independently of the liblo runtime.
//
// See OSCSubchannel.h for the full path/argument-format specification.
// Note: /haptic/gain has no slot dimension and therefore has no subchannel variant.
int OSCServer::haptic_subchannel_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;

        // Delegate path parsing to the testable free function.
        const SubchannelPath sub = ParseSubchannelPath(path);
        if (!sub.valid) return 0;

        const int slot = sub.slot;

        switch (sub.effect) {
        // /haptic/rumble/N  iffi  (id, low_freq, high_freq, duration_ms)
        case SubchannelPath::Effect::Rumble:
            if (std::strcmp(types, "iffi") == 0 && argc == 4) {
                const int   id       = argv[0]->i;
                const float low      = argv[1]->f;
                const float high     = argv[2]->f;
                const int   duration = argv[3]->i;
                server->m_OutputMapper->QueueRumble(id, slot, low, high, duration);
            }
            break;

        // /haptic/constant/N  ifi  (id, strength, duration_ms)
        case SubchannelPath::Effect::Constant:
            if (std::strcmp(types, "ifi") == 0 && argc == 3) {
                const int   id       = argv[0]->i;
                const float strength = argv[1]->f;
                const int   duration = argv[2]->i;
                server->m_OutputMapper->QueueConstantForce(id, slot, strength, duration);
            }
            break;

        // /haptic/periodic/N
        //   iififfii  — id, wave_type, strength, period, magnitude, offset, phase, duration_ms
        //   ififfii   — legacy (no wave_type); defaults to Sine
        case SubchannelPath::Effect::Periodic:
            if (std::strcmp(types, "iififfii") == 0 && argc == 8) {
                const int   id        = argv[0]->i;
                const HapticPeriodicType wave_type = PeriodicTypeFromIndex(argv[1]->i);
                const float strength  = argv[2]->f;
                const int   period    = argv[3]->i;
                const float magnitude = argv[4]->f;
                const float offset    = argv[5]->f;
                const int   phase     = argv[6]->i;
                const int   duration  = argv[7]->i;
                server->m_OutputMapper->QueuePeriodic(id, slot, wave_type, strength, period, magnitude, offset, phase, duration);
            } else if (std::strcmp(types, "ififfii") == 0 && argc == 7) {
                const int   id        = argv[0]->i;
                const float strength  = argv[1]->f;
                const int   period    = argv[2]->i;
                const float magnitude = argv[3]->f;
                const float offset    = argv[4]->f;
                const int   phase     = argv[5]->i;
                const int   duration  = argv[6]->i;
                server->m_OutputMapper->QueuePeriodic(id, slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase, duration);
            }
            break;

        // /haptic/condition/N  iiffffffi
        //   id, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms
        case SubchannelPath::Effect::Condition:
            if (std::strcmp(types, "iiffffffi") == 0 && argc == 9) {
                const int   id       = argv[0]->i;
                const HapticConditionType ctype = ConditionTypeFromIndex(argv[1]->i);
                const float rsat     = argv[2]->f;
                const float lsat     = argv[3]->f;
                const float rcoeff   = argv[4]->f;
                const float lcoeff   = argv[5]->f;
                const float db       = argv[6]->f;
                const float center   = argv[7]->f;
                const int   duration = argv[8]->i;
                server->m_OutputMapper->QueueCondition(id, slot, ctype, rsat, lsat, rcoeff, lcoeff, db, center, duration);
            }
            break;

        default:
            break;
        }
    } catch (...) {}
    return 0;
}

bool OSCServer::Start(const std::string& send_host, int send_port, int recv_port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return true;
    }

    m_clients.clear();

    // Update internal state for UI
    strncpy(m_send_host, send_host.c_str(), sizeof(m_send_host) - 1);
    m_send_host[sizeof(m_send_host) - 1] = '\0';
    m_send_port = send_port;
    m_recv_port = recv_port;

    m_running_send_host = send_host;
    m_running_send_port = send_port;
    m_running_recv_port = recv_port;

    // Setup sending address
    m_send_address = lo_address_new(send_host.c_str(), std::to_string(send_port).c_str());
    if (!m_send_address) {
        std::cerr << "OSC Error: Could not create send address " << send_host << ":" << send_port << std::endl;
        return false;
    }

    // Setup receiving server
    std::string recv_port_str = std::to_string(recv_port);
    m_server_thread = lo_server_thread_new_with_proto(recv_port_str.c_str(), LO_UDP, nullptr);
    if (!m_server_thread) {
        std::cerr << "OSC Error: Could not create server on port " << recv_port << std::endl;
        lo_address_free(m_send_address);
        m_send_address = nullptr;
        return false;
    }

    lo_server_thread_add_method(m_server_thread, kHapticRumblePath,      "iiffi",      haptic_rumble_handler,    this);
    lo_server_thread_add_method(m_server_thread, kHapticConstantPath,    "iifi",       haptic_constant_handler,  this);
    lo_server_thread_add_method(m_server_thread, kHapticPeriodicPath,    "iififfii",   haptic_periodic_handler,  this);
    lo_server_thread_add_method(m_server_thread, kHapticConditionPath, "iiiffffffi", haptic_condition_handler, this);
    lo_server_thread_add_method(m_server_thread, kHapticGainPath, "ii", haptic_gain_handler, this);
    // Subchannel handler: catches /haptic/<effect>/<slot> paths (slot in path, no slot arg).
    // Registered before the generic catch-all so it runs first for subchannel messages.
    lo_server_thread_add_method(m_server_thread, nullptr, nullptr, haptic_subchannel_handler, this);
    lo_server_thread_add_method(m_server_thread, nullptr, nullptr, generic_handler, this);
    lo_server_thread_start(m_server_thread);

    m_running = true;
    m_isConnected = true;

    // Restore the OutputMapper that was saved when Stop() was last called.
    // Stop() deliberately nulls m_OutputMapper to guard against use-after-free
    // during shutdown, but this leaves it null when the server is restarted from
    // the UI.  Restoring the saved pointer here ensures haptic effects keep
    // working across Stop/Start cycles without requiring the caller to
    // re-call SetOutputMapper().
    if (!m_OutputMapper && m_savedOutputMapper)
        m_OutputMapper = m_savedOutputMapper;

    std::cout << "OSC server started. Sending to " << send_host << ":" << send_port
              << ", Listening on port " << recv_port << std::endl;

    m_logs.push_back("OSC server started. Sending to " + send_host + ":" + std::to_string(send_port) + ", Listening on port " + std::to_string(recv_port));
    if (m_logs.size() > 100) m_logs.pop_front();

    return true;
}

void OSCServer::Stop() {
    lo_server_thread thread_to_stop = nullptr;
    lo_address address_to_free = nullptr;
    OutputMapper* mapper = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            return;
        }

        m_running = false;
        m_isConnected = false;

        thread_to_stop = m_server_thread;
        m_server_thread = nullptr;

        address_to_free = m_send_address;
        m_send_address = nullptr;

        // Clear the client list immediately so the UI shows no clients
        // as soon as Stop() is called, rather than keeping stale entries
        // until the next Start().
        m_clients.clear();

        // Capture and null m_OutputMapper inside the lock.  The static haptic
        // handlers check m_running (atomic) first, but a handler that passed
        // that check before we set m_running=false could still call into
        // m_OutputMapper.  Clearing the pointer here closes that window: any
        // such handler will see nullptr on the next check and bail out safely.
        // We also save a copy in m_savedOutputMapper so that a subsequent
        // Start() call can restore it without requiring an external
        // SetOutputMapper() call (fixing the "effects stop after UI restart" bug).
        mapper = m_OutputMapper;
        m_savedOutputMapper = m_OutputMapper;
        m_OutputMapper = nullptr;
    }

    if (mapper) {
        mapper->StopAllHapticEffects();
    }

    // lo_server_thread_stop() blocks until the liblo receive thread exits.
    // With active clients continuously sending UDP packets this can stall
    // the caller (the UI/main thread) for a noticeable period.  Move the
    // blocking teardown onto a detached thread so the UI stays responsive.
    // Both handles are captured by value; the OSCServer singleton outlives
    // the detached thread, so the final log append is safe.
    if (thread_to_stop || address_to_free) {
        // Join any previous cleanup thread before launching a new one.
        // This prevents overlapping cleanups and ensures the stored thread
        // is always in a joinable or default (not-a-thread) state.
        if (m_cleanupThread.joinable())
            m_cleanupThread.join();

        m_cleanupThread = std::thread([this, thread_to_stop, address_to_free]() {
            if (thread_to_stop) {
                lo_server_thread_stop(thread_to_stop);
                lo_server_thread_free(thread_to_stop);
            }
            if (address_to_free) {
                lo_address_free(address_to_free);
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            std::cout << "OSC server stopped." << std::endl;
            m_logs.push_back("OSC server stopped.");
            if (m_logs.size() > 100) m_logs.pop_front();
        });
    }
}

void OSCServer::WaitStopped() {
    // Block until the liblo cleanup thread (spawned by Stop()) has finished
    // calling lo_server_thread_stop().  Once joined, no more liblo callbacks
    // can fire, so it is safe to destroy objects those callbacks reference
    // (e.g. OutputMapper).
    if (m_cleanupThread.joinable())
        m_cleanupThread.join();
}

bool OSCServer::IsDestroyed() {
    return s_isDestroyed.load();
}

bool OSCServer::IsRunning() const {
    return m_running;
}

bool OSCServer::HasClients() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_clients.empty();
}

void OSCServer::CheckInactivity() {
    const uint64_t OSC_INACTIVITY_TIMEOUT_MS = 5000;
    bool timed_out = false;
    OutputMapper* mapper = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running && !m_clients.empty()) {
            if (m_lastMessageTime == 0) {
                // Timeout already fired; re-arm so we keep checking on future
                // frames.  If the client sends again, the handler will write a
                // fresh tick and normal detection resumes.
                m_lastMessageTime = SDL_GetTicks();
            } else if (SDL_GetTicks() - m_lastMessageTime > OSC_INACTIVITY_TIMEOUT_MS) {
                timed_out = true;
                m_clients.clear();
                m_lastMessageTime = 0;
                m_logs.push_back("OSC clients timed out (no data). Stopping haptics.");
                if (m_logs.size() > 100) m_logs.pop_front();
                // m_OutputMapper may be null if Stop() ran during the same frame;
                // m_savedOutputMapper always holds the last valid pointer.
                mapper = m_OutputMapper ? m_OutputMapper : m_savedOutputMapper;
            }
        }
    }
    if (timed_out && mapper) {
        mapper->StopAllHapticEffects();
    }
}

void OSCServer::SetPortsFromProfile(const std::string& sendHost, int sendPort, int recvPort) {
    std::lock_guard<std::mutex> lock(m_mutex);
    strncpy(m_send_host, sendHost.c_str(), sizeof(m_send_host) - 1);
    m_send_host[sizeof(m_send_host) - 1] = '\0';
    m_send_port = sendPort;
    m_recv_port = recvPort;
}

bool OSCServer::IsOutputEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_outputEnabled;
}
bool OSCServer::IsInputEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inputEnabled;
}
void OSCServer::SetOutputEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputEnabled = enabled;
}
void OSCServer::SetInputEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputEnabled = enabled;
}

void OSCServer::Send(const std::string& path, const char* types, ...) {
    if (s_isDestroyed) return;

    // Avoid sending updates for unbound outputs
    if (!InputMapper::GetInstance().IsOutputAddressBound(path)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || !m_send_address || !m_outputEnabled) return;

    va_list ap;
    va_start(ap, types);
    lo_message msg = lo_message_new();
    // Append "$$" to types to bypass liblo's LO_MARKER check, which fails when wrapping varargs
    std::string types_str = (types ? types : "") + std::string("$$");
    lo_message_add_varargs(msg, types_str.c_str(), ap);
    int result = lo_send_message(m_send_address, path.c_str(), msg);
    m_isConnected = (result != -1);
    lo_message_free(msg);
    va_end(ap);
}

static std::string formatFloat(float val, int precision) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, val);
    std::string s(buf);
    std::replace(s.begin(), s.end(), ',', '.');
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.')
        s.pop_back();
    return s;
}

void OSCServer::SendWheel(float steer, float brake, float throttle, float pitch, float roll) {
    // Currently all OSC protocols use the same sending logic in this server implementation
    // Ideally this would delegate to m_protocol->format_wheel, but we need to handle binary bundles.
    // For now, we keep the logic here but it applies to all selected OSC protocols.
    if (m_protocol) {
        Send(kWheelSteerPath, "f", steer);
        Send(kWheelBrakePath, "f", brake);
        Send(kWheelThrottlePath, "f", throttle);
        Send(kWheelPitchPath, "f", pitch);
        Send(kWheelRollPath, "f", roll);
    }
}

void OSCServer::SendButtons(const std::vector<uint32_t>& buttons) {
    int b0 = buttons.size() > 0 ? static_cast<int>(buttons[0]) : 0;
    int b1 = buttons.size() > 1 ? static_cast<int>(buttons[1]) : 0;
    int b2 = buttons.size() > 2 ? static_cast<int>(buttons[2]) : 0;
    int b3 = buttons.size() > 3 ? static_cast<int>(buttons[3]) : 0;

    if (m_protocol) {
        Send(kWheelButtons0Path, "i", b0);
        Send(kWheelButtons1Path, "i", b1);
        Send(kWheelButtons2Path, "i", b2);
        Send(kWheelButtons3Path, "i", b3);
    }
}

void OSCServer::SetHandler(OSCHandler handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handler = std::move(handler);
}

int OSCServer::generic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data) {
    if (s_isDestroyed) return 0;
    auto* server = static_cast<OSCServer*>(user_data);
    if (!server) return 0;

    OutputMapper* mapper = nullptr;
    try {
        // --- Snapshot all state we need under the lock, then release it.
        // Holding m_mutex while doing protocol dispatch or ProtocolManager calls
        // can stall Send(), Stop(), and the UI thread, and creates a lock-order
        // dependency (m_mutex → ProtocolManager::m_mutex) that can deadlock if
        // the main thread ever takes those locks in the opposite order.
        std::shared_ptr<IProtocol>  protoCopy;
        OSCHandler                  handlerCopy;
        bool                        isRunning;
        bool                        inputEnabled;
        std::string                 legacyInputProto;

        {
            std::lock_guard<std::mutex> lock(server->m_mutex);
            server->m_lastMessageTime = SDL_GetTicks();
            isRunning    = server->m_running;
            inputEnabled = server->m_inputEnabled;
            mapper = server->m_OutputMapper;

            // Track the source client while we still hold the lock
            lo_address src = lo_message_get_source(msg);
            if (src) {
                const char* hostname = lo_address_get_hostname(src);
                const char* port     = lo_address_get_port(src);
                if (hostname && port) {
                    server->m_clients.insert(std::string(hostname) + ":" + std::string(port));
                }
            }

            // Build a log entry showing the path and all typed arguments so
            // the UI log is useful regardless of which dedicated handler fired.
            std::string logEntry = "Recv: " + std::string(path) + " [";
            for (int i = 0; i < argc; ++i) {
                if (i > 0) logEntry += ", ";
                char argBuf[64];
                switch (types[i]) {
                    case 'i': std::snprintf(argBuf, sizeof(argBuf), "%d",   argv[i]->i); break;
                    case 'f': std::snprintf(argBuf, sizeof(argBuf), "%.3f", argv[i]->f); break;
                    case 's': std::snprintf(argBuf, sizeof(argBuf), "\"%s\"", &argv[i]->s); break;
                    default:  std::snprintf(argBuf, sizeof(argBuf), "(%c)", types[i]); break;
                }
                logEntry += argBuf;
            }
            logEntry += "]";
            server->m_logs.push_back(logEntry);


            if (server->m_logs.size() > 100) server->m_logs.pop_front();

            protoCopy    = server->m_protocol;
            handlerCopy  = server->m_handler;
        }
        // m_mutex is now released — safe to do slow work below.

        if (!isRunning) return 0;

        // If input is disabled, log the message but do not dispatch it.
        if (!inputEnabled) return 0;

        // Resolve the active legacy input protocol outside the lock so we don't
        // hold m_mutex while calling into ProtocolManager.
        legacyInputProto = ProtocolManager::GetInstance().GetActiveInputLegacyProtocol();
        if (!legacyInputProto.empty()) {
            auto p = ProtocolManager::GetInstance().GetProtocol(legacyInputProto);
            if (p) protoCopy = p;
        }

        if (protoCopy) {
            auto oscProtocol = std::dynamic_pointer_cast<OSCBaseProtocol>(protoCopy);
            if (oscProtocol) {
                oscProtocol->handle_osc_message(path, types, argv, argc);
            }
        } else if (handlerCopy) {
            handlerCopy(path, types, argv, argc);
        }
    } catch (...) {} // inner try/catch for message processing

    // Note: StopAllHapticEffects on no-clients is handled by the per-frame
    // CheckInactivity() and the edge-detection in Application::UpdateLogic(),
    // not here.  Calling it inside the message handler is incorrect because
    // m_clients is populated earlier in this same call — HasClients() can
    // return false on the very first message from a new client if the insert
    // above was skipped (e.g. lo_message_get_source returned null), causing
    // a spurious stop that interrupts valid haptic sessions.
    return 0;
}

void OSCServer::SetProtocol(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_protocol = ProtocolManager::GetInstance().GetProtocol(name);
    if (m_protocol) m_protocolName = name;
}

std::string OSCServer::GetProtocol() const {
    return m_protocolName;
}

void OSCServer::SetDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedDefinitionId = definitionId;

    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) {
            // Sync host/port from the definition into the UI fields
            strncpy(m_send_host, def->oscHost.c_str(), sizeof(m_send_host) - 1);
            m_send_host[sizeof(m_send_host) - 1] = '\0';
            m_send_port = def->oscSendPort;
            m_recv_port = def->oscRecvPort;
        }
    }
}

std::string OSCServer::GetDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedDefinitionId;
}

void OSCServer::SetOutputDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputDefinitionId = definitionId;
    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) {
            strncpy(m_send_host, def->oscHost.c_str(), sizeof(m_send_host) - 1);
            m_send_host[sizeof(m_send_host) - 1] = '\0';
            m_send_port = def->oscSendPort;
        }
    }
}

void OSCServer::SetInputDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputDefinitionId = definitionId;
    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) m_recv_port = def->oscRecvPort;
    }
}

std::string OSCServer::GetOutputDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_outputDefinitionId;
}

std::string OSCServer::GetInputDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inputDefinitionId;
}

void OSCServer::LoadConfig(const PreferencesManager& prefs) {
    std::string send_host = prefs.GetString(kOSCSection, kSendHostKey, kDefaultHost);
    int send_port = prefs.GetInt(kOSCSection, kSendPortKey, 9066);
    int recv_port = prefs.GetInt(kOSCSection, kRecvPortKey, 9068);
    std::string protocol   = prefs.GetString(kOSCSection, kProtocolKey, kDefaultProtocol);
    std::string inputProtocol = prefs.GetString(kOSCSection, kInputProtocolKey, "");
    std::string outDefId   = prefs.GetString(kOSCSection, kOutputDefIdKey, "");
    std::string inDefId    = prefs.GetString(kOSCSection, kInputDefIdKey,  "");
    bool enabled = prefs.GetBool(kOSCSection, kEnabledKey, false);
    bool outputEnabled = prefs.GetBool(kOSCSection, kOutputEnabledKey, true);
    bool inputEnabled  = prefs.GetBool(kOSCSection, kInputEnabledKey,  true);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        strncpy(m_send_host, send_host.c_str(), sizeof(m_send_host) - 1);
        m_send_host[sizeof(m_send_host) - 1] = '\0';
        m_send_port = send_port;
        m_recv_port = recv_port;
        m_outputEnabled = outputEnabled;
        m_inputEnabled  = inputEnabled;
    }

    SetProtocol(protocol);

    if (!inputProtocol.empty()) ProtocolManager::GetInstance().SetActiveInputLegacyProtocol(inputProtocol);
    else ProtocolManager::GetInstance().SetActiveInputLegacyProtocol(protocol);

    if (!outDefId.empty()) SetOutputDefinition(outDefId);
    if (!inDefId.empty())  SetInputDefinition(inDefId);

    if (enabled) {
        Start(send_host, send_port, recv_port);
    }
}

void OSCServer::SaveConfig(PreferencesManager& prefs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    prefs.SetString(kOSCSection, kSendHostKey,           m_send_host);
    prefs.SetInt   (kOSCSection, kSendPortKey,            m_send_port);
    prefs.SetInt   (kOSCSection, kRecvPortKey,         m_recv_port);
    prefs.SetString(kOSCSection, kProtocolKey,            m_protocolName);
    prefs.SetString(kOSCSection, kInputProtocolKey,       ProtocolManager::GetInstance().GetActiveInputLegacyProtocol());
    prefs.SetString(kOSCSection, kOutputDefIdKey,  m_outputDefinitionId);
    prefs.SetString(kOSCSection, kInputDefIdKey,   m_inputDefinitionId);
    prefs.SetBool  (kOSCSection, kEnabledKey,             m_running);
    prefs.SetBool  (kOSCSection, kOutputEnabledKey,       m_outputEnabled);
    prefs.SetBool  (kOSCSection, kInputEnabledKey,        m_inputEnabled);
}

void OSCServer::SetSelectedDevice(int id) {
    m_selectedDeviceId = id;
}

int OSCServer::GetSelectedDevice() const {
    return m_selectedDeviceId;
}

const char* OSCServer::GetSendHost() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_send_host;
}

int OSCServer::GetSendPort() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_send_port;
}

int OSCServer::GetReceivePort() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recv_port;
}

void OSCServer::DrawContent() {
    ImGui::InputInt("Send Port",    &m_send_port);
    ImGui::InputInt("Receive Port", &m_recv_port);

    // ── Read state under lock ─────────────────────────────────────────────────
    std::string outDefId, inDefId, currentProto;
    bool outputEnabled, inputEnabled;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        outDefId      = m_outputDefinitionId;
        inDefId       = m_inputDefinitionId;
        currentProto  = m_protocolName;
        outputEnabled = m_outputEnabled;
        inputEnabled  = m_inputEnabled;
    }

    std::string currentInputProto = ProtocolManager::GetInstance().GetActiveInputLegacyProtocol();
    if (currentInputProto.empty()) currentInputProto = currentProto;

    // ── Combo helpers ─────────────────────────────────────────────────────────
    struct Entry {
        std::string label;
        bool isSeparator  = false;
        bool isDefinition = false;
        std::string protocolName;
        std::string definitionId;
    };

    // Built-in OSC protocols (named community protocols only — exclude the
    // generic "OSC" fallback which has no real behaviour of its own).
    std::vector<std::string> builtins;
    for (const auto& p : ProtocolManager::GetInstance().GetAvailableProtocols())
        if (p.find("OSC") != std::string::npos && p != "OSC")
            builtins.push_back(p);

    auto buildEntries = [&](ProtocolDirection dir) {
        std::vector<Entry> v;
        for (const auto& p : builtins) { Entry e; e.label = p; e.protocolName = p; v.push_back(e); }
        bool addedSep = false;
        for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
            if (def.transport != ProtocolTransport::OSC) continue;
            if (def.direction != dir) continue;
            if (!addedSep) { Entry s; s.label = "--- Custom ---"; s.isSeparator = true; v.push_back(s); addedSep = true; }
            Entry e; e.label = def.name; e.isDefinition = true; e.definitionId = def.id; v.push_back(e);
        }
        return v;
    };

    auto findIdx = [&](const std::vector<Entry>& v, const std::string& selDefId, const std::string& selProto) {
        int idx = 0;
        for (int i = 0; i < (int)v.size(); ++i) {
            if (v[i].isSeparator) continue;
            if (v[i].isDefinition && !selDefId.empty() && v[i].definitionId == selDefId) { idx = i; break; }
            if (!v[i].isDefinition && selDefId.empty() && v[i].protocolName == selProto) { idx = i; }
        }
        return idx;
    };

    auto drawCombo = [&](const char* label, std::vector<Entry>& v, int& curIdx, int& newIdx) {
        const char* preview = v.empty() ? "(none)" : v[curIdx].label.c_str();
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < (int)v.size(); ++i) {
                const auto& e = v[i];
                if (e.isSeparator) { ImGui::Separator(); ImGui::TextDisabled("Custom Protocols"); continue; }
                bool sel = (i == curIdx);
                if (ImGui::Selectable(e.label.c_str(), sel)) newIdx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    };

    // ── Output protocol (server → client) ─────────────────────────────────────
    {
        if (ImGui::Checkbox("##osc_out_en", &outputEnabled)) {
            SetOutputEnabled(outputEnabled);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::SameLine();
        if (!outputEnabled) ImGui::BeginDisabled();
        auto entries = buildEntries(ProtocolDirection::Output);
        int curIdx = findIdx(entries, outDefId, currentProto);
        int newIdx = curIdx;
        ImGui::Text("Output (send to client)");
        drawCombo("##osc_out", entries, curIdx, newIdx);
        if (newIdx != curIdx && !entries[newIdx].isSeparator) {
            const auto& c = entries[newIdx];
            if (c.isDefinition) SetOutputDefinition(c.definitionId);
            else                { SetOutputDefinition(""); SetProtocol(c.protocolName); }
        }
        if (!outDefId.empty() && ProtocolRegistry::GetInstance().FindById(outDefId))
            { ImGui::SameLine(); ImGui::TextDisabled("(ports synced)"); }
        if (!outputEnabled) ImGui::EndDisabled();
    }

    // ── Input protocol (client → server) ──────────────────────────────────────
    {
        if (ImGui::Checkbox("##osc_in_en", &inputEnabled)) {
            SetInputEnabled(inputEnabled);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::SameLine();
        if (!inputEnabled) ImGui::BeginDisabled();
        auto entries = buildEntries(ProtocolDirection::Input);
        int curIdx = findIdx(entries, inDefId, currentInputProto);
        int newIdx = curIdx;
        ImGui::Text("Input (receive from client)");
        drawCombo("##osc_in", entries, curIdx, newIdx);
        if (newIdx != curIdx && !entries[newIdx].isSeparator) {
            const auto& c = entries[newIdx];
            if (c.isDefinition) {
                SetInputDefinition(c.definitionId);
                ProtocolManager::GetInstance().SetActiveInputLegacyProtocol("");
            }
            else { SetInputDefinition(""); ProtocolManager::GetInstance().SetActiveInputLegacyProtocol(c.protocolName); }
        }
        if (!inDefId.empty() && ProtocolRegistry::GetInstance().FindById(inDefId))
            { ImGui::SameLine(); ImGui::TextDisabled("(recv port synced)"); }
        if (!inputEnabled) ImGui::EndDisabled();
    }

    // ── Start / stop ──────────────────────────────────────────────────────────
    if (IsRunning()) {
        bool settingsChanged = (m_send_port != m_running_send_port) ||
                               (m_recv_port != m_running_recv_port) ||
                               (m_running_send_host != m_send_host);
        if (settingsChanged) {
            if (ImGui::Button("Restart to apply")) {
                Stop();
                Start(m_send_host, m_send_port, m_recv_port);
                InputMapper::GetInstance().SaveCurrentProfile();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Stop OSC")) {
            Stop();
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0,1,0,1), "Running");
        ImGui::SameLine();
        ImGui::TextColored(m_isConnected ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
                           m_isConnected ? "Connected" : "Send Error");
    } else {
        if (ImGui::Button("Start OSC")) {
            Start(m_send_host, m_send_port, m_recv_port);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0,0,1), "Stopped");
    }

    std::deque<std::string> logs;
    std::set<std::string> clients;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        logs    = m_logs;
        clients = m_clients;
    }

    ImGui::Separator();
    ImGui::Text("Known Clients (Sources): %d", (int)clients.size());
    if (ImGui::TreeNode("Client List")) {
        if (ImGui::BeginChild("Clients", ImVec2(0, 100), true))
            for (const auto& c : clients) ImGui::TextUnformatted(c.c_str());
        ImGui::EndChild();
        ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::Text("Log");
    if (ImGui::BeginChild("Log", ImVec2(0, 150), true)) {
        for (const auto& l : logs) ImGui::TextUnformatted(l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void OSCServer::SetOutputMapper(OutputMapper* mapper) {
    // Protect with the mutex: the static haptic handlers read m_OutputMapper
    // from the liblo receive thread, so writes from the main thread must be
    // synchronised to prevent a data race.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_OutputMapper = mapper;
}