#include "ui/NewProjectScreen.h"

#include "app/App.h"
#include "ui/widgets.h"
#include "model/Project.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <cmath>
#include <string>

namespace gt::ui {

namespace {
const ImVec4 kOk(0.35f, 0.85f, 0.40f, 1.0f);
const ImVec4 kWarn(0.95f, 0.65f, 0.20f, 1.0f);
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

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Q%d", j + 1);

        ImGui::SameLine(50);
        ImGui::SetNextItemWidth(150);
        ImGui::InputText("title##t", &q.title);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputDouble("points##m", &q.maxPoints, 0.0, 0.0, "%.2f")) {
            if (q.maxPoints < 0) q.maxPoints = 0;
            gt::normalizeQuestion(q);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("sub-Qs##s", &q.subCount)) {
            if (q.subCount < 1)  q.subCount = 1;
            if (q.subCount > 50) q.subCount = 50;
            gt::normalizeQuestion(q);
        }

        ImGui::SameLine();
        int splitInt = (q.split == gt::SplitMode::Custom) ? 1 : 0;
        if (ImGui::RadioButton("Equal", &splitInt, 0)) { q.split = gt::SplitMode::Equal;  gt::normalizeQuestion(q); }
        ImGui::SameLine();
        if (ImGui::RadioButton("Custom", &splitInt, 1)) { q.split = gt::SplitMode::Custom; gt::normalizeQuestion(q); }

        if (q.split == gt::SplitMode::Custom) {
            if (static_cast<int>(q.subPoints.size()) != q.subCount)
                q.subPoints.resize(static_cast<size_t>(q.subCount), gt::equalShare(q));
            ImGui::Indent(50);
            double sum = 0.0;
            for (int k = 0; k < q.subCount; ++k) {
                ImGui::PushID(k);
                ImGui::SetNextItemWidth(70);
                ImGui::InputDouble("##sp", &q.subPoints[static_cast<size_t>(k)], 0.0, 0.0, "%.2f");
                sum += q.subPoints[static_cast<size_t>(k)];
                ImGui::PopID();
                if ((k % 6) != 5 && k != q.subCount - 1)
                    ImGui::SameLine();
            }
            const bool matches = std::fabs(sum - q.maxPoints) < 1e-6;
            ImGui::TextColored(matches ? kOk : kWarn, "sub-points sum = %s / %s",
                               fmtNum(sum).c_str(), fmtNum(q.maxPoints).c_str());
            ImGui::Unindent(50);
        } else {
            ImGui::Indent(50);
            ImGui::TextDisabled("each of %d sub-questions = %s pts", q.subCount, fmtNum(gt::equalShare(q)).c_str());
            ImGui::Unindent(50);
        }

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
