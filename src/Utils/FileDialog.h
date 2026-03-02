#pragma once

#include <string>
#include <vector>

/**
 * FileDialog
 * 
 * Cross-platform utility for native file dialogs.
 * Falls back to ImGui-based browser if native dialogs are unavailable.
 */
class FileDialog {
public:
    enum class Type {
        Open,
        Save
    };

    /**
     * Opens a native file dialog (or falls back to ImGui browser).
     * 
     * @param type Type of dialog (Open or Save)
     * @param title Dialog window title
     * @param defaultPath Default directory or file path
     * @param filters File type filters (e.g., {{"JSON Files", "*.json"}, {"All Files", "*.*"}})
     * @param outPath Output path selected by user
     * @return true if user selected a file, false if cancelled
     */
    static bool Show(Type type, 
                     const std::string& title, 
                     const std::string& defaultPath,
                     const std::vector<std::pair<std::string, std::string>>& filters,
                     std::string& outPath);

    /**
     * Check if native dialogs are available on this platform.
     */
    static bool IsNativeDialogAvailable();

private:
    static bool ShowNativeDialog(Type type,
                                  const std::string& title,
                                  const std::string& defaultPath,
                                  const std::vector<std::pair<std::string, std::string>>& filters,
                                  std::string& outPath);
};
