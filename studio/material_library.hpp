// Geekatplay TerraForge — on-disk material library
#pragma once
#include <string>
#include <vector>

namespace studio {
struct App;

struct LibraryMaterial {
  std::string name;
  std::string path;      // .gpxmat json
  std::string thumb;     // .png thumbnail next to it (may be missing)
  unsigned thumb_tex = 0; // lazily loaded GL texture
};

// library folder (%LOCALAPPDATA%/GeekatplayTerraForge/material_library)
std::string material_library_dir();
std::vector<LibraryMaterial> &material_library(); // cached listing
void material_library_rescan();
// save the material subgraph + a rendered thumbnail; returns saved path
std::string material_library_save(App &a, unsigned long long mat_node_id,
                                  std::string &err);
// instantiate a saved material into the graph; returns new MaterialOutput id
unsigned long long material_library_load(App &a, const LibraryMaterial &m,
                                         std::string &err);
// import a folder of PBR textures (standard suffix conventions) as a new
// material wired from TextureFile nodes; returns MaterialOutput id
unsigned long long material_import_texture_set(App &a, const std::string &any_file,
                                               std::string &err);
unsigned material_thumb_texture(LibraryMaterial &m); // 0 if none
} // namespace studio
