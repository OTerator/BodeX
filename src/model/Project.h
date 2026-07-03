#pragma once

// Core data model for a grading project. Deliberately free of any GUI or
// platform dependency so it can be unit-tested on its own (see tests/).
//
// A project is an M×N grid: N students (rows, IDs 1..N) × M questions
// (columns). Each cell records what the grader entered for that student on that
// question. Building/serialization helpers are inline so the model is header
// only apart from Scoring/Serialization.

#include <string>
#include <vector>
#include <utility>

namespace gt {

enum class SplitMode { Equal, Custom };

// One question (column). maxPoints is the total for the question; a question is
// made of subCount sub-questions whose point values are in subPoints. For an
// Equal split every sub-question is worth maxPoints/subCount; for Custom the
// grader assigns each sub-question's value explicitly.
struct Question {
    std::string         title = "Q1";
    double              maxPoints = 10.0;
    int                 subCount = 1;
    SplitMode           split = SplitMode::Equal;
    std::vector<double> subPoints; // size == subCount once normalized
};

// One graded cell (student × question). Per the chosen scoring model the grader
// types `awarded` directly; `subAnswered` (the X in "X/Y") is stored for
// reference only and does not feed the score. `fullTick` (green tick) overrides
// to the question's full points. `lastPage` is a free-text resume marker.
struct Cell {
    bool        fullTick = false;
    double      awarded = 0.0;
    int         subAnswered = 0;
    std::string lastPage;
    std::string note;
    bool        touched = false; // grader entered something (blank vs explicit 0)
};

// One student (row). Just an ID plus a per-question cell vector. noSubmission
// forces the whole row's score to 0.
struct Student {
    int               id = 0;
    bool              noSubmission = false;
    std::vector<Cell> cells; // one per question
};

struct Project {
    int                   schemaVersion = 1;
    std::string           name;
    std::string           createdIso;
    std::vector<Question> questions;
    std::vector<Student>  students; // size N; each student's cells size == questions size
};

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
}

// Guarantee the grid is rectangular and every derived field is consistent:
// normalize questions, assign student IDs 1..N, and size each row's cells to
// match the number of questions.
inline void ensureShape(Project& p) {
    for (auto& q : p.questions)
        normalizeQuestion(q);
    for (size_t i = 0; i < p.students.size(); ++i) {
        Student& s = p.students[i];
        if (s.id == 0)
            s.id = static_cast<int>(i) + 1;
        if (s.cells.size() != p.questions.size())
            s.cells.resize(p.questions.size());
    }
}

// Build a blank project: N students with IDs 1..N and an empty cell per
// question. Questions are normalized on the way in.
inline Project makeProject(std::string name, int studentCount, std::vector<Question> questions) {
    Project p;
    p.name = std::move(name);
    p.schemaVersion = 1;
    for (auto& q : questions)
        normalizeQuestion(q);
    p.questions = std::move(questions);
    if (studentCount < 0)
        studentCount = 0;
    p.students.resize(static_cast<size_t>(studentCount));
    for (int i = 0; i < studentCount; ++i) {
        p.students[static_cast<size_t>(i)].id = i + 1;
        p.students[static_cast<size_t>(i)].cells.assign(p.questions.size(), Cell{});
    }
    return p;
}

} // namespace gt
