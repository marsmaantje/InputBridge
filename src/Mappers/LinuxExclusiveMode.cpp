#include "LinuxExclusiveMode.h"

#ifdef __linux__
#include <filesystem>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

#ifndef HIDIOCGRAB
#define HIDIOCGRAB _IOW('H', 0x0C, int)
#endif

LinuxExclusiveMode::~LinuxExclusiveMode() {
    ReleaseAll();
}

void LinuxExclusiveMode::Apply(SDL_Joystick* joystick, bool enabled) {
    if (!enabled) {
        ReleaseAll();
        return;
    }
    ReleaseAll();
    Grab(joystick);
}

void LinuxExclusiveMode::ReleaseAll() {
    if (!m_GrabbedDeviceFds.empty()) {
        for (auto& pair : m_GrabbedDeviceFds) {
            ioctl(pair.first, EVIOCGRAB, 0);
            close(pair.first);
        }
        m_GrabbedDeviceFds.clear();
    }
}

void LinuxExclusiveMode::Grab(SDL_Joystick* joystick) {
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
        } catch (...) {
            SDL_Log("Could not find event devices for %s", path);
        }
    }
    else if (strncmp(path, "/dev/input/event", 16) == 0) {
        event_devices.push_back(path);
    }
    
    for (const auto& event_path : event_devices) {
        int fd = open(event_path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
            SDL_Log("EVIOCGRAB failed for %s: %s", event_path.c_str(), std::strerror(errno));
            close(fd);
            continue;
        }
        m_GrabbedDeviceFds.push_back({fd, event_path});
    }
}
#endif