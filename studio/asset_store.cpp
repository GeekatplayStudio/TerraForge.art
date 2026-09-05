// Geekatplay TerraForge - the asset manager's studio side. See asset_store.hpp.
#include "asset_store.hpp"
#include "app.hpp"
#include "material_library.hpp"
#include "mesh_object.hpp"
#include "material_ui.hpp"
#include <glad/gl.h>
#include <cstdlib>
#include <filesystem>
#include <json.hpp>
#include "stb_image.h"
#include <unordered_map>

namespace fs = std::filesystem;

namespace studio {

namespace {

gpx::AssetIndex g_index;
bool g_loaded = false;
std::unordered_map<std::string, unsigned> g_thumbs;

std::string settings_base() {
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path dir = base ? fs::path(base) : fs::temp_directory_path();
  dir = dir / "GeekatplayTerraForge";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

void add_default_roots() {
  auto have = [&](const std::string &p) {
    for (const gpx::AssetRoot &r : g_index.roots)
      if (r.path == p) return true;
    return false;
  };
  std::string mats = material_library_dir();
  if (!have(mats))
    g_index.roots.push_back({mats, "material", asset_kind_extensions("material")});
  std::string layouts = (fs::path(settings_base()) / "layouts").string();
  if (!have(layouts))
    g_index.roots.push_back({layouts, "layout", asset_kind_extensions("layout")});
}

} // namespace

std::string asset_index_file() {
  return (fs::path(settings_base()) / "assets.json").string();
}

std::vector<std::string> asset_kind_extensions(const std::string &kind) {
  if (kind == "material") return {"gpxmat"};
  if (kind == "texture") return {"png", "jpg", "jpeg", "tga", "exr", "hdr", "tif", "tiff"};
  if (kind == "mesh") return {"obj", "stl", "ply", "off"};
  if (kind == "layout") return {"json"};
  if (kind == "macro") return {"gpxmacro", "json"};
  if (kind == "project") return {"gpx", "json"};
  return {"gpxmat", "obj", "stl", "ply", "off", "png", "jpg", "exr", "hdr", "json"};
}

gpx::AssetIndex &asset_index() {
  if (!g_loaded) {
    g_loaded = true;
    bool had = g_index.load(asset_index_file());
    add_default_roots();
    if (!had) asset_rescan();
  }
  return g_index;
}

size_t asset_rescan() {
  asset_index();
  size_t n = g_index.scan();
  g_index.save(asset_index_file());
  asset_thumbs_clear();
  return n;
}

bool asset_save() { return asset_index().save(asset_index_file()); }

bool asset_add_root(const std::string &path, const std::string &kind,
                    std::string &err) {
  std::error_code ec;
  if (!fs::is_directory(path, ec)) {
    err = "no such folder: " + path;
    return false;
  }
  gpx::AssetIndex &ix = asset_index();
  std::string abs = fs::absolute(path, ec).string();
  for (gpx::AssetRoot &r : ix.roots)
    if (r.path == abs) {
      r.kind = kind.empty() ? r.kind : kind;
      asset_rescan();
      return true;
    }
  std::string k = kind.empty() ? "other" : kind;
  ix.roots.push_back({abs, k, asset_kind_extensions(k)});
  asset_rescan();
  return true;
}

bool asset_remove_root(const std::string &path) {
  gpx::AssetIndex &ix = asset_index();
  std::error_code ec;
  std::string abs = fs::absolute(path, ec).string();
  for (size_t i = 0; i < ix.roots.size(); ++i)
    if (ix.roots[i].path == abs || ix.roots[i].path == path) {
      ix.roots.erase(ix.roots.begin() + (long)i);
      asset_rescan();
      return true;
    }
  return false;
}

bool asset_open(App &a, const std::string &id, std::string &err) {
  const gpx::AssetRecord *r = asset_index().find(id);
  if (!r) {
    err = "no asset '" + id + "'";
    return false;
  }
  if (r->kind == "material") {
    LibraryMaterial lm;
    lm.name = r->name;
    lm.path = r->path;
    lm.thumb = r->thumb;
    unsigned long long mid = material_library_load(a, lm, err);
    if (!mid) return false;
    a.graph_layout_serial++;
    a.request_eval();
    a.show_material_studio = true;
    material_studio().material = 0;
    material_studio_open(a, mid);
    a.status = "loaded '" + r->name + "'";
    return true;
  }
  if (r->kind == "mesh") {
    int idx = scene_import_mesh(r->path, err);
    if (idx < 0) return false;
    a.status = "imported " + r->name;
    return true;
  }
  if (r->kind == "layout") {
    std::string name = fs::path(r->path).stem().string();
    if (!layout_load_named(a, name, err)) return false;
    a.status = "layout '" + name + "' applied";
    return true;
  }
  if (r->kind == "texture") {
    a.status = "texture: " + r->path;
    return true;
  }
  err = "an asset of kind '" + r->kind + "' has no open action yet";
  return false;
}

unsigned asset_thumb_texture(const gpx::AssetRecord &r) {
  // A texture asset is its own thumbnail.
  std::string file = !r.thumb.empty() ? r.thumb : (r.kind == "texture" ? r.path : "");
  if (file.empty()) return 0;
  auto it = g_thumbs.find(file);
  if (it != g_thumbs.end()) return it->second;
  int w, h, c;
  // Mipmapped, so an 8k texture asset draws clean at tile size; loaded
  // once, the first time a tile for it is on screen.
  unsigned char *data = stbi_load(file.c_str(), &w, &h, &c, 4);
  unsigned tex = 0;
  if (data) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);
  }
  g_thumbs[file] = tex; // zero is remembered too: no retry every frame
  return tex;
}

void asset_thumbs_clear() {
  for (auto &kv : g_thumbs)
    if (kv.second) glDeleteTextures(1, &kv.second);
  g_thumbs.clear();
}

} // namespace studio
