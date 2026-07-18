// Non-GUI unit tests for the grading core: scoring rules and JSON round-trip.
// Built and run via `mingw32-make test`. No test framework — a couple of macros
// keep it dependency-free.

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>

#include "model/Project.h"
#include "model/Scoring.h"
#include "model/Serialization.h"
#include "model/AppConfig.h"
#include "model/Bidi.h"
#include "model/Assets.h"  // buildBmpFromDib (header-inline, pure)

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

    // Attach images to Q2 (subCount 2). The 9 is an out-of-range sub-question tag
    // that ensureShape() must drop on load.
    QuestionImage imgQ; imgQ.file = "q2-question.png"; imgQ.role = ImageRole::Question; imgQ.caption = "stem";
    QuestionImage imgS; imgS.file = "q2-sol.png"; imgS.role = ImageRole::Solution; imgS.caption = "answer key";
    imgS.subQuestions = {0, 1, 9};
    p.questions[1].images = {imgQ, imgS};

    // Sub-question labels on Q1: sub 0 gets a Hebrew label (byte-escaped so the file
    // stays source-encoding-agnostic; Aleph = U+05D0 = "\xD7\x90"), the rest fall
    // back to their numeric header ("2".."5").
    p.questions[0].subLabels[0] = "\xD7\x90";

    // A per-sub note-suggestion pool on Q1: two sub-specific entries plus one in the
    // general (-1) bucket.
    p.questions[0].noteSuggestions = {
        NoteSuggestion{0, "recheck this part", TextDir::Auto},
        NoteSuggestion{2, "\xD7\x90\xD7\x91\xD7\x92", TextDir::RTL},
        NoteSuggestion{-1, "well done overall", TextDir::Auto},
    };

    // Student 1: Q1 awarded 12 with 3/5 answered (2 skipped -> 8 locked, effMax 12,
    // so 12 caps to 12); green tick on Q2 -> 10. Total 22.
    p.students[0].cells[0].awarded = 12.0;
    p.students[0].cells[0].subAnswered = 3;
    p.students[0].cells[0].touched = true;
    p.students[0].cells[0].lastPage = "p.14";
    p.students[0].cells[0].note = "recheck part b";
    p.students[0].cells[0].noteDir = TextDir::RTL;
    // One per-sub-question note (RTL), on sub-question 2 (0-based).
    p.students[0].cells[0].subNotes = { SubNote{2, "\xD7\x90\xD7\x91\xD7\x92", TextDir::RTL} };
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
    CHECK(st.submitted == 2);         // student 2 is no-submission -> excluded
    CHECK_NEAR(st.minScore, 0.0);
    CHECK_NEAR(st.maxScore, 22.0);
    CHECK_NEAR(st.average, (22.0 + 0.0 + 20.0) / 3.0);
    // avg over submitters skips the no-submission 0 (denominator 2, not 3).
    CHECK_NEAR(st.averageSubmitted, (22.0 + 20.0) / 2.0);

    // Per-question stats are averaged over the 2 submitters only (the no-submission
    // row never counts). Q1: (12 + 20)/2 = 16, answered (3 + 5)/2 = 4;
    // Q2: full-tick 10 and clamped-to-0 -> (10 + 0)/2 = 5, answered (2 + 2)/2 = 2.
    std::vector<QuestionStats> qs = perQuestionStats(p);
    CHECK(qs.size() == 2);
    CHECK(qs[0].counted == 2);
    CHECK(qs[0].attempted == 2);      // both submitters touched Q1
    CHECK_NEAR(qs[0].maxPoints, 20.0);
    CHECK_NEAR(qs[0].average, 16.0);
    CHECK_NEAR(qs[0].avgAnswered, 4.0);
    CHECK(qs[1].counted == 2);
    CHECK(qs[1].attempted == 2);      // student 0 full-tick, student 2 touched
    CHECK_NEAR(qs[1].maxPoints, 10.0);
    CHECK_NEAR(qs[1].average, 5.0);
    CHECK_NEAR(qs[1].avgAnswered, 2.0);
}

// The answer rate (avgAnswered) must exclude cells the grader never touched: a blank
// cell defaults to subAnswered==subCount, which would otherwise inflate the rate.
static void testPerQuestionAnswered()
{
    Question q; q.title = "Q1"; q.maxPoints = 15.0; q.subCount = 3;
    Project p = makeProject("Answered", 2, {q});

    // Student 1 attempted Q1: 2 of 3 sub-questions answered (one skipped).
    setAnsweredCount(p.questions[0], p.students[0].cells[0], 2);
    // subAnswered defaults to 3 for the still-blank cell; cellPoints on the touched
    // cell is effectiveMax for 2/3 = 10.
    CHECK(p.students[0].cells[0].touched);
    CHECK(p.students[0].cells[0].subAnswered == 2);

    // Student 2 left Q1 blank (untouched) — its subAnswered is the default 3.
    CHECK(!p.students[1].cells[0].touched);
    CHECK(p.students[1].cells[0].subAnswered == 3);

    std::vector<QuestionStats> qs = perQuestionStats(p);
    CHECK(qs.size() == 1);
    CHECK(qs[0].counted == 2);        // both submitters feed `average`
    CHECK(qs[0].attempted == 1);      // only student 1 engaged with Q1
    CHECK_NEAR(qs[0].avgAnswered, 2.0); // blank excluded -> 2/1, NOT (2 + 3)/2 = 2.5
    // average still counts the blank as a 0: (10 + 0) / 2 = 5.
    CHECK_NEAR(qs[0].average, 5.0);
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
    CHECK(p2.students[0].cells[0].noteDir == TextDir::RTL);
    CHECK(p2.students[0].cells[0].subNotes.size() == 1);
    CHECK(p2.students[0].cells[0].subNotes[0].sub == 2);
    CHECK(p2.students[0].cells[0].subNotes[0].text == "\xD7\x90\xD7\x91\xD7\x92");
    CHECK(p2.students[0].cells[0].subNotes[0].dir == TextDir::RTL);
    CHECK(p2.questions[0].subLabels.size() == 5);
    CHECK(p2.questions[0].subLabels[0] == "\xD7\x90");
    CHECK(p2.questions[0].noteSuggestions.size() == 3);
    CHECK(p2.questions[0].noteSuggestions[2].sub == -1);
    CHECK(p2.questions[0].noteSuggestions[2].text == "well done overall");

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
    CHECK(p2.students[0].cells[0].subNotes.size() == 1);
    CHECK(p2.questions[0].subLabels[0] == "\xD7\x90");
    CHECK(p2.questions[0].noteSuggestions.size() == 3);

    std::remove(path.c_str());

    // Loading a missing file must fail cleanly, not crash.
    Project p3;
    std::string err2;
    CHECK(!loadProject("build/_does_not_exist_zzz.json", p3, &err2));
}

static void testImagesRoundTrip()
{
    Project p = buildSample();
    CHECK(!p.id.empty());               // makeProject assigns a project id

    std::string text = toJsonString(p);
    Project p2;
    std::string err;
    CHECK(projectFromJsonString(text, p2, &err));

    CHECK(p2.id == p.id);
    CHECK(p2.questions[1].images.size() == 2);

    const QuestionImage& a = p2.questions[1].images[0];
    CHECK(a.role == ImageRole::Question);
    CHECK(a.file == "q2-question.png");
    CHECK(a.caption == "stem");
    CHECK(a.subQuestions.empty());      // whole question

    const QuestionImage& b = p2.questions[1].images[1];
    CHECK(b.role == ImageRole::Solution);
    CHECK(b.caption == "answer key");
    CHECK(b.subQuestions.size() == 2);  // the out-of-range 9 was dropped
    CHECK(b.subQuestions[0] == 0);
    CHECK(b.subQuestions[1] == 1);
}

// Per-question view state (folded + base column width) must survive a JSON round
// trip, and old files that omit the keys must default to unfolded / 190 (additive
// fields, no schemaVersion bump).
static void testColumnViewRoundTrip()
{
    Project p = buildSample();
    p.questions[0].folded    = true;
    p.questions[0].viewWidth = 120.0f;
    p.questions[1].folded    = false;
    p.questions[1].viewWidth = 260.0f;

    Project p2;
    std::string err;
    CHECK(projectFromJsonString(toJsonString(p), p2, &err));
    CHECK(p2.questions[0].folded == true);
    CHECK_NEAR(p2.questions[0].viewWidth, 120.0);
    CHECK(p2.questions[1].folded == false);
    CHECK_NEAR(p2.questions[1].viewWidth, 260.0);

    // A project JSON without the keys loads with the defaults (unfolded / 190).
    const char* noKeys =
        "{\"schemaVersion\":2,\"name\":\"n\",\"questions\":["
        "{\"title\":\"Q1\",\"maxPoints\":10,\"subCount\":1,\"split\":\"equal\"}"
        "],\"students\":[{\"id\":1,\"noSubmission\":false,\"cells\":[{}]}]}";
    Project p3;
    CHECK(projectFromJsonString(noKeys, p3, &err));
    CHECK(p3.questions[0].folded == false);
    CHECK_NEAR(p3.questions[0].viewWidth, 190.0);
}

// Per-sub-question notes (subLabels / Cell::subNotes / Question::noteSuggestions)
// are additive fields (no schemaVersion bump): old files that omit them load with
// empty defaults, and ensureShape() prunes out-of-range/empty entries on load.
static void testNotesRoundTripDefaults()
{
    Project p = buildSample();
    Project p2;
    std::string err;
    CHECK(projectFromJsonString(toJsonString(p), p2, &err));
    CHECK(p2.questions[0].subLabels[0] == "\xD7\x90");
    CHECK(p2.students[0].cells[0].subNotes.size() == 1);
    CHECK(p2.questions[0].noteSuggestions.size() == 3);

    // A project JSON without any of the new keys loads with empty defaults, schema 2.
    const char* noKeys =
        "{\"schemaVersion\":2,\"name\":\"n\",\"questions\":["
        "{\"title\":\"Q1\",\"maxPoints\":10,\"subCount\":3,\"split\":\"equal\"}"
        "],\"students\":[{\"id\":1,\"noSubmission\":false,\"cells\":[{}]}]}";
    Project p3;
    CHECK(projectFromJsonString(noKeys, p3, &err));
    CHECK(p3.schemaVersion == 2);
    CHECK(p3.questions[0].subLabels.size() == 3);
    CHECK(p3.questions[0].subLabels[0].empty());
    CHECK(p3.questions[0].noteSuggestions.empty());
    CHECK(p3.students[0].cells[0].subNotes.empty());

    // Out-of-range sub (9, on a subCount-3 question) and an empty-text entry must be
    // pruned by ensureShape() on load (image-tag precedent).
    const char* dirty =
        "{\"schemaVersion\":2,\"name\":\"n\",\"questions\":["
        "{\"title\":\"Q1\",\"maxPoints\":10,\"subCount\":3,\"split\":\"equal\","
        "\"noteSuggestions\":["
        "{\"sub\":1,\"text\":\"ok\",\"dir\":0},"
        "{\"sub\":9,\"text\":\"bad\",\"dir\":0},"
        "{\"sub\":0,\"text\":\"\",\"dir\":0},"
        "{\"sub\":-1,\"text\":\"general\",\"dir\":0}"
        "]}],"
        "\"students\":[{\"id\":1,\"noSubmission\":false,\"cells\":[{\"subNotes\":["
        "{\"sub\":1,\"text\":\"kept\",\"dir\":2},"
        "{\"sub\":9,\"text\":\"dropped (out of range)\",\"dir\":0},"
        "{\"sub\":2,\"text\":\"\",\"dir\":0}"
        "]}]}]}";
    Project p4;
    CHECK(projectFromJsonString(dirty, p4, &err));
    CHECK(p4.questions[0].noteSuggestions.size() == 2); // sub=1 "ok" and sub=-1 "general" kept
    CHECK(p4.students[0].cells[0].subNotes.size() == 1);
    CHECK(p4.students[0].cells[0].subNotes[0].sub == 1);
    CHECK(p4.students[0].cells[0].subNotes[0].text == "kept");
    CHECK(p4.students[0].cells[0].subNotes[0].dir == TextDir::RTL);
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

// Config <-> JSON string round-trip, incl. the crash-recovery autosave record.
// Pure (no filesystem), so it exercises the serialization of the new field.
static void testConfigRoundTrip()
{
    // Populated: recents + an autosave record survive a round-trip verbatim.
    AppConfig cfg;
    cfg.recentProjects = { "a.json", "b.json" };
    cfg.autosave = { "C:\\aut\\deadbeef.autosave", "C:\\proj\\Exam.json",
                     "Midterm", "2026-07-05 14:32" };

    AppConfig r;
    CHECK(configFromJsonString(configToJsonString(cfg), r));
    CHECK(r.recentProjects.size() == 2);
    CHECK(r.recentProjects[0] == "a.json");
    CHECK(!r.autosave.empty());
    CHECK(r.autosave.file == "C:\\aut\\deadbeef.autosave");
    CHECK(r.autosave.projectPath == "C:\\proj\\Exam.json");
    CHECK(r.autosave.name == "Midterm");
    CHECK(r.autosave.savedIso == "2026-07-05 14:32");

    // Empty record: no "autosave" key is written, and it round-trips as empty.
    AppConfig none;
    none.recentProjects = { "x.json" };
    const std::string js = configToJsonString(none);
    CHECK(js.find("autosave") == std::string::npos);
    AppConfig r2;
    CHECK(configFromJsonString(js, r2));
    CHECK(r2.autosave.empty());
    CHECK(r2.recentProjects.size() == 1);

    // An old config with no "autosave" key parses to an empty record (not an error).
    AppConfig r3;
    CHECK(configFromJsonString("{ \"recentProjects\": [\"only.json\"] }", r3));
    CHECK(r3.autosave.empty());
    CHECK(r3.recentProjects.size() == 1);

    // Corrupt JSON -> false, and the out-config is reset to defaults.
    AppConfig r4;
    r4.recentProjects = { "stale.json" };
    CHECK(!configFromJsonString("{ not json", r4));
    CHECK(r4.recentProjects.empty());
    CHECK(r4.autosave.empty());
}

// Skipped sub-questions lock out their points: they cap awarded and lower the
// awardable max. Equal split uses the answered count; Custom uses per-sub ticks.
static void testSubQuestionDeduction()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 10.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Question qc; qc.title = "Qc"; qc.maxPoints = 10.0; qc.subCount = 3; qc.split = SplitMode::Custom;
    qc.subPoints = {7.0, 2.0, 1.0};
    Project p = makeProject("Deduction", 1, {qe, qc});
    Student& s = p.students[0];

    // New cells default to all-answered => no deduction, full effMax.
    CHECK(s.cells[0].subAnswered == 4);
    CHECK_NEAR(effectiveMax(p.questions[0], s.cells[0]), 10.0);
    CHECK(s.cells[1].subAnswered == 3);
    CHECK(s.cells[1].subChecks.size() == 3);
    CHECK_NEAR(effectiveMax(p.questions[1], s.cells[1]), 10.0);

    // Equal: skip 1 of 4 -> lock 2.5 -> effMax 7.5. awarded 8 caps to 7.5.
    s.cells[0].subAnswered = 3; s.cells[0].awarded = 8.0; s.cells[0].touched = true;
    CHECK_NEAR(lockedSubPoints(p.questions[0], s.cells[0]), 2.5);
    CHECK_NEAR(effectiveMax(p.questions[0], s.cells[0]), 7.5);
    CHECK_NEAR(cellPoints(s, p.questions[0], s.cells[0]), 7.5);
    CHECK(cellOverMax(p.questions[0], s.cells[0]));          // 8 > 7.5

    // The editor's "8 -> 5.5" result passes straight through (within the ceiling).
    s.cells[0].awarded = 5.5;
    CHECK_NEAR(cellPoints(s, p.questions[0], s.cells[0]), 5.5);
    CHECK(!cellOverMax(p.questions[0], s.cells[0]));

    // Custom: skip the 2-pt sub-question specifically -> lock exactly 2 -> effMax 8.
    s.cells[1].subChecks = {1, 0, 1}; s.cells[1].awarded = 8.0; s.cells[1].touched = true;
    CHECK_NEAR(lockedSubPoints(p.questions[1], s.cells[1]), 2.0);
    CHECK_NEAR(effectiveMax(p.questions[1], s.cells[1]), 8.0);
    CHECK_NEAR(cellPoints(s, p.questions[1], s.cells[1]), 8.0);

    // Skip the 7-pt one instead -> lock 7 -> effMax 3, so awarded 8 caps to 3.
    s.cells[1].subChecks = {0, 1, 1};
    CHECK_NEAR(lockedSubPoints(p.questions[1], s.cells[1]), 7.0);
    CHECK_NEAR(cellPoints(s, p.questions[1], s.cells[1]), 3.0);
}

// The editor helpers: locking a sub-question deducts its points from awarded (the
// user's "8 -> 5.5" example) and unlocking adds them back, clamped to the ceiling.
static void testEditorAdjust()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 10.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Question qc; qc.title = "Qc"; qc.maxPoints = 10.0; qc.subCount = 3; qc.split = SplitMode::Custom;
    qc.subPoints = {7.0, 2.0, 1.0};
    Project p = makeProject("Adjust", 1, {qe, qc});
    Cell& ce = p.students[0].cells[0];
    Cell& cc = p.students[0].cells[1];

    // Equal: graded 8 with all 4 answered; skip one -> 8 - 2.5 = 5.5, ceiling 7.5.
    // touched=true marks it already-scored, so the deduct path runs (not the sync).
    ce.awarded = 8.0; ce.subAnswered = 4; ce.touched = true;
    setAnsweredCount(qe, ce, 3);
    CHECK_NEAR(ce.awarded, 5.5);
    CHECK_NEAR(effectiveMax(qe, ce), 7.5);
    CHECK(ce.subAnswered == 3);
    CHECK(!ce.fullTick);
    CHECK(ce.touched);
    // Re-answer it -> 5.5 + 2.5 = 8 again.
    setAnsweredCount(qe, ce, 4);
    CHECK_NEAR(ce.awarded, 8.0);
    CHECK_NEAR(effectiveMax(qe, ce), 10.0);

    // Custom: graded 10; skip the 2-pt sub-q -> 8 (ceiling 8). touched=true so the
    // deduct path runs rather than the first-interaction sync.
    cc.awarded = 10.0; cc.subChecks = {1, 1, 1}; cc.subAnswered = 3; cc.touched = true;
    setSubAnswered(qc, cc, 1, false);
    CHECK_NEAR(cc.awarded, 8.0);
    CHECK_NEAR(effectiveMax(qc, cc), 8.0);
    CHECK(cc.subAnswered == 2);
    // Also skip the 7-pt one -> 8 - 7 = 1 (ceiling 1).
    setSubAnswered(qc, cc, 0, false);
    CHECK_NEAR(cc.awarded, 1.0);
    CHECK_NEAR(effectiveMax(qc, cc), 1.0);
    // Toggling the same box again is idempotent (no double-deduct).
    setSubAnswered(qc, cc, 0, false);
    CHECK_NEAR(cc.awarded, 1.0);
    // Re-answer the 2-pt one -> 1 + 2 = 3 (ceiling 3).
    setSubAnswered(qc, cc, 1, true);
    CHECK_NEAR(cc.awarded, 3.0);
    CHECK_NEAR(effectiveMax(qc, cc), 3.0);
}

// First interaction with the sub-question controls on a blank (or full) cell syncs
// `awarded` to the effective max — the answered parts are assumed correct — rather
// than nudging the 0/implied-full placeholder. Later edits deduct/add as usual.
static void testEditorFirstInteractionSync()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 20.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Question qc; qc.title = "Qc"; qc.maxPoints = 10.0; qc.subCount = 3; qc.split = SplitMode::Custom;
    qc.subPoints = {7.0, 2.0, 1.0};
    Project p = makeProject("Sync", 1, {qe, qc});

    // Equal, blank cell (the reported bug): 4/4 -> 3/4 syncs awarded to effMax 15,
    // not 0. A subsequent 3/4 -> 2/4 then deducts one share (15 - 5 = 10).
    Cell& ce = p.students[0].cells[0];
    CHECK(!ce.touched);
    setAnsweredCount(qe, ce, 3);
    CHECK_NEAR(ce.awarded, 15.0);
    CHECK_NEAR(effectiveMax(qe, ce), 15.0);
    CHECK(ce.subAnswered == 3);
    CHECK(!ce.fullTick);
    CHECK(ce.touched);
    setAnsweredCount(qe, ce, 2);          // now touched -> deduct one more share
    CHECK_NEAR(ce.awarded, 10.0);
    setAnsweredCount(qe, ce, 4);          // add both shares back -> full 20
    CHECK_NEAR(ce.awarded, 20.0);
    CHECK_NEAR(effectiveMax(qe, ce), 20.0);

    // Equal, full cell: decrementing must sync to effMax (15), not drop to 0.
    Cell& ce2 = p.students[0].cells[0] = blankCell(qe);
    ce2.fullTick = true; ce2.touched = true;   // an editor full tick
    setAnsweredCount(qe, ce2, 3);
    CHECK_NEAR(ce2.awarded, 15.0);
    CHECK(!ce2.fullTick);

    // Custom, blank cell: unticking the 2-pt sub-question on first touch syncs awarded
    // to effMax 8 (all other parts correct); a second untick (7-pt) then deducts to 1.
    Cell& cc = p.students[0].cells[1];
    CHECK(!cc.touched);
    setSubAnswered(qc, cc, 1, false);     // untick the 2-pt part
    CHECK_NEAR(cc.awarded, 8.0);
    CHECK_NEAR(effectiveMax(qc, cc), 8.0);
    CHECK(cc.subAnswered == 2);
    CHECK(cc.touched);
    setSubAnswered(qc, cc, 0, false);     // now touched -> deduct the 7-pt part
    CHECK_NEAR(cc.awarded, 1.0);
    CHECK_NEAR(effectiveMax(qc, cc), 1.0);
}

// stepAwarded: +/- steps awarded points in ONE press. '-' docks a point immediately
// from the full baseline (green/blank cell) or the current value (graded); '+' on a
// blank cell builds up from 0. fullTick is cleared; the green look comes from
// isFullMarks (awarded == maxPoints), so '+' back to full reads green again.
static void testStepAwarded()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 20.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Project p = makeProject("Step", 1, {qe});
    const Question& q = p.questions[0];

    // Fresh cell, '-': assume full and dock one in a single press -> 19.
    Cell& a = p.students[0].cells[0] = blankCell(qe);
    CHECK(!a.touched);
    stepAwarded(q, a, -1.0);
    CHECK_NEAR(a.awarded, 19.0);
    CHECK(a.touched);
    CHECK(!a.fullTick);
    stepAwarded(q, a, -1.0);
    CHECK_NEAR(a.awarded, 18.0);

    // Fresh cell, '+': build up from an empty (0) baseline -> 1.
    Cell& b = p.students[0].cells[0] = blankCell(qe);
    stepAwarded(q, b, 1.0);
    CHECK_NEAR(b.awarded, 1.0);
    CHECK(b.touched);

    // Green full-tick cell, '-': dock one straight to 19 (clears the tick, no longer full).
    Cell& f = p.students[0].cells[0] = blankCell(qe);
    f.fullTick = true; f.touched = true;
    stepAwarded(q, f, -1.0);
    CHECK_NEAR(f.awarded, 19.0);
    CHECK(!f.fullTick);
    CHECK(!isFullMarks(q, f));
    // '+' back to the full mark reads full again (via isFullMarks, not the tick).
    stepAwarded(q, f, 1.0);
    CHECK_NEAR(f.awarded, 20.0);
    CHECK(isFullMarks(q, f));

    // Full-tick cell, '+': stays full (already at the ceiling).
    Cell& g = p.students[0].cells[0] = blankCell(qe);
    g.fullTick = true;
    stepAwarded(q, g, 1.0);
    CHECK_NEAR(g.awarded, 20.0);
    CHECK(isFullMarks(q, g));

    // Graded cell: plain +/- around the current value.
    Cell& d = p.students[0].cells[0] = blankCell(qe);
    d.awarded = 12.0; d.touched = true;
    stepAwarded(q, d, -1.0); CHECK_NEAR(d.awarded, 11.0);
    stepAwarded(q, d, 1.0);  CHECK_NEAR(d.awarded, 12.0);

    // Clamps to [0, effMax].
    Cell& h = p.students[0].cells[0] = blankCell(qe);
    h.awarded = 1.0; h.touched = true;
    stepAwarded(q, h, -1.0); CHECK_NEAR(h.awarded, 0.0);
    stepAwarded(q, h, -1.0); CHECK_NEAR(h.awarded, 0.0);   // floors at 0
    h.awarded = 19.0;
    stepAwarded(q, h, 1.0); CHECK_NEAR(h.awarded, 20.0);
    stepAwarded(q, h, 1.0); CHECK_NEAR(h.awarded, 20.0);   // caps at effMax

    // Baseline respects locked sub-questions: skip 1 of 4 -> effMax 15, so a blank
    // '-' docks to 14 and never reads full.
    Cell& k = p.students[0].cells[0] = blankCell(qe);
    k.subAnswered = 3;                    // one skipped -> lock 5 -> effMax 15
    CHECK_NEAR(effectiveMax(q, k), 15.0);
    stepAwarded(q, k, -1.0);
    CHECK_NEAR(k.awarded, 14.0);
    CHECK(!isFullMarks(q, k));
}

// isFullMarks: a cell reads green FULL when it earns the question's full marks by
// any route (explicit tick, or awarded == maxPoints with all sub-questions answered).
// Over-max stays an orange warning, and a sub-question-capped cell never reads full.
static void testIsFullMarks()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 20.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Project p = makeProject("Full", 1, {qe});
    const Question& q = p.questions[0];
    Cell& c = p.students[0].cells[0];

    c = blankCell(qe);                 CHECK(!isFullMarks(q, c));  // blank
    c.fullTick = true;                 CHECK(isFullMarks(q, c));   // explicit tick
    c = blankCell(qe); c.awarded = 20.0; c.touched = true;
                                       CHECK(isFullMarks(q, c));   // typed to full == green
    c.awarded = 19.0;                  CHECK(!isFullMarks(q, c));  // below full
    c.awarded = 25.0;                  CHECK(!isFullMarks(q, c));  // over max -> warning, not full
    c = blankCell(qe); c.subAnswered = 3; c.awarded = 15.0; c.touched = true;
    CHECK_NEAR(effectiveMax(q, c), 15.0);
    CHECK(!isFullMarks(q, c));         // capped at 15 of 20 -> not full
}

// subChecks survive a round-trip; a pre-v2 file loads as all-answered so its old
// (reference-only X/Y) scores are preserved rather than retro-deducted.
static void testSubChecksAndMigration()
{
    Question qc; qc.title = "Qc"; qc.maxPoints = 10.0; qc.subCount = 3; qc.split = SplitMode::Custom;
    qc.subPoints = {7.0, 2.0, 1.0};
    Project p = makeProject("RT", 1, {qc});
    p.students[0].cells[0].subChecks = {0, 1, 1};   // skip the 7-pt part
    p.students[0].cells[0].subAnswered = 2;
    p.students[0].cells[0].awarded = 3.0;
    p.students[0].cells[0].touched = true;

    Project p2; std::string err;
    CHECK(projectFromJsonString(toJsonString(p), p2, &err));
    CHECK(p2.students[0].cells[0].subChecks.size() == 3);
    CHECK(p2.students[0].cells[0].subChecks[0] == 0);
    CHECK_NEAR(effectiveMax(p2.questions[0], p2.students[0].cells[0]), 3.0);
    CHECK_NEAR(studentTotal(p2, 0), 3.0);

    // A v1 file with subAnswered=2 must load as all-answered (score preserved).
    const std::string v1 =
        "{\"schemaVersion\":1,\"name\":\"old\",\"questions\":["
        "{\"title\":\"Q1\",\"maxPoints\":20,\"subCount\":5,\"split\":\"equal\","
        "\"subPoints\":[4,4,4,4,4]}],"
        "\"students\":[{\"id\":1,\"noSubmission\":false,\"cells\":["
        "{\"fullTick\":false,\"awarded\":12,\"subAnswered\":2,\"lastPage\":\"\","
        "\"note\":\"\",\"touched\":true}]}]}";
    Project pv; std::string err2;
    CHECK(projectFromJsonString(v1, pv, &err2));
    CHECK(pv.schemaVersion == 2);                    // upgraded in memory
    CHECK(pv.students[0].cells[0].subAnswered == 5); // reset to all-answered
    CHECK_NEAR(effectiveMax(pv.questions[0], pv.students[0].cells[0]), 20.0);
    CHECK_NEAR(cellPoints(pv.students[0], pv.questions[0], pv.students[0].cells[0]), 12.0);
}

// Per-sub-question note helpers: setSubNote (create/replace/erase-on-empty, keeps
// subNotes sorted), findSubNote (hit/miss, const and mutable), cellHasAnyNote, and
// subHeader (label vs numeric fallback vs out-of-range).
static void testSubNoteHelpers()
{
    Question q; q.title = "Q"; q.maxPoints = 10.0; q.subCount = 3;
    normalizeQuestion(q);
    q.subLabels[1] = "\xD7\x91"; // Hebrew Bet on sub-question 1 (0-based)

    CHECK(subHeader(q, 0) == "1");            // no label -> numeric fallback
    CHECK(subHeader(q, 1) == "\xD7\x91");     // label wins
    CHECK(subHeader(q, 2) == "3");
    CHECK(subHeader(q, 99) == "100");         // out-of-range index -> still numeric fallback

    Cell c = blankCell(q);
    CHECK(!cellHasAnyNote(c));
    CHECK(findSubNote(c, 0) == nullptr);

    // Create, in reverse order, to exercise the sorted insert.
    setSubNote(c, 2, "third", TextDir::Auto);
    setSubNote(c, 0, "first", TextDir::Auto);
    CHECK(c.subNotes.size() == 2);
    CHECK(c.subNotes[0].sub == 0 && c.subNotes[0].text == "first");  // sorted by sub
    CHECK(c.subNotes[1].sub == 2 && c.subNotes[1].text == "third");
    CHECK(cellHasAnyNote(c));

    const SubNote* found = findSubNote(c, 2);
    CHECK(found != nullptr);
    CHECK(found->text == "third");
    CHECK(findSubNote(c, 1) == nullptr);      // miss: no note on sub 1

    // Replace in place (no growth, no re-sort needed).
    setSubNote(c, 0, "first (edited)", TextDir::RTL);
    CHECK(c.subNotes.size() == 2);
    CHECK(c.subNotes[0].text == "first (edited)");
    CHECK(c.subNotes[0].dir == TextDir::RTL);

    // Empty text erases the entry.
    setSubNote(c, 0, "", TextDir::Auto);
    CHECK(c.subNotes.size() == 1);
    CHECK(findSubNote(c, 0) == nullptr);
    CHECK(cellHasAnyNote(c));                 // sub 2's note still there

    setSubNote(c, 2, "", TextDir::Auto);
    CHECK(c.subNotes.empty());
    CHECK(!cellHasAnyNote(c));

    c.note = "general note";
    CHECK(cellHasAnyNote(c));                 // the whole-cell note also counts
}

// The note-suggestion pool (Question::noteSuggestions): append, exact (sub,text)
// dedup, same text under a different sub adds, empty text refused, and an edited
// pick becomes a new entry while the original stays. normalizeQuestion prunes
// out-of-range subs but always keeps the general (-1) bucket.
static void testNoteSuggestionPool()
{
    Question q; q.title = "Q"; q.maxPoints = 10.0; q.subCount = 2;
    normalizeQuestion(q);

    CHECK(addNoteSuggestion(q, 0, "good work", TextDir::Auto));
    CHECK(q.noteSuggestions.size() == 1);

    CHECK(!addNoteSuggestion(q, 0, "good work", TextDir::Auto)); // exact dup refused
    CHECK(q.noteSuggestions.size() == 1);

    CHECK(addNoteSuggestion(q, 1, "good work", TextDir::Auto));  // same text, different sub
    CHECK(q.noteSuggestions.size() == 2);

    CHECK(!addNoteSuggestion(q, -1, "", TextDir::Auto));         // empty text refused
    CHECK(q.noteSuggestions.size() == 2);

    CHECK(addNoteSuggestion(q, -1, "nice job", TextDir::Auto));  // general bucket
    CHECK(q.noteSuggestions.size() == 3);

    // "Edited pick" pattern: picking "good work" then editing it to "good work!"
    // adds a new entry; the original is never overwritten.
    CHECK(addNoteSuggestion(q, 0, "good work!", TextDir::Auto));
    CHECK(q.noteSuggestions.size() == 4);
    CHECK(q.noteSuggestions[0].text == "good work"); // original untouched

    // normalizeQuestion (subCount shrinks to 1) prunes sub==1 entries but keeps -1.
    q.subCount = 1;
    normalizeQuestion(q);
    for (const auto& s : q.noteSuggestions)
        CHECK(s.sub == 0 || s.sub == -1);
    CHECK(q.noteSuggestions.size() == 3); // the two sub==0 entries + the -1 one
}

// Cell/Student value equality (defaulted operator==) drives the undo history's
// change-detection: every field participates, so a real edit is never missed and a
// no-op change never fabricates a dead history entry.
static void testGradingEquality()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 20.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Question qc; qc.title = "Qc"; qc.maxPoints = 10.0; qc.subCount = 3; qc.split = SplitMode::Custom;
    qc.subPoints = {7.0, 2.0, 1.0};

    Cell a = blankCell(qe), b = a;
    CHECK(a == b);                                   // two fresh identical cells
    b = a; b.awarded = 5.0;    CHECK(!(a == b));
    b = a; b.fullTick = true;  CHECK(!(a == b));
    b = a; b.subAnswered = 3;  CHECK(!(a == b));
    b = a; b.lastPage = "p.3"; CHECK(!(a == b));
    b = a; b.note = "x";       CHECK(!(a == b));
    b = a; b.noteDir = TextDir::RTL; CHECK(!(a == b)); // direction participates in undo diff
    b = a; b.subNotes = { SubNote{0, "x", TextDir::Auto} }; CHECK(!(a == b)); // per-sub notes too
    b = a; b.touched = true;   CHECK(!(a == b));

    Cell c1 = blankCell(qc), c2 = c1;
    CHECK(c1 == c2);
    c2.subChecks = {1, 0, 1};  CHECK(!(c1 == c2));   // per-sub answered mask compared

    Student s1; s1.id = 1; s1.cells = {a, c1};
    Student s2 = s1;
    CHECK(s1 == s2);
    s2.noSubmission = true;            CHECK(!(s1 == s2)); // row flag
    s2 = s1; s2.cells[0].awarded = 9.0; CHECK(!(s1 == s2)); // any cell
}

// firstGradingDiff finds the first (row,col) two grids differ at (row-major), used
// to jump the selection to the reverted cell on undo/redo.
static void testFirstGradingDiff()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 20.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Project p = makeProject("Diff", 3, {qe, qe});   // 3 students x 2 questions
    std::vector<Student> a = p.students, b = a;

    auto d0 = firstGradingDiff(a, b);               // identical
    CHECK(d0.first == -1 && d0.second == -1);

    b = a; b[1].cells[1].awarded = 5.0; b[1].cells[1].touched = true;
    auto d1 = firstGradingDiff(a, b);               // single changed cell
    CHECK(d1.first == 1 && d1.second == 1);

    b = a; b[2].noSubmission = true;
    auto d2 = firstGradingDiff(a, b);               // row-level change -> col 0
    CHECK(d2.first == 2 && d2.second == 0);

    b = a;
    b[0].cells[1].awarded = 1.0; b[0].cells[1].touched = true;
    b[2].cells[0].awarded = 2.0; b[2].cells[0].touched = true;
    auto d3 = firstGradingDiff(a, b);               // returns the FIRST difference
    CHECK(d3.first == 0 && d3.second == 1);
}

// reshapeProject (Project.h): post-creation structure editing (Project Settings §8d).
// Grades carry over where a column/row is retained; new columns/rows are blank;
// removed ones drop; carried cells re-sync to their (possibly changed) question.
static void testReshapeProject()
{
    Question qe; qe.title = "Qe"; qe.maxPoints = 20.0; qe.subCount = 4; qe.split = SplitMode::Equal;
    Question qc; qc.title = "Qc"; qc.maxPoints = 10.0; qc.subCount = 2; qc.split = SplitMode::Custom;
    qc.subPoints = {7.0, 3.0};
    Project p = makeProject("Reshape", 3, {qe, qc}); // 3 students x 2 questions
    p.students[0].cells[0].awarded = 20.0; p.students[0].cells[0].touched = true; // graded Qe
    p.students[1].cells[1].fullTick = true;                                       // full Qc
    ensureShape(p);

    // 1. Add a sub-question to Qe (subCount 4 -> 5): grid stays rectangular, stored
    //    awarded is preserved, and the other column's full tick survives.
    {
        std::vector<Question> nq = p.questions;
        nq[0].subCount = 5; normalizeQuestion(nq[0]);
        Project t = p;
        reshapeProject(t, nq, {0, 1}, 3);
        CHECK(t.questions.size() == 2);
        CHECK(t.questions[0].subCount == 5);
        CHECK(t.students.size() == 3);
        for (const auto& s : t.students) CHECK(s.cells.size() == 2);
        CHECK_NEAR(t.students[0].cells[0].awarded, 20.0);
        CHECK(t.students[1].cells[1].fullTick);
    }

    // 2. Append a new question column (originalIndex -1): old grades carry, new col blank.
    {
        Question qn; qn.title = "Qnew"; qn.maxPoints = 5.0; qn.subCount = 1; qn.split = SplitMode::Equal;
        normalizeQuestion(qn);
        std::vector<Question> nq = p.questions; nq.push_back(qn);
        Project t = p;
        reshapeProject(t, nq, {0, 1, -1}, 3);
        CHECK(t.questions.size() == 3);
        for (const auto& s : t.students) CHECK(s.cells.size() == 3);
        CHECK(!t.students[0].cells[2].touched && !t.students[0].cells[2].fullTick);
        CHECK_NEAR(t.students[0].cells[0].awarded, 20.0);
        CHECK(t.students[1].cells[1].fullTick);
    }

    // 3. Remove the first question: the second column's grade slides to index 0.
    {
        std::vector<Question> nq = {p.questions[1]};
        Project t = p;
        reshapeProject(t, nq, {1}, 3);
        CHECK(t.questions.size() == 1);
        CHECK(t.questions[0].title == "Qc");
        CHECK(t.students[1].cells[0].fullTick);
    }

    // 4. Grow / shrink students from the end; new rows blank + ids assigned, existing kept.
    {
        Project t = p;
        reshapeProject(t, p.questions, {0, 1}, 5);
        CHECK(t.students.size() == 5);
        CHECK(t.students[3].id == 4 && t.students[4].id == 5);
        for (const auto& s : t.students) CHECK(s.cells.size() == 2);
        CHECK(!t.students[4].cells[0].touched);
        CHECK_NEAR(t.students[0].cells[0].awarded, 20.0);
    }
    {
        Project t = p;
        reshapeProject(t, p.questions, {0, 1}, 2); // drops student index 2
        CHECK(t.students.size() == 2);
        CHECK(t.students[1].cells[1].fullTick);
    }

    // 5. Round-trip a reshaped project: schema stays 2, grid stays rectangular.
    {
        Question qn; qn.title = "Qx"; qn.maxPoints = 8.0; qn.subCount = 3; qn.split = SplitMode::Equal;
        normalizeQuestion(qn);
        std::vector<Question> nq = p.questions; nq.push_back(qn);
        Project t = p;
        reshapeProject(t, nq, {0, 1, -1}, 4);
        std::string js = toJsonString(t);
        Project r; std::string err;
        CHECK(projectFromJsonString(js, r, &err));
        CHECK(r.schemaVersion == 2);
        CHECK(r.questions.size() == 3);
        CHECK(r.students.size() == 4);
        for (const auto& s : r.students) CHECK(s.cells.size() == 3);
    }
}

// Reduced BiDi (Bidi.cpp): logical -> visual reordering for Hebrew notes. Hebrew
// letters are given as codepoints so the test file stays ASCII/source-encoding
// agnostic. Aleph..Vav = U+05D0..U+05D5.
static std::u32string u32(std::initializer_list<char32_t> c) { return std::u32string(c); }

static void testBidi()
{
    using gt::BaseDir;
    const char32_t A = 0x05D0, B = 0x05D1, G = 0x05D2, D = 0x05D3, H = 0x05D4, V = 0x05D5;

    // Pure ASCII: order unchanged, base stays LTR.
    {
        gt::BidiResult r = gt::bidiReorder(u32({'a', 'b', 'c'}), BaseDir::LTR);
        CHECK(r.visual == u32({'a', 'b', 'c'}));
        CHECK(!r.baseRtl);
    }
    // Pure Hebrew: reversed to visual order; Auto detects RTL; index maps invert.
    {
        gt::BidiResult r = gt::bidiReorder(u32({A, B, G}), BaseDir::Auto);
        CHECK(r.baseRtl);
        CHECK(r.visual == u32({G, B, A}));
        CHECK(r.visualToLogical.size() == 3);
        CHECK(r.visualToLogical[0] == 2 && r.visualToLogical[2] == 0);
        CHECK(r.logicalToVisual[0] == 2 && r.logicalToVisual[2] == 0);
    }
    // Hebrew reverses even under a forced LTR base (it's still an RTL run).
    {
        gt::BidiResult r = gt::bidiReorder(u32({A, B, G}), BaseDir::LTR);
        CHECK(!r.baseRtl);
        CHECK(r.visual == u32({G, B, A}));
    }
    // Latin then Hebrew, base LTR: "abc " kept, Hebrew reversed after it.
    {
        gt::BidiResult r = gt::bidiReorder(u32({'a', 'b', 'c', ' ', A, B, G}), BaseDir::LTR);
        CHECK(r.visual == u32({'a', 'b', 'c', ' ', G, B, A}));
    }
    // Hebrew then Latin, Auto -> RTL base: visual "abc <sp> GBA".
    {
        gt::BidiResult r = gt::bidiReorder(u32({A, B, G, ' ', 'a', 'b', 'c'}), BaseDir::Auto);
        CHECK(r.baseRtl);
        CHECK(r.visual == u32({'a', 'b', 'c', ' ', G, B, A}));
    }
    // Numbers keep LTR order inside an RTL run: "ABG 123 DHV" (base RTL) ->
    // visual "VHD 123 GBA" (each Hebrew word reversed, the number run intact).
    {
        gt::BidiResult r = gt::bidiReorder(
            u32({A, B, G, ' ', '1', '2', '3', ' ', D, H, V}), BaseDir::RTL);
        CHECK(r.visual == u32({V, H, D, ' ', '1', '2', '3', ' ', G, B, A}));
    }
    // UTF-8 helpers round-trip; the convenience wrappers agree with the reorder.
    {
        const std::u32string cps = u32({A, B, G});
        const std::string utf8 = gt::codepointsToUtf8(cps);
        CHECK(gt::utf8ToCodepoints(utf8) == cps);
        CHECK(gt::bidiVisualUtf8(utf8, BaseDir::Auto) == gt::codepointsToUtf8(u32({G, B, A})));
        CHECK(gt::bidiBaseIsRtl(utf8, BaseDir::Auto));
        CHECK(!gt::bidiBaseIsRtl("abc", BaseDir::Auto));
    }
    // Multiline: each line reorders independently and the '\n' keeps its place.
    {
        gt::BidiResult r = gt::bidiReorder(u32({A, B, '\n', 'a', 'b'}), BaseDir::Auto);
        CHECK(r.visual.size() == 5);
        CHECK(r.visual[0] == B && r.visual[1] == A); // line 1 reversed
        CHECK(r.visual[2] == '\n');                  // separator fixed
        CHECK(r.visual[3] == 'a' && r.visual[4] == 'b'); // line 2 (LTR run) kept
    }
    // Mirroring: paired punctuation flips glyph inside an RTL run, but only the
    // display copy — logical order/text is untouched. "(A)" with RTL base reorders
    // to ")A(" by position, then both parens mirror, giving a correctly-oriented
    // "(A)" (without mirroring it would read ")A(").
    {
        gt::BidiResult r = gt::bidiReorder(u32({'(', A, ')'}), BaseDir::RTL);
        CHECK(r.visual == u32({'(', A, ')'}));
        CHECK(r.visualRtl.size() == 3);
        CHECK(r.visualRtl[0] == 1 && r.visualRtl[1] == 1 && r.visualRtl[2] == 1);
        CHECK(gt::bidiMirror(U'(') == U')' && gt::bidiMirror(U'>') == U'<');
        CHECK(gt::bidiMirror(U'a') == U'a'); // no mirror -> unchanged
    }
    // LTR brackets are NOT mirrored, and visualRtl flags each glyph's level.
    {
        gt::BidiResult r = gt::bidiReorder(u32({'(', 'a', ')'}), BaseDir::LTR);
        CHECK(r.visual == u32({'(', 'a', ')'}));   // untouched under LTR base
        CHECK(r.visualRtl[0] == 0 && r.visualRtl[1] == 0 && r.visualRtl[2] == 0);
    }
    // Mixed run flags direction per glyph: "aA" (Latin, Hebrew) LTR base.
    {
        gt::BidiResult r = gt::bidiReorder(u32({'a', A}), BaseDir::LTR);
        CHECK(r.visual == u32({'a', A}));
        CHECK(r.visualRtl[0] == 0 && r.visualRtl[1] == 1);
    }
    // Empty string is a no-op.
    {
        gt::BidiResult r = gt::bidiReorder(std::u32string(), BaseDir::Auto);
        CHECK(r.visual.empty());
        CHECK(!r.baseRtl);
    }
}

// The pure DIB->BMP wrapper used by the clipboard-paste path (Assets.h). Feed a
// hand-built BITMAPINFOHEADER DIB and check the prepended 14-byte file header.
static void testBmpFromDib()
{
    // 40-byte BITMAPINFOHEADER, 2x2, 24bpp, BI_RGB, then 16 bytes of pixel data
    // (2 rows * (2px*3B = 6, padded to 8B) = 16). No palette, no masks.
    std::vector<uint8_t> dib(40 + 16, 0);
    auto put32 = [&](size_t o, uint32_t v) {
        dib[o] = v & 0xFF; dib[o+1] = (v>>8)&0xFF; dib[o+2] = (v>>16)&0xFF; dib[o+3] = (v>>24)&0xFF;
    };
    auto put16 = [&](size_t o, uint16_t v) { dib[o] = v & 0xFF; dib[o+1] = (v>>8)&0xFF; };
    put32(0, 40);          // biSize
    put32(4, 2);           // biWidth
    put32(8, 2);           // biHeight
    put16(12, 1);          // biPlanes
    put16(14, 24);         // biBitCount
    put32(16, 0);          // biCompression = BI_RGB
    for (size_t i = 40; i < dib.size(); ++i) dib[i] = static_cast<uint8_t>(i); // marker pixels

    std::vector<uint8_t> bmp;
    CHECK(buildBmpFromDib(dib.data(), dib.size(), bmp));
    CHECK(bmp.size() == 14 + dib.size());
    CHECK(bmp[0] == 'B' && bmp[1] == 'M');

    auto get32 = [&](size_t o) {
        return static_cast<uint32_t>(bmp[o]) | (static_cast<uint32_t>(bmp[o+1])<<8) |
               (static_cast<uint32_t>(bmp[o+2])<<16) | (static_cast<uint32_t>(bmp[o+3])<<24);
    };
    CHECK(get32(2) == 14 + dib.size());  // bfSize
    CHECK(get32(10) == 54);              // bfOffBits = 14 + 40, no palette/masks
    CHECK(std::memcmp(bmp.data() + 14, dib.data(), dib.size()) == 0); // DIB appended verbatim

    // A too-small buffer is rejected.
    std::vector<uint8_t> tiny(8, 0), out;
    CHECK(!buildBmpFromDib(tiny.data(), tiny.size(), out));

    // 8bpp with a 256-entry palette pushes bfOffBits past the header.
    std::vector<uint8_t> pal(40 + 256*4 + 4, 0);
    pal[0] = 40;           // biSize
    pal[14] = 8;           // biBitCount = 8 (low byte; biClrUsed left 0 => 1<<8 = 256)
    std::vector<uint8_t> pbmp;
    CHECK(buildBmpFromDib(pal.data(), pal.size(), pbmp));
    auto pget32 = [&](size_t o) {
        return static_cast<uint32_t>(pbmp[o]) | (static_cast<uint32_t>(pbmp[o+1])<<8) |
               (static_cast<uint32_t>(pbmp[o+2])<<16) | (static_cast<uint32_t>(pbmp[o+3])<<24);
    };
    CHECK(pget32(10) == 14u + 40u + 256u*4u); // header + 256-color palette
}

int main()
{
    testScoring();
    testPerQuestionAnswered();
    testRoundTrip();
    testFileRoundTrip();
    testImagesRoundTrip();
    testColumnViewRoundTrip();
    testNotesRoundTripDefaults();
    testMalformedJson();
    testRecentAliasSafe();
    testConfigRoundTrip();
    testSubQuestionDeduction();
    testEditorAdjust();
    testEditorFirstInteractionSync();
    testStepAwarded();
    testIsFullMarks();
    testSubChecksAndMigration();
    testSubNoteHelpers();
    testNoteSuggestionPool();
    testGradingEquality();
    testFirstGradingDiff();
    testReshapeProject();
    testBidi();
    testBmpFromDib();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
