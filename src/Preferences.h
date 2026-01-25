#pragma once
#include <string>
#include <map>
#include <set>
#include <SDL3/SDL.h>

extern std::map<std::string, std::string> g_DeviceVisualizerPrefs;
extern std::set<SDL_JoystickID> g_AppliedPreferences;

void LoadPreferences();
void SavePreferences();
