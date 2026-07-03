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

double cellPoints(const Student& s, const Question& q, const Cell& c)
{
    if (s.noSubmission)
        return 0.0;
    if (c.fullTick)
        return q.maxPoints;
    return clampCell(c.awarded, q.maxPoints);
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
    return c.awarded > q.maxPoints + 1e-9;
}

ClassStats classStats(const Project& p)
{
    ClassStats st;
    st.students = static_cast<int>(p.students.size());
    if (p.students.empty())
        return st;

    double sum = 0.0;
    bool first = true;
    for (const auto& s : p.students) {
        const double total = studentTotal(p, s);
        sum += total;
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
    return st;
}

} // namespace gt
