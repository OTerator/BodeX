#pragma once

// Score computation over the data model. The rules (highest precedence first):
//   1. student.noSubmission  -> the whole row scores 0
//   2. cell.fullTick (green) -> that cell scores the question's full maxPoints
//   3. otherwise             -> the manually entered awarded, clamped to
//                               [0, effectiveMax] (see below)
// Skipped sub-questions lock out their points: they lower the awardable ceiling
// from maxPoints to effectiveMax (Equal: each skip = maxPoints/subCount; Custom:
// the specific skipped subPoints). See `lockedSubPoints` in Project.h.

#include "model/Project.h"

namespace gt {

// awarded clamped into [0, maxPoints].
double clampCell(double awarded, double maxPoints);

// The awardable ceiling for a cell: maxPoints minus points locked out by skipped
// sub-questions. Equals maxPoints when every sub-question is answered.
double effectiveMax(const Question& q, const Cell& c);

// Effective points for one cell, applying the rules above.
double cellPoints(const Student& s, const Question& q, const Cell& c);

// Sum of a student's cell points (0 if no-submission). Safe if the row has
// fewer cells than there are questions.
double studentTotal(const Project& p, const Student& s);
double studentTotal(const Project& p, size_t studentIndex);

// Sum of every question's maxPoints — the denominator in "score / max".
double projectMaxTotal(const Project& p);

// True when the grader typed more than the question is worth (for a visual
// warning; storage is not silently capped).
bool cellOverMax(const Question& q, const Cell& c);

// Editor helpers: change which sub-questions count as answered while deducting
// from full marks. Locking a sub-question subtracts its points from `awarded`
// (unlocking adds them back); `awarded` is then clamped into [0, effectiveMax] and
// `fullTick` is cleared if any sub-question is now skipped. Both mark the cell
// touched. GUI-free so the "8 -> 5.5" behavior is unit-tested.
void setAnsweredCount(const Question& q, Cell& c, int newAnswered);        // Equal split
void setSubAnswered(const Question& q, Cell& c, int index, bool answered); // Custom split

// Simple class-wide summary for the status bar.
struct ClassStats {
    int    students = 0; // total rows
    int    graded = 0;   // rows marked no-submission or with any touched cell
    double average = 0.0;
    double minScore = 0.0;
    double maxScore = 0.0;
};
ClassStats classStats(const Project& p);

} // namespace gt
