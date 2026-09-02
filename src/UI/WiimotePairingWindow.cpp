#include "WiimotePairingWindow.h"

#include "Bluetooth/WiimoteBluetoothPairing.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include <string>
#include <vector>

using namespace InputBridge::Bluetooth;

namespace {

bool s_Open = false;
bool s_ShouldOpenPopup = false;

// Function-local static equivalent state, defined below via GetPairing()
// so the backend (which, e.g. on Linux spins up a background D-Bus thread
// in its constructor - see LinuxWiimoteBluetoothPairing) is only ever
// constructed the first time the dialog is actually opened, not at
// process startup.
WiimotePairing &GetPairing() {
    static WiimotePairing pairing;
    return pairing;
}

std::vector<DiscoveredDevice> s_Devices;
bool s_Scanning = false;
std::string s_StatusText;

// Non-empty while a Pair/Connect click is outstanding for that device -
// separate trackers since PairDevice() and ConnectDevice() are visually
// distinct buttons that could (in principle) target different devices,
// even though the backend only actually runs one at a time.
std::string s_PairingAddress;
std::string s_ConnectingAddress;
std::string s_BusyName;

void UpsertDevice(const DiscoveredDevice &dev) {
    for (auto &d : s_Devices) {
        if (d.address == dev.address) { d = dev; return; }
    }
    s_Devices.push_back(dev);
}

void StartScan() {
    s_Devices.clear();
    s_StatusText.clear();
    if (!GetPairing().IsAvailable()) {
        s_StatusText = "Bluetooth pairing isn't available on this system.";
        return;
    }
    s_Scanning = true;
    GetPairing().StartDiscovery(
        [](const DiscoveredDevice &dev) { UpsertDevice(dev); },
        [](bool /*timed_out*/) { s_Scanning = false; });
}

} // namespace

void WiimotePairingWindow::Open() {
    s_Open = true;
    s_ShouldOpenPopup = true;
    StartScan();
}

void WiimotePairingWindow::Draw() {
    if (!s_Open) return;

    // Keep the backend's async callbacks flowing every frame the dialog
    // could be open - not just from inside BeginPopupModal below - so a
    // scan started right before the popup's first draw still delivers
    // results instead of sitting queued until some later frame.
    GetPairing().Pump();

    if (s_ShouldOpenPopup) {
        ImGui::OpenPopup("Pair Wiimote");
        s_ShouldOpenPopup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(440, 460), ImGuiCond_FirstUseEver);
    bool popup_open = true;
    if (ImGui::BeginPopupModal("Pair Wiimote", &popup_open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextDisabled("Hold SYNC under the battery cover, or press 1+2, to make a Wiimote discoverable.");
        if (ImGui::CollapsingHeader("How pairing works")) {
            ImGui::TextWrapped(
                "Hold the red SYNC button under the battery cover until it "
                "starts blinking, then wait for it to appear below. It stays "
                "discoverable for about 20 seconds.");
            ImGui::TextWrapped(
                "SYNC gives a permanent pairing - use \"Pair\" for it. "
                "1+2 only gives a temporary session that won't be remembered "
                "next time - use \"Connect\" for it instead, and note it may "
                "not work at all depending on your system's Bluetooth security "
                "settings (see the status message below if it fails).");
        }
        ImGui::Spacing();

        if (s_Scanning) {
            ImGui::TextDisabled("Scanning...");
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) {
                GetPairing().StopDiscovery();
            }
        } else {
            if (ImGui::Button("Scan Again")) {
                StartScan();
            }
        }

        if (!s_StatusText.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", s_StatusText.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::BeginChild("##WiimoteList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                           ImGuiChildFlags_Borders);
        if (s_Devices.empty()) {
            ImGui::TextDisabled(s_Scanning ? "Looking for Wiimotes nearby..." : "No devices found yet.");
        }
        for (auto &dev : s_Devices) {
            ImGui::PushID(dev.address.c_str());

            const std::string label = dev.name.empty() ? "(unnamed device)" : dev.name;
            const bool is_pairing_this = (s_PairingAddress == dev.address);
            const bool is_connecting_this = (s_ConnectingAddress == dev.address);
            const bool busy_elsewhere = (!s_PairingAddress.empty() && !is_pairing_this) ||
                                         (!s_ConnectingAddress.empty() && !is_connecting_this);

            if (!dev.looks_like_wiimote) ImGui::BeginDisabled();
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine();

            if (dev.already_paired) {
                ImGui::TextDisabled("Paired");
            } else if (is_pairing_this) {
                ImGui::TextDisabled("Pairing...");
            } else if (is_connecting_this) {
                ImGui::TextDisabled("Connecting...");
            } else {
                const std::string address = dev.address; // captured by value below

                if (busy_elsewhere) ImGui::BeginDisabled();
                if (ImGui::SmallButton("Pair")) {
                    s_PairingAddress = address;
                    s_BusyName = label;
                    s_StatusText.clear();
                    GetPairing().PairDevice(address, [address](PairResult result, const std::string &detail) {
                        if (s_PairingAddress == address) s_PairingAddress.clear();
                        if (result == PairResult::Success || result == PairResult::AlreadyPaired) {
                            s_StatusText = s_BusyName +
                                           ": paired! It should show up in your device list in a moment.";
                            for (auto &d : s_Devices) {
                                if (d.address == address) d.already_paired = true;
                            }
                        } else {
                            s_StatusText = s_BusyName + ": pairing failed (" + ToString(result) + ")" +
                                           (detail.empty() ? "." : (" - " + detail));
                        }
                    });
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Connect")) {
                    s_ConnectingAddress = address;
                    s_BusyName = label;
                    s_StatusText.clear();
                    GetPairing().ConnectDevice(address, [address](PairResult result, const std::string &detail) {
                        if (s_ConnectingAddress == address) s_ConnectingAddress.clear();
                        if (result == PairResult::Success || result == PairResult::AlreadyPaired) {
                            s_StatusText = s_BusyName +
                                           ": connected for this session. It should show up in your "
                                           "device list in a moment, but won't be remembered next time - "
                                           "hold 1+2 again if you need to reconnect it later.";
                        } else {
                            s_StatusText = s_BusyName + ": connect failed (" + ToString(result) + ")" +
                                           (detail.empty() ? "." : (" - " + detail));
                        }
                    });
                }
                if (busy_elsewhere) ImGui::EndDisabled();
            }
            if (!dev.looks_like_wiimote) ImGui::EndDisabled();

            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            popup_open = false;
        }
        if (!popup_open) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (!popup_open) {
        GetPairing().StopDiscovery();
        s_Open = false;
    }
}
