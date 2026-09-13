#include "SidebarLayout.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include "Devices/DeviceManager.h"
#include "Devices/VirtualDeviceManager.h"
#include "Mappers/InputMapper.h"
#include "Mappers/OutputMapper.h"
#include "Network/NetworkStatusWindow.h"
#include "Protocols/ProtocolEditorWindow.h"
#include "UI/AboutWindow.h"
#include "UI/DebugLogPanel.h"
#include "UI/DevicePanel.h"
#include "UI/IconsFontAwesome6.h"
#include "UI/SettingsPanel.h"

#if defined(__linux__)
#include "Devices/Wiimote/WiimoteManager.h"
#include "Devices/Wiimote/Linux/LinuxUdevInstaller.h"
#include <chrono>
#include <future>
#endif

#include <string>
#include <vector>

// -- Persistent sidebar state -------------------------------------------------
// Section IDs: 0=Devices  1=Input  2=Output  3=Network
//              4=Protocols  5=Settings  6=About  7=DebugLog
static int   g_ActiveSection   = 0;
static bool  g_SidebarExpanded = true;
static float g_SidebarW        = 0.0f; // 0 = initialise from SIDEBAR_W_FULL on first frame

// Battery-LED toggle is shared between the Devices section (applies LEDs) and
// the Settings panel (hosts the checkbox), so it lives at file scope.
static bool  s_EnableBatteryLED  = true;
static bool  s_BatteryLEDLoaded  = false;
static bool  s_DisableGamepadNav = false;
static bool  s_GamepadNavLoaded  = false;
static bool  s_DisableKeyboardNav = false;
static bool  s_KeyboardNavLoaded  = false;
static bool  s_BatteryIntervalLoaded = false;

#if defined(__linux__)
// State for the "Fix permissions" button shown when
// WiimoteManager::HadRecentLinuxPermissionError() is true (see
// SidebarLayout.cpp's Devices tab body). pkexec blocks on user interaction
// with the polkit auth dialog, so it runs on a background thread via
// std::async and this struct is polled once per frame rather than the UI
// thread calling LinuxUdevInstaller directly and freezing the render loop
// until the dialog is dismissed.
namespace {
struct UdevInstallUiState {
    std::future<InputBridge::Wiimote::LinuxUdevInstaller::RunOutcome> pending;
    bool has_result = false;
    InputBridge::Wiimote::LinuxUdevInstaller::RunOutcome last_result;

    bool IsRunning() const {
        return pending.valid() &&
               pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }

    // Call once per frame; picks up the result the frame it completes.
    void Poll() {
        if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            last_result = pending.get();
            has_result = true;
        }
    }
};
UdevInstallUiState s_UdevInstallUi;
} // namespace

// Panel-wide (not per-device) banner offering to fix the udev rule when
// the most recent WiimoteManager::Scan() hit EACCES opening a hidraw
// node - see WiimoteManager.h's HadRecentLinuxPermissionError() doc
// comment for why this only reflects the latest scan.
static void DrawLinuxUdevPermissionBanner() {
    using InputBridge::Wiimote::LinuxUdevInstaller;
    using InputBridge::Wiimote::WiimoteManager;

    s_UdevInstallUi.Poll();

    // Once installed successfully, stop nagging even if the device hasn't
    // been replugged yet this session - the flag itself will clear on its
    // own the next time a scan actually succeeds in opening the device.
    if (s_UdevInstallUi.has_result &&
        s_UdevInstallUi.last_result.result == LinuxUdevInstaller::Result::Success &&
        !s_UdevInstallUi.IsRunning()) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), ICON_FA_CHECK " Permissions installed.");
        // Show the script's own "Next steps" block (unplug/replug, log out
        // if it added the user to plugdev, relaunch) rather than a
        // hardcoded summary - those steps only exist in the script's
        // stdout, so if the UI doesn't surface it here the user never
        // sees them (they'd otherwise only end up wherever the process's
        // stdout happens to go, not in the app itself).
        if (!s_UdevInstallUi.last_result.stdout_tail.empty()) {
            ImGui::TextWrapped("%s", s_UdevInstallUi.last_result.stdout_tail.c_str());
        } else {
            // Fallback for an older/customized script that prints nothing
            // to stdout - still give the user something actionable.
            ImGui::TextWrapped("Unplug and replug the Wiimote/Balance Board (or its "
                                "Bluetooth dongle) to finish.");
        }
        return;
    }

    if (!WiimoteManager::HadRecentLinuxPermissionError() && !s_UdevInstallUi.IsRunning())
        return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.25f, 0.05f, 0.35f));
    ImGui::BeginChild("##udev_permission_banner", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                       ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                        "! A Wiimote or Balance Board was found but couldn't be opened "
                        "(permission denied).");
    ImGui::TextWrapped("This is common when connecting through a USB Bluetooth dongle: "
                        "the device needs a one-time permission rule installed.");

    if (s_UdevInstallUi.IsRunning()) {
        ImGui::TextDisabled("Waiting for authentication...");
    } else {
        const bool pkexec_available = LinuxUdevInstaller::IsPkexecAvailable();
        ImGui::BeginDisabled(!pkexec_available);
        if (ImGui::Button("Fix permissions...")) {
            s_UdevInstallUi.has_result = false;
            s_UdevInstallUi.pending = std::async(std::launch::async,
                                                  &LinuxUdevInstaller::InstallRules);
        }
        ImGui::EndDisabled();
        if (!pkexec_available) {
            ImGui::SameLine();
            ImGui::TextDisabled("(pkexec not found)");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("or run: sudo ./packaging/linux/install-udev-rules.sh");

        if (s_UdevInstallUi.has_result &&
            s_UdevInstallUi.last_result.result != LinuxUdevInstaller::Result::Success) {
            using Result = LinuxUdevInstaller::Result;
            const char *why =
                s_UdevInstallUi.last_result.result == Result::UserCancelled  ? "Authentication was cancelled." :
                s_UdevInstallUi.last_result.result == Result::ScriptNotFound ? "install-udev-rules.sh wasn't found "
                                                                                "next to the InputBridge binary." :
                s_UdevInstallUi.last_result.result == Result::PkexecNotFound ? "pkexec isn't available on this system." :
                                                                                "The installer script failed.";
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", why);
            if (!s_UdevInstallUi.last_result.stderr_tail.empty() &&
                s_UdevInstallUi.last_result.result == Result::Failed) {
                ImGui::TextWrapped("%s", s_UdevInstallUi.last_result.stderr_tail.c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
#endif // __linux__

// ---------------------------------------------------------------------------

void DrawSidebarLayout(SidebarContext& ctx)
{
    // -- Sizing (adapts to font / DPI) -------------------------------------
    const float FONT_SZ        = ImGui::GetFontSize();
    const float PAD            = ImGui::GetStyle().WindowPadding.x;
    const float ITEM_SPC       = ImGui::GetStyle().ItemSpacing.y;
    const float BTN_H          = FONT_SZ + ImGui::GetStyle().FramePadding.y * 2.0f;
    const float TEXT_W         = 130.0f;
    const float SIDEBAR_W_FULL = FONT_SZ + PAD * 3.0f + TEXT_W;
    const float SIDEBAR_W_SML  = FONT_SZ + PAD * 3.0f + 2.0f;
    const float SPLITTER_W     = 4.0f;

    if (g_SidebarW <= 0.0f)
        g_SidebarW = SIDEBAR_W_FULL;

    const float sidebar_w = g_SidebarExpanded ? g_SidebarW : SIDEBAR_W_SML;

    // -- Full-screen host window -------------------------------------------
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::Begin("##MainLayout", nullptr,
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);

    const float total_h   = ImGui::GetContentRegionAvail().y;
    const float total_w   = ImGui::GetContentRegionAvail().x;
    const float content_w = total_w - sidebar_w - SPLITTER_W;

    // -- LEFT SIDEBAR ------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD, PAD));
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_w, total_h),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // Collapse / Expand toggle
    {
        const char* lbl = g_SidebarExpanded ? "\xC2\xAB  Collapse" : "\xC2\xBB";
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Button(lbl, { ImGui::GetContentRegionAvail().x, BTN_H }))
            g_SidebarExpanded = !g_SidebarExpanded;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        if (!g_SidebarExpanded && ImGui::IsItemHovered())
            ImGui::SetTooltip("Expand");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -- Scrollable navigation + utility area ------------------------------
    const float sep_h    = ITEM_SPC * 2.0f + 1.0f;
    const float bottom_h = BTN_H + ITEM_SPC + sep_h + ITEM_SPC;
    float       scroll_h = ImGui::GetContentRegionAvail().y - bottom_h;
    if (scroll_h < BTN_H) scroll_h = BTN_H;

    ImGui::BeginChild("##NavScroll", { ImGui::GetContentRegionAvail().x, scroll_h },
                      ImGuiChildFlags_None);

    // Icon column width matches GlyphMin/MaxAdvanceX for true monospacing.
    const float ICON_COL_W = FONT_SZ + ImGui::GetStyle().ItemInnerSpacing.x;

    // Reusable FA-icon nav button.
    // Draws an invisible interaction button, then overlays the icon and label
    // at fixed column offsets so layout is stable regardless of glyph width.
    auto NavItem = [&](const char* icon, const char* label, int idx)
    {
        const bool active = (g_ActiveSection == idx);

        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            active ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                   : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

        // Left-edge accent bar for the active item.
        if (active) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                p, { p.x + 3.0f, p.y + BTN_H },
                ImGui::GetColorU32(ImGuiCol_SliderGrab));
        }

        const float indent = g_SidebarExpanded ? 6.0f : 0.0f;
        if (indent > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

        const ImVec2 btn_pos = ImGui::GetCursorScreenPos();
        const float  btn_w   = ImGui::GetContentRegionAvail().x;

        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "##nav%d", idx);
        if (ImGui::Button(id_buf, { btn_w, BTN_H }))
            g_ActiveSection = idx;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        // Overlay: icon + (optional) label at fixed column offsets.
        ImDrawList* dl       = ImGui::GetWindowDrawList();
        ImFont*     font     = ImGui::GetFont();
        const ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
        const float text_y   = btn_pos.y + (BTN_H - FONT_SZ) * 0.5f;

        if (g_SidebarExpanded) {
            const float icon_w = ImGui::CalcTextSize(icon).x;
            const float icon_x = btn_pos.x + (ICON_COL_W - icon_w) * 0.5f;
            dl->AddText(font, FONT_SZ, ImVec2(icon_x, text_y), text_col, icon);

            const float label_x = btn_pos.x + ICON_COL_W + ImGui::GetStyle().ItemInnerSpacing.x;
            dl->AddText(font, FONT_SZ, ImVec2(label_x, text_y), text_col, label);
        } else {
            const float icon_w = ImGui::CalcTextSize(icon).x;
            const float icon_x = btn_pos.x + (btn_w - icon_w) * 0.5f;
            dl->AddText(font, FONT_SZ, ImVec2(icon_x, text_y), text_col, icon);
        }

        if (!g_SidebarExpanded && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", label);

        ImGui::Spacing();
    };

    // Main navigation entries
    NavItem(ICON_FA_GAMEPAD,   "Devices",   0);
    NavItem(ICON_FA_SLIDERS,   "Input",     1);
    NavItem(ICON_FA_BOLT,      "Output",    2);
    NavItem(ICON_FA_WIFI,      "Network",   3);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Utility navigation entries
    NavItem(ICON_FA_FILE_CODE,   "Protocols", 4);
    NavItem(ICON_FA_GEAR,        "Settings",  5);
    NavItem(ICON_FA_INFO_CIRCLE, "About",     6);
    NavItem(ICON_FA_BUG,         "Debug Log", 7);

    ImGui::EndChild(); // ##NavScroll

    // -- Pinned Exit button ------------------------------------------------
    ImGui::Separator();
    ImGui::Spacing();
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

        const float indent = g_SidebarExpanded ? 6.0f : 0.0f;
        if (indent > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

        const ImVec2 btn_pos = ImGui::GetCursorScreenPos();
        const float  btn_w   = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button("##exit_btn", { btn_w, BTN_H }))
            ctx.done = false;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImDrawList*  dl       = ImGui::GetWindowDrawList();
        ImFont*      font     = ImGui::GetFont();
        const ImU32  text_col = ImGui::GetColorU32(ImGuiCol_Text);
        const float  text_y   = btn_pos.y + (BTN_H - FONT_SZ) * 0.5f;
        const char*  icon     = ICON_FA_POWER_OFF;

        if (g_SidebarExpanded) {
            const float icon_w = ImGui::CalcTextSize(icon).x;
            const float icon_x = btn_pos.x + (ICON_COL_W - icon_w) * 0.5f;
            dl->AddText(font, FONT_SZ, ImVec2(icon_x, text_y), text_col, icon);
            const float label_x = btn_pos.x + ICON_COL_W + ImGui::GetStyle().ItemInnerSpacing.x;
            dl->AddText(font, FONT_SZ, ImVec2(label_x, text_y), text_col, "Exit");
        } else {
            const float icon_w = ImGui::CalcTextSize(icon).x;
            const float icon_x = btn_pos.x + (btn_w - icon_w) * 0.5f;
            dl->AddText(font, FONT_SZ, ImVec2(icon_x, text_y), text_col, icon);
        }

        if (!g_SidebarExpanded && ImGui::IsItemHovered())
            ImGui::SetTooltip("Exit");
    }

    ImGui::EndChild(); // ##Sidebar

    // -- Drag-to-resize splitter -------------------------------------------
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##splitter", { SPLITTER_W, total_h });
    if (ImGui::IsItemActive()) {
        g_SidebarW += ImGui::GetIO().MouseDelta.x;
        const float min_w = FONT_SZ * 2.0f + PAD * 2.0f;
        const float max_w = total_w * 0.6f;
        if (g_SidebarW < min_w) g_SidebarW = min_w;
        if (g_SidebarW > max_w) g_SidebarW = max_w;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SameLine(0, 0);

    // -- RIGHT CONTENT AREA ------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("##ContentArea", { content_w, total_h },
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // Profile selector is always pinned at the top of the content area.
    ctx.inputMapper.DrawProfileSelector();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Inner child provides scrolling for the section content below the profile bar.
    // Note: we do NOT pass ImGuiWindowFlags_HorizontalScrollbar here - that flag
    // permanently reserves scrollbar space even when content fits, so the bar
    // never disappears after the window is widened.  Omitting it lets ImGui show
    // the scrollbar only when content actually overflows.
    ImGui::BeginChild("##SectionScroll", { 0.0f, 0.0f },
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_None);

    switch (g_ActiveSection) {

        case 0: { // -- Devices ---------------------------------------------
            // Load battery LED preference once on first entry.
            if (!s_BatteryLEDLoaded) {
                s_EnableBatteryLED = ctx.prefs.GetBool("EnableBatteryLED", true);
                s_BatteryLEDLoaded = true;
            }

            // Load gamepad navigation preference once on first entry and apply it.
            if (!s_GamepadNavLoaded) {
                s_DisableGamepadNav = ctx.prefs.GetBool("DisableGamepadNavigation", false);
                s_GamepadNavLoaded  = true;
                ImGuiIO& io = ImGui::GetIO();
                if (s_DisableGamepadNav)
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
                else
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            }

            // Load keyboard navigation preference once on first entry and apply it.
            if (!s_KeyboardNavLoaded) {
                s_DisableKeyboardNav = ctx.prefs.GetBool("DisableKeyboardNavigation", false);
                s_KeyboardNavLoaded  = true;
                ImGuiIO& io = ImGui::GetIO();
                if (s_DisableKeyboardNav)
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                else
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            }

            auto& devices = ctx.deviceManager.GetDevices();
            ImGui::Text("Connected Devices: %d", static_cast<int>(devices.size()));

            // Update gamepads' LED colour based on cached battery info once per second
            // (every ~60 frames at 60 fps).
            static int frame_ctr = 0;
            if (frame_ctr++ >= 60) {
                frame_ctr = 0;
                for (auto& dev : devices) {
                    if (s_EnableBatteryLED && dev.gamepad) {
                        Uint8 r = 0, g = 0, b = 0;
                        bool  upd = false;
                        if (dev.battery_state == SDL_POWERSTATE_CHARGING) {
                            r = 0; g = 0; b = 255; upd = true;
                        } else if (dev.battery_state == SDL_POWERSTATE_CHARGED) {
                            r = 0; g = 255; b = 0; upd = true;
                        } else if (dev.battery_state != SDL_POWERSTATE_UNKNOWN
                                   && dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
                            if      (dev.battery_percent >= 70) { r = 0;   g = 255; b = 0;   }
                            else if (dev.battery_percent >= 30) { r = 255; g = 165; b = 0;   }
                            else                                { r = 255; g = 0;   b = 0;   }
                            upd = true;
                        }
                        if (upd)
                            SDL_SetGamepadLED(dev.gamepad, r, g, b);
                    }
                }
            }

            // Virtual Device management panel
            {
                auto& vdm = VirtualDeviceManager::GetInstance();

                ImGui::Separator();
                const bool vOpen = ImGui::CollapsingHeader(
                    "Virtual Devices", ImGuiTreeNodeFlags_DefaultOpen);

                if (vOpen) {
                    ImGui::Indent();

                    static int  s_typeIdx = 0;
                    static char s_devName[64] = "Virtual Gamepad";

                    const char*        typeNames[]   = { "Gamepad", "Steering Wheel", "Flight Stick", "Generic" };
                    const VirtualDeviceType typeMap[] = {
                        VirtualDeviceType::Gamepad,
                        VirtualDeviceType::SteeringWheel,
                        VirtualDeviceType::FlightStick,
                        VirtualDeviceType::Generic
                    };
                    const char* defaultNames[] = {
                        "Virtual Gamepad", "Virtual Wheel", "Virtual Flight Stick", "Virtual Device"
                    };

                    ImGui::SetNextItemWidth(140.0f);
                    if (ImGui::Combo("Type##vdev", &s_typeIdx, typeNames, 4)) {
                        strncpy(s_devName, defaultNames[s_typeIdx], sizeof(s_devName) - 1);
                        s_devName[sizeof(s_devName) - 1] = '\0';
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::InputText("Name##vdev", s_devName, sizeof(s_devName));
                    ImGui::SameLine();
                    if (ImGui::Button("Add##vdev"))
                        vdm.AddDevice(typeMap[s_typeIdx], std::string(s_devName));

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Creates a virtual joystick that appears in the device list.\n"
                            "Use the 'Simulate Inputs' tab on the device to drive its axes\n"
                            "and buttons so protocols and mappers can be tested without\n"
                            "real hardware.");

                    const auto& vDevs = vdm.GetDevices();
                    if (!vDevs.empty()) {
                        ImGui::Spacing();
                        ImGui::Text("Active virtual devices:");
                        for (const auto& vs : vDevs) {
                            ImGui::PushID(static_cast<int>(vs->joystick_id));
                            ImGui::BulletText("%s", vs->name.c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%s)", VirtualDeviceTypeName(vs->type));
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Remove")) {
                                const SDL_JoystickID toRemove = vs->joystick_id;
                                vdm.RemoveDevice(toRemove);
                                ImGui::PopID();
                                break; // iterator invalidated - redraw next frame
                            }
                            ImGui::PopID();
                        }
                    } else {
                        ImGui::TextDisabled("No virtual devices active.");
                    }

                    ImGui::Unindent();
                }
            }

#if defined(__linux__)
            DrawLinuxUdevPermissionBanner();
#endif

            // Wiimote / Balance Board / Nunchuk / Classic Controller / Guitar Hero.
            // Not SDL_Joystick-backed (see Devices/Wiimote/README.md), so they
            // live in their own list rather than `devices` above.
            {
                const auto& wiimotes = ctx.deviceManager.GetWiimotes();
                if (!wiimotes.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Wiimotes: %d", static_cast<int>(wiimotes.size()));
                    int idx = 0;
                    for (auto& w : wiimotes)
                        DrawWiimoteItem(*w, ctx.prefs, idx++);
                }
            }

            ImGui::Separator();
            for (auto& dev : devices)
                DrawDeviceItem(dev, ctx.deviceManager, ctx.prefs, ctx.show_named_inputs);
            break;
        }

        case 1: // -- Input Mapper -----------------------------------------
            ctx.inputMapper.DrawMappingContent();
            break;

        case 2: // -- Output Mapper ----------------------------------------
            ctx.outputMapper.DrawContentOnly();
            break;

        case 3: // -- Network ----------------------------------------------
            NetworkStatusWindow::DrawContentOnly(
                ctx.server_update_rate,
                ctx.server_dynamic_rate,
                ctx.current_messages_per_second);
            break;

        case 4: // -- Protocol Editor --------------------------------------
            ProtocolEditorWindow::DrawContent();
            break;

        case 5: // -- Settings ---------------------------------------------
            if (!s_BatteryIntervalLoaded) {
                int interval = ctx.prefs.GetInt("BatteryUpdateIntervalMs", 5000);
                ctx.deviceManager.SetBatteryUpdateInterval(interval);
                s_BatteryIntervalLoaded = true;
            }
            DrawSettingsContent(
                ctx.user_ui_scale, ctx.user_font_scale, ctx.scale_with_window,
                ctx.window, ctx.initial_width, ctx.initial_height, ctx.prefs,
                ctx.vsync, ctx.framerate_limit, ctx.renderer,
                ImGui::GetIO(),
                s_EnableBatteryLED,
                s_DisableGamepadNav,
                s_DisableKeyboardNav,
                ctx.deviceManager,
                ctx.show_named_inputs,
                ctx.show_slider_edit_buttons);
            break;

        case 6: // -- About ------------------------------------------------
            AboutWindow::DrawContent();
            break;

        case 7: // -- Debug Log --------------------------------------------
            DrawDebugLogContent();
            break;
    }

    ImGui::EndChild(); // ##SectionScroll
    ImGui::EndChild(); // ##ContentArea
    ImGui::End();      // ##MainLayout
}
