#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Network/WebSocketServer.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#endif

#ifdef __linux__
#include <filesystem>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifndef HIDIOCGRAB
#define HIDIOCGRAB _IOW('H', 0x0C, int)
#endif
#endif

#ifdef __APPLE__
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#ifndef kIOMainPortDefault
#define kIOMainPortDefault kIOMasterPortDefault
#endif
#endif

namespace { // Anonymous namespace for private helper functions

SDL_Joystick *GetSelectedJoystick(SDL_JoystickID selectedId,
                                  const DeviceManager &deviceManager) {
    if (selectedId == 0) {
        return nullptr;
    }
    const auto &devices = deviceManager.GetDevices();
    auto it = std::find_if(devices.begin(), devices.end(),
                           [selectedId](const DeviceState &dev) {
                               return dev.instance_id == selectedId;
                           });

    return (it != devices.end()) ? it->joystick : nullptr;
}

} // namespace

InputMapper::InputMapper(const DeviceManager &deviceManager)
    : m_DeviceManager(deviceManager) {}

static void DrawAxisConfig(const char *label, InputMapper::AxisConfig &config,
                           int numAxes) {
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::BeginCombo("##axis",
                          config.axisIndex == -1
                              ? "None"
                              : std::to_string(config.axisIndex).c_str())) {
        if (ImGui::Selectable("None", config.axisIndex == -1))
            config.axisIndex = -1;
        for (int i = 0; i < numAxes; i++) {
            if (ImGui::Selectable(std::to_string(i).c_str(),
                                  config.axisIndex == i))
                config.axisIndex = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Inv", &config.invert);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("Deadzone", &config.deadzone, 0.0f, 0.5f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char *ranges[] = {"-1..1", "0..1", "-1..0"};
    ImGui::Combo("Range", &config.outputRange, ranges, IM_ARRAYSIZE(ranges));
    ImGui::PopID();
}

InputMapper::~InputMapper() {
#ifdef __linux__
    for (auto& pair : m_GrabbedDeviceFds) {
        ioctl(pair.first, EVIOCGRAB, 0);
        close(pair.first);
    }
    m_GrabbedDeviceFds.clear();
#endif

#ifdef _WIN32
    if (m_WindowsDIDevice) {
        m_WindowsDIDevice->Unacquire();
        m_WindowsDIDevice->Release();
    }
    if (m_WindowsDIInterface) {
        m_WindowsDIInterface->Release();
    }
#endif

#ifdef __APPLE__
    if (m_MacOSHIDDevice) {
        IOHIDDeviceClose(m_MacOSHIDDevice, kIOHIDOptionsTypeSeizeDevice);
        CFRelease(m_MacOSHIDDevice);
    }
#endif
}

void InputMapper::DrawUI() {
    ImGui::Begin("Input Mapper");

    const auto &devices = m_DeviceManager.GetDevices();
    const DeviceState *selectedDeviceState = nullptr;

    // Device Selector
    const char *currentDeviceName = "None";
    if (m_SelectedDeviceID != 0) {
        auto it = std::find_if(devices.begin(), devices.end(),
                               [this](const DeviceState &dev) {
                                   return dev.instance_id == m_SelectedDeviceID;
                               });
        if (it != devices.end()) {
            selectedDeviceState = &*it;
            currentDeviceName = selectedDeviceState->name.c_str();
        } else {
            m_SelectedDeviceID = 0; // Device was disconnected
        }
    }

    if (ImGui::BeginCombo("Source Device", currentDeviceName)) {
        if (ImGui::Selectable("None", m_SelectedDeviceID == 0))
            m_SelectedDeviceID = 0;
        for (const auto &dev : devices) {
            bool isSelected = (m_SelectedDeviceID == dev.instance_id);
            std::string label =
                dev.name + "##" + std::to_string(dev.instance_id);
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                m_SelectedDeviceID = dev.instance_id;
                selectedDeviceState = &dev;
                ApplyExclusiveMode();
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Exclusive Mode (Hide from other apps)", &m_ExclusiveMode)) {
        ApplyExclusiveMode();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Attempts to prevent other applications from receiving input from this device.\n"
                          "Note: This is platform dependent and may require administrative privileges.");
    }

    if (selectedDeviceState) {
        ImGui::Separator();
        ImGui::Text("Axis Mapping");
        DrawAxisConfig("Steering", m_Steering, selectedDeviceState->num_axes);
        DrawAxisConfig("Throttle", m_Throttle, selectedDeviceState->num_axes);
        DrawAxisConfig("Brake", m_Brake, selectedDeviceState->num_axes);
        DrawAxisConfig("Clutch", m_Clutch, selectedDeviceState->num_axes);
        DrawAxisConfig("Handbrake", m_Handbrake, selectedDeviceState->num_axes);
    }

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    std::string outputPreview = UpdateAndBroadcastMessage();
    ImGui::TextWrapped("%s", outputPreview.c_str());

    ImGui::End();

    WebSocketServer::GetInstance().DrawUI();
}

float InputMapper::ProcessAxis(SDL_Joystick *joystick,
                               const AxisConfig &config) {
    if (config.axisIndex < 0)
        return 0.0f;

    Sint16 val = SDL_GetJoystickAxis(joystick, config.axisIndex);
    float norm;
    if (val < 0) {
        norm = static_cast<float>(val) / 32768.0f;
    } else {
        norm = static_cast<float>(val) / 32767.0f;
    }

    if (config.invert)
        norm = -norm;

    if (std::abs(norm) < config.deadzone)
        norm = 0.0f;

    float result = std::clamp(norm, -1.0f, 1.0f);
    if (config.outputRange == 1) { // 0 to 1
        result = (result + 1.0f) * 0.5f;
    } else if (config.outputRange == 2) { // -1 to 0
        result = (result - 1.0f) * 0.5f;
    }
    return result;
}

std::string InputMapper::UpdateAndBroadcastMessage() {
    SDL_Joystick *joystick =
        GetSelectedJoystick(m_SelectedDeviceID, m_DeviceManager);

    if (!joystick)
        return "";

    float steering = ProcessAxis(joystick, m_Steering);
    float throttle = ProcessAxis(joystick, m_Throttle);
    float brake = ProcessAxis(joystick, m_Brake);
    float clutch = ProcessAxis(joystick, m_Clutch);
    float handbrake = ProcessAxis(joystick, m_Handbrake);

    auto &server = WebSocketServer::GetInstance();
    server.Broadcast_wheel(steering, brake, throttle);

    server.Broadcast("/wheel/steer", steering);
    server.Broadcast("/wheel/throttle", throttle);
    server.Broadcast("/wheel/brake", brake);
    if (m_Clutch.axisIndex != -1)
        server.Broadcast("/wheel/clutch", clutch);
    server.Broadcast("/wheel/handbrake", handbrake);

    int buttons_mask = 0;
    int num_buttons = SDL_GetNumJoystickButtons(joystick);
    for (int i = 0; i < num_buttons && i < 32; ++i) {
        if (SDL_GetJoystickButton(joystick, i))
            buttons_mask |= (1 << i);
    }
    server.Broadcast("/wheel/buttons", buttons_mask);

    return "Broadcasting...";
}

void InputMapper::LoadConfig(const PreferencesManager &prefs) {
    std::string deviceGUID = prefs.GetString("InputMapper.DeviceGUID");
    if (!deviceGUID.empty()) {
        const auto &devices = m_DeviceManager.GetDevices();
        for (const auto &dev : devices) {
            if (DeviceManager::GetDeviceGUIDString(dev) == deviceGUID) {
                m_SelectedDeviceID = dev.instance_id;
                break;
            }
        }
    }

    auto LoadAxis = [&](const char *prefix, AxisConfig &config) {
        config.axisIndex = prefs.GetInt(std::string(prefix) + ".Axis", -1);
        config.invert = prefs.GetBool(std::string(prefix) + ".Invert", false);
        config.deadzone =
            prefs.GetFloat(std::string(prefix) + ".Deadzone", 0.05f);
        config.outputRange = prefs.GetInt(std::string(prefix) + ".Range", 0);
    };

    LoadAxis("InputMapper.Steering", m_Steering);
    LoadAxis("InputMapper.Throttle", m_Throttle);
    LoadAxis("InputMapper.Brake", m_Brake);
    LoadAxis("InputMapper.Clutch", m_Clutch);
    LoadAxis("InputMapper.Handbrake", m_Handbrake);
    m_ExclusiveMode = prefs.GetBool("InputMapper.ExclusiveMode", false);

#ifdef ENABLE_WEBSOCKETS
    int wsPort = prefs.GetInt("WebSocketServer.Port", 9001);
    if (!WebSocketServer::GetInstance().IsRunning() ||
        WebSocketServer::GetInstance().GetPort() != wsPort) {
        WebSocketServer::GetInstance().Stop();
        WebSocketServer::GetInstance().Start(wsPort);
    }
#endif

    ApplyExclusiveMode();
}

void InputMapper::SaveConfig(PreferencesManager &prefs) const {
    if (m_SelectedDeviceID != 0) {
        const auto &devices = m_DeviceManager.GetDevices();
        for (const auto &dev : devices) {
            if (dev.instance_id == m_SelectedDeviceID) {
                prefs.SetString("InputMapper.DeviceGUID",
                                DeviceManager::GetDeviceGUIDString(dev));
                break;
            }
        }
    } else {
        prefs.SetString("InputMapper.DeviceGUID", "");
    }

    auto SaveAxis = [&](const char *prefix, const AxisConfig &config) {
        prefs.SetInt(std::string(prefix) + ".Axis", config.axisIndex);
        prefs.SetBool(std::string(prefix) + ".Invert", config.invert);
        prefs.SetFloat(std::string(prefix) + ".Deadzone", config.deadzone);
        prefs.SetInt(std::string(prefix) + ".Range", config.outputRange);
    };

    SaveAxis("InputMapper.Steering", m_Steering);
    SaveAxis("InputMapper.Throttle", m_Throttle);
    SaveAxis("InputMapper.Brake", m_Brake);
    SaveAxis("InputMapper.Clutch", m_Clutch);
    SaveAxis("InputMapper.Handbrake", m_Handbrake);
    prefs.SetBool("InputMapper.ExclusiveMode", m_ExclusiveMode);

#ifdef ENABLE_WEBSOCKETS
    prefs.SetInt("WebSocketServer.Port",
                 WebSocketServer::GetInstance().GetPort());
#endif
}

void InputMapper::ApplyExclusiveMode() {
    SDL_Joystick *joystick = GetSelectedJoystick(m_SelectedDeviceID, m_DeviceManager);
    if (!joystick)
        return;

#ifdef _WIN32
    ApplyExclusiveModeWindows(joystick);
#elif defined(__APPLE__)
    ApplyExclusiveModeMacOS(joystick);
#elif defined(__linux__)
    ApplyExclusiveModeLinux(joystick);
#else
    SDL_Log("Exclusive mode not implemented on this platform");
#endif
}

#ifdef __linux__
void InputMapper::ApplyExclusiveModeLinux(SDL_Joystick *joystick) {
    const char *path = SDL_GetJoystickPath(joystick);
    if (!path) {
        SDL_Log("Could not get joystick path for exclusive mode.");
        return;
    }
    
    SDL_Log("SDL provided path: %s", path);
    
    std::vector<std::string> event_devices;
    
    // Handle hidraw devices - find corresponding event devices
    if (strncmp(path, "/dev/hidraw", 11) == 0) {
        SDL_Log("Device is hidraw, searching for event devices...");
        
        int hidraw_num = atoi(path + 11);
        char sys_device_path[512];
        snprintf(sys_device_path, sizeof(sys_device_path), 
                 "/sys/class/hidraw/hidraw%d/device", hidraw_num);
        
        try {
            std::filesystem::path device_path = std::filesystem::read_symlink(sys_device_path);
            SDL_Log("HID device path: %s", device_path.c_str());
            
            std::filesystem::path abs_device_path = std::filesystem::canonical(
                std::filesystem::path(sys_device_path).parent_path() / device_path
            );
            
            SDL_Log("Absolute device path: %s", abs_device_path.c_str());
            
            std::vector<std::filesystem::path> search_paths = {
                abs_device_path / "input",
                abs_device_path
            };
            
            for (const auto& search_path : search_paths) {
                if (!std::filesystem::exists(search_path)) {
                    continue;
                }
                
                for (const auto &entry : std::filesystem::directory_iterator(search_path)) {
                    std::string filename = entry.path().filename().string();
                    
                    if (filename.find("input") == 0 && filename.length() > 5) {
                        std::filesystem::path input_dir = entry.path();
                        
                        if (std::filesystem::is_directory(input_dir)) {
                            for (const auto &event_entry : std::filesystem::directory_iterator(input_dir)) {
                                std::string event_filename = event_entry.path().filename().string();
                                if (event_filename.find("event") == 0 && isdigit(event_filename[5])) {
                                    std::string event_path = "/dev/input/" + event_filename;
                                    event_devices.push_back(event_path);
                                    SDL_Log("Found event device: %s", event_path.c_str());
                                }
                            }
                        }
                    }
                    else if (filename.find("event") == 0 && isdigit(filename[5])) {
                        std::string event_path = "/dev/input/" + filename;
                        event_devices.push_back(event_path);
                        SDL_Log("Found event device: %s", event_path.c_str());
                    }
                }
            }
            
            if (event_devices.empty()) {
                SDL_Log("No event devices found for hidraw%d", hidraw_num);
                return;
            }
            
        } catch (const std::exception& e) {
            SDL_Log("Error finding event devices: %s", e.what());
            return;
        }
    }
    else if (strncmp(path, "/dev/input/js", 13) == 0) {
        SDL_Log("Device is joystick, searching for event devices...");
        
        int js_num = atoi(path + 13);
        char sys_path[256];
        snprintf(sys_path, sizeof(sys_path), "/sys/class/input/js%d/device", js_num);
        
        try {
            for (const auto &entry : std::filesystem::directory_iterator(sys_path)) {
                std::string filename = entry.path().filename().string();
                if (filename.find("event") == 0 && isdigit(filename[5])) {
                    std::string event_path = "/dev/input/" + filename;
                    event_devices.push_back(event_path);
                    SDL_Log("Found event device: %s", event_path.c_str());
                }
            }
            
            if (event_devices.empty()) {
                SDL_Log("No event devices found for js%d", js_num);
                return;
            }
        } catch (...) {
            SDL_Log("Could not find event devices for %s", path);
            return;
        }
    }
    else if (strncmp(path, "/dev/input/event", 16) == 0) {
        event_devices.push_back(path);
    }
    else {
        SDL_Log("Unknown device type: %s", path);
        return;
    }
    
    SDL_Log("Found %zu event device(s) total", event_devices.size());
    
    // If we're turning OFF exclusive mode, ungrab all devices
    if (!m_ExclusiveMode && !m_GrabbedDeviceFds.empty()) {
        SDL_Log("Releasing grab on %zu device(s)", m_GrabbedDeviceFds.size());
        
        for (auto& pair : m_GrabbedDeviceFds) {
            int fd = pair.first;
            const std::string& dev_path = pair.second;
            
            int res = ioctl(fd, EVIOCGRAB, 0);
            
            if (res < 0) {
                SDL_Log("EVIOCGRAB (release) failed for %s: %s", dev_path.c_str(), std::strerror(errno));
            } else {
                SDL_Log("Successfully released %s", dev_path.c_str());
            }
            
            close(fd);
        }
        
        m_GrabbedDeviceFds.clear();
        return;
    }
    
    // If we're turning ON exclusive mode
    if (m_ExclusiveMode) {
        if (!m_GrabbedDeviceFds.empty()) {
            SDL_Log("Closing previously grabbed devices");
            for (auto& pair : m_GrabbedDeviceFds) {
                ioctl(pair.first, EVIOCGRAB, 0);
                close(pair.first);
            }
            m_GrabbedDeviceFds.clear();
        }
        
        for (const auto& event_path : event_devices) {
            SDL_Log("Attempting to grab: %s", event_path.c_str());
            
            int fd = open(event_path.c_str(), O_RDWR | O_NONBLOCK);
            if (fd < 0) {
                SDL_Log("Could not open %s: %s", event_path.c_str(), std::strerror(errno));
                continue;
            }
            
            char name[256] = "Unknown";
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
                SDL_Log("Device name: %s", name);
            }
            
            int res = ioctl(fd, EVIOCGRAB, 1);
            
            if (res < 0) {
                SDL_Log("EVIOCGRAB failed for %s: %s", event_path.c_str(), std::strerror(errno));
                close(fd);
                continue;
            }
            
            SDL_Log("Successfully grabbed %s (fd=%d)", event_path.c_str(), fd);
            m_GrabbedDeviceFds.push_back({fd, event_path});
        }
        
        if (m_GrabbedDeviceFds.empty()) {
            SDL_Log("Failed to grab any event devices!");
        } else {
            SDL_Log("Exclusive mode enabled - grabbed %zu event device(s)", m_GrabbedDeviceFds.size());
        }
    }
}
#endif

#ifdef _WIN32
void InputMapper::ApplyExclusiveModeWindows(SDL_Joystick *joystick) {
    // On Windows, we need to use DirectInput for exclusive mode
    // SDL uses various backends (XInput, DirectInput, WinMM), so this is complex
    
    // Get the instance ID to track which device we're working with
    SDL_JoystickID instance_id = SDL_GetJoystickID(joystick);
    
    if (m_ExclusiveMode) {
        // We need to acquire the device in exclusive mode using DirectInput
        // This requires getting the device GUID from SDL
        
        SDL_GUID guid = SDL_GetJoystickGUID(joystick);
        char guid_str[64];
        SDL_GetJoystickGUIDString(guid, guid_str, sizeof(guid_str));
        
        SDL_Log("Attempting to set exclusive mode for device: %s", SDL_GetJoystickName(joystick));
        SDL_Log("Device GUID: %s", guid_str);
        
        // Create DirectInput device
        LPDIRECTINPUT8 di_interface = nullptr;
        HRESULT hr = DirectInput8Create(
            GetModuleHandle(nullptr),
            DIRECTINPUT_VERSION,
            IID_IDirectInput8,
            (LPVOID*)&di_interface,
            nullptr
        );
        
        if (FAILED(hr)) {
            SDL_Log("Failed to create DirectInput8 interface: 0x%lx", hr);
            return;
        }
        
        // Convert SDL GUID to DirectInput GUID
        GUID di_guid;
        if (!ConvertSDLGUIDToDirectInputGUID(guid, &di_guid)) {
            SDL_Log("Failed to convert SDL GUID to DirectInput GUID");
            di_interface->Release();
            return;
        }
        
        // Create the device
        LPDIRECTINPUTDEVICE8 di_device = nullptr;
        hr = di_interface->CreateDevice(di_guid, &di_device, nullptr);
        
        if (FAILED(hr)) {
            SDL_Log("Failed to create DirectInput device: 0x%lx", hr);
            di_interface->Release();
            return;
        }
        
        // Set data format (joystick)
        hr = di_device->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) {
            SDL_Log("Failed to set data format: 0x%lx", hr);
            di_device->Release();
            di_interface->Release();
            return;
        }
        
        // Get the window handle (you'll need to provide this - SDL can help)
        HWND hwnd = GetActiveWindow(); // Or get from SDL: SDL_GetProperty(SDL_GetWindowProperties(window), "SDL.window.win32.hwnd", NULL)
        
        // Set cooperative level to EXCLUSIVE + FOREGROUND
        hr = di_device->SetCooperativeLevel(
            hwnd,
            DISCL_EXCLUSIVE | DISCL_FOREGROUND
        );
        
        if (FAILED(hr)) {
            SDL_Log("Failed to set cooperative level: 0x%lx", hr);
            di_device->Release();
            di_interface->Release();
            return;
        }
        
        // Acquire the device
        hr = di_device->Acquire();
        if (FAILED(hr)) {
            SDL_Log("Failed to acquire device in exclusive mode: 0x%lx", hr);
            di_device->Release();
            di_interface->Release();
            return;
        }
        
        SDL_Log("Successfully set exclusive mode for device");
        
        // Store the DirectInput device and interface for later cleanup
        m_WindowsDIDevice = di_device;
        m_WindowsDIInterface = di_interface;
    }
    else {
        // Release exclusive mode
        if (m_WindowsDIDevice) {
            SDL_Log("Releasing exclusive mode");
            
            m_WindowsDIDevice->Unacquire();
            m_WindowsDIDevice->Release();
            m_WindowsDIDevice = nullptr;
        }
        
        if (m_WindowsDIInterface) {
            m_WindowsDIInterface->Release();
            m_WindowsDIInterface = nullptr;
        }
    }
}

// Helper function to convert SDL GUID to DirectInput GUID
bool InputMapper::ConvertSDLGUIDToDirectInputGUID(SDL_GUID sdl_guid, GUID* di_guid) {
    // SDL GUIDs for DirectInput devices on Windows have a specific format
    // The GUID is stored in the first 16 bytes
    
    Uint8 data[16];
    
    // Get raw GUID data
    memcpy(data, sdl_guid.data, 16);
    
    // Check if this is a DirectInput device (starts with specific pattern)
    // DirectInput devices on Windows typically have the product/vendor ID in specific positions
    
    // For now, we'll try to use the raw data as-is
    // This may need adjustment based on actual SDL GUID format
    di_guid->Data1 = *(DWORD*)&data[0];
    di_guid->Data2 = *(WORD*)&data[4];
    di_guid->Data3 = *(WORD*)&data[6];
    memcpy(di_guid->Data4, &data[8], 8);
    
    return true;
}
#endif

#ifdef __APPLE__
void InputMapper::ApplyExclusiveModeMacOS(SDL_Joystick *joystick) {
    // On macOS, we use IOKit to set exclusive access
    
    SDL_Log("Attempting to set exclusive mode on macOS for device: %s", SDL_GetJoystickName(joystick));
    
    if (m_ExclusiveMode) {
        // Get the IOHIDDevice from SDL (this requires diving into SDL internals or using IOKit directly)
        // SDL doesn't expose the IOHIDDevice directly, so we need to find it via IOKit
        
        // Get device product and vendor IDs
        Uint16 vendor = SDL_GetJoystickVendor(joystick);
        Uint16 product = SDL_GetJoystickProduct(joystick);
        
        SDL_Log("Looking for device with VendorID: 0x%04X, ProductID: 0x%04X", vendor, product);
        
        // Create a matching dictionary
        CFMutableDictionaryRef matching_dict = IOServiceMatching(kIOHIDDeviceKey);
        if (!matching_dict) {
            SDL_Log("Failed to create matching dictionary");
            return;
        }
        
        // Add vendor and product ID to matching criteria
        CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &vendor);
        CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &product);
        
        CFDictionarySetValue(matching_dict, CFSTR(kIOHIDVendorIDKey), vendor_ref);
        CFDictionarySetValue(matching_dict, CFSTR(kIOHIDProductIDKey), product_ref);
        
        CFRelease(vendor_ref);
        CFRelease(product_ref);
        
        // Get matching services
        io_iterator_t iterator = 0;
        kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault, matching_dict, &iterator);
        
        if (result != KERN_SUCCESS) {
            SDL_Log("Failed to get matching services: %d", result);
            return;
        }
        
        // Find the device
        io_object_t device_obj;
        IOHIDDeviceRef hid_device = nullptr;
        
        while ((device_obj = IOIteratorNext(iterator))) {
            // Create IOHIDDevice
            hid_device = IOHIDDeviceCreate(kCFAllocatorDefault, device_obj);
            
            if (hid_device) {
                // Try to open with exclusive access
                IOReturn ret = IOHIDDeviceOpen(hid_device, kIOHIDOptionsTypeSeizeDevice);
                
                if (ret == kIOReturnSuccess) {
                    SDL_Log("Successfully opened device in exclusive mode");
                    m_MacOSHIDDevice = hid_device;
                    IOObjectRelease(device_obj);
                    break;
                } else {
                    SDL_Log("Failed to open device exclusively: 0x%08x", ret);
                    CFRelease(hid_device);
                    hid_device = nullptr;
                }
            }
            
            IOObjectRelease(device_obj);
        }
        
        IOObjectRelease(iterator);
        
        if (!m_MacOSHIDDevice) {
            SDL_Log("Failed to find or open device in exclusive mode");
        }
    }
    else {
        // Release exclusive mode
        if (m_MacOSHIDDevice) {
            SDL_Log("Releasing exclusive mode on macOS");
            IOHIDDeviceClose(m_MacOSHIDDevice, kIOHIDOptionsTypeSeizeDevice);
            CFRelease(m_MacOSHIDDevice);
            m_MacOSHIDDevice = nullptr;
        }
    }
}
#endif