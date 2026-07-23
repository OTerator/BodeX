#pragma once

class App;

namespace gt::ui {

// Top-bar "Settings" panel: a full-screen, two-pane screen with a left category
// nav (General, Keybinds, Project) and a content pane. General edits the persisted
// App preferences (theme, +/- step, UI scale, resolution, autosave); Keybinds is a
// read-only shortcut reference; Project embeds projectSettingsSection. Reached via
// App::openSettings; see App.cpp and spec.md.
void settingsScreen(App& app);

} // namespace gt::ui
