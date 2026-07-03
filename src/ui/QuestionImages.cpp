#include "ui/QuestionImages.h"

#include "app/App.h"
#include "ui/ImageStore.h"
#include "ui/platform_dialogs.h"
#include "model/Assets.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <string>

namespace gt::ui {

namespace {

const ImVec4 kWarn(0.95f, 0.55f, 0.30f, 1.0f);

const char* roleName(gt::ImageRole r) { return r == gt::ImageRole::Solution ? "Solution" : "Question"; }

// "all" for whole-question, else 1-based sub-question list like "1,3".
std::string subTags(const gt::QuestionImage& im)
{
    if (im.subQuestions.empty())
        return "all";
    std::string s;
    for (size_t i = 0; i < im.subQuestions.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(im.subQuestions[i] + 1);
    }
    return s;
}

// Render the images of one role; returns an index to erase (-1 = none this frame).
int renderGroup(App& app, gt::Question& q, int qi, const char* header, gt::ImageRole role)
{
    ImGui::SeparatorText(header);
    bool any = false;
    int toErase = -1;
    for (int k = 0; k < static_cast<int>(q.images.size()); ++k) {
        gt::QuestionImage& im = q.images[static_cast<size_t>(k)];
        if (im.role != role)
            continue;
        any = true;
        ImGui::PushID(k);
        if (ImGui::Button("Preview")) {
            app.previewQuestion = qi;
            app.previewImage = k;
            app.previewOpen = true;
            app.previewFit = true;
        }
        ImGui::SameLine();
        const std::string label = im.caption.empty() ? im.file : im.caption;
        ImGui::Text("%s", label.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("[subs: %s]", subTags(im).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            toErase = k;
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextDisabled("  (none)");
    return toErase;
}

} // namespace

void questionImagesPopup(App& app)
{
    if (!ImGui::BeginPopup("QuestionImages"))
        return;

    const int qi = app.imageMenuQuestion;
    if (qi < 0 || qi >= static_cast<int>(app.project.questions.size())) {
        ImGui::EndPopup();
        return;
    }
    gt::Question& q = app.project.questions[static_cast<size_t>(qi)];

    ImGui::Text("%s - images", q.title.c_str());
    ImGui::TextDisabled("Attach question screenshots and solution references; tag the sub-questions they cover.");
    ImGui::Spacing();

    int erase = renderGroup(app, q, qi, "Question images", gt::ImageRole::Question);
    int erase2 = renderGroup(app, q, qi, "Solution references", gt::ImageRole::Solution);
    if (erase < 0) erase = erase2;
    if (erase >= 0 && erase < static_cast<int>(q.images.size())) {
        q.images.erase(q.images.begin() + erase);
        app.markDirty();
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (app.addImagePendingFile.empty()) {
        if (ImGui::Button("Add image...")) {
            std::string path;
            if (gt::ui::openImageDialog(path, std::string())) {
                app.addImagePendingFile = path;
                app.addImageRole = 0;
                app.addImageCaption.clear();
                app.addImageSubs.assign(static_cast<size_t>(q.subCount), 0);
            }
        }
    } else {
        ImGui::SeparatorText("New image");
        ImGui::TextDisabled("%s", app.addImagePendingFile.c_str());

        ImGui::RadioButton("Question image", &app.addImageRole, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Solution reference", &app.addImageRole, 1);

        ImGui::SetNextItemWidth(260);
        ImGui::InputText("Caption (optional)", &app.addImageCaption);

        if (static_cast<int>(app.addImageSubs.size()) != q.subCount)
            app.addImageSubs.assign(static_cast<size_t>(q.subCount), 0);
        ImGui::TextUnformatted("Applies to sub-questions (none = whole question):");
        for (int s = 0; s < q.subCount; ++s) {
            ImGui::PushID(s);
            bool v = app.addImageSubs[static_cast<size_t>(s)] != 0;
            if (ImGui::Checkbox(std::to_string(s + 1).c_str(), &v))
                app.addImageSubs[static_cast<size_t>(s)] = v ? 1 : 0;
            ImGui::PopID();
            if ((s % 8) != 7 && s != q.subCount - 1)
                ImGui::SameLine();
        }

        if (ImGui::Button("Add")) {
            const std::string fname = gt::importImage(app.addImagePendingFile, app.assetsDir);
            if (!fname.empty()) {
                gt::QuestionImage im;
                im.file = fname;
                im.role = app.addImageRole == 1 ? gt::ImageRole::Solution : gt::ImageRole::Question;
                im.caption = app.addImageCaption;
                for (int s = 0; s < static_cast<int>(app.addImageSubs.size()); ++s)
                    if (app.addImageSubs[static_cast<size_t>(s)])
                        im.subQuestions.push_back(s);
                q.images.push_back(std::move(im));
                app.markDirty();
            } else {
                app.statusMsg = "Could not import image (copy failed).";
            }
            app.addImagePendingFile.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            app.addImagePendingFile.clear();
    }

    ImGui::Separator();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void imagePreviewWindow(App& app)
{
    if (!app.previewOpen)
        return;

    const int qi = app.previewQuestion;
    if (qi < 0 || qi >= static_cast<int>(app.project.questions.size())) { app.previewOpen = false; return; }
    gt::Question& q = app.project.questions[static_cast<size_t>(qi)];
    const int ki = app.previewImage;
    if (ki < 0 || ki >= static_cast<int>(q.images.size())) { app.previewOpen = false; return; }
    gt::QuestionImage& im = q.images[static_cast<size_t>(ki)];

    const std::string absPath = app.assetsDir.empty() ? im.file : app.assetsDir + "/" + im.file;
    const Tex tex = gt::ui::imageStoreGet(absPath);

    ImGui::SetNextWindowSize(ImVec2(720, 580), ImGuiCond_FirstUseEver);
    // Fixed ### id so the visible title can change without making a new window.
    const std::string title = std::string(q.title) + " - " + roleName(im.role) + " image###bodex_img_preview";

    bool open = true;
    if (ImGui::Begin(title.c_str(), &open)) {
        if (!im.caption.empty())
            ImGui::TextUnformatted(im.caption.c_str());
        ImGui::TextDisabled("subs: %s    %s", subTags(im).c_str(), im.file.c_str());

        ImGui::Checkbox("Fit", &app.previewFit);
        ImGui::SameLine();
        ImGui::BeginDisabled(app.previewFit);
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("zoom", &app.previewZoom, 0.05f, 8.0f, "%.2fx");
        ImGui::EndDisabled();
        ImGui::Separator();

        ImGui::BeginChild("imgscroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (tex.ok) {
            float zoom;
            if (app.previewFit) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float zx = tex.w > 0 ? avail.x / static_cast<float>(tex.w) : 1.0f;
                const float zy = tex.h > 0 ? avail.y / static_cast<float>(tex.h) : 1.0f;
                zoom = std::min(zx, zy);
                if (zoom <= 0.0f) zoom = 1.0f;
                app.previewZoom = zoom; // reflect in the slider
            } else {
                zoom = app.previewZoom;
            }
            ImGui::Image(tex.id, ImVec2(tex.w * zoom, tex.h * zoom));

            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                app.previewFit = false;
                float z = zoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f);
                app.previewZoom = std::min(8.0f, std::max(0.05f, z));
            }
        } else {
            ImGui::TextColored(kWarn, "Could not load image:");
            ImGui::TextWrapped("%s", absPath.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();

    if (!open)
        app.previewOpen = false;
}

} // namespace gt::ui
