#include "ui/QuestionImages.h"

#include "app/App.h"
#include "ui/ImageStore.h"
#include "ui/platform_dialogs.h"
#include "model/Assets.h"
#include "model/AppConfig.h"  // appDataDir(), removeFile()
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

// Open a preview for (qi, ki), or raise it if one is already open for that image
// (avoids duplicate windows for the same image; several *different* images can be
// open at once).
void openPreview(App& app, int qi, int ki)
{
    for (PreviewWin& pw : app.previews) {
        if (pw.question == qi && pw.image == ki) {
            pw.open = true;
            pw.focusNext = true;
            return;
        }
    }
    PreviewWin pw;
    pw.id = app.nextPreviewId++;
    pw.question = qi;
    pw.image = ki;
    pw.fit = true;
    pw.zoom = 1.0f;
    pw.focusNext = true;
    app.previews.push_back(pw);
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
        if (ImGui::Button("Preview"))
            openPreview(app, qi, k);
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

bool beginPasteImage(App& app, int qi)
{
    if (qi < 0 || qi >= static_cast<int>(app.project.questions.size()))
        return false;

    const std::string tmp = gt::clipboardImageToTempFile(gt::appDataDir());
    if (tmp.empty()) {
        app.statusMsg = "No image on the clipboard.";
        return false;
    }

    const gt::Question& q = app.project.questions[static_cast<size_t>(qi)];
    app.addImagePendingFile = tmp;
    app.addImagePasteTemp   = true;
    app.addImageRole        = 0;
    app.addImageCaption.clear();
    app.addImageSubs.assign(static_cast<size_t>(q.subCount), 0);
    return true;
}

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
                app.addImagePasteTemp = false;
                app.addImageRole = 0;
                app.addImageCaption.clear();
                app.addImageSubs.assign(static_cast<size_t>(q.subCount), 0);
            }
        }
        ImGui::SameLine();
        // Paste an image straight from the clipboard (Ctrl+V also works while this
        // popup is open, unless a text field is focused so its own paste still runs).
        if (ImGui::Button("Paste (Ctrl+V)") ||
            (!ImGui::IsAnyItemActive() &&
             ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)))
            beginPasteImage(app, qi);
    } else {
        ImGui::SeparatorText("New image");
        if (app.addImagePasteTemp)
            ImGui::TextDisabled("(pasted from clipboard)");
        else
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
            if (app.addImagePasteTemp)
                gt::removeFile(app.addImagePendingFile); // spent clipboard temp
            app.addImagePendingFile.clear();
            app.addImagePasteTemp = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            if (app.addImagePasteTemp)
                gt::removeFile(app.addImagePendingFile);
            app.addImagePendingFile.clear();
            app.addImagePasteTemp = false;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

namespace {

const float kZoomMin = 0.05f;
const float kZoomMax = 8.0f;
const float kZoomStep = 1.25f; // per +/- click or key press

float clampZoom(float z) { return std::min(kZoomMax, std::max(kZoomMin, z)); }

// Draw one preview window. Returns false if it should be closed this frame.
bool drawPreview(App& app, PreviewWin& pw)
{
    if (pw.question < 0 || pw.question >= static_cast<int>(app.project.questions.size()))
        return false;
    gt::Question& q = app.project.questions[static_cast<size_t>(pw.question)];
    const int nImg = static_cast<int>(q.images.size());
    if (nImg == 0)
        return false;
    pw.image = std::min(std::max(pw.image, 0), nImg - 1); // clamp if images were removed
    gt::QuestionImage& im = q.images[static_cast<size_t>(pw.image)];

    const std::string absPath = app.assetsDir.empty() ? im.file : app.assetsDir + "/" + im.file;
    const Tex tex = gt::ui::imageStoreGet(absPath);

    ImGui::SetNextWindowSize(ImVec2(720, 580), ImGuiCond_FirstUseEver);
    // Fixed ###id per window so its visible title (image i/N, role) can change
    // without ImGui treating it as a different window.
    const std::string title = std::string(q.title) + " - " + roleName(im.role) +
        " (" + std::to_string(pw.image + 1) + "/" + std::to_string(nImg) + ")" +
        "###bodex_img_preview_" + std::to_string(pw.id);

    bool open = true;
    if (ImGui::Begin(title.c_str(), &open)) {
        if (pw.focusNext) { ImGui::SetWindowFocus(); pw.focusNext = false; }
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused) app.anyPreviewFocused = true; // grid keyboard yields to a focused preview

        if (!im.caption.empty())
            ImGui::TextUnformatted(im.caption.c_str());
        ImGui::TextDisabled("subs: %s    %s", subTags(im).c_str(), im.file.c_str());

        // Toolbar: prev/next + counter, fit, zoom. `cycle` (-1/+1) also comes from
        // the wheel and arrow keys below; applied once at the end.
        int cycle = 0;
        ImGui::BeginDisabled(nImg < 2);
        if (ImGui::Button("< Prev")) cycle = -1;
        ImGui::SameLine();
        if (ImGui::Button("Next >")) cycle = +1;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Fit", &pw.fit);
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) { pw.fit = false; pw.zoom = clampZoom(pw.zoom / kZoomStep); }
        ImGui::SameLine();
        ImGui::Text("%.0f%%", pw.zoom * 100.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) { pw.fit = false; pw.zoom = clampZoom(pw.zoom * kZoomStep); }
        ImGui::SameLine();
        ImGui::TextDisabled("(wheel: flip  |  ctrl+wheel / +-: zoom  |  drag: pan)");

        // Keyboard shortcuts while this preview is focused.
        if (focused) {
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) cycle = +1;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  cycle = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_F)) pw.fit = !pw.fit;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                { pw.fit = false; pw.zoom = clampZoom(pw.zoom * kZoomStep); }
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                { pw.fit = false; pw.zoom = clampZoom(pw.zoom / kZoomStep); }
        }

        ImGui::Separator();

        ImGui::BeginChild("imgscroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (tex.ok) {
            float zoom;
            if (pw.fit) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float zx = tex.w > 0 ? avail.x / static_cast<float>(tex.w) : 1.0f;
                const float zy = tex.h > 0 ? avail.y / static_cast<float>(tex.h) : 1.0f;
                zoom = std::min(zx, zy);
                if (zoom <= 0.0f) zoom = 1.0f;
                pw.zoom = zoom; // reflect in the readout
            } else {
                zoom = pw.zoom;
            }
            const ImVec2 imgSize(tex.w * zoom, tex.h * zoom);
            const ImVec2 imgPos = ImGui::GetCursorScreenPos();
            ImGui::Image(tex.id, imgSize);

            // Transparent overlay over the image so a left-drag registers as an
            // active item and pans (via scroll) instead of moving the whole
            // window (ImGui moves a window when you drag its empty background).
            ImGui::SetCursorScreenPos(imgPos);
            ImGui::InvisibleButton("pan", imgSize);
            const bool imgActive = ImGui::IsItemActive();

            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const float wheel = ImGui::GetIO().MouseWheel;
            if (hovered && wheel != 0.0f) {
                if (ImGui::GetIO().KeyCtrl) {           // ctrl+wheel = zoom
                    pw.fit = false;
                    pw.zoom = clampZoom(zoom * (1.0f + wheel * 0.1f));
                } else {                                 // wheel = flip images
                    cycle += (wheel > 0.0f) ? +1 : -1;   // up = next, down = prev
                }
            }
            // Left-drag on the image pans it (when zoomed beyond the region).
            if (imgActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
                ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
            }
        } else {
            ImGui::TextColored(kWarn, "Could not load image:");
            ImGui::TextWrapped("%s", absPath.c_str());
        }

        // Apply navigation once: step image and reset the pan for the new one.
        if (cycle != 0 && nImg > 1) {
            const int dir = (cycle > 0) - (cycle < 0);
            pw.image = (pw.image + dir + nImg) % nImg;
            ImGui::SetScrollX(0.0f);
            ImGui::SetScrollY(0.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();

    return open;
}

} // namespace

void imagePreviewWindows(App& app)
{
    // Don't draw previews while the unsaved-changes modal is up. A focused
    // floating window renders above a modal opened from the main-loop context and
    // would swallow the clicks meant for its Save/Discard/Cancel buttons, making
    // the app impossible to close (X button, Ctrl+N/O, File > Exit all route
    // through that modal). Previews stay in app.previews and return once it
    // closes. (Called after the Grading window's End(), same id-stack context as
    // renderUnsavedPrompt, so IsPopupOpen matches.)
    if (ImGui::IsPopupOpen("Unsaved Changes"))
        return;

    app.anyPreviewFocused = false; // set by drawPreview when a preview is focused
    for (PreviewWin& pw : app.previews)
        if (pw.open && !drawPreview(app, pw))
            pw.open = false;

    app.previews.erase(
        std::remove_if(app.previews.begin(), app.previews.end(),
                       [](const PreviewWin& p) { return !p.open; }),
        app.previews.end());
}

} // namespace gt::ui
