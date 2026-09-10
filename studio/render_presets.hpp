// Geekatplay TerraForge - named render presets and the batch queue.
//
// A camera carries its own render assignment (scene.hpp RenderAssign:
// engine, size, samples, passes, file). A preset is one of those with a
// name, kept in the scene (SceneState::render_presets, saved with the
// project), so "final 4K Cycles" or "draft Mitsuba" can be applied to any
// camera in one step, from the Render menu, a camera's properties, the
// Render tab, or the `render_preset` op. Rendering several cameras is a
// queue (App::render_queue): render_service_requests starts the next one
// when the last has finished.
#pragma once
#include <string>
#include <vector>

namespace studio {
struct App;
struct RenderAssign;
struct RenderPreset;

// The preset by name, or null.
RenderPreset *render_preset_find(const std::string &name);
// Save (or replace) a preset from an assignment; returns its index.
int render_preset_upsert(const std::string &name, const RenderAssign &from);
// Apply a preset to an assignment (the output file is copied only when the
// preset has one). False when there is no such preset.
bool render_preset_apply(const std::string &name, RenderAssign &to);
bool render_preset_delete(const std::string &name);
// A name that is not taken yet: "Preset 1", "Preset 2", ...
std::string render_preset_free_name();

// Queue cameras for rendering, one after the other; a preset name applies
// it to each first. Cameras whose output file is the default get one named
// after them, so a batch never writes every picture to the same file.
// Returns how many were queued.
int render_batch_queue(App &a, const std::vector<int> &cameras, const std::string &preset);

} // namespace studio
