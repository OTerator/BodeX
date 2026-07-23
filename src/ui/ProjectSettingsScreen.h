#pragma once

class App;

namespace gt::ui {

// Post-creation "Project Settings" section (the Project tab of the Settings panel):
// edit an existing project's structure (name, students, per-question setup,
// add/remove questions). Edits App::settings (a working copy); Apply commits via
// App::tryApplyProjectSettings (§8d). Draws into the caller's window/child — no
// Begin/End of its own — so the Settings panel owns the outer frame.
void projectSettingsSection(App& app);

} // namespace gt::ui
