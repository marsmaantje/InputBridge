#include "NetworkStatusWindow.h"
#include "OSCServer.h"
#include "WebSocketServer.h"
#include "Mappers/OutputMapper.h"
#include "imgui.h"

void NetworkStatusWindow::Draw(int& update_rate, bool& dynamic_rate, float messages_per_second) {
    if (ImGui::Begin("Network Server")) {
        DrawContentOnly(update_rate, dynamic_rate, messages_per_second);
    }
    ImGui::End();
}

void NetworkStatusWindow::DrawContentOnly(int& update_rate, bool& dynamic_rate, float messages_per_second) {
    ImGui::Text("Update Rate");
    ImGui::SameLine();
    ImGui::Checkbox("Dynamic Rate", &dynamic_rate);
    if (dynamic_rate) {
        ImGui::SameLine();
        ImGui::Text("Messages/sec: %.1f", messages_per_second);
    } else {
        ImGui::SameLine();
        const char* label = "Target Rate (Hz)";
        float width = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemInnerSpacing.x;
        if (width < 10.0f) width = 10.0f;
        ImGui::SetNextItemWidth(width);
        ImGui::SliderInt(label, &update_rate, 1, 200);
    }
    ImGui::Separator();

    ImGui::Text("Haptic Effects:");
    ImGui::SameLine();
    if (OutputMapper::GetInstance().IsHapticsActive()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active");
    } else {
        ImGui::TextDisabled("Idle");
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("NetworkTabs")) {
        if (ImGui::BeginTabItem("OSC Server")) {
            OSCServer::GetInstance().DrawContent();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("WebSocket Server")) {
            WebSocketServer::GetInstance().DrawContent();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
