// Geekatplay Studio â€” application state shared by all panels
#pragma once
#include "gpx/node_graph.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct GLFWwindow;

namespace studio {

struct EvalState {
  std::atomic<bool> running{false};
  std::atomic<bool> request{false};  // re-eval wanted
  std::atomic<int> progress_done{0};
  std::atomic<int> progress_total{0};
  std::string current_node; // guarded by mutex below
  std::mutex mtx;
  std::thread worker;
};

struct App {
  GLFWwindow *window = nullptr;
  gpx::Graph graph;
  std::mutex graph_mtx; // hold while touching graph from UI or eval thread

  uint64_t selected_node = 0;
  uint64_t view_node = 0; // node shown in 3D viewport (0 = selected)
  std::string project_path;
  bool graph_changed_since_eval = true;
  uint64_t eval_serial = 0;   // bumped when an eval finishes (upload triggers)
  uint64_t uploaded_serial = 0;

  EvalState eval;

  // ---- non-blocking UI snapshot ----------------------------------------
  // Evaluation holds graph_mtx for its whole run. Panels must never block on
  // it (that made the graph and properties blink out while scrolling), so
  // they draw from this snapshot, refreshed whenever the lock is free.
  struct PortView {
    std::string name;
    bool is_input = false;
    bool is_texture = false;
    bool is_field = false;   // the field domain rather than a buffer
    unsigned field_type = 0; // gpx::FieldType, meaningful when is_field
    bool optional = false;
  };
  struct NodeView {
    uint64_t id = 0;
    std::string type, category, error;
    float pos_x = 0, pos_y = 0;
    double ms = 0;
    bool enabled = true;
    std::vector<PortView> ports;
  };
  struct LinkView {
    uint64_t id = 0, from_node = 0, to_node = 0;
    std::string from_port, to_port;
  };
  std::vector<NodeView> node_views;
  std::vector<LinkView> link_views;
  int snapshot_resolution = 512;
  size_t snapshot_bytes = 0;
  double snapshot_total_ms = 0;
  void refresh_snapshot(); // call only while holding graph_mtx
  // while a slider is being dragged, evaluate at a low resolution for
  // smooth realtime feedback; a full-res pass runs on release
  std::atomic<bool> eval_interactive{false};

  // workspace screens: 0 Terrain, 1 Materials, 2 Atmosphere, 3 Render
  int workspace = 0;
  bool graph_show_all_domains = false;
  uint64_t scene_selection_serial = 1; // bumped when the scene selection changes
  int request_camera_render = -1;      // camera index queued for rendering
  // Blender-style Properties editor: which vertical tab is active.
  // 0 Render, 1 Scene, 2 World, 3 Object, 4 Material, 5 Node
  int prop_tab = 3;

  // ui visibility
  bool show_library = true;
  bool show_nodelist = true;
  bool show_properties = true;
  bool show_viewport = true;
  bool show_toolbar = true;
  bool request_layout_reset = false;
  uint64_t graph_layout_serial = 1; // bump to push node positions into editor
  std::string status;

  void request_eval() {
    graph_changed_since_eval = true;
    eval.request.store(true);
  }
};

App &app();

// which workspace a node category belongs to
inline int domain_of_category(const std::string &cat) {
  if (cat == "Material" || cat == "Texture") return 1;
  if (cat == "Atmosphere") return 2;
  if (cat == "Render") return 3;
  return 0; // terrain
}

const char *view_window_name(int slot);

// panels
void draw_toolbar(App &a);
void draw_panel_graph(App &a);
void draw_panel_properties(App &a);
void draw_panel_library(App &a);
void draw_panel_nodelist(App &a);
void draw_panel_viewport(App &a);
void draw_panel_ai(App &a);
void draw_panel_scene(App &a); // the Outliner

// Properties-editor tab bodies (no window of their own)
void world_properties_ui(App &a);
void material_properties_ui(App &a);
void render_properties_ui(App &a);
void draw_render_window(App &a); // live progressive render view
struct SceneObject;
void camera_properties_ui(App &a, SceneObject &obj);
void camera_apply_film();
void studio_api_tick(App &a); // scripting / MCP bridge
void apply_scene_nodes(App &a);
// true when the Properties search box is empty or matches this label
bool prop_filter_match(const char *text);

enum PropTab { TAB_RENDER = 0, TAB_SCENE, TAB_WORLD, TAB_OBJECT, TAB_MATERIAL,
               TAB_NODE };

// theme
void apply_theme();
// flat square toggle (no check mark) used app-wide instead of ImGui::Checkbox
bool Checkbox(const char *label, bool *v);

// previews (per-node thumbnails as GL textures)
void previews_update(App &a);
void previews_clear();
unsigned previews_get(uint64_t node_id, int *w = nullptr, int *h = nullptr);

// renderer
bool renderer_init();
void renderer_shutdown();
// uploads heightmap + optional albedo of the viewed node
void renderer_set_terrain(const gpx::Heightmap &h, const gpx::TextureRGBA *albedo);
// renders to an FBO sized (w,h), returns color texture id
unsigned renderer_draw(int w, int h, float dt);
void renderer_handle_input(float dx, float dy, float wheel, bool rotating, bool panning);
struct RenderSettings; // full definition in render_settings.hpp
void renderer_settings_ui();

// project io
void project_new(App &a);
bool project_save(App &a, const std::string &path);
bool project_load(App &a, const std::string &path);
void project_default_graph(App &a);
// The node editor's saved pan/zoom/positions. A file with non-finite or absurd
// values makes the editor hang for ever on every launch, so it is validated
// before the editor loads it and discarded if it cannot be trusted.
bool graph_view_is_sane(const std::string &text);
void discard_insane_graph_view(const std::string &path);

} // namespace studio


