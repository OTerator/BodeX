#include "model/Scoring.h"

namespace gt {

double clampCell(double awarded, double maxPoints)
{
    if (awarded < 0.0)
        return 0.0;
    if (awarded > maxPoints)
        return maxPoints;
    return awarded;
}

double effectiveMax(const Question& q, const Cell& c)
{
    double m = q.maxPoints - lockedSubPoints(q, c);
    return m < 0.0 ? 0.0 : m;
}

double cellPoints(const Student& s, const Question& q, const Cell& c)
{
    if (s.noSubmission)
        return 0.0;
    if (c.fullTick)
        return q.maxPoints;
    return clampCell(c.awarded, effectiveMax(q, c));
}

double studentTotal(const Project& p, const Student& s)
{
    if (s.noSubmission)
        return 0.0;
    double sum = 0.0;
    const size_t n = s.cells.size() < p.questions.size() ? s.cells.size() : p.questions.size();
    for (size_t j = 0; j < n; ++j)
        sum += cellPoints(s, p.questions[j], s.cells[j]);
    return sum;
}

double studentTotal(const Project& p, size_t studentIndex)
{
    if (studentIndex >= p.students.size())
        return 0.0;
    return studentTotal(p, p.students[studentIndex]);
}

double projectMaxTotal(const Project& p)
{
    double sum = 0.0;
    for (const auto& q : p.questions)
        sum += q.maxPoints;
    return sum;
}

bool cellOverMax(const Question& q, const Cell& c)
{
    if (c.fullTick)
        return false;
    return c.awarded > effectiveMax(q, c) + 1e-9;
}

bool isFullMarks(const Question& q, const Cell& c)
{
    if (c.fullTick)
        return true;
    // Earns full marks: no sub-question locked (so the ceiling is the full max) and
    // awarded lands exactly on maxPoints. Over-max stays an orange warning, not full.
    return effectiveMax(q, c) >= q.maxPoints - 1e-9
        && c.awarded >= q.maxPoints - 1e-9
        && c.awarded <= q.maxPoints + 1e-9;
}

static void clampAwardedToEffMax(const Question& q, Cell& c)
{
    const double em = effectiveMax(q, c);
    if (c.awarded < 0.0) c.awarded = 0.0;
    if (c.awarded > em)  c.awarded = em;
}

void setAnsweredCount(const Question& q, Cell& c, int newAnswered)
{
    if (newAnswered < 0)          newAnswered = 0;
    if (newAnswered > q.subCount) newAnswered = q.subCount;
    if (!c.touched || c.fullTick) {
        // First real interaction on a blank or full cell: the still-answered
        // sub-questions are assumed correct, so award the full effective max for the
        // new count (blank cells start at 0, not the implied full — sync them here).
        c.fullTick = false;
        c.subAnswered = newAnswered;
        c.awarded = effectiveMax(q, c);
    } else {
        // Subsequent edits deduct/add just the changed share (the "8 -> 5.5" case).
        c.awarded += (newAnswered - c.subAnswered) * equalShare(q); // -: lock, +: unlock
        c.subAnswered = newAnswered;
        if (newAnswered < q.subCount) c.fullTick = false;
        clampAwardedToEffMax(q, c);
    }
    c.touched = true;
}

void setSubAnswered(const Question& q, Cell& c, int index, bool answered)
{
    if (index < 0 || index >= q.subCount)
        return;
    if (static_cast<int>(c.subChecks.size()) != q.subCount)
        c.subChecks.assign(static_cast<size_t>(q.subCount), 1);
    const bool sync = !c.touched || c.fullTick; // first interaction: assume ticked correct
    const bool was  = c.subChecks[static_cast<size_t>(index)] != 0;
    c.subChecks[static_cast<size_t>(index)] = answered ? 1 : 0;
    int cnt = 0; for (char v : c.subChecks) if (v) ++cnt;
    c.subAnswered = cnt;               // keep the count for X/Y display
    if (sync) {
        // Blank or full cell: award the full effective max (all ticked parts correct)
        // instead of nudging the 0/implied-full placeholder by one sub-question.
        c.fullTick = false;
        c.awarded = effectiveMax(q, c);
    } else {
        if (was != answered) {
            const double kpts = index < static_cast<int>(q.subPoints.size())
                                ? q.subPoints[static_cast<size_t>(index)] : 0.0;
            c.awarded += answered ? kpts : -kpts;
        }
        if (!answered) c.fullTick = false;
        clampAwardedToEffMax(q, c);
    }
    c.touched = true;
}

void stepAwarded(const Question& q, Cell& c, double delta)
{
    const double em = effectiveMax(q, c);
    double base;
    if (c.fullTick)
        base = q.maxPoints;                       // green full: dock/keep from full
    else if (!c.touched)
        base = (delta > 0.0) ? 0.0 : em;          // blank: '+' builds from 0, '-' from full
    else
        base = c.awarded;                         // graded (incl. a typed full)
    c.awarded  = clampCell(base + delta, em);
    c.fullTick = false;                           // numeric now; green comes from isFullMarks
    c.touched  = true;
}

ClassStats classStats(const Project& p)
{
    ClassStats st;
    st.students = static_cast<int>(p.students.size());
    if (p.students.empty())
        return st;

    double sum = 0.0;
    double submittedSum = 0.0;
    bool first = true;
    for (const auto& s : p.students) {
        const double total = studentTotal(p, s);
        sum += total;
        if (!s.noSubmission) {
            submittedSum += total;
            ++st.submitted;
        }
        if (first) {
            st.minScore = st.maxScore = total;
            first = false;
        } else {
            if (total < st.minScore) st.minScore = total;
            if (total > st.maxScore) st.maxScore = total;
        }

        bool touched = s.noSubmission;
        if (!touched) {
            for (const auto& c : s.cells) {
                if (c.touched || c.fullTick) { touched = true; break; }
            }
        }
        if (touched)
            ++st.graded;
    }
    st.average = sum / static_cast<double>(p.students.size());
    st.averageSubmitted = st.submitted > 0
        ? submittedSum / static_cast<double>(st.submitted) : 0.0;
    return st;
}

std::vector<QuestionStats> perQuestionStats(const Project& p)
{
    std::vector<QuestionStats> out(p.questions.size());
    for (size_t j = 0; j < p.questions.size(); ++j) {
        out[j].maxPoints = p.questions[j].maxPoints;
        out[j].subCount  = p.questions[j].subCount;
    }

    for (const auto& s : p.students) {
        if (s.noSubmission)
            continue;
        const size_t n = s.cells.size() < p.questions.size() ? s.cells.size()
                                                             : p.questions.size();
        for (size_t j = 0; j < n; ++j) {
            const Question& q = p.questions[j];
            const Cell&     c = s.cells[j];
            out[j].average += cellPoints(s, q, c);
            ++out[j].counted;
            // Only cells the grader actually engaged with feed the answer rate — a
            // never-touched cell defaults to subAnswered==subCount and would inflate it.
            if (c.touched || c.fullTick) {
                out[j].avgAnswered += static_cast<double>(c.subAnswered);
                ++out[j].attempted;
            }
        }
    }
    for (auto& qs : out) {
        if (qs.counted > 0)
            qs.average /= static_cast<double>(qs.counted);
        if (qs.attempted > 0)
            qs.avgAnswered /= static_cast<double>(qs.attempted);
    }
    return out;
}

} // namespace gt
