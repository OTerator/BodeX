#include "ui/HomeScreen.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "imgui.h"

#include <string>

namespace gt::ui {

namespace {
std::string baseName(const std::string& path)
{
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}
} // namespace

void homeScreen(App& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Home", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Dummy(ImVec2(0, 12));
    bigTitle("BodeX", 30.0f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("grading tracker");
    ImGui::TextDisabled("Track exam grading: students x questions, sub-question tallies,");
    ImGui::TextDisabled("full-mark ticks, no-submission rows and last-page resume markers.");
    ImGui::Dummy(ImVec2(0, 16));

    if (ImGui::Button("New Project", ImVec2(180, 44)))
        app.newProjectStart();
    ImGui::SameLine();
    if (ImGui::Button("Open Project...", ImVec2(180, 44)))
        app.openProjectDialog();

    ImGui::Dummy(ImVec2(0, 18));
    ImGui::SeparatorText("Recent projects");

    if (app.config.recentProjects.empty()) {
        ImGui::TextDisabled("No recent projects yet. Create one to get started.");
    } else {
        // Note: openProjectPath() mutates config.recentProjects (moves the opened
        // entry to the front). Don't call it mid-iteration with a reference into
        // that vector - copy the path and defer the open until after the loop.
        std::string toOpen;
        for (size_t i = 0; i < app.config.recentProjects.size(); ++i) {
            std::string path = app.config.recentProjects[i]; // copy, not a reference
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(baseName(path).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                toOpen = path;
            ImGui::SameLine();
            ImGui::TextDisabled("   %s", path.c_str());
            ImGui::PopID();
        }
        if (!toOpen.empty())
            app.openProjectPath(toOpen);
    }

    if (!app.statusMsg.empty()) {
        ImGui::Dummy(ImVec2(0, 16));
        ImGui::TextDisabled("%s", app.statusMsg.c_str());
    }

    ImGui::End();
}

} // namespace gt::ui
