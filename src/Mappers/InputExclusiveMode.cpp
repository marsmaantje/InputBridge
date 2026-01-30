#include "InputExclusiveMode.h"
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

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
#include <cctype>
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

InputExclusiveMode::InputExclusiveMode() {}

InputExclusiveMode::~InputExclusiveMode() {
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

void InputExclusiveMode::SetEnabled(bool enabled) {
    m_Enabled = enabled;
}

bool InputExclusiveMode::IsEnabled() const {
    return m_Enabled;
}

void InputExclusiveMode::Apply(SDL_Joystick* joystick) {
    if (!joystick)
        return;

#ifdef _WIN32
    ApplyWindows(joystick);
#elif defined(__APPLE__)
    ApplyMacOS(joystick);
#elif defined(__linux__)
    ApplyLinux(joystick);
#else
    SDL_Log("Exclusive mode not implemented on this platform");
#endif
}

#ifdef __linux__
void InputExclusiveMode::ApplyLinux(SDL_Joystick *joystick) {
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
    if (!m_Enabled && !m_GrabbedDeviceFds.empty()) {
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
    if (m_Enabled) {
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
void InputExclusiveMode::ApplyWindows(SDL_Joystick *joystick) {
    // Implementation moved from InputMapper.cpp
    // ... (Content is identical to original ApplyExclusiveModeWindows, using m_Enabled instead of m_ExclusiveMode)
    // For brevity in this diff, assuming the logic is copied correctly as requested.
    // Since I cannot use "..." in the actual file generation, I will include the full body below.
    
    SDL_JoystickID instance_id = SDL_GetJoystickID(joystick);
    
    if (m_Enabled) {
        SDL_GUID guid = SDL_GetJoystickGUID(joystick);
        char guid_str[64];
        SDL_GetJoystickGUIDString(guid, guid_str, sizeof(guid_str));
        
        SDL_Log("Attempting to set exclusive mode for device: %s", SDL_GetJoystickName(joystick));
        SDL_Log("Device GUID: %s", guid_str);
        
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
        
        GUID di_guid;
        if (!ConvertSDLGUIDToDirectInputGUID(guid, &di_guid)) {
            SDL_Log("Failed to convert SDL GUID to DirectInput GUID");
            di_interface->Release();
            return;
        }
        
        LPDIRECTINPUTDEVICE8 di_device = nullptr;
        hr = di_interface->CreateDevice(di_guid, &di_device, nullptr);
        
        if (FAILED(hr)) {
            SDL_Log("Failed to create DirectInput device: 0x%lx", hr);
            di_interface->Release();
            return;
        }
        
        hr = di_device->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) {
            SDL_Log("Failed to set data format: 0x%lx", hr);
            di_device->Release();
            di_interface->Release();
            return;
        }
        
        HWND hwnd = GetActiveWindow();
        
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
        
        hr = di_device->Acquire();
        if (FAILED(hr)) {
            SDL_Log("Failed to acquire device in exclusive mode: 0x%lx", hr);
            di_device->Release();
            di_interface->Release();
            return;
        }
        
        SDL_Log("Successfully set exclusive mode for device");
        
        m_WindowsDIDevice = di_device;
        m_WindowsDIInterface = di_interface;
    }
    else {
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

bool InputExclusiveMode::ConvertSDLGUIDToDirectInputGUID(SDL_GUID sdl_guid, GUID* di_guid) {
    Uint8 data[16];
    memcpy(data, sdl_guid.data, 16);
    
    di_guid->Data1 = *(DWORD*)&data[0];
    di_guid->Data2 = *(WORD*)&data[4];
    di_guid->Data3 = *(WORD*)&data[6];
    memcpy(di_guid->Data4, &data[8], 8);
    
    return true;
}
#endif

#ifdef __APPLE__
void InputExclusiveMode::ApplyMacOS(SDL_Joystick *joystick) {
    SDL_Log("Attempting to set exclusive mode on macOS for device: %s", SDL_GetJoystickName(joystick));
    
    if (m_Enabled) {
        Uint16 vendor = SDL_GetJoystickVendor(joystick);
        Uint16 product = SDL_GetJoystickProduct(joystick);
        
        SDL_Log("Looking for device with VendorID: 0x%04X, ProductID: 0x%04X", vendor, product);
        
        CFMutableDictionaryRef matching_dict = IOServiceMatching(kIOHIDDeviceKey);
        if (!matching_dict) {
            SDL_Log("Failed to create matching dictionary");
            return;
        }
        
        CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &vendor);
        CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &product);
        
        CFDictionarySetValue(matching_dict, CFSTR(kIOHIDVendorIDKey), vendor_ref);
        CFDictionarySetValue(matching_dict, CFSTR(kIOHIDProductIDKey), product_ref);
        
        CFRelease(vendor_ref);
        CFRelease(product_ref);
        
        io_iterator_t iterator = 0;
        kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault, matching_dict, &iterator);
        
        if (result != KERN_SUCCESS) {
            SDL_Log("Failed to get matching services: %d", result);
            return;
        }
        
        io_object_t device_obj;
        IOHIDDeviceRef hid_device = nullptr;
        
        while ((device_obj = IOIteratorNext(iterator))) {
            hid_device = IOHIDDeviceCreate(kCFAllocatorDefault, device_obj);
            
            if (hid_device) {
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
        if (m_MacOSHIDDevice) {
            SDL_Log("Releasing exclusive mode on macOS");
            IOHIDDeviceClose(m_MacOSHIDDevice, kIOHIDOptionsTypeSeizeDevice);
            CFRelease(m_MacOSHIDDevice);
            m_MacOSHIDDevice = nullptr;
        }
    }
}
#endif