#include "DebugLogPanel.h"
#include "App/AppLog.h"
#include "imgui.h"
#include <SDL3/SDL_clipboard.h>
#include <string>
#include <sstream>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static ImVec4 LevelColour(AppLogLevel level) {
    switch (level) {
        case AppLogLevel::Verbose:  return { 0.55f, 0.55f, 0.55f, 1.0f }; // grey
        case AppLogLevel::Debug:    return { 0.50f, 0.80f, 1.00f, 1.0f }; // light blue
        case AppLogLevel::Info:     return { 1.00f, 1.00f, 1.00f, 1.0f }; // white
        case AppLogLevel::Warn:     return { 1.00f, 0.85f, 0.20f, 1.0f }; // yellow
        case AppLogLevel::Error:    return { 1.00f, 0.35f, 0.35f, 1.0f }; // red
        case AppLogLevel::Critical: return { 1.00f, 0.20f, 0.80f, 1.0f }; // magenta
        default:                    return { 1.00f, 1.00f, 1.00f, 1.0f };
    }
}

static const char* LevelName(AppLogLevel level) {
    switch (level) {
        case AppLogLevel::Verbose:  return "VERBOSE";
        case AppLogLevel::Debug:    return "DEBUG";
        case AppLogLevel::Info:     return "INFO";
        case AppLogLevel::Warn:     return "WARN";
        case AppLogLevel::Error:    return "ERROR";
        case AppLogLevel::Critical: return "CRITICAL";
        default:                    return "";
    }
}

// ── Panel state ───────────────────────────────────────────────────────────────

static char  s_filterBuf[256]          = "";
static bool  s_autoScroll              = true;
static bool  s_wordWrap                = false;
static bool  s_levelFilter[6]          = { true, true, true, true, true, true };
static bool  s_showLevelFilterPopup    = false;

// ── DrawDebugLogContent ───────────────────────────────────────────────────────

void DrawDebugLogContent() {
    AppLog& log = AppLog::Get();

    // ── Toolbar ───────────────────────────────────────────────────────────────
    ImGui::Text("Debug Log");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu entries)", log.Entries().size());

    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::Button("Clear"))
        log.Clear();

    ImGui::SameLine();
    if (ImGui::Button("Copy All")) {
        std::ostringstream oss;
        for (const auto& e : log.Entries())
            oss << e.text << '\n';
        SDL_SetClipboardText(oss.str().c_str());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy all visible log lines to the clipboard");

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &s_autoScroll);

    ImGui::SameLine();
    ImGui::Checkbox("Word wrap", &s_wordWrap);

    ImGui::SameLine(0.0f, 16.0f);
    // Level filter toggle button
    if (ImGui::Button("Levels \xE2\x96\xBE")) // ▾
        ImGui::OpenPopup("##LevelFilter");

    if (ImGui::BeginPopup("##LevelFilter")) {
        ImGui::Text("Show levels:");
        ImGui::Separator();
        static const AppLogLevel kLevels[] = {
            AppLogLevel::Verbose, AppLogLevel::Debug, AppLogLevel::Info,
            AppLogLevel::Warn,    AppLogLevel::Error,  AppLogLevel::Critical
        };
        for (int i = 0; i < 6; ++i) {
            ImGui::PushStyleColor(ImGuiCol_Text, LevelColour(kLevels[i]));
            ImGui::Checkbox(LevelName(kLevels[i]), &s_levelFilter[i]);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (ImGui::SmallButton("All"))   { for (auto& f : s_levelFilter) f = true;  }
        ImGui::SameLine();
        if (ImGui::SmallButton("None"))  { for (auto& f : s_levelFilter) f = false; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Errors only")) {
            for (auto& f : s_levelFilter) f = false;
            s_levelFilter[4] = true; // Error
            s_levelFilter[5] = true; // Critical
        }
        ImGui::EndPopup();
    }

    // Text filter
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "Filter...", s_filterBuf, sizeof(s_filterBuf));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show only lines containing this text (case-sensitive)");

    ImGui::Separator();

    // ── Log area ──────────────────────────────────────────────────────────────
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::BeginChild("##LogArea", avail, ImGuiChildFlags_Borders,
                       s_wordWrap ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    const bool hasFilter = (s_filterBuf[0] != '\0');

    // Take a snapshot reference — entries only grow or get cleared, never
    // individually removed, so iterating without the lock is safe on the
    // render thread (Push holds the lock only during the write).
    const auto& entries = log.Entries();
    ImGuiListClipper clipper;

    // Collect visible indices so the clipper works on the correct count.
    // We build this small filtered index list each frame; for 2048 entries
    // this is negligible (~microseconds).
    static std::vector<int> s_visible;
    s_visible.clear();
    s_visible.reserve(entries.size());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& e = entries[i];
        const int   li = static_cast<int>(e.level);
        if (!s_levelFilter[li]) continue;
        if (hasFilter && e.text.find(s_filterBuf) == std::string::npos) continue;
        s_visible.push_back(i);
    }

    clipper.Begin(static_cast<int>(s_visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& e = entries[s_visible[row]];

            ImGui::PushID(s_visible[row]);

            // Record the top-left of this row in screen space before rendering text.
            const ImVec2 rowScreenMin = ImGui::GetCursorScreenPos();
            const float  rowWidth     = ImGui::GetContentRegionAvail().x;

            // 1. Render the coloured text first — this advances the cursor naturally
            //    so no rewinding is needed and row height is always correct.
            ImGui::PushStyleColor(ImGuiCol_Text, LevelColour(e.level));
            if (s_wordWrap)
                ImGui::TextWrapped("%s", e.text.c_str());
            else
                ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopStyleColor();

            // 2. Now we know the row's screen-space bounds. Draw a hover/click
            //    highlight behind the text using the draw list, and register an
            //    InvisibleButton over the same rect for hover detection and the
            //    right-click context menu.
            const ImVec2 rowScreenMax = ImVec2(rowScreenMin.x + rowWidth,
                                               ImGui::GetCursorScreenPos().y);
            const float  rowH         = rowScreenMax.y - rowScreenMin.y;

            ImGui::SetCursorScreenPos(rowScreenMin);
            ImGui::InvisibleButton("##hit", ImVec2(rowWidth, rowH));

            if (ImGui::IsItemHovered()) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rowScreenMin, rowScreenMax,
                    IM_COL32(255, 255, 255, 20));
            }
            if (ImGui::IsItemActive()) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rowScreenMin, rowScreenMax,
                    IM_COL32(255, 255, 255, 40));
            }

            // Right-click context menu.
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (ImGui::MenuItem("Copy line"))
                    SDL_SetClipboardText(e.text.c_str());
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }
    clipper.End();

    // Auto-scroll: only scroll when the user hasn't scrolled up themselves.
    if (s_autoScroll && log.HasNewEntries()) {
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        log.ConsumeScrollRequest();
    }

    ImGui::EndChild(); // ##LogArea
}
