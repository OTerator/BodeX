#pragma once

// Core data model for a grading project. Deliberately free of any GUI or
// platform dependency so it can be unit-tested on its own (see tests/).
//
// A project is an M×N grid: N students (rows, IDs 1..N) × M questions
// (columns). Each cell records what the grader entered for that student on that
// question. Building/serialization helpers are inline so the model is header
// only apart from Scoring/Serialization.

#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <random>
#include <cstdint>
#include <cstdio>

namespace gt {

enum class SplitMode { Equal, Custom };

// Base text direction for a cell note. Auto picks left/right-to-left from the first
// strong character (see model/Bidi.h); LTR/RTL force it (the Ctrl+Left/Right-Shift
// toggle in the cell editor). Serialized as an int, so keep the order stable.
enum class TextDir { Auto, LTR, RTL };

// A screenshot attached to a question: either the question itself or a solution
// reference used to verify checks. Stored as a filename inside the project's
// ".assets" directory; may be tagged with the sub-question(s) it covers.
enum class ImageRole { Question, Solution };
struct QuestionImage {
    std::string      file;          // filename within the project's .assets dir
    ImageRole        role = ImageRole::Question;
    std::string      caption;       // optional label
    std::vector<int> subQuestions;  // 0-based indices; empty = whole question
};

// A pool entry of previously-committed per-sub-question note text, offered as a
// clickable suggestion in the cell editor. Append-only (exact-dedup on (sub,text));
// editing a picked suggestion adds a new entry rather than overwriting the original,
// so the pool only grows. sub == -1 is the "general" (whole-cell) note bucket.
struct NoteSuggestion {
    int         sub = -1;
    std::string text;
    TextDir     dir = TextDir::Auto;

    bool operator==(const NoteSuggestion& o) const = default;
};

// One question (column). maxPoints is the total for the question; a question is
// made of subCount sub-questions whose point values are in subPoints. For an
// Equal split every sub-question is worth maxPoints/subCount; for Custom the
// grader assigns each sub-question's value explicitly.
struct Question {
    std::string                title = "Q1";
    double                     maxPoints = 10.0;
    int                        subCount = 1;
    SplitMode                  split = SplitMode::Equal;
    std::vector<double>        subPoints; // size == subCount once normalized
    std::vector<std::string>   subLabels; // size == subCount once normalized; "" = numeric fallback
    std::vector<QuestionImage> images;    // attached screenshots / solution refs
    std::vector<NoteSuggestion> noteSuggestions; // per-sub note suggestion pool (append-only)
    // View state (persisted so a finished column stays put across reopen).
    bool                       folded = false;    // collapsed to a narrow, locked strip
    float                      viewWidth = 190.0f; // base (un-zoomed) column width, px
};

// A note attached to one sub-question of a cell. Sparse: only sub-questions with
// non-empty text get an entry (kept sorted by `sub`, at most one per sub).
struct SubNote {
    int         sub = 0;   // 0-based sub-question index
    std::string text;
    TextDir     dir = TextDir::Auto;

    bool operator==(const SubNote& o) const = default;
};

// One graded cell (student × question). The grader types `awarded`; sub-questions
// default to all-answered and *skipped* ones lock out their points (they cap the
// awardable max — see Scoring `effectiveMax`). `subAnswered` (the X in "X/Y") is
// the answered count and drives the deduction for an **Equal** split; `subChecks`
// (per-sub-question answered flags, 1 = answered) drives it for a **Custom** split
// and is empty otherwise. `fullTick` (green tick) overrides to the question's full
// points. `lastPage` is a free-text resume marker. `note` is the "general" (whole-
// cell) note; `subNotes` holds additional per-sub-question notes.
struct Cell {
    bool              fullTick = false;
    double            awarded = 0.0;
    int               subAnswered = 0;   // answered count (default set to subCount)
    std::vector<char> subChecks;         // Custom split only: per-sub answered flags
    std::string       lastPage;
    std::string       note;
    TextDir           noteDir = TextDir::Auto; // note's base direction (Hebrew/RTL)
    std::vector<SubNote> subNotes;        // per-sub-question notes (sparse, sorted by sub)
    bool              touched = false;   // grader entered something (blank vs explicit 0)

    // Value equality (all members are plain values) — used by the undo/redo
    // history to detect whether a grading edit actually changed the grid.
    bool operator==(const Cell& o) const = default;
};

// One student (row). Just an ID plus a per-question cell vector. noSubmission
// forces the whole row's score to 0.
struct Student {
    int               id = 0;
    bool              noSubmission = false;
    std::vector<Cell> cells; // one per question

    bool operator==(const Student& o) const = default; // compares every cell
};

struct Project {
    int                   schemaVersion = 2;
    std::string           id;         // short random id; names the staging assets dir
    std::string           name;
    std::string           createdIso;
    std::vector<Question> questions;
    std::vector<Student>  students; // size N; each student's cells size == questions size
};

// A random 16-hex-digit id for a project (used to name its staging assets dir
// before the project is first saved). The inline-function-local static gives a
// single shared RNG across translation units.
inline std::string newProjectId() {
    static std::mt19937_64 rng{ std::random_device{}() };
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(rng()));
    return std::string(buf);
}

// Points a single sub-question is worth under an equal split.
inline double equalShare(const Question& q) {
    return q.subCount > 0 ? q.maxPoints / q.subCount : q.maxPoints;
}

// Make a question self-consistent: subCount >= 1 and subPoints sized to it.
// Equal split (re)derives every share; Custom preserves existing values and
// pads/truncates to subCount.
inline void normalizeQuestion(Question& q) {
    if (q.subCount < 1)
        q.subCount = 1;
    if (q.split == SplitMode::Equal) {
        q.subPoints.assign(static_cast<size_t>(q.subCount), equalShare(q));
    } else {
        if (static_cast<int>(q.subPoints.size()) != q.subCount)
            q.subPoints.resize(static_cast<size_t>(q.subCount), equalShare(q));
    }
    if (static_cast<int>(q.subLabels.size()) != q.subCount)
        q.subLabels.resize(static_cast<size_t>(q.subCount)); // pad ""/truncate, like subPoints
    // Drop image sub-question tags that fall outside [0, subCount).
    for (auto& img : q.images) {
        std::vector<int> keep;
        for (int idx : img.subQuestions)
            if (idx >= 0 && idx < q.subCount)
                keep.push_back(idx);
        img.subQuestions = std::move(keep);
    }
    // Drop suggestion-pool entries that no longer map to a valid sub-question (or
    // never had text) — sub == -1 (the general bucket) is always kept.
    std::vector<NoteSuggestion> keepSugg;
    for (auto& sug : q.noteSuggestions)
        if (sug.sub >= -1 && sug.sub < q.subCount && !sug.text.empty())
            keepSugg.push_back(std::move(sug));
    q.noteSuggestions = std::move(keepSugg);
}

// The sub-question's display header: its free-text label if set, else the 1-based
// number as a fallback ("1", "2", ...). Callers prefix it before the note text/input.
inline std::string subHeader(const Question& q, int k) {
    if (k >= 0 && k < static_cast<int>(q.subLabels.size()) && !q.subLabels[static_cast<size_t>(k)].empty())
        return q.subLabels[static_cast<size_t>(k)];
    return std::to_string(k + 1);
}

// The cell's note for sub-question `sub`, or nullptr if none is set.
inline SubNote* findSubNote(Cell& c, int sub) {
    for (auto& sn : c.subNotes)
        if (sn.sub == sub) return &sn;
    return nullptr;
}
inline const SubNote* findSubNote(const Cell& c, int sub) {
    for (const auto& sn : c.subNotes)
        if (sn.sub == sub) return &sn;
    return nullptr;
}

// Set (or clear, if `text` is empty) the cell's note for sub-question `sub`, keeping
// `subNotes` sorted by sub with at most one entry per sub.
inline void setSubNote(Cell& c, int sub, const std::string& text, TextDir dir) {
    auto it = std::find_if(c.subNotes.begin(), c.subNotes.end(),
                           [sub](const SubNote& sn) { return sn.sub == sub; });
    if (text.empty()) {
        if (it != c.subNotes.end())
            c.subNotes.erase(it);
        return;
    }
    if (it != c.subNotes.end()) {
        it->text = text;
        it->dir = dir;
        return;
    }
    SubNote sn; sn.sub = sub; sn.text = text; sn.dir = dir;
    auto pos = std::lower_bound(c.subNotes.begin(), c.subNotes.end(), sub,
                                [](const SubNote& a, int s) { return a.sub < s; });
    c.subNotes.insert(pos, std::move(sn));
}

// True if the cell has a general note or any per-sub-question note.
inline bool cellHasAnyNote(const Cell& c) { return !c.note.empty() || !c.subNotes.empty(); }

// Add `text` to the question's note-suggestion pool for `sub` (-1 = general), unless
// it's empty or an exact (sub, text) duplicate already exists. Returns whether it was
// added; the pool is append-only (an edited pick never overwrites the original).
inline bool addNoteSuggestion(Question& q, int sub, const std::string& text, TextDir dir) {
    if (text.empty())
        return false;
    for (const auto& s : q.noteSuggestions)
        if (s.sub == sub && s.text == text)
            return false;
    NoteSuggestion s; s.sub = sub; s.text = text; s.dir = dir;
    q.noteSuggestions.push_back(std::move(s));
    return true;
}

// A fresh cell for question q, defaulting to **all sub-questions answered** so
// grading starts from full marks and subtracts skipped sub-questions. For a Custom
// split every per-sub-question tick starts answered; Equal uses the count only.
inline Cell blankCell(const Question& q) {
    Cell c;
    c.subAnswered = q.subCount;
    if (q.split == SplitMode::Custom)
        c.subChecks.assign(static_cast<size_t>(q.subCount), 1);
    return c;
}

// Points locked out by skipped sub-questions (they cap the awardable max). Equal:
// each skipped sub-question is worth equalShare; Custom: sum the specific skipped
// subPoints. Header-only so the (GUI-free) helpers and tests can share it.
inline double lockedSubPoints(const Question& q, const Cell& c) {
    if (q.subCount <= 0) return 0.0;
    if (q.split == SplitMode::Custom && static_cast<int>(c.subChecks.size()) == q.subCount) {
        double locked = 0.0;
        for (int k = 0; k < q.subCount; ++k)
            if (!c.subChecks[static_cast<size_t>(k)] &&
                k < static_cast<int>(q.subPoints.size()))
                locked += q.subPoints[static_cast<size_t>(k)];
        return locked;
    }
    // Equal split (or a Custom cell without a valid mask): use the answered count.
    int skipped = q.subCount - c.subAnswered;
    if (skipped < 0) skipped = 0;
    return skipped * equalShare(q);
}

// Guarantee the grid is rectangular and every derived field is consistent:
// normalize questions, assign student IDs 1..N, size each row's cells to match the
// number of questions, and keep each cell's sub-question fields (subAnswered /
// subChecks) consistent with its question's split and subCount.
inline void ensureShape(Project& p) {
    for (auto& q : p.questions)
        normalizeQuestion(q);
    for (size_t i = 0; i < p.students.size(); ++i) {
        Student& s = p.students[i];
        if (s.id == 0)
            s.id = static_cast<int>(i) + 1;
        // Grow/shrink the row to match the questions; new cells default all-answered.
        while (s.cells.size() < p.questions.size())
            s.cells.push_back(blankCell(p.questions[s.cells.size()]));
        if (s.cells.size() > p.questions.size())
            s.cells.resize(p.questions.size());
        for (size_t j = 0; j < p.questions.size(); ++j) {
            const Question& q = p.questions[j];
            Cell& c = s.cells[j];
            if (c.subAnswered < 0) c.subAnswered = 0;
            if (c.subAnswered > q.subCount) c.subAnswered = q.subCount;
            if (q.split == SplitMode::Custom) {
                if (static_cast<int>(c.subChecks.size()) != q.subCount)
                    c.subChecks.assign(static_cast<size_t>(q.subCount), 1);
                int answered = 0;
                for (char v : c.subChecks) if (v) ++answered;
                c.subAnswered = answered; // keep the count in sync for display
            } else {
                c.subChecks.clear();
            }
            // Rebuild subNotes: valid sub range, non-empty text, first entry per sub,
            // sorted (mirrors the (sub,text) pool pruning in normalizeQuestion).
            if (!c.subNotes.empty()) {
                std::vector<SubNote> keep;
                for (auto& sn : c.subNotes) {
                    if (sn.sub < 0 || sn.sub >= q.subCount || sn.text.empty())
                        continue;
                    bool dup = false;
                    for (const auto& k : keep)
                        if (k.sub == sn.sub) { dup = true; break; }
                    if (!dup)
                        keep.push_back(sn);
                }
                std::sort(keep.begin(), keep.end(),
                         [](const SubNote& a, const SubNote& b) { return a.sub < b.sub; });
                c.subNotes = std::move(keep);
            }
        }
    }
}

// Build a blank project: N students with IDs 1..N and an empty cell per
// question. Questions are normalized on the way in.
inline Project makeProject(std::string name, int studentCount, std::vector<Question> questions) {
    Project p;
    p.name = std::move(name);
    p.schemaVersion = 2;
    p.id = newProjectId();
    for (auto& q : questions)
        normalizeQuestion(q);
    p.questions = std::move(questions);
    if (studentCount < 0)
        studentCount = 0;
    p.students.resize(static_cast<size_t>(studentCount));
    for (int i = 0; i < studentCount; ++i) {
        Student& s = p.students[static_cast<size_t>(i)];
        s.id = i + 1;
        s.cells.clear();
        s.cells.reserve(p.questions.size());
        for (const auto& q : p.questions)   // start every cell at all-answered
            s.cells.push_back(blankCell(q));
    }
    return p;
}

// First cell (row, col) where two grading grids differ, scanning row-major;
// {-1, -1} if identical. A row-level difference (e.g. noSubmission toggled, or a
// changed row length) reports {row, 0}. Used by undo/redo to move the selection to
// the cell that changed so the user sees what was reverted. GUI-free / testable.
inline std::pair<int, int> firstGradingDiff(const std::vector<Student>& a,
                                            const std::vector<Student>& b) {
    const size_t rows = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < rows; ++i) {
        const Student& sa = a[i];
        const Student& sb = b[i];
        if (sa.noSubmission != sb.noSubmission || sa.cells.size() != sb.cells.size())
            return { static_cast<int>(i), 0 };
        for (size_t j = 0; j < sa.cells.size(); ++j)
            if (!(sa.cells[j] == sb.cells[j]))
                return { static_cast<int>(i), static_cast<int>(j) };
    }
    if (a.size() != b.size())
        return { static_cast<int>(rows), 0 }; // rows added/removed past the common prefix
    return { -1, -1 };
}

} // namespace gt
