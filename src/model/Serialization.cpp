#include "model/Serialization.h"

#include "util/utf.h"     // must precede json.hpp so windows.h gets NOMINMAX
#include "json.hpp"

#include <cstdio>

namespace gt {

using nlohmann::json;

namespace {

const char* splitToStr(SplitMode m) { return m == SplitMode::Custom ? "custom" : "equal"; }
SplitMode   splitFromStr(const std::string& s) { return s == "custom" ? SplitMode::Custom : SplitMode::Equal; }

const char* roleToStr(ImageRole r) { return r == ImageRole::Solution ? "solution" : "question"; }
ImageRole   roleFromStr(const std::string& s) { return s == "solution" ? ImageRole::Solution : ImageRole::Question; }

json imageToJson(const QuestionImage& im)
{
    return json{
        {"file", im.file},
        {"role", roleToStr(im.role)},
        {"caption", im.caption},
        {"subQuestions", im.subQuestions},
    };
}

QuestionImage imageFromJson(const json& j)
{
    QuestionImage im;
    im.file    = j.value("file", std::string());
    im.role    = roleFromStr(j.value("role", std::string("question")));
    im.caption = j.value("caption", std::string());
    if (j.contains("subQuestions") && j.at("subQuestions").is_array())
        im.subQuestions = j.at("subQuestions").get<std::vector<int>>();
    return im;
}

json cellToJson(const Cell& c)
{
    json subChecks = json::array();          // Custom-split per-sub answered flags
    for (char v : c.subChecks)
        subChecks.push_back(v ? 1 : 0);
    return json{
        {"fullTick", c.fullTick},
        {"awarded", c.awarded},
        {"subAnswered", c.subAnswered},
        {"subChecks", std::move(subChecks)},
        {"lastPage", c.lastPage},
        {"note", c.note},
        {"noteDir", static_cast<int>(c.noteDir)},
        {"touched", c.touched},
    };
}

Cell cellFromJson(const json& j)
{
    Cell c;
    c.fullTick    = j.value("fullTick", false);
    c.awarded     = j.value("awarded", 0.0);
    c.subAnswered = j.value("subAnswered", 0);
    if (j.contains("subChecks") && j.at("subChecks").is_array())
        for (const auto& v : j.at("subChecks"))
            c.subChecks.push_back(v.get<int>() ? 1 : 0);
    c.lastPage    = j.value("lastPage", std::string());
    c.note        = j.value("note", std::string());
    int nd        = j.value("noteDir", 0);            // additive; old files -> Auto (0)
    if (nd < 0 || nd > 2) nd = 0;
    c.noteDir     = static_cast<TextDir>(nd);
    c.touched     = j.value("touched", false);
    return c;
}

json questionToJson(const Question& q)
{
    json images = json::array();
    for (const auto& im : q.images)
        images.push_back(imageToJson(im));
    return json{
        {"title", q.title},
        {"maxPoints", q.maxPoints},
        {"subCount", q.subCount},
        {"split", splitToStr(q.split)},
        {"subPoints", q.subPoints},
        {"images", std::move(images)},
        {"folded", q.folded},         // view state; additive, old files -> false
        {"viewWidth", q.viewWidth},   // additive, old files -> 190
    };
}

Question questionFromJson(const json& j)
{
    Question q;
    q.title     = j.value("title", std::string("Q"));
    q.maxPoints = j.value("maxPoints", 0.0);
    q.subCount  = j.value("subCount", 1);
    q.split     = splitFromStr(j.value("split", std::string("equal")));
    if (j.contains("subPoints") && j.at("subPoints").is_array())
        q.subPoints = j.at("subPoints").get<std::vector<double>>();
    if (j.contains("images") && j.at("images").is_array())
        for (const auto& ji : j.at("images"))
            q.images.push_back(imageFromJson(ji));
    q.folded    = j.value("folded", false);          // additive; old files -> unfolded
    q.viewWidth = j.value("viewWidth", 190.0f);       // additive; old files -> 190
    if (q.viewWidth < 34.0f) q.viewWidth = 34.0f;     // floor == kFoldedWidth (GradingTable)
    return q;
}

json studentToJson(const Student& s)
{
    json cells = json::array();
    for (const auto& c : s.cells)
        cells.push_back(cellToJson(c));
    return json{
        {"id", s.id},
        {"noSubmission", s.noSubmission},
        {"cells", std::move(cells)},
    };
}

Student studentFromJson(const json& j)
{
    Student s;
    s.id           = j.value("id", 0);
    s.noSubmission = j.value("noSubmission", false);
    if (j.contains("cells") && j.at("cells").is_array())
        for (const auto& jc : j.at("cells"))
            s.cells.push_back(cellFromJson(jc));
    return s;
}

bool writeFileUtf8(const std::string& path, const std::string& data, std::string* err)
{
    std::wstring wp = utf8_to_wide(path);
    FILE* f = ::_wfopen(wp.c_str(), L"wb");
    if (!f) { if (err) *err = "cannot open file for writing: " + path; return false; }
    size_t written = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (written != data.size()) { if (err) *err = "short write to: " + path; return false; }
    return true;
}

bool readFileUtf8(const std::string& path, std::string& out, std::string* err)
{
    std::wstring wp = utf8_to_wide(path);
    FILE* f = ::_wfopen(wp.c_str(), L"rb");
    if (!f) { if (err) *err = "cannot open file for reading: " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); if (err) *err = "cannot size file: " + path; return false; }
    out.resize(static_cast<size_t>(sz));
    size_t got = sz > 0 ? std::fread(out.data(), 1, static_cast<size_t>(sz), f) : 0;
    std::fclose(f);
    out.resize(got);
    return true;
}

} // namespace

std::string toJsonString(const Project& p, int indent)
{
    json questions = json::array();
    for (const auto& q : p.questions)
        questions.push_back(questionToJson(q));

    json students = json::array();
    for (const auto& s : p.students)
        students.push_back(studentToJson(s));

    json root{
        {"schemaVersion", p.schemaVersion},
        {"id", p.id},
        {"name", p.name},
        {"createdIso", p.createdIso},
        {"questions", std::move(questions)},
        {"students", std::move(students)},
    };
    return root.dump(indent);
}

bool projectFromJsonString(const std::string& text, Project& out, std::string* err)
{
    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception& e) {
        if (err) *err = std::string("JSON parse error: ") + e.what();
        return false;
    }

    Project p;
    p.schemaVersion = root.value("schemaVersion", 1);
    p.id            = root.value("id", std::string());
    p.name          = root.value("name", std::string());
    p.createdIso    = root.value("createdIso", std::string());

    if (root.contains("questions") && root.at("questions").is_array())
        for (const auto& jq : root.at("questions"))
            p.questions.push_back(questionFromJson(jq));

    if (root.contains("students") && root.at("students").is_array())
        for (const auto& js : root.at("students"))
            p.students.push_back(studentFromJson(js));

    // Migrate pre-v2 files. In v1 the answered count (X/Y) was reference-only and
    // never affected the score; in v2 skipped sub-questions lock out points. To
    // preserve every old grade exactly, treat old cells as all-answered (no
    // deduction) and drop the stale reference count.
    if (p.schemaVersion < 2) {
        for (auto& s : p.students)
            for (size_t j = 0; j < s.cells.size() && j < p.questions.size(); ++j) {
                s.cells[j].subAnswered = p.questions[j].subCount;
                s.cells[j].subChecks.clear();
            }
        p.schemaVersion = 2; // upgraded in memory; the next save writes v2
    }

    ensureShape(p);
    out = std::move(p);
    return true;
}

bool saveProject(const std::string& path, const Project& p, std::string* err)
{
    return writeFileUtf8(path, toJsonString(p), err);
}

bool loadProject(const std::string& path, Project& out, std::string* err)
{
    std::string text;
    if (!readFileUtf8(path, text, err))
        return false;
    return projectFromJsonString(text, out, err);
}

} // namespace gt
