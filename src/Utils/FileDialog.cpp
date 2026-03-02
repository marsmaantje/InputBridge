#include "FileDialog.h"
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #include <commdlg.h>
#elif __APPLE__
    // macOS native dialog support could be added here using Cocoa
    // #include <Cocoa/Cocoa.h>
#elif __linux__
    // Linux native dialog support could use GTK or KDE dialogs
    // For now, we'll use the ImGui fallback
#endif

namespace fs = std::filesystem;

bool FileDialog::IsNativeDialogAvailable() {
#ifdef _WIN32
    return true; // Windows common dialogs are always available
#else
    return false; // Native dialogs not implemented for other platforms yet
#endif
}

bool FileDialog::Show(Type type, 
                      const std::string& title, 
                      const std::string& defaultPath,
                      const std::vector<std::pair<std::string, std::string>>& filters,
                      std::string& outPath) {
    
    if (IsNativeDialogAvailable()) {
        return ShowNativeDialog(type, title, defaultPath, filters, outPath);
    }
    
    // Fallback: ImGui-based browser will be used by the calling code
    return false;
}

bool FileDialog::ShowNativeDialog(Type type,
                                   const std::string& title,
                                   const std::string& defaultPath,
                                   const std::vector<std::pair<std::string, std::string>>& filters,
                                   std::string& outPath) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    
    // Initialize default path if provided
    if (!defaultPath.empty()) {
        strncpy_s(szFile, MAX_PATH, defaultPath.c_str(), _TRUNCATE);
    }
    
    // Build filter string (format: "Description\0*.ext\0Description2\0*.ext2\0\0")
    std::string filterStr;
    for (const auto& filter : filters) {
        filterStr += filter.first + '\0' + filter.second + '\0';
    }
    filterStr += '\0';
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL; // Could be set to SDL window handle if needed
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filterStr.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    
    // Set initial directory
    std::string initialDir = defaultPath;
    if (!initialDir.empty() && fs::is_regular_file(initialDir)) {
        initialDir = fs::path(initialDir).parent_path().string();
    }
    ofn.lpstrInitialDir = initialDir.empty() ? NULL : initialDir.c_str();
    
    // Set flags based on dialog type
    if (type == Type::Open) {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn)) {
            outPath = std::string(ofn.lpstrFile);
            return true;
        }
    } else {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&ofn)) {
            outPath = std::string(ofn.lpstrFile);
            
            // Add extension if not present
            if (!filters.empty() && fs::path(outPath).extension().empty()) {
                std::string ext = filters[ofn.nFilterIndex - 1].second;
                // Extract extension from pattern (e.g., "*.json" -> ".json")
                size_t dotPos = ext.find('.');
                if (dotPos != std::string::npos) {
                    ext = ext.substr(dotPos);
                    // Remove any wildcards
                    ext.erase(std::remove(ext.begin(), ext.end(), '*'), ext.end());
                    outPath += ext;
                }
            }
            return true;
        }
    }
    
    return false; // User cancelled
    
#else
    // Native dialogs not implemented for this platform
    return false;
#endif
}
