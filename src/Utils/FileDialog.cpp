#include "FileDialog.h"

// Native file dialogs have been removed in favour of the built-in ImGui
// file browser (DrawFileBrowser in ProtocolEditorWindow.cpp), which works
// consistently across all platforms without any platform-specific code or
// COM initialisation requirements.

bool FileDialog::IsNativeDialogAvailable() {
    return false;
}

bool FileDialog::Show(Type type,
                      const std::string& title,
                      const std::string& defaultPath,
                      const std::vector<std::pair<std::string, std::string>>& filters,
                      std::string& outPath) {
    // Always fall back to the ImGui browser in the calling code.
    (void)type; (void)title; (void)defaultPath; (void)filters; (void)outPath;
    return false;
}

bool FileDialog::ShowNativeDialog(Type type,
                                   const std::string& title,
                                   const std::string& defaultPath,
                                   const std::vector<std::pair<std::string, std::string>>& filters,
                                   std::string& outPath) {
    (void)type; (void)title; (void)defaultPath; (void)filters; (void)outPath;
    return false;
}
