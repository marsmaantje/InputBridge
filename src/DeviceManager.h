#pragma once
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include "DeviceState.h"

extern std::vector<DeviceState> g_Devices;

std::string GetDeviceGUIDString(const DeviceState& dev);
void HandleDeviceAdded(SDL_JoystickID instance_id);
void HandleDeviceRemoved(SDL_JoystickID instance_id);
void CloseAllDevices();
