#include "App/Log.h"
#include "Network/OSCServer.h"
#include "Network/OSCSubchannel.h"
#include "Network/HapticDispatcher.h"
#include "Mappers/OutputMapper.h"
#include "Mappers/InputMapper.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "Protocols/OSCBaseProtocol.h"
#include <SDL3/SDL_timer.h>
#include <string>
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <string_view>

static constexpr const char* kTag = "OSCServer";

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
    const char* const kInactivityTimeoutEnabledKey = "InactivityTimeoutEnabled";
    const char* const kInactivityTimeoutMsKey = "InactivityTimeoutMs";
    const char* const kOutputEnabledKey = "OutputEnabled";
    const char* const kInputEnabledKey  = "InputEnabled";

    // Default values
    const char* const kDefaultHost = "127.0.0.1";
    const char* const kDefaultProtocol = "OSC Back Ally Racing";

    // OSC Paths for haptic/DualSense fields are now driven dynamically from
    // the active input ProtocolDefinition - see kFieldHandlerSpecs and
    // OSCServer::RebuildInputHandlers() below. The strings there double as
    // the built-in defaults used when no input definition is selected, or
    // when the selected one doesn't mention a given field.

    const char* const kWheelSteerPath = "/wheel/steer";
    const char* const kWheelBrakePath = "/wheel/brake";
    const char* const kWheelThrottlePath = "/wheel/throttle";
    const char* const kWheelPitchPath = "/wheel/pitch";
    const char* const kWheelRollPath = "/wheel/roll";

    const char* const kWheelButtons0Path = "/wheel/buttons/0";
    const char* const kWheelButtons1Path = "/wheel/buttons/1";
    const char* const kWheelButtons2Path = "/wheel/buttons/2";
    const char* const kWheelButtons3Path = "/wheel/buttons/3";

    // One entry per OSC-receivable haptic/DualSense field. fieldId matches
    // FieldDescriptor::id in the input field catalog (see
    // ProtocolRegistry.cpp's addIn(...) calls) so RebuildInputHandlers() can
    // look up each field's *current*, possibly-user-customized oscPath.
    // defaultPath/typespec double as the fallback used when no input
    // definition is selected, or the selected one doesn't mention a field.
    //
    // Periodic has two entries sharing one fieldId: liblo distinguishes them
    // by typespec, so both the "with wave_type" and legacy formats stay
    // addressable on the same (default or user-chosen) path.
    struct FieldHandlerSpec {
        const char* fieldId;
        const char* defaultPath;
        const char* typespec;
        OSCServer::HapticEffectKind kind;
        const char* side; // "left" / "right" / nullptr
    };

    const FieldHandlerSpec kFieldHandlerSpecs[] = {
        { "haptic_rumble",    "/haptic/rumble",    "iiffi",      OSCServer::HapticEffectKind::Rumble,   nullptr },
        { "haptic_constant",  "/haptic/constant",  "iifi",       OSCServer::HapticEffectKind::Constant, nullptr },
        { "haptic_periodic",  "/haptic/periodic",  "iiififfii",  OSCServer::HapticEffectKind::PeriodicNew,    nullptr },
        { "haptic_periodic",  "/haptic/periodic",  "iififfii",   OSCServer::HapticEffectKind::PeriodicLegacy, nullptr },
        { "haptic_condition", "/haptic/condition", "iiiffffffi", OSCServer::HapticEffectKind::Condition, nullptr },
        { "haptic_gain",      "/haptic/gain",      "ii",         OSCServer::HapticEffectKind::Gain,      nullptr },

        { "ds_trigger_left_feedback",   "/haptic/dualsense/trigger/left/feedback",   "iii",     OSCServer::HapticEffectKind::DsFeedback,  "left"  },
        { "ds_trigger_right_feedback",  "/haptic/dualsense/trigger/right/feedback",  "iii",     OSCServer::HapticEffectKind::DsFeedback,  "right" },
        { "ds_trigger_left_weapon",     "/haptic/dualsense/trigger/left/weapon",     "iiii",    OSCServer::HapticEffectKind::DsWeapon,    "left"  },
        { "ds_trigger_right_weapon",    "/haptic/dualsense/trigger/right/weapon",    "iiii",    OSCServer::HapticEffectKind::DsWeapon,    "right" },
        { "ds_trigger_left_vibration",  "/haptic/dualsense/trigger/left/vibration",  "iiii",    OSCServer::HapticEffectKind::DsVibration, "left"  },
        { "ds_trigger_right_vibration", "/haptic/dualsense/trigger/right/vibration", "iiii",    OSCServer::HapticEffectKind::DsVibration, "right" },
        { "ds_trigger_left_slope_feedback",  "/haptic/dualsense/trigger/left/slope_feedback",  "iiiii", OSCServer::HapticEffectKind::DsSlopeFeedback, "left"  },
        { "ds_trigger_right_slope_feedback", "/haptic/dualsense/trigger/right/slope_feedback", "iiiii", OSCServer::HapticEffectKind::DsSlopeFeedback, "right" },
        { "ds_trigger_left_multi_position_feedback",  "/haptic/dualsense/trigger/left/multi_position_feedback",  "iiiiiiiiiii", OSCServer::HapticEffectKind::DsMultiPositionFeedback, "left"  },
        { "ds_trigger_right_multi_position_feedback", "/haptic/dualsense/trigger/right/multi_position_feedback", "iiiiiiiiiii", OSCServer::HapticEffectKind::DsMultiPositionFeedback, "right" },
        { "ds_trigger_left_multi_position_vibration",  "/haptic/dualsense/trigger/left/multi_position_vibration",  "iiiiiiiiiiii", OSCServer::HapticEffectKind::DsMultiPositionVibration, "left"  },
        { "ds_trigger_right_multi_position_vibration", "/haptic/dualsense/trigger/right/multi_position_vibration", "iiiiiiiiiiii", OSCServer::HapticEffectKind::DsMultiPositionVibration, "right" },
        { "ds_trigger_left_bow",        "/haptic/dualsense/trigger/left/bow",        "iiiii",   OSCServer::HapticEffectKind::DsBow,       "left"  },
        { "ds_trigger_right_bow",       "/haptic/dualsense/trigger/right/bow",       "iiiii",   OSCServer::HapticEffectKind::DsBow,       "right" },
        { "ds_trigger_left_galloping",  "/haptic/dualsense/trigger/left/galloping",  "iiiiii",  OSCServer::HapticEffectKind::DsGalloping, "left"  },
        { "ds_trigger_right_galloping", "/haptic/dualsense/trigger/right/galloping", "iiiiii",  OSCServer::HapticEffectKind::DsGalloping, "right" },
        { "ds_trigger_left_machine",    "/haptic/dualsense/trigger/left/machine",    "iiiiiii", OSCServer::HapticEffectKind::DsMachine,   "left"  },
        { "ds_trigger_right_machine",   "/haptic/dualsense/trigger/right/machine",   "iiiiiii", OSCServer::HapticEffectKind::DsMachine,   "right" },
        { "ds_trigger_left_off",        "/haptic/dualsense/trigger/left/off",        "i",       OSCServer::HapticEffectKind::DsOff,       "left"  },
        { "ds_trigger_right_off",       "/haptic/dualsense/trigger/right/off",       "i",       OSCServer::HapticEffectKind::DsOff,       "right" },
    };
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
    m_inactivityTimeoutEnabled = true;
    m_inactivityTimeoutMs = 5000;
}

OSCServer::~OSCServer() {
    Stop();
    // Join the cleanup thread (if any) so it finishes accessing our members
    // before the destructor body returns and the members are destroyed.
    if (m_cleanupThread.joinable())
        m_cleanupThread.join();
    s_isDestroyed = true;
}

// Single trampoline for every dynamically registered field handler (see
// RebuildInputHandlers()). user_data is a FieldHandlerCtx*, not an
// OSCServer* - it carries both the owning server and which
// HapticDispatcher::Dispatch* call this particular (path, typespec)
// registration corresponds to.
//
// Unlike the old fixed handlers this replaces, this trampoline also does the
// bookkeeping generic_handler does for other messages (client tracking, the
// UI "Recv:" log, m_lastMessageTime, and honoring the input-enabled toggle),
// since none of that ran for haptic/DualSense traffic before - it went
// straight from liblo into HapticDispatcher with no visibility and no way to
// disable it via "Receive OSC" in the UI.
int OSCServer::dynamic_field_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* ctx = static_cast<FieldHandlerCtx*>(user_data);
        if (!ctx || !ctx->server) return 0;
        OSCServer* server = ctx->server;

        OutputMapper* mapper = nullptr;
        {
            std::lock_guard<std::mutex> lock(server->m_mutex);
            if (!server->m_running || !server->m_inputEnabled) return 0;
            mapper = server->m_OutputMapper;
            server->m_lastMessageTime = SDL_GetTicks();

            lo_address src = lo_message_get_source(msg);
            if (src) {
                const char* hostname = lo_address_get_hostname(src);
                const char* port     = lo_address_get_port(src);
                if (hostname && port) {
                    server->m_clients.insert(std::string(hostname) + ":" + std::string(port));
                }
            }

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
            server->m_logs.push_back({logEntry, false});
            if (server->m_logs.size() > 100) server->m_logs.pop_front();
        }
        // m_mutex released - HapticDispatcher/OutputMapper work happens outside the lock.

        if (!mapper) return 0;

        switch (ctx->kind) {
            case HapticEffectKind::Rumble:          HapticDispatcher::DispatchRumble(argv, argc, mapper); break;
            case HapticEffectKind::Constant:        HapticDispatcher::DispatchConstant(argv, argc, mapper); break;
            case HapticEffectKind::PeriodicNew:
            case HapticEffectKind::PeriodicLegacy:  HapticDispatcher::DispatchPeriodic(argv, argc, mapper); break;
            case HapticEffectKind::Condition:       HapticDispatcher::DispatchCondition(argv, argc, mapper); break;
            case HapticEffectKind::Gain:            HapticDispatcher::DispatchGain(argv, argc, mapper); break;
            case HapticEffectKind::DsFeedback:      HapticDispatcher::DispatchDualSenseFeedback(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsWeapon:        HapticDispatcher::DispatchDualSenseWeapon(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsVibration:     HapticDispatcher::DispatchDualSenseVibration(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsSlopeFeedback: HapticDispatcher::DispatchDualSenseSlopeFeedback(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsMultiPositionFeedback:  HapticDispatcher::DispatchDualSenseMultiPositionFeedback(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsMultiPositionVibration: HapticDispatcher::DispatchDualSenseMultiPositionVibration(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsBow:           HapticDispatcher::DispatchDualSenseBow(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsGalloping:     HapticDispatcher::DispatchDualSenseGalloping(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsMachine:       HapticDispatcher::DispatchDualSenseMachine(argv, argc, mapper, ctx->side); break;
            case HapticEffectKind::DsOff:           HapticDispatcher::DispatchDualSenseOff(argv, argc, mapper, ctx->side); break;
        }
    } catch (...) {}
    return 0;
}

// Tears down every handler previously registered by RebuildInputHandlersFor().
// Safe to call even if nothing is registered yet. Caller must hold m_handlerMutex.
void OSCServer::ClearInputHandlers() {
    if (!m_server_thread) { m_fieldHandlerCtxs.clear(); return; }
    for (auto& ctx : m_fieldHandlerCtxs) {
        lo_server_thread_del_method(m_server_thread, ctx->oscPath.c_str(), ctx->typespec.c_str());
    }
    m_fieldHandlerCtxs.clear();
}

// Rebuilds the per-field OSC receive handlers for the given input
// ProtocolDefinition id, one lo_server_thread_add_method registration per
// enabled field, bound to that field's *current* oscPath - so a custom path
// set in the Protocol Editor is what actually gets listened on, not just
// what a field-lookup scan would have matched inside generic_handler.
// Falls back to each field's built-in default path/typespec when
// inputDefinitionId is empty, or the definition doesn't mention a given
// field, so the built-in defaults keep working.
//
// Must run before haptic_subchannel_handler/generic_handler are registered
// (see Start()) so liblo tries these specific (path, typespec) matches
// first; only messages that don't match any of them fall through to the
// wildcard handlers.
//
// Only ever locks m_handlerMutex (never m_mutex) - see the header comment
// on this method for why.
void OSCServer::RebuildInputHandlersFor(const std::string& inputDefinitionId) {
    if (!m_server_thread) return;

    // ProtocolRegistry has its own internal locking; call it before taking
    // m_handlerMutex, same "look up outside the lock" convention used by
    // SetInputDefinition()/SetOutputDefinition() above.
    const ProtocolDefinition* def = nullptr;
    if (!inputDefinitionId.empty()) {
        def = ProtocolRegistry::GetInstance().FindById(inputDefinitionId);
    }

    std::lock_guard<std::mutex> lock(m_handlerMutex);
    ClearInputHandlers();

    for (const auto& spec : kFieldHandlerSpecs) {
        std::string path = spec.defaultPath;
        bool enabled = true;

        if (def) {
            const ProtocolField* field = nullptr;
            for (const auto& f : def->fields) {
                if (f.fieldId == spec.fieldId) { field = &f; break; }
            }
            if (field) {
                enabled = field->enabled;
                if (!field->oscPath.empty()) path = field->oscPath;
            }
            // Field absent from this definition entirely -> keep the
            // built-in default path/enabled state, same as before dynamic
            // registration existed.
        }

        if (!enabled) continue;

        auto ctx = std::make_unique<FieldHandlerCtx>();
        ctx->server   = this;
        ctx->kind     = spec.kind;
        ctx->side     = spec.side;
        ctx->oscPath  = std::move(path);
        ctx->typespec = spec.typespec;

        lo_server_thread_add_method(m_server_thread, ctx->oscPath.c_str(), ctx->typespec.c_str(),
                                     dynamic_field_handler, ctx.get());
        m_fieldHandlerCtxs.push_back(std::move(ctx));
    }
}

void OSCServer::RebuildInputHandlers() {
    std::string inputDefId;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        inputDefId = m_inputDefinitionId;
    }
    RebuildInputHandlersFor(inputDefId);
}

void OSCServer::OnDefinitionSaved(const std::string& definitionId) {
    if (IsDestroyed() || definitionId.empty()) return;
    bool isActiveInput = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        isActiveInput = m_running && (definitionId == m_inputDefinitionId);
    }
    if (isActiveInput) RebuildInputHandlers();
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
        if (!server || !server->m_running || !server->m_OutputMapper) return 1;

        // Delegate path parsing to the testable free function.
        const SubchannelPath sub = ParseSubchannelPath(path);
        // Not a /haptic/<effect>/<slot> subchannel path: return "not handled"
        // (non-zero) so liblo keeps trying other registered methods for this
        // message (the dynamic per-field handlers, then generic_handler).
        // This used to unconditionally return 0 here, which - since this
        // wildcard handler is registered before generic_handler - made
        // generic_handler's client tracking, "Recv:" log, and legacy
        // IProtocol dispatch permanently unreachable for every message that
        // wasn't itself a subchannel path.
        if (!sub.valid) return 1;

        // The slot is carried as argv[1] (int), identical to the old fixed paths.
        // The slot in the OSC path (/haptic/<effect>/<N>) is used purely to give
        // each slot a distinct path so hosts like Resonite - which only deliver
        // one message per path per frame - can address multiple slots in the same
        // frame.  The argument slot is always authoritative.

        switch (sub.effect) {
        case SubchannelPath::Effect::Rumble:
            HapticDispatcher::DispatchRumble(argv, argc, server->m_OutputMapper);
            break;

        case SubchannelPath::Effect::Constant:
            HapticDispatcher::DispatchConstant(argv, argc, server->m_OutputMapper);
            break;

        case SubchannelPath::Effect::Periodic:
            HapticDispatcher::DispatchPeriodic(argv, argc, server->m_OutputMapper);
            break;

        case SubchannelPath::Effect::Condition:
            HapticDispatcher::DispatchCondition(argv, argc, server->m_OutputMapper);
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
        LOG_ERROR(kTag, "Could not create send address %s:%d", send_host.c_str(), send_port);
        return false;
    }

    // Setup receiving server
    std::string recv_port_str = std::to_string(recv_port);
    m_server_thread = lo_server_thread_new_with_proto(recv_port_str.c_str(), LO_UDP, nullptr);
    if (!m_server_thread) {
        LOG_ERROR(kTag, "Could not create server on port %d", recv_port);
        lo_address_free(m_send_address);
        m_send_address = nullptr;
        return false;
    }

    // Haptic/DualSense receive handlers are generated dynamically from the
    // active input ProtocolDefinition's *current* field paths (falling back
    // to built-in defaults for any field it doesn't customize). We already
    // hold m_mutex here, so read m_inputDefinitionId directly rather than
    // going through the self-locking RebuildInputHandlers() wrapper.
    // Must happen before the wildcard handlers below so liblo tries these
    // specific (path, typespec) registrations first.
    RebuildInputHandlersFor(m_inputDefinitionId);

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

    LOG_INFO(kTag, "Started - sending to %s:%d, listening on port %d",
             send_host.c_str(), send_port, recv_port);

    m_logs.push_back({"OSC server started. Sending to " + send_host + ":" + std::to_string(send_port) + ", Listening on port " + std::to_string(recv_port), false});
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

    {
        // The lo_server_thread these were registered on is being torn down
        // on the detached cleanup thread below; m_server_thread is already
        // null at this point so there's nothing to de-register against.
        std::lock_guard<std::mutex> handlerLock(m_handlerMutex);
        m_fieldHandlerCtxs.clear();
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
            LOG_INFO(kTag, "Stopped");
            m_logs.push_back({"OSC server stopped.", false});
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
    bool timed_out = false;
    OutputMapper* mapper = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_inactivityTimeoutEnabled) return;
        if (m_running && !m_clients.empty()) {
            if (m_lastMessageTime == 0) {
                // Timeout already fired; re-arm so we keep checking on future
                // frames.  If the client sends again, the handler will write a
                // fresh tick and normal detection resumes.
                m_lastMessageTime = SDL_GetTicks();
            } else if (SDL_GetTicks() - m_lastMessageTime > m_inactivityTimeoutMs) {
                timed_out = true;
                m_clients.clear();
                m_lastMessageTime = 0;
                m_logs.push_back({"OSC clients timed out (no data). Stopping haptics.", false});
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

void OSCServer::Send(const std::string& address, float value) {
    if (!InputMapper::GetInstance().IsOutputAddressBound(address)) return;
    Send(address, "f", value);
}

void OSCServer::Send(const std::string& address, int value) {
    if (!InputMapper::GetInstance().IsOutputAddressBound(address)) return;
    Send(address, "i", value);
}

void OSCServer::Send(const std::string& address, const std::string& value) {
    if (!InputMapper::GetInstance().IsOutputAddressBound(address)) return;
    Send(address, "s", value.c_str());
}

void OSCServer::Send(const std::string& path, const char* types, ...) {
    if (s_isDestroyed) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || !m_send_address || !m_outputEnabled) return;

    va_list ap;
    va_start(ap, types);
    lo_message msg = lo_message_new();
    if (types) {
        // Append "$$" to types to bypass liblo's LO_MARKER check, which fails
        // when wrapping varargs without passing the internal liblo markers.
        std::string types_str = std::string(types) + "$$";
        lo_message_add_varargs(msg, types_str.c_str(), ap);
    }
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
        InputMapper& im = InputMapper::GetInstance();
        if (im.IsOutputAddressBound("wheel"))    Send(kWheelSteerPath, "f", steer);
        if (im.IsOutputAddressBound("brake"))    Send(kWheelBrakePath, "f", brake);
        if (im.IsOutputAddressBound("throttle")) Send(kWheelThrottlePath, "f", throttle);
        if (im.IsOutputAddressBound("pitch"))    Send(kWheelPitchPath, "f", pitch);
        if (im.IsOutputAddressBound("roll"))     Send(kWheelRollPath, "f", roll);
    }
}

void OSCServer::SendButtons(const std::vector<uint32_t>& buttons) {
    int b0 = buttons.size() > 0 ? static_cast<int>(buttons[0]) : 0;
    int b1 = buttons.size() > 1 ? static_cast<int>(buttons[1]) : 0;
    int b2 = buttons.size() > 2 ? static_cast<int>(buttons[2]) : 0;
    int b3 = buttons.size() > 3 ? static_cast<int>(buttons[3]) : 0;

    if (m_protocol) {
        InputMapper& im = InputMapper::GetInstance();
        if (im.IsOutputAddressBound("button0")) Send(kWheelButtons0Path, "i", b0);
        if (im.IsOutputAddressBound("button1")) Send(kWheelButtons1Path, "i", b1);
        if (im.IsOutputAddressBound("button2")) Send(kWheelButtons2Path, "i", b2);
        if (im.IsOutputAddressBound("button3")) Send(kWheelButtons3Path, "i", b3);
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
            server->m_logs.push_back({logEntry, false});


            if (server->m_logs.size() > 100) server->m_logs.pop_front();

            protoCopy    = server->m_protocol;
            handlerCopy  = server->m_handler;
        }
        // m_mutex is now released - safe to do slow work below.

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
                bool handled = oscProtocol->handle_osc_message(path, types, argv, argc);
                if (!handled) {
                    std::lock_guard<std::mutex> errLock(server->m_mutex);
                    // Mark the "Recv:" entry logged for this exact message (pushed
                    // above, always the most recent entry - liblo dispatches one
                    // message at a time on this thread, so there's no race) as
                    // invalid instead of appending a second, duplicate line.
                    if (!server->m_logs.empty()) server->m_logs.back().isError = true;
                }
            }
        } else if (handlerCopy) {
            handlerCopy(path, types, argv, argc);
        }
    } catch (...) {} // inner try/catch for message processing

    // Note: StopAllHapticEffects on no-clients is handled by the per-frame
    // CheckInactivity() and the edge-detection in Application::UpdateLogic(),
    // not here.  Calling it inside the message handler is incorrect because
    // m_clients is populated earlier in this same call - HasClients() can
    // return false on the very first message from a new client if the insert
    // above was skipped (e.g. lo_message_get_source returned null), causing
    // a spurious stop that interrupts valid haptic sessions.
    return 0;
}

void OSCServer::SetProtocol(const std::string& name) {
    // Call ProtocolManager outside the lock to avoid lock-ordering deadlocks.
    std::shared_ptr<IProtocol> proto = ProtocolManager::GetInstance().GetProtocol(name);
    if (proto) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_protocol = proto;
        m_protocolName = name;
    }
}

std::string OSCServer::GetProtocol() const {
    return m_protocolName;
}

void OSCServer::SetDefinition(const std::string& definitionId) {
    // Call ProtocolRegistry outside the lock to avoid lock-ordering deadlocks.
    const ProtocolDefinition* def = nullptr;
    if (!definitionId.empty()) {
        def = ProtocolRegistry::GetInstance().FindById(definitionId);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedDefinitionId = definitionId;
    if (def) {
        // Sync host/port from the definition into the UI fields
        strncpy(m_send_host, def->oscHost.c_str(), sizeof(m_send_host) - 1);
        m_send_host[sizeof(m_send_host) - 1] = '\0';
        m_send_port = def->oscSendPort;
        m_recv_port = def->oscRecvPort;
    }
}

std::string OSCServer::GetDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedDefinitionId;
}

void OSCServer::SetOutputDefinition(const std::string& definitionId) {
    // Call ProtocolRegistry outside the lock to avoid lock-ordering deadlocks.
    const ProtocolDefinition* def = nullptr;
    if (!definitionId.empty()) {
        def = ProtocolRegistry::GetInstance().FindById(definitionId);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputDefinitionId = definitionId;
    if (def) {
        strncpy(m_send_host, def->oscHost.c_str(), sizeof(m_send_host) - 1);
        m_send_host[sizeof(m_send_host) - 1] = '\0';
        m_send_port = def->oscSendPort;
    }
}

void OSCServer::SetInputDefinition(const std::string& definitionId) {
    // Call ProtocolRegistry outside the lock to avoid lock-ordering deadlocks.
    const ProtocolDefinition* def = nullptr;
    if (!definitionId.empty()) {
        def = ProtocolRegistry::GetInstance().FindById(definitionId);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inputDefinitionId = definitionId;
        if (def) m_recv_port = def->oscRecvPort;
    }
    // If we're already listening, re-point the dynamic OSC handlers at this
    // definition's fields immediately rather than waiting for a manual
    // restart (recv port changes still need a restart - only the receive
    // handler set is re-pointed here).
    RebuildInputHandlers();
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
    bool timeoutEnabled = prefs.GetBool(kOSCSection, kInactivityTimeoutEnabledKey, true);
    int timeoutMs       = prefs.GetInt (kOSCSection, kInactivityTimeoutMsKey, 5000);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        strncpy(m_send_host, send_host.c_str(), sizeof(m_send_host) - 1);
        m_send_host[sizeof(m_send_host) - 1] = '\0';
        m_send_port = send_port;
        m_recv_port = recv_port;
        m_outputEnabled = outputEnabled;
        m_inputEnabled  = inputEnabled;
        m_inactivityTimeoutEnabled = timeoutEnabled;
        m_inactivityTimeoutMs      = static_cast<uint64_t>(timeoutMs);
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
    std::string inputProto = ProtocolManager::GetInstance().GetActiveInputLegacyProtocol();

    std::string sendHost, protocol, outDef, inDef;
    int sendPort, recvPort;
    bool running, outEn, inEn, timeoutEn;
    uint64_t timeoutMs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        sendHost = m_send_host;
        sendPort = m_send_port;
        recvPort = m_recv_port;
        protocol = m_protocolName;
        outDef   = m_outputDefinitionId;
        inDef    = m_inputDefinitionId;
        running  = m_running;
        outEn    = m_outputEnabled;
        inEn     = m_inputEnabled;
        timeoutEn = m_inactivityTimeoutEnabled;
        timeoutMs = m_inactivityTimeoutMs;
    }

    prefs.SetString(kOSCSection, kSendHostKey,           sendHost);
    prefs.SetInt   (kOSCSection, kSendPortKey,            sendPort);
    prefs.SetInt   (kOSCSection, kRecvPortKey,         recvPort);
    prefs.SetString(kOSCSection, kProtocolKey,            protocol);
    prefs.SetString(kOSCSection, kInputProtocolKey,       inputProto);
    prefs.SetString(kOSCSection, kOutputDefIdKey,  outDef);
    prefs.SetString(kOSCSection, kInputDefIdKey,   inDef);
    prefs.SetBool  (kOSCSection, kEnabledKey,             running);
    prefs.SetBool  (kOSCSection, kOutputEnabledKey,       outEn);
    prefs.SetBool  (kOSCSection, kInputEnabledKey,        inEn);
    prefs.SetBool  (kOSCSection, kInactivityTimeoutEnabledKey, timeoutEn);
    prefs.SetInt   (kOSCSection, kInactivityTimeoutMsKey,      static_cast<int>(timeoutMs));
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

    // Built-in OSC protocols (named community protocols only - exclude the
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
        auto entries = buildEntries(ProtocolDirection::Send);
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
        auto entries = buildEntries(ProtocolDirection::Receive);
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

    // ── Inactivity Timeout ────────────────────────────────────────────────────
    {
        ImGui::Separator();
        if (ImGui::Checkbox("Inactivity Timeout", &m_inactivityTimeoutEnabled)) {
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        if (!m_inactivityTimeoutEnabled) ImGui::BeginDisabled();
        int timeout = static_cast<int>(m_inactivityTimeoutMs);
        if (ImGui::InputInt("Limit (ms)##osc_timeout", &timeout)) {
            if (timeout < 100) timeout = 100; // Sensible minimum
            m_inactivityTimeoutMs = static_cast<uint64_t>(timeout);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        if (!m_inactivityTimeoutEnabled) ImGui::EndDisabled();
    }

    // ── Start / stop ──────────────────────────────────────────────────────────
    if (IsRunning()) {
        bool settingsChanged = (m_send_port != m_running_send_port) ||
                               (m_recv_port != m_running_recv_port) ||
                               (m_running_send_host != m_send_host);
        if (settingsChanged) {
            if (ImGui::Button("Restart to apply")) {
                Stop();
                // Wait for the detached liblo cleanup thread to fully release
                // the old port before binding the new one.  Without this join,
                // lo_server_thread_new_with_proto() races against the teardown
                // and fails because the UDP port is still in use, leaving the
                // server stopped after the button press.
                WaitStopped();
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

    std::deque<LogEntry> logs;
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
    ImGui::SameLine();
    ImGui::Checkbox("Valid##log_valid", &m_showValidMessages);
    ImGui::SameLine();
    ImGui::Checkbox("Invalid##log_invalid", &m_showInvalidMessages);
    if (ImGui::BeginChild("Log", ImVec2(0, 150), true)) {
        // Filter only applies to per-message "Recv:" entries - server
        // lifecycle/status lines (started, stopped, client timeout, etc.)
        // aren't received messages, so they always stay visible.
        static constexpr const char* kRecvPrefix = "Recv: ";
        for (const auto& l : logs) {
            bool isRecvEntry = l.text.compare(0, std::strlen(kRecvPrefix), kRecvPrefix) == 0;
            if (isRecvEntry) {
                if (l.isError && !m_showInvalidMessages) continue;
                if (!l.isError && !m_showValidMessages) continue;
            }
            if (l.isError)
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", l.text.c_str());
            else
                ImGui::TextUnformatted(l.text.c_str());
        }
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