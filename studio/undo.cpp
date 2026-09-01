#include "undo.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/serialization.hpp"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace studio {

// Imported mesh vertex data never changes after import, so snapshots share a
// single copy per mesh instead of duplicating megabytes on every undo step.
using MeshData = std::shared_ptr<const std::vector<float>>;
static std::map<std::string, MeshData> g_mesh_pool;

static std::string mesh_key(const SceneObject &o) {
  return o.path + "#" + std::to_string(o.verts.size());
}

// One editable-state snapshot: `label` names the edit that *produced* this
// state, so g_stack[0] is always "Initial state".
//
// Callers push before making a change, which means the state after that change
// does not exist yet. The entry is left unfilled and materialized the moment
// anything needs it — the next push, an undo, or a jump. That also means a
// drag that streams many values only records its final result.
struct Snapshot {
  std::string label;
  bool filled = false;
  std::string graph_json;
  bool has_graph = false;
  SceneState scene;                 // mesh verts stripped out, see mesh_data
  std::vector<MeshData> mesh_data;  // parallel to scene.objects
  RenderSettings world;
  int selected_index = -1;   // index into graph.nodes; ids are renumbered on load
  int view_index = -1;
  int graph_resolution = 512;
  int active_camera = -1, last_camera = -1;
};

static std::deque<Snapshot> g_stack; // g_stack[g_pos] is the current state
static int g_pos = -1;
static const size_t MAX_STEPS = 64;
static std::vector<std::string> g_labels_cache;

// `graph_held` says the caller already owns App::graph_mtx, so we must not try
// to take it again.
static Snapshot capture(App &a, const std::string &label, bool graph_held) {
  Snapshot s;
  s.label = label;
  s.filled = true;
  s.scene = scene();
  s.world = render_settings();
  s.active_camera = scene_active_camera();
  s.last_camera = scene_last_used_camera();

  s.mesh_data.resize(s.scene.objects.size());
  for (size_t i = 0; i < s.scene.objects.size(); ++i) {
    SceneObject &o = s.scene.objects[i];
    if (o.verts.empty()) continue;
    std::string key = mesh_key(o);
    auto it = g_mesh_pool.find(key);
    if (it == g_mesh_pool.end())
      it = g_mesh_pool.emplace(key, std::make_shared<const std::vector<float>>(
                                        o.verts)).first;
    s.mesh_data[i] = it->second;
    o.verts.clear();
    o.verts.shrink_to_fit();
  }

  // The evaluation thread may hold the graph; a snapshot of the scene alone is
  // still better than dropping the step entirely.
  std::unique_lock<std::mutex> lk;
  if (!graph_held) lk = std::unique_lock<std::mutex>(a.graph_mtx, std::try_to_lock);
  if (graph_held || lk.owns_lock()) {
    s.graph_json = gpx::graph_to_json(a.graph);
    s.has_graph = true;
    s.graph_resolution = a.graph.resolution;
    for (size_t i = 0; i < a.graph.nodes.size(); ++i) {
      if (a.graph.nodes[i]->id == a.selected_node) s.selected_index = (int)i;
      if (a.graph.nodes[i]->id == a.view_node) s.view_index = (int)i;
    }
  }
  return s;
}

static void restore(App &a, const Snapshot &s) {
  scene() = s.scene;
  for (size_t i = 0; i < scene().objects.size(); ++i) {
    if (i < s.mesh_data.size() && s.mesh_data[i]) {
      scene().objects[i].verts = *s.mesh_data[i];
      // the VAO was never deleted, so the mesh is still on the GPU as-is
    }
  }
  render_settings() = s.world;
  scene_active_camera() = s.active_camera;
  scene_last_used_camera() = s.last_camera;

  if (s.has_graph) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    std::string err;
    if (gpx::graph_from_json(a.graph, s.graph_json, err)) {
      a.graph.resolution = s.graph_resolution;
      // node ids are reassigned by the loader, so selection travels by index
      auto id_at = [&](int idx) -> uint64_t {
        return idx >= 0 && idx < (int)a.graph.nodes.size()
                   ? a.graph.nodes[idx]->id
                   : 0;
      };
      a.selected_node = id_at(s.selected_index);
      a.view_node = id_at(s.view_index);
      a.graph.mark_all_dirty();
    }
  }
  a.graph_layout_serial++;
  a.scene_selection_serial++;
  a.uploaded_serial = 0; // force material maps back to the GPU
  a.request_eval();
}

// Fill in the state of the entry we are sitting on. Until this runs, that
// entry only carries the name of the edit that is still in progress.
static void materialize(App &a, bool graph_held) {
  if (g_pos < 0 || g_pos >= (int)g_stack.size()) return;
  if (g_stack[g_pos].filled) return;
  std::string label = g_stack[g_pos].label;
  g_stack[g_pos] = capture(a, label, graph_held);
}

static void push_impl(App &a, const std::string &label, bool graph_held) {
  // a new edit rewrites the future: drop any redo branch
  while ((int)g_stack.size() > g_pos + 1) g_stack.pop_back();
  // the first push also records the state we are leaving
  if (g_stack.empty()) {
    g_stack.push_back(capture(a, "Initial state", graph_held));
    g_pos = 0;
  } else {
    // the previous edit is finished; record where it landed
    materialize(a, graph_held);
  }
  Snapshot pending;
  pending.label = label; // filled once the edit it names has been made
  g_stack.push_back(pending);
  g_pos = (int)g_stack.size() - 1;
  while (g_stack.size() > MAX_STEPS) {
    g_stack.pop_front();
    --g_pos;
  }
}

void undo_push(App &a, const std::string &label) {
  push_impl(a, label, false);
}

void undo_push_locked(App &a, const std::string &label) {
  push_impl(a, label, true);
}

bool undo_can_undo() { return g_pos > 0; }
bool undo_can_redo() { return g_pos >= 0 && g_pos + 1 < (int)g_stack.size(); }

std::string undo_next_label() {
  return undo_can_undo() ? g_stack[g_pos].label : std::string();
}

std::string undo_redo_label() {
  return undo_can_redo() ? g_stack[g_pos + 1].label : std::string();
}

bool undo_perform(App &a) {
  if (!undo_can_undo()) return false;
  materialize(a, false); // so redo can come back to where we are now
  // g_stack[g_pos] is the state that step produced, so stepping back one entry
  // lands on the state before it
  --g_pos;
  if (!g_stack[g_pos].filled) return false;
  restore(a, g_stack[g_pos]);
  return true;
}

bool redo_perform(App &a) {
  if (!undo_can_redo()) return false;
  if (!g_stack[g_pos + 1].filled) return false;
  ++g_pos;
  restore(a, g_stack[g_pos]);
  return true;
}

void undo_clear() {
  g_stack.clear();
  g_mesh_pool.clear();
  g_pos = -1;
}

int undo_history(const std::string **labels_out) {
  g_labels_cache.clear();
  for (const auto &s : g_stack) g_labels_cache.push_back(s.label);
  if (labels_out) *labels_out = g_labels_cache.data();
  return (int)g_labels_cache.size();
}

int undo_history_position() { return g_pos; }

void undo_jump_to(App &a, int index) {
  if (index < 0 || index >= (int)g_stack.size()) return;
  materialize(a, false); // keep the state we are leaving reachable
  if (!g_stack[index].filled) return;
  g_pos = index;
  restore(a, g_stack[index]);
}

} // namespace studio

