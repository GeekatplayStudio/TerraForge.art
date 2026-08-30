// Geekatplay Studio — project save/load + default startup graph
#include "app.hpp"
#include "gpx/serialization.hpp"

namespace studio {

void project_new(App &a) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  a.graph.clear();
  a.selected_node = a.view_node = 0;
  a.project_path.clear();
  a.status = "new project";
}

bool project_save(App &a, const std::string &path) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  bool ok = gpx::save_project(a.graph, path);
  a.status = ok ? ("saved " + path) : ("SAVE FAILED: " + path);
  if (ok) a.project_path = path;
  return ok;
}

bool project_load(App &a, const std::string &path) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  std::string err;
  bool ok = gpx::load_project(a.graph, path, err);
  a.status = ok ? ("loaded " + path) : ("LOAD FAILED: " + err);
  if (ok) {
    a.project_path = path;
    a.selected_node = a.view_node = 0;
    a.graph_layout_serial++;
    a.request_eval();
  }
  return ok;
}

// startup demo: ridged mountains -> warp -> hydraulic erosion -> texture
void project_default_graph(App &a) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  a.graph.clear();
  a.graph.resolution = 512;
  gpx::Node *noise = a.graph.add_node("Noise", 0, 120);
  gpx::Node *warp = a.graph.add_node("WarpNoise", 260, 120);
  gpx::Node *erode = a.graph.add_node("Hydraulic", 520, 120);
  gpx::Node *tex = a.graph.add_node("TerrainTexture", 820, 260);
  if (noise && warp && erode && tex) {
    a.graph.add_link(noise->id, "output", warp->id, "input");
    a.graph.add_link(warp->id, "output", erode->id, "input");
    a.graph.add_link(erode->id, "output", tex->id, "input");
    a.view_node = erode->id;
    a.selected_node = erode->id;
  }
}

} // namespace studio
