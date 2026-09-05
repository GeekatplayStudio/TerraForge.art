// Geekatplay Studio — application state shared by all panels
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
    bool is_points = false;  // the point-cloud domain
    unsigned field_type = 0; // gpx::FieldType, meaningful when is_field
    bool optional = false;
    std::string value; // outputs: what the connector carries ("0.02..0.97")
  };
  struct NodeView {
    uint64_t id = 0;
    std::string type, category, error;
    float pos_x = 0, pos_y = 0;
    double ms = 0;
    bool enabled = true;
    int collapse = 0; // 0 expanded, 1 compact (no preview), 2 header only
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

  // workspace screen, one of the WS_* domains below (never WS_ALL)
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
  bool show_console = true;
  bool show_timeline = false;
  bool show_preview = true;
  bool show_material_editor = false;
  bool show_mesh_tools = false;
  // The Materials workspace's own windows: the studio (preview + every
  // property of the material being edited) and the browser (project
  // materials, the library, the asset index).
  bool show_material_studio = false;
  bool show_material_browser = false;
  // animation transport
  float anim_start = 0.f, anim_end = 10.f;
  bool anim_playing = false, anim_loop = true;
  // PNG-sequence capture: one animation frame per completed evaluation
  bool seq_active = false;
  float seq_fps = 30.f;
  int seq_frame = 0, seq_total = 0, seq_w = 1280, seq_h = 720;
  std::string seq_dir;
  // optional fly-through: a Points node whose ordered cloud is the camera
  // path; the active camera rides it for the length of the sequence
  unsigned long long seq_cam_path = 0;
  float seq_cam_height = 0.08f;
  // optional sun sweep across the sequence: azimuth/altitude interpolated
  // from [0]/[1] to [2]/[3] when enabled - a day cycle in one op
  bool seq_sun_sweep = false;
  float seq_sun[4] = {90.f, 10.f, 270.f, 10.f};
  bool request_layout_reset = false;
  unsigned dockspace_id = 0; // the main dockspace, for "dock this window back"
  // A layout being loaded. ImGui's window state can only be replaced between
  // frames, so the menu (or a script) parks the ini text here and app.cpp
  // applies it at the top of the next frame, before any window is submitted.
  std::string pending_layout_ini;
  // The viewport the user touched last: where "add a viewport" and "split"
  // put the new one, so a new view appears beside the one being worked in.
  int view_focus = 0;
  uint64_t graph_layout_serial = 1; // bump to push node positions into editor
  // Set to a node id to have the graph select it and pan to it on the next
  // frame; the graph panel clears it. This is how "open this in the node
  // editor" works from anywhere else in the application.
  uint64_t focus_node = 0;
  std::string status;

  void request_eval() {
    graph_changed_since_eval = true;
    eval.request.store(true);
  }
};

App &app();

// The workspaces. Each is one node editor domain, a tab on the workspace
// bar, and a filter on the library; a node belongs to exactly one through its
// category. The numbering is historical (4 was "all domains" before the
// later workspaces existed, and saved editor layouts carry these numbers),
// so new workspaces append rather than renumber. WORKSPACE_ORDER is the
// order the bar shows them in.
enum : int {
  WS_TERRAIN = 0,
  WS_MATERIALS = 1,
  WS_ATMOSPHERE = 2,
  WS_RENDER = 3,
  WS_ALL = 4, // a node editor pinned to every domain at once
  WS_OBJECTS = 5,
  WS_LIGHTING = 6,
  WS_CAMERAS = 7,
  WS_ANIMATION = 8,
  WS_COUNT = 9
};
static const int WORKSPACE_ORDER[8] = {WS_TERRAIN,  WS_MATERIALS, WS_OBJECTS,
                                       WS_ATMOSPHERE, WS_LIGHTING, WS_CAMERAS,
                                       WS_ANIMATION, WS_RENDER};
// localised name of a workspace (toolbar_bars.cpp)
const char *workspace_name(int ws);

// which workspace a node category belongs to
inline int domain_of_category(const std::string &cat) {
  if (cat == "Material" || cat == "Texture") return WS_MATERIALS;
  if (cat == "Atmosphere" || cat == "Cloud") return WS_ATMOSPHERE;
  if (cat == "Render") return WS_RENDER;
  if (cat == "Scene") return WS_OBJECTS;
  if (cat == "Light") return WS_LIGHTING;
  if (cat == "Camera") return WS_CAMERAS;
  if (cat == "Animation") return WS_ANIMATION;
  return WS_TERRAIN;
}

const char *view_window_name(int slot);

// ------------------------------------------------------------------ layout
// The dock arrangement: which viewports exist and where every window sits.
// Viewports are a set of up to RenderSettings::MAX_VIEWS slots (Prefs::view_mask);
// the helpers below change that set *in place*, without rebuilding the whole
// layout, so adding a view never costs the user the arrangement they built.
void build_default_layout(unsigned dockspace_id, unsigned view_mask);
int view_first_free();                 // -1 when every slot is open
void view_open(App &a, int slot);      // beside the focused view, as a tab
void view_close(App &a, int slot);     // never closes the last one
void view_split(App &a, int slot, bool vertical); // new view beside `slot`
void views_arrange(App &a, int n);     // n cells in the viewport region
// Extra node editors, set to exactly these domains (layout load).
void graph_editors_set(App &a, const std::vector<int> &domains);

// Named layouts (layout_store.cpp). Saving captures the live arrangement;
// loading applies our own state now and the window positions next frame.
bool layout_save_current(App &a, const std::string &name, std::string &err);
bool layout_load_named(App &a, const std::string &name, std::string &err);

// Show `node` in the node editor: switch to the workspace it belongs to,
// select it, and pan the graph to it. Does nothing if the node is gone.
void graph_focus_node(App &a, uint64_t node);

// panels
void draw_toolbar(App &a);       // row 1: the classic text menus
void draw_workspace_bar(App &a); // row 2: which workflow
void draw_panel_mesh(App &a);    // Mesh Tools: analyse, repair, reduce
void draw_panel_settings(App &a); // Settings: general, shortcuts, AI services, ComfyUI, apps, assets
void settings_open();
void draw_panel_material_studio(App &a);  // panel_material_studio.cpp
void draw_panel_material_browser(App &a); // panel_material_browser.cpp
// Every workspace keeps its own window arrangement (layout_workspace.cpp).
void workspace_layout_switch(App &a, int from, int to);
void build_materials_layout(unsigned dockspace_id, unsigned view_mask);
void mesh_tool_buttons(App &a);  // the same, on the Objects tool row
void draw_tool_bar(App &a);      // row 3: the tools for that workflow
void draw_global_tools(App &a);  // left column: tools common to every workflow
void draw_panel_graph(App &a);
void draw_panel_properties(App &a);
void draw_panel_library(App &a);
void draw_panel_nodelist(App &a);
void draw_panel_viewport(App &a);
void draw_panel_ai(App &a);
void draw_panel_scene(App &a); // the Objects tree
void scene_layers_ui(App &a);  // the layer list, drawn inside Properties
void draw_console(App &a);     // the message log

// Properties-editor tab bodies (no window of their own)
void object_properties_ui(App &a);
void node_properties_ui(App &a);
// The same parameters for a given node, as the side pane of a node editor:
// any_workspace skips the "belongs to another workspace" redirect.
void node_properties_ui(App &a, uint64_t node_id, bool any_workspace);
// Open another node editor window: domain 0..3 (terrain, materials,
// atmosphere, render) or 4 for every domain at once.
void graph_editor_add(App &a, int domain);
void scene_properties_ui(App &a);
// the labelled slider + stepper used throughout the properties editor
bool scalar_float(const char *id, float *v, float mn, float mx,
                  bool log_scale = false);
void world_properties_ui(App &a);
void material_properties_ui(App &a);
void render_properties_ui(App &a);
void render_passes_ui(App &a);   // panel_render_editor.cpp: format + passes
void render_backdrop_ui(App &a); // panel_render_editor.cpp: the HDR dome
void draw_render_window(App &a); // live progressive render view
void render_service_requests(App &a); // camera/AI render requests, per frame
// the progressive result of the running/last render, for other panels
unsigned render_live_texture(int &w, int &h, bool &busy, std::string &line);
const char *render_engine_label(int engine);
void render_cancel();
// The Preview panel: the chosen camera's view, rendered on its own with its
// own atmosphere/cloud/water/shadow switches and quality, independent of
// what the working viewports are showing - plus the final engine's result.
void draw_panel_preview(App &a);
// The layer stack of the selected object's material, shown the
// way an image editor shows layers. A second view of the same
// MaterialLayer nodes, not a separate store.
void draw_panel_material_editor(App &a);
bool draw_attribute(gpx::Attribute &at);
void draw_panel_timeline(App &a);
void scene_rebuild_scatter_instances(App &a);
// per-frame services (app_services.cpp)
void app_service_sequence(App &a);
void app_service_points_overlay(App &a);
void app_service_camera_anim(App &a);
void app_set_overlay_terrain(std::shared_ptr<gpx::Heightmap> hm);
struct SceneObject;
void camera_properties_ui(App &a, SceneObject &obj);
// The optical simulation and the copy-to-other-cameras button, split out
// into panel_camera_optics.cpp.
struct CameraData;
bool camera_optics_ui(App &a, CameraData &cd);
bool camera_copy_ui(App &a, int this_index);
// the SurfaceDisplacement picker + "Edit graph" buttons (panel_properties_object_surface.cpp)
void surface_graph_picker(App &a, unsigned long long *node);
// panel_properties_object_planet.cpp: the Planet and InfiniteSurface pages
void object_properties_planet_ui(App &a, SceneObject &o);
void object_properties_surface_ui(App &a, SceneObject &o);
void camera_apply_film();
void studio_api_tick(App &a); // scripting / MCP bridge
void apply_scene_nodes(App &a);
// true when the Properties search box is empty or matches this label
bool prop_filter_match(const char *text);
// true when the user has typed something into that search box
bool prop_filter_active();

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
// The level the tile was placed at (studio/planet_place.cpp), heightmap
// units; the infinite surround builds its relief on it so the two meet.
void renderer_set_terrain_base(float ground);
// world-space x,y,z triplets ticked into every 3D view (empty clears)
void renderer_set_points_overlay(const std::vector<float> &xyz);
// renders to an FBO sized (w,h), returns color texture id
unsigned renderer_draw(int w, int h, float dt);
void renderer_handle_input(float dx, float dy, float wheel, bool rotating, bool panning);
struct RenderSettings; // full definition in render_settings.hpp
void renderer_settings_ui();

// project io
void project_new(App &a);
// File > New: ask for size, relief and resolution before discarding anything
void new_terrain_request();
void new_terrain_dialog(App &a);
bool project_save(App &a, const std::string &path);
bool project_load(App &a, const std::string &path);
void project_default_graph(App &a);
// The node editor's saved pan/zoom/positions. A file with non-finite or absurd
// values makes the editor hang for ever on every launch, so it is validated
// before the editor loads it and discarded if it cannot be trusted.
bool graph_view_is_sane(const std::string &text);
void discard_insane_graph_view(const std::string &path);

} // namespace studio


