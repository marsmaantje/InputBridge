#pragma once

#include <string>
#include "Mappers/OutputMapper.h"

/**
 * @file ITransport.h
 * @brief Abstract transport interface for network output protocols.
 *
 * ITransport decouples the rest of the application from the specifics of any
 * particular wire protocol (OSC, WebSocket, or any future addition such as
 * MIDI, raw TCP, or serial).  The application code only depends on this
 * interface; protocol-specific details are hidden inside each concrete
 * implementation.
 *
 * Thread-safety contract
 * ----------------------
 * - Send*() methods may be called from the main/UI thread.
 * - SetOutputMapper() must be called before Start().
 * - CheckInactivity() must be called once per frame from the main thread.
 * - All other methods are assumed to be called from the main thread unless
 *   documented otherwise.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    /**
     * @brief Start the transport (open sockets, bind ports, spawn threads).
     * @return true on success, false if the transport could not start.
     */
    virtual bool Start() = 0;

    /**
     * @brief Initiate a graceful shutdown.  May return before the background
     *        thread has fully stopped; call WaitStopped() to synchronise.
     */
    virtual void Stop() = 0;

    /**
     * @brief Block until the transport's background thread has fully exited.
     *
     * Must be called after Stop() and before destroying any object the
     * transport's callbacks reference (e.g. OutputMapper).
     */
    virtual void WaitStopped() = 0;

    /** @return true while the transport is running. */
    virtual bool IsRunning() const = 0;

    // ── Output (server → clients) ─────────────────────────────────────────

    /**
     * @brief Send a named float value to all connected clients.
     * @param address  OSC path / WebSocket key for the field.
     * @param value    The float value to transmit.
     */
    virtual void SendFloat(const std::string& address, float value) = 0;

    /**
     * @brief Send a named integer value to all connected clients.
     */
    virtual void SendInt(const std::string& address, int value) = 0;

    /**
     * @brief Send a named string value to all connected clients.
     */
    virtual void SendString(const std::string& address, const std::string& value) = 0;

    // ── Haptic input (clients → server) ──────────────────────────────────

    /**
     * @brief Attach the OutputMapper that will receive queued haptic commands.
     *
     * Must be called before Start().  The pointer is non-owning; the
     * OutputMapper must outlive the transport.
     */
    virtual void SetOutputMapper(OutputMapper* mapper) = 0;

    // ── Direction enable flags ────────────────────────────────────────────

    /** Enable or disable outgoing data (server → clients). */
    virtual void SetOutputEnabled(bool enabled) = 0;
    virtual bool IsOutputEnabled() const = 0;

    /** Enable or disable incoming haptic commands (clients → server). */
    virtual void SetInputEnabled(bool enabled) = 0;
    virtual bool IsInputEnabled() const = 0;

    // ── Inactivity handling ───────────────────────────────────────────────

    /**
     * @brief Call once per frame.
     *
     * Implementations should fire OutputMapper::StopAllHapticEffects() when
     * the last connected client has been silent for longer than their
     * inactivity timeout.
     */
    virtual void CheckInactivity() = 0;

    // ── Client introspection ──────────────────────────────────────────────

    /** @return The number of currently connected clients (0 if not applicable). */
    virtual int GetClientCount() const = 0;

    /** @return A human-readable name for this transport, e.g. "OSC" or "WebSocket". */
    virtual std::string GetName() const = 0;
};
