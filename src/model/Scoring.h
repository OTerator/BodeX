#pragma once

// Score computation over the data model. The rules (highest precedence first):
//   1. student.noSubmission  -> the whole row scores 0
//   2. cell.fullTick (green) -> that cell scores the question's full maxPoints
//   3. otherwise             -> the manually entered awarded, clamped to
//                               [0, maxPoints]
// The X/Y sub-questions-answered fraction is reference-only and never affects
// the score.

#include "model/Project.h"

namespace gt {

// awarded clamped into [0, maxPoints].
double clampCell(double awarded, double maxPoints);

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
