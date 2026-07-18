#include "ui/NewProjectScreen.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "model/Project.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <string>

namespace gt::ui {

namespace {
const ImVec4 kOk(0.35f, 0.85f, 0.40f, 1.0f);
} // namespace

void newProjectScreen(App& app)
{
    gt::NewProjectDraft& d = app.draft;
    d.syncQuestions(); // keep questions vector sized to questionCount every frame

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("New Project", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    bigTitle("New Project", 24.0f);
    ImGui::TextDisabled("Set the table size, then configure the points and sub-questions for each column.");
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::SetNextItemWidth(320);
    ImGui::InputText("Project name", &d.name);

    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt("Students (rows)", &d.studentCount)) {
        if (d.studentCount < 1)    d.studentCount = 1;
        if (d.studentCount > 5000) d.studentCount = 5000;
    }
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt("Questions (columns)", &d.questionCount)) {
        if (d.questionCount < 1)   d.questionCount = 1;
        if (d.questionCount > 200) d.questionCount = 200;
        d.syncQuestions();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SeparatorText("Questions");

    // Reserve space at the bottom for the action buttons.
    ImGui::BeginChild("qconfig", ImVec2(0, -48), ImGuiChildFlags_Borders);
    // Roomier spacing + taller fields so the per-question controls are comfortable.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 5.0f));
    for (int j = 0; j < d.questionCount; ++j) {
        gt::Question& q = d.questions[static_cast<size_t>(j)];
        ImGui::PushID(j);
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        questionConfigBlock(q, j);

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    // Live total of all question points (the exam's max score), before creation.
    double totalPoints = 0.0;
    for (int j = 0; j < d.questionCount; ++j)
        totalPoints += d.questions[static_cast<size_t>(j)].maxPoints;
    ImGui::TextColored(kOk, "Total exam points: %s   (across %d question%s)",
                       fmtNum(totalPoints).c_str(), d.questionCount, d.questionCount == 1 ? "" : "s");

    const bool valid = d.studentCount >= 1 && d.questionCount >= 1;
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Create Project", ImVec2(170, 36)))
        app.createProjectFromDraft();
    if (!valid) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 36)))
        app.screen = app.hasProject ? App::Screen::Grading : App::Screen::Home;

    ImGui::End();
}

} // namespace gt::ui
