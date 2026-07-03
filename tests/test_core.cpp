// Non-GUI unit tests for the grading core: scoring rules and JSON round-trip.
// Built and run via `mingw32-make test`. No test framework — a couple of macros
// keep it dependency-free.

#include <cstdio>
#include <cmath>
#include <string>

#include "model/Project.h"
#include "model/Scoring.h"
#include "model/Serialization.h"
#include "model/AppConfig.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
#define CHECK_NEAR(a, b) do { \
    ++g_checks; \
    if (!near((a), (b))) { ++g_failures; \
        std::printf("FAIL %s:%d  %s == %g, expected %g\n", __FILE__, __LINE__, #a, (double)(a), (double)(b)); } \
} while (0)

using namespace gt;

static Project buildSample()
{
    Question q1;
    q1.title = "Q1"; q1.maxPoints = 20.0; q1.subCount = 5; q1.split = SplitMode::Equal;

    Question q2;
    q2.title = "Q2"; q2.maxPoints = 10.0; q2.subCount = 2; q2.split = SplitMode::Custom;
    q2.subPoints = {7.0, 3.0};

    Project p = makeProject("Sample Exercise", 3, {q1, q2});
    p.createdIso = "2026-07-03 12:00";

    // Student 1: manual 12/20 on Q1 (3/5 answered), green tick on Q2 -> 22.
    p.students[0].cells[0].awarded = 12.0;
    p.students[0].cells[0].subAnswered = 3;
    p.students[0].cells[0].touched = true;
    p.students[0].cells[0].lastPage = "p.14";
    p.students[0].cells[0].note = "recheck part b";
    p.students[0].cells[1].fullTick = true;
    p.students[0].cells[1].touched = true;

    // Student 2: no submission -> total 0 regardless of any values entered.
    p.students[1].noSubmission = true;
    p.students[1].cells[0].awarded = 20.0;

    // Student 3: over/under-range values must clamp -> 20 + 0 = 20.
    p.students[2].cells[0].awarded = 25.0; // over max 20 -> 20
    p.students[2].cells[0].touched = true;
    p.students[2].cells[1].awarded = -5.0; // below 0 -> 0
    p.students[2].cells[1].touched = true;

    return p;
}

static void testScoring()
{
    Project p = buildSample();

    // Normalized equal split.
    CHECK(p.questions[0].subPoints.size() == 5);
    CHECK_NEAR(p.questions[0].subPoints[0], 4.0);
    // Custom split preserved.
    CHECK(p.questions[1].split == SplitMode::Custom);
    CHECK_NEAR(p.questions[1].subPoints[0], 7.0);
    CHECK_NEAR(p.questions[1].subPoints[1], 3.0);

    // Green tick yields full question points.
    CHECK_NEAR(cellPoints(p.students[0], p.questions[1], p.students[0].cells[1]), 10.0);
    // Manual value passes through.
    CHECK_NEAR(cellPoints(p.students[0], p.questions[0], p.students[0].cells[0]), 12.0);

    CHECK_NEAR(studentTotal(p, 0), 22.0);
    CHECK_NEAR(studentTotal(p, 1), 0.0);   // no submission
    CHECK_NEAR(studentTotal(p, 2), 20.0);  // 25 clamped to 20, -5 clamped to 0

    CHECK_NEAR(projectMaxTotal(p), 30.0);

    CHECK(cellOverMax(p.questions[0], p.students[2].cells[0])); // 25 > 20
    CHECK(!cellOverMax(p.questions[1], p.students[0].cells[1])); // fullTick never "over"

    ClassStats st = classStats(p);
    CHECK(st.students == 3);
    CHECK(st.graded == 3);            // all three touched or no-submission
    CHECK_NEAR(st.minScore, 0.0);
    CHECK_NEAR(st.maxScore, 22.0);
    CHECK_NEAR(st.average, (22.0 + 0.0 + 20.0) / 3.0);
}

static void testRoundTrip()
{
    Project p = buildSample();

    std::string text = toJsonString(p);
    CHECK(!text.empty());

    Project p2;
    std::string err;
    bool ok = projectFromJsonString(text, p2, &err);
    CHECK(ok);
    if (!ok) { std::printf("  parse error: %s\n", err.c_str()); return; }

    CHECK(p2.name == p.name);
    CHECK(p2.createdIso == p.createdIso);
    CHECK(p2.schemaVersion == p.schemaVersion);
    CHECK(p2.questions.size() == 2);
    CHECK(p2.students.size() == 3);

    CHECK(p2.questions[1].split == SplitMode::Custom);
    CHECK(p2.questions[1].subPoints.size() == 2);
    CHECK_NEAR(p2.questions[1].subPoints[0], 7.0);

    CHECK(p2.students[0].id == 1);
    CHECK(p2.students[1].noSubmission == true);
    CHECK(p2.students[0].cells[1].fullTick == true);
    CHECK_NEAR(p2.students[0].cells[0].awarded, 12.0);
    CHECK(p2.students[0].cells[0].subAnswered == 3);
    CHECK(p2.students[0].cells[0].lastPage == "p.14");
    CHECK(p2.students[0].cells[0].note == "recheck part b");

    // Scores must be identical after a round-trip.
    CHECK_NEAR(studentTotal(p2, 0), 22.0);
    CHECK_NEAR(studentTotal(p2, 1), 0.0);
    CHECK_NEAR(studentTotal(p2, 2), 20.0);
}

static void testFileRoundTrip()
{
    Project p = buildSample();
    const std::string path = "build/_test_roundtrip.json";

    std::string err;
    CHECK(saveProject(path, p, &err));

    Project p2;
    CHECK(loadProject(path, p2, &err));
    CHECK(p2.students.size() == 3);
    CHECK_NEAR(studentTotal(p2, 0), 22.0);
    CHECK_NEAR(studentTotal(p2, 1), 0.0);
    CHECK_NEAR(studentTotal(p2, 2), 20.0);
    CHECK(p2.students[0].cells[0].lastPage == "p.14");
    CHECK(p2.questions[1].split == SplitMode::Custom);

    std::remove(path.c_str());

    // Loading a missing file must fail cleanly, not crash.
    Project p3;
    std::string err2;
    CHECK(!loadProject("build/_does_not_exist_zzz.json", p3, &err2));
}

static void testMalformedJson()
{
    Project p;
    std::string err;
    CHECK(!projectFromJsonString("{ this is not json", p, &err));
    CHECK(!err.empty());
}

// Regression: opening a "Recent projects" entry used to pass a reference into
// config.recentProjects straight into addRecentProject(), which then erased/
// inserted into that same vector - a dangling reference / use-after-free that
// crashed. addRecentProject() must copy the input first.
static void testRecentAliasSafe()
{
    AppConfig cfg;
    cfg.recentProjects = { "a.json", "b.json", "c.json" };

    // Alias element [2] (the exact crashing pattern).
    addRecentProject(cfg, cfg.recentProjects[2]);
    CHECK(cfg.recentProjects.size() == 3);
    CHECK(cfg.recentProjects[0] == "c.json");

    // Alias the current front; list stays stable.
    addRecentProject(cfg, cfg.recentProjects[0]);
    CHECK(cfg.recentProjects.size() == 3);
    CHECK(cfg.recentProjects[0] == "c.json");

    // A genuinely new path prepends.
    addRecentProject(cfg, "d.json");
    CHECK(cfg.recentProjects.size() == 4);
    CHECK(cfg.recentProjects[0] == "d.json");
}

int main()
{
    testScoring();
    testRoundTrip();
    testFileRoundTrip();
    testMalformedJson();
    testRecentAliasSafe();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
