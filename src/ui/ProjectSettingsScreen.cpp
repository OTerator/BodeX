#include "ui/ProjectSettingsScreen.h"

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

void projectSettingsSection(App& app)
{
    gt::ProjectSettingsDraft& s = app.settings;

    ImGui::SeparatorText("Project structure");
    ImGui::TextDisabled("Edit the project structure. Existing grades are kept where a question or student is kept;");
    ImGui::TextDisabled("changes that would alter or discard grades are confirmed before they apply.");
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::SetNextItemWidth(320);
    ImGui::InputText("Project name", &s.name);

    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt("Students (rows)", &s.studentCount)) {
        if (s.studentCount < 1)    s.studentCount = 1;
        if (s.studentCount > 5000) s.studentCount = 5000;
    }
    if (s.studentCount != s.origStudentCount) {
        ImGui::SameLine();
        if (s.studentCount > s.origStudentCount)
            ImGui::TextDisabled("(+%d new)", s.studentCount - s.origStudentCount);
        else
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f),
                               "(-%d removed from the end)", s.origStudentCount - s.studentCount);
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SeparatorText("Questions");

    // Reserve space at the bottom for the action buttons.
    ImGui::BeginChild("qconfig", ImVec2(0, -48), ImGuiChildFlags_Borders);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 5.0f));

    int removeAt = -1;                       // deferred: don't mutate mid-loop
    const int qCount = static_cast<int>(s.questions.size());
    for (int j = 0; j < qCount; ++j) {
        gt::Question& q = s.questions[static_cast<size_t>(j)];
        ImGui::PushID(j);
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // Remove button + "new column" marker, right of the Q-label row.
        if (qCount <= 1) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Remove"))
            removeAt = j;
        if (qCount <= 1) ImGui::EndDisabled();
        if (j < static_cast<int>(s.originalIndex.size()) && s.originalIndex[static_cast<size_t>(j)] < 0) {
            ImGui::SameLine();
            ImGui::TextColored(kOk, "(new)");
        }

        if (questionConfigBlock(q, j)) { /* edits are local to the working copy */ }

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::Separator();
        ImGui::PopID();
    }

    if (ImGui::Button("+ Add question")) {
        gt::Question q;
        q.title = "Q" + std::to_string(qCount + 1);
        q.maxPoints = 10.0;
        q.subCount = 1;
        q.split = gt::SplitMode::Equal;
        gt::normalizeQuestion(q);
        s.questions.push_back(q);
        s.originalIndex.push_back(-1); // brand-new column
    }

    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    if (removeAt >= 0 && removeAt < static_cast<int>(s.questions.size())) {
        s.questions.erase(s.questions.begin() + removeAt);
        if (removeAt < static_cast<int>(s.originalIndex.size()))
            s.originalIndex.erase(s.originalIndex.begin() + removeAt);
    }

    // Live total of all question points (the exam's max score).
    double totalPoints = 0.0;
    for (const auto& q : s.questions)
        totalPoints += q.maxPoints;
    const int nQ = static_cast<int>(s.questions.size());
    ImGui::TextColored(kOk, "Total exam points: %s   (across %d question%s)",
                       fmtNum(totalPoints).c_str(), nQ, nQ == 1 ? "" : "s");

    const bool valid = s.studentCount >= 1 && nQ >= 1;
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Apply", ImVec2(140, 36)))
        app.tryApplyProjectSettings();
    if (!valid) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 36)))
        app.screen = App::Screen::Grading; // discard the working copy, no side effects
}

} // namespace gt::ui
