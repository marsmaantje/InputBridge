#pragma once

namespace InputBridge
{

// ─────────────────────────────────────────────────────────────────────────────
// Haptic errors
// ─────────────────────────────────────────────────────────────────────────────

enum class HapticError
{
    DeviceNotFound,
    InitializationFailed,
    UnsupportedEffect,
    EffectUploadFailed,
    SDLError,
    NotReady,
};

inline const char* ToString(HapticError e)
{
    switch (e)
    {
        case HapticError::DeviceNotFound:      return "Device not found";
        case HapticError::InitializationFailed: return "Initialization failed";
        case HapticError::UnsupportedEffect:   return "Unsupported effect";
        case HapticError::EffectUploadFailed:  return "Effect upload failed";
        case HapticError::SDLError:            return "SDL error";
        case HapticError::NotReady:            return "Device not ready";
        default:                               return "Unknown haptic error";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Device errors
// ─────────────────────────────────────────────────────────────────────────────

enum class DeviceError
{
    OpenFailed,
    InvalidInstanceID,
    UnsupportedType,
    AlreadyOpen,
    NotFound,
};

inline const char* ToString(DeviceError e)
{
    switch (e)
    {
        case DeviceError::OpenFailed:        return "Failed to open device";
        case DeviceError::InvalidInstanceID: return "Invalid instance ID";
        case DeviceError::UnsupportedType:   return "Unsupported device type";
        case DeviceError::AlreadyOpen:       return "Device already open";
        case DeviceError::NotFound:          return "Device not found";
        default:                             return "Unknown device error";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Network errors
// ─────────────────────────────────────────────────────────────────────────────

enum class NetworkError
{
    BindFailed,
    ConnectionFailed,
    SendFailed,
    ReceiveFailed,
    InvalidAddress,
    Timeout,
};

inline const char* ToString(NetworkError e)
{
    switch (e)
    {
        case NetworkError::BindFailed:        return "Failed to bind";
        case NetworkError::ConnectionFailed:  return "Connection failed";
        case NetworkError::SendFailed:        return "Send failed";
        case NetworkError::ReceiveFailed:     return "Receive failed";
        case NetworkError::InvalidAddress:    return "Invalid address";
        case NetworkError::Timeout:           return "Operation timed out";
        default:                              return "Unknown network error";
    }
}

} // namespace InputBridge
