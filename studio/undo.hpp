// Geekatplay TerraForge — undo / redo.
//
// Snapshot model (as Blender does it): before a change is applied we record
// the whole editable state — node graph, scene objects, world settings — and
// name the step. Undo restores the previous snapshot. This survives any kind
// of edit, including ones made by the AI assistant, the scripting API or MCP,
// because they all mutate the same state.
#pragma once
#include <string>

namespace studio {
struct App;

// Record the state *before* performing `label`. Call it immediately before
// the mutation, not after.
void undo_push(App &a, const std::string &label);

// Same, for callers that already hold App::graph_mtx (the node editor holds it
// for its whole frame). Taking it again would be undefined behaviour, and
// letting the snapshot skip the graph would quietly make the edit un-undoable.
void undo_push_locked(App &a, const std::string &label);

bool undo_can_undo();
bool undo_can_redo();
std::string undo_next_label(); // what Ctrl+Z would revert
std::string undo_redo_label();

bool undo_perform(App &a);
bool redo_perform(App &a);
void undo_clear();

// history for the UI: newest last; `current` marks where we are
int undo_history(const std::string **labels_out); // returns count
int undo_history_position();
void undo_jump_to(App &a, int index);
} // namespace studio
