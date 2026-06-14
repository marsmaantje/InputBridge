#pragma once
#include <SDL3/SDL.h>
#include <memory>

namespace InputBridge {

static constexpr const char* kTag = "SDLHandles";

/**
 * @brief RAII wrapper for SDL resource handles with custom deleters
 * 
 * This template provides automatic resource management for SDL types,
 * ensuring resources are properly cleaned up even in the presence of exceptions.
 * 
 * @tparam T The SDL resource type (e.g., SDL_Haptic, SDL_Joystick)
 * @tparam Deleter Function pointer to the cleanup function
 * 
 * @example
 * HapticHandle haptic(SDL_OpenHapticFromJoystick(joystick));
 * // haptic is automatically closed when going out of scope
 */
template<typename T, void(*Deleter)(T*)>
class SDLHandle {
public:
    /**
     * @brief Default constructor creating an empty handle
     */
    SDLHandle() noexcept : m_handle(nullptr) {}
    
    /**
     * @brief Constructs handle taking ownership of an SDL resource
     * @param handle The SDL resource pointer (can be nullptr)
     */
    explicit SDLHandle(T* handle) noexcept : m_handle(handle) {}
    
    /**
     * @brief Destructor automatically calls the deleter if handle is valid
     */
    ~SDLHandle() {
        Reset();
    }
    
    // Delete copy operations - resources must have unique ownership
    SDLHandle(const SDLHandle&) = delete;
    SDLHandle& operator=(const SDLHandle&) = delete;
    
    /**
     * @brief Move constructor transfers ownership
     */
    SDLHandle(SDLHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }
    
    /**
     * @brief Move assignment operator
     */
    SDLHandle& operator=(SDLHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }
    
    /**
     * @brief Gets the raw handle without transferring ownership
     * @return Raw pointer to the SDL resource (can be nullptr)
     */
    T* Get() const noexcept { 
        return m_handle; 
    }
    
    /**
     * @brief Releases ownership of the handle without calling the deleter
     * @return The raw pointer (caller becomes responsible for cleanup)
     */
    T* Release() noexcept {
        T* temp = m_handle;
        m_handle = nullptr;
        return temp;
    }
    
    /**
     * @brief Replaces the managed resource
     * @param handle New handle to manage (can be nullptr)
     * 
     * If a resource is currently managed, it is properly cleaned up first.
     */
    void Reset(T* handle = nullptr) noexcept {
        if (m_handle && m_handle != handle) {
            Deleter(m_handle);
        }
        m_handle = handle;
    }
    
    /**
     * @brief Checks if the handle is valid (non-null)
     */
    explicit operator bool() const noexcept { 
        return m_handle != nullptr; 
    }
    
    /**
     * @brief Dereference operator (use with caution)
     * @warning Behavior is undefined if handle is null
     */
    T& operator*() const noexcept {
        return *m_handle;
    }
    
    /**
     * @brief Arrow operator for accessing handle members
     * @warning Behavior is undefined if handle is null
     */
    T* operator->() const noexcept {
        return m_handle;
    }
    
private:
    T* m_handle;
};

// ============================================================================
// Type aliases for common SDL resources
// ============================================================================

/**
 * @brief RAII wrapper for SDL_Haptic handles
 */
using HapticHandle = SDLHandle<SDL_Haptic, SDL_CloseHaptic>;

/**
 * @brief RAII wrapper for SDL_Joystick handles
 */
using JoystickHandle = SDLHandle<SDL_Joystick, SDL_CloseJoystick>;

/**
 * @brief RAII wrapper for SDL_Gamepad handles
 */
using GamepadHandle = SDLHandle<SDL_Gamepad, SDL_CloseGamepad>;

/**
 * @brief RAII wrapper for SDL_Window handles
 */
using WindowHandle = SDLHandle<SDL_Window, SDL_DestroyWindow>;

/**
 * @brief RAII wrapper for SDL_Renderer handles
 */
using RendererHandle = SDLHandle<SDL_Renderer, SDL_DestroyRenderer>;

// ============================================================================
// Example usage in HapticDevice class
// ============================================================================

/*
class HapticDevice {
public:
    HapticDevice(SDL_Joystick* joystick) {
        // joystick is non-owning reference, we don't close it
        m_joystick = joystick;
    }
    
    bool Init() {
        if (!m_joystick) return false;
        
        if (SDL_IsJoystickHaptic(m_joystick)) {
            // Take ownership of haptic handle - automatically cleaned up
            m_haptic.Reset(SDL_OpenHapticFromJoystick(m_joystick));
            
            if (!m_haptic) {
                LOG_WARN(kTag, "Failed to open haptic: %s", SDL_GetError());
                return false;
            }
            
            if (SDL_HapticRumbleSupported(m_haptic.Get())) {
                SDL_InitHapticRumble(m_haptic.Get());
            }
            
            return true;
        }
        return false;
    }
    
    void Close() {
        // Automatic cleanup when Reset() is called or when m_haptic is destroyed
        m_haptic.Reset();
    }
    
    bool IsReady() const {
        return static_cast<bool>(m_haptic);
    }
    
private:
    SDL_Joystick* m_joystick = nullptr;  // Non-owning
    HapticHandle m_haptic;               // RAII - owns the haptic handle
};
*/

// ============================================================================
// Shared ownership for SDL resources (if needed)
// ============================================================================

/**
 * @brief Custom deleter for use with std::shared_ptr
 * 
 * @example
 * std::shared_ptr<SDL_Haptic> haptic(
 *     SDL_OpenHapticFromJoystick(joystick),
 *     SDLDeleter<SDL_Haptic, SDL_CloseHaptic>()
 * );
 */
template<typename T, void(*Deleter)(T*)>
struct SDLDeleter {
    void operator()(T* ptr) const {
        if (ptr) {
            Deleter(ptr);
        }
    }
};

// Shared ownership type aliases
using SharedHaptic = std::shared_ptr<SDL_Haptic>;
using SharedJoystick = std::shared_ptr<SDL_Joystick>;
using SharedGamepad = std::shared_ptr<SDL_Gamepad>;

/**
 * @brief Creates a shared_ptr with proper SDL deleter
 * 
 * @example
 * auto haptic = MakeSharedSDL<SDL_Haptic, SDL_CloseHaptic>(
 *     SDL_OpenHapticFromJoystick(joystick)
 * );
 */
template<typename T, void(*Deleter)(T*)>
std::shared_ptr<T> MakeSharedSDL(T* ptr) {
    return std::shared_ptr<T>(ptr, SDLDeleter<T, Deleter>());
}

} // namespace InputBridge
