#pragma once

class App;

namespace gt::ui {

// Post-creation "Project Settings" screen: edit an existing project's structure
// (name, students, per-question setup, add/remove questions). Edits App::settings
// (a working copy); Apply commits via App::tryApplyProjectSettings (§8d).
void projectSettingsScreen(App& app);

} // namespace gt::ui
