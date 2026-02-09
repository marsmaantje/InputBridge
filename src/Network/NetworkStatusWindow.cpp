#include "NetworkStatusWindow.h"
#include "OSCServer.h"
#include "WebSocketServer.h"
#include "../Mappers/InputMapper.h"
#include "../Mappers/OutputMapper.h"
#include "imgui.h"

void NetworkStatusWindow::Draw() {
    if (ImGui::Begin("Network Server")) {
        if (ImGui::BeginTabBar("NetworkTabs")) {
            if (ImGui::BeginTabItem("Input Mapper")) {
                InputMapper::GetInstance().DrawContent();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Output Mapper")) {
                OutputMapper::GetInstance().DrawContent();
                ImGui::EndTabItem();
            }
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
    ImGui::End();
}
