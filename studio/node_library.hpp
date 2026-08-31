// Geekatplay TerraForge — the MetaNode library (P0.4).
//
// Vue's argument for MetaNodes (manual p976-978) is that saving and reloading
// them is what turns grouping into a building block: you build a "weathered
// granite" or "coastal shelf" once and then reuse it like any built-in node.
// Saved MetaNodes appear in the node browser beside the built-ins.
#pragma once
#include <string>
#include <vector>

namespace studio {
struct App;

struct SavedMetaNode {
  std::string name;   // shown in the browser
  std::string note;   // one-line description, becomes the tooltip
  std::string path;   // file on disk
  int inner_nodes = 0;
  int published = 0;
};

std::string node_library_dir();
// rescan the folder; cheap enough to call when a browser opens
const std::vector<SavedMetaNode> &node_library(bool refresh = false);

// Save the selected MetaNode under `name`. Returns false with `err` set.
bool node_library_save(App &a, unsigned long long metanode_id,
                       const std::string &name, const std::string &note,
                       std::string &err);
// Instantiate a saved MetaNode into the graph at (x, y). Returns its id, or 0.
unsigned long long node_library_load(App &a, const std::string &path, float x,
                                     float y, std::string &err);
bool node_library_delete(const std::string &path);

} // namespace studio
