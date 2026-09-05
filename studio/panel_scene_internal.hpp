// Geekatplay TerraForge — the Objects panel's shared state.
//
// The Object Manager is four files: panel_scene.cpp (frame, header strip,
// tree walk), panel_scene_row.cpp (one row's painting and hit zones),
// panel_scene_dnd.cpp (drag and drop, and the structural edits that move
// objects in the array) and panel_scene_menu.cpp (the context menu). They
// share this one state block, which lives for the life of the application.
#pragma once
#include "scene.hpp"
#include <cstdint>
#include <imgui.h>
#include <vector>

namespace studio {
struct App;

// One line of the tree as it is drawn this frame.
struct TreeRow {
  int idx = -1;          // object index, or -1 for a layer header row
  int layer = -1;        // the layer, for a header row
  int depth = 0;
  bool has_children = false;
  bool last = true;      // last among its siblings: the elbow closes here
  uint64_t guides = 0;   // bit d set: a vertical guide runs through depth d
};

struct TreeState {
  // view options (the Show menu, the search bar, the filter, Set As Root)
  char search[64] = {0};
  bool type_on[SceneObject::Light + 1];
  bool flat = false;
  bool by_layer = false;
  bool sort_name = false;
  bool show_tags = true;
  int icon_size = 1;     // 0 small, 1 medium, 2 large
  int root = -1;         // Set As Root: -1 = the whole scene

  // in-place rename
  int rename = -1;
  bool rename_focus = false;
  char rename_buf[80] = {0};

  // paint-to-inherit: which state is being brushed across rows, and what to
  int paint_kind = -1;   // 0 viewport dot, 1 render dot, 2 enabled, 3 layer
  int paint_value = 0;
  int press_zone = 0;    // the zone the mouse went down on (row file's enum)

  // drag and drop, resolved once per frame after the tree is drawn
  int drop_src = -1;
  int drop_target = -1;
  int drop_mode = 0;     // 1 before, 2 after, 3 into
  bool drop_copy = false;

  // edits requested by the menu, applied after the tree is drawn
  std::vector<int> req_delete;
  int req_duplicate = -1;
  bool req_group = false;

  std::vector<TreeRow> rows; // this frame's rows, in order
  uint64_t seen_serial = 0;  // App::scene_selection_serial last seen here

  TreeState() {
    for (bool &b : type_on) b = true;
  }
};

TreeState &tree_state();
float tree_row_height();
float tree_indent();

// selection
bool tree_is_selected(const SceneState &sc, int i);
void tree_select(App &a, SceneState &sc, int i, bool ctrl, bool shift);
void tree_select_subtree(App &a, SceneState &sc, int i);
void tree_begin_rename(const SceneState &sc, int i);
// the row number of object `i` in this frame's rows, or -1
int tree_row_of(int i);

// panel_scene_row.cpp
void tree_row_draw(App &a, SceneState &sc, const TreeRow &r, int row_no);
// apply one of the painted states to an object (and its subtree)
void tree_set_state(SceneState &sc, int i, int kind, int value, bool subtree);

// panel_scene_menu.cpp
void tree_context_menu(App &a, SceneState &sc, int i);

// panel_scene_dnd.cpp
void tree_dnd_source(const SceneState &sc, int i);
void tree_dnd_target(const SceneState &sc, const TreeRow &r, ImVec2 mn,
                     ImVec2 mx);
void tree_apply_edits(App &a, SceneState &sc);
int tree_duplicate(App &a, SceneState &sc, int i);

} // namespace studio
