#pragma once
#include <variant>
#include <stdexcept>
#include <type_traits>

namespace InputBridge {

/**
 * @brief A Result type for error handling without exceptions
 * 
 * Result<T, E> represents either a success value of type T or an error of type E.
 * This is similar to std::expected in C++23 but works with C++17.
 * 
 * @tparam T The success value type
 * @tparam E The error type
 * 
 * @example
 * Result<int, std::string> divide(int a, int b) {
 *     if (b == 0) {
 *         return Err<std::string>("Division by zero");
 *     }
 *     return Ok(a / b);
 * }
 * 
 * auto result = divide(10, 2);
 * if (result.IsOk()) {
 *     std::cout << "Result: " << result.Value() << std::endl;
 * } else {
 *     std::cout << "Error: " << result.Error() << std::endl;
 * }
 */
template<typename T, typename E>
class Result {
public:
    /**
     * @brief Creates a success result
     */
    static Result Ok(T value) {
        return Result(OkTag{}, std::move(value));
    }
    
    /**
     * @brief Creates an error result
     */
    static Result Err(E error) {
        return Result(ErrTag{}, std::move(error));
    }
    
    /**
     * @brief Checks if this result contains a success value
     */
    bool IsOk() const noexcept {
        return std::holds_alternative<T>(m_data);
    }
    
    /**
     * @brief Checks if this result contains an error
     */
    bool IsErr() const noexcept {
        return std::holds_alternative<E>(m_data);
    }
    
    /**
     * @brief Gets the success value
     * @throws std::runtime_error if this result contains an error
     */
    const T& Value() const & {
        if (IsErr()) {
            throw std::runtime_error("Attempted to access value of error result");
        }
        return std::get<T>(m_data);
    }
    
    /**
     * @brief Gets the success value (rvalue overload)
     * @throws std::runtime_error if this result contains an error
     */
    T&& Value() && {
        if (IsErr()) {
            throw std::runtime_error("Attempted to access value of error result");
        }
        return std::get<T>(std::move(m_data));
    }
    
    /**
     * @brief Gets the error
     * @throws std::runtime_error if this result contains a success value
     */
    const E& Error() const & {
        if (IsOk()) {
            throw std::runtime_error("Attempted to access error of success result");
        }
        return std::get<E>(m_data);
    }
    
    /**
     * @brief Gets the error (rvalue overload)
     * @throws std::runtime_error if this result contains a success value
     */
    E&& Error() && {
        if (IsOk()) {
            throw std::runtime_error("Attempted to access error of success result");
        }
        return std::get<E>(std::move(m_data));
    }
    
    /**
     * @brief Gets the value or a default
     * @param default_value Value to return if this result is an error
     */
    T ValueOr(T default_value) const & {
        return IsOk() ? std::get<T>(m_data) : std::move(default_value);
    }
    
    /**
     * @brief Gets the value or a default (rvalue overload)
     */
    T ValueOr(T default_value) && {
        return IsOk() ? std::get<T>(std::move(m_data)) : std::move(default_value);
    }
    
    /**
     * @brief Applies a function to the success value if present
     * 
     * @tparam F Function type (T -> U)
     * @return Result<U, E> containing either the transformed value or the original error
     * 
     * @example
     * Result<int, string> r = Ok(5);
     * auto doubled = r.Map([](int x) { return x * 2; }); // Result<int, string> with value 10
     */
    template<typename F>
    auto Map(F&& func) const & -> Result<std::invoke_result_t<F, const T&>, E> {
        using U = std::invoke_result_t<F, const T&>;
        if (IsOk()) {
            return Result<U, E>::Ok(func(std::get<T>(m_data)));
        }
        return Result<U, E>::Err(std::get<E>(m_data));
    }
    
    /**
     * @brief Chains operations that return Result
     * 
     * @tparam F Function type (T -> Result<U, E>)
     * @return Result<U, E>
     * 
     * @example
     * Result<int, string> r = Ok(10);
     * auto result = r.AndThen([](int x) {
     *     return x > 0 ? Ok(x * 2) : Err<string>("Must be positive");
     * });
     */
    template<typename F>
    auto AndThen(F&& func) const & -> std::invoke_result_t<F, const T&> {
        if (IsOk()) {
            return func(std::get<T>(m_data));
        }
        using U = typename std::invoke_result_t<F, const T&>::value_type;
        return std::invoke_result_t<F, const T&>::Err(std::get<E>(m_data));
    }
    
    /**
     * @brief Applies a function to the error if present
     * 
     * @example
     * Result<int, string> r = Err<string>("error");
     * auto logged = r.MapErr([](const string& e) {
     *     SDL_Log("Error: %s", e.c_str());
     *     return e;
     * });
     */
    template<typename F>
    auto MapErr(F&& func) const & -> Result<T, std::invoke_result_t<F, const E&>> {
        using NewE = std::invoke_result_t<F, const E&>;
        if (IsErr()) {
            return Result<T, NewE>::Err(func(std::get<E>(m_data)));
        }
        return Result<T, NewE>::Ok(std::get<T>(m_data));
    }
    
    /**
     * @brief Implicit conversion to bool (true if Ok, false if Err)
     */
    explicit operator bool() const noexcept {
        return IsOk();
    }
    
private:
    struct OkTag {};
    struct ErrTag {};
    
    Result(OkTag, T value) : m_data(std::move(value)) {}
    Result(ErrTag, E error) : m_data(std::move(error)) {}
    
    std::variant<T, E> m_data;
};

// ============================================================================
// Helper functions for creating Results
// ============================================================================

/**
 * @brief Creates a success Result
 * 
 * @example
 * auto result = Ok(42); // Deduces Result<int, ?>
 */
template<typename T>
auto Ok(T value) {
    // Note: Error type must be specified at call site or will be deduced from context
    return value; // Return value directly, will be wrapped by Result::Ok() at call site
}

/**
 * @brief Creates an error Result
 * 
 * @example
 * auto result = Err<std::string>("Something went wrong");
 */
template<typename E>
struct ErrHelper {
    E error;
    explicit ErrHelper(E e) : error(std::move(e)) {}
};

template<typename E>
ErrHelper<E> Err(E error) {
    return ErrHelper<E>(std::move(error));
}

// ============================================================================
// Common error types for InputBridge
// ============================================================================

/**
 * @brief Enumeration of possible haptic device errors
 */
enum class HapticError {
    DeviceNotFound,
    InitializationFailed,
    UnsupportedEffect,
    EffectUploadFailed,
    SDLError,
    NotReady
};

/**
 * @brief Converts HapticError to string for logging
 */
inline const char* ToString(HapticError error) {
    switch (error) {
        case HapticError::DeviceNotFound: return "Device not found";
        case HapticError::InitializationFailed: return "Initialization failed";
        case HapticError::UnsupportedEffect: return "Unsupported effect";
        case HapticError::EffectUploadFailed: return "Effect upload failed";
        case HapticError::SDLError: return "SDL error";
        case HapticError::NotReady: return "Device not ready";
        default: return "Unknown error";
    }
}

/**
 * @brief Device-related errors
 */
enum class DeviceError {
    OpenFailed,
    InvalidInstanceID,
    UnsupportedType,
    AlreadyOpen,
    NotFound
};

inline const char* ToString(DeviceError error) {
    switch (error) {
        case DeviceError::OpenFailed: return "Failed to open device";
        case DeviceError::InvalidInstanceID: return "Invalid instance ID";
        case DeviceError::UnsupportedType: return "Unsupported device type";
        case DeviceError::AlreadyOpen: return "Device already open";
        case DeviceError::NotFound: return "Device not found";
        default: return "Unknown error";
    }
}

/**
 * @brief Network-related errors
 */
enum class NetworkError {
    BindFailed,
    ConnectionFailed,
    SendFailed,
    ReceiveFailed,
    InvalidAddress,
    Timeout
};

inline const char* ToString(NetworkError error) {
    switch (error) {
        case NetworkError::BindFailed: return "Failed to bind";
        case NetworkError::ConnectionFailed: return "Connection failed";
        case NetworkError::SendFailed: return "Send failed";
        case NetworkError::ReceiveFailed: return "Receive failed";
        case NetworkError::InvalidAddress: return "Invalid address";
        case NetworkError::Timeout: return "Operation timed out";
        default: return "Unknown error";
    }
}

// ============================================================================
// Usage examples
// ============================================================================

/*
// Example 1: Basic usage
Result<bool, HapticError> InitHaptic(SDL_Joystick* joystick) {
    if (!joystick) {
        return Result<bool, HapticError>::Err(HapticError::DeviceNotFound);
    }
    
    if (!SDL_IsJoystickHaptic(joystick)) {
        return Result<bool, HapticError>::Err(HapticError::UnsupportedEffect);
    }
    
    SDL_Haptic* haptic = SDL_OpenHapticFromJoystick(joystick);
    if (!haptic) {
        SDL_Log("SDL Error: %s", SDL_GetError());
        return Result<bool, HapticError>::Err(HapticError::SDLError);
    }
    
    return Result<bool, HapticError>::Ok(true);
}

// Example 2: Error handling at call site
auto result = InitHaptic(myJoystick);
if (result.IsOk()) {
    SDL_Log("Haptic initialized successfully");
} else {
    SDL_Log("Failed to initialize haptic: %s", ToString(result.Error()));
}

// Example 3: Chaining operations
auto result = InitHaptic(joystick)
    .AndThen([](bool) { return InitRumble(); })
    .AndThen([](bool) { return SetGain(100); })
    .Map([](bool success) { return success ? 1 : 0; });

if (result) {
    SDL_Log("All initialization steps succeeded");
}

// Example 4: Getting value with default
bool initialized = InitHaptic(joystick).ValueOr(false);
*/

} // namespace InputBridge
