// Geekatplay TerraForge - a saved window layout, as data.
//
// A layout is everything about *where things are* and nothing about what the
// scene contains: the dock arrangement (ImGui's own ini text, which already
// describes every window's node, size, tab order and floating position),
// which viewports are open and what each one shows, which panels are open,
// and the extra node editors.
//
// This half is pure data and file I/O - no ImGui, no GL - so the round trip
// can be tested without a window. The half that reads it out of the live UI
// and puts it back is layout_store.cpp.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct LayoutRecord {
  std::string name;
  // ImGui::SaveIniSettingsToMemory(): window positions, sizes, dock nodes,
  // splits and tab order, in ImGui's own format. Opaque to us on purpose -
  // re-deriving it would mean re-implementing the dock builder.
  std::string ini;

  // One bit per viewport slot, bit 0 = View 1.
  unsigned view_mask = 1;

  // What each viewport shows. Saved with the layout because "quad view" means
  // perspective/top/front/right, not four copies of the same camera.
  struct View {
    int camera = 0;       // 0 perspective, 1 top, 2 front, 3 right
    int display = 2;      // 0 wireframe, 1 solid, 2 textured
    int scene_camera = -2;// -2 active camera, -1 free orbit, >=0 object index
    bool atmosphere = true;
    bool water = true;
    bool grid = false;
    bool outlines = true;
  };
  std::vector<View> views;

  // Extra node editors, one domain each (see WS_* in app.hpp).
  std::vector<int> editor_domains;

  // Which panels are open.
  bool library = true, nodelist = true, properties = true, viewport = true;
  bool console = true, timeline = false, preview = true;
  bool material_editor = false;

  int workspace = 0; // which workspace tab was active
};

// Serialization. `layout_from_json` fills defaults for anything absent, so an
// older file still loads; it returns false only for text that is not an
// object at all.
std::string layout_to_json(const LayoutRecord &r);
bool layout_from_json(const std::string &text, LayoutRecord &r,
                      std::string &err);

// A name that is safe as a file name and still recognisable: letters, digits,
// spaces, dashes and underscores survive; everything else is dropped. Empty
// or all-punctuation names become "Layout".
std::string layout_safe_name(const std::string &name);

// Where layouts live: <settings>/layouts/. Created on demand.
std::filesystem::path layouts_dir();

// The saved layouts, by display name, sorted.
std::vector<std::string> layout_list();

bool layout_write(const LayoutRecord &r, std::string &err);
bool layout_read(const std::string &name, LayoutRecord &r, std::string &err);
bool layout_erase(const std::string &name, std::string &err);

} // namespace studio
