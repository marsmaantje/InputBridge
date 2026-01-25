#include "Preferences.h"
#include <fstream>

std::map<std::string, std::string> g_DeviceVisualizerPrefs;
std::set<SDL_JoystickID> g_AppliedPreferences;
const char* CONFIG_FILENAME = "visualizer_prefs.ini";

void LoadPreferences() {
    std::ifstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string guid = line.substr(0, eqPos);
                std::string viz = line.substr(eqPos + 1);
                if (!guid.empty() && !viz.empty()) {
                    g_DeviceVisualizerPrefs[guid] = viz;
                }
            }
        }
        file.close();
    }
}

void SavePreferences() {
    std::ofstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        for (const auto& pair : g_DeviceVisualizerPrefs) {
            file << pair.first << "=" << pair.second << "\n";
        }
        file.close();
    }
}
