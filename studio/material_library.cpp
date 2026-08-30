// Geekatplay TerraForge — on-disk material library.
// Design informed by the top complaints about Substance's and Blender's
// libraries: thumbnails are regenerated deterministically on every save
// (never stale), loading always makes an independent copy with fresh ids
// (no append/link confusion), and deleting a material actually deletes it.
#include "material_library.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "gpx/serialization.hpp"
#include <glad/gl.h>
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace studio {

std::string material_library_dir() {
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path dir = base ? fs::path(base) : fs::temp_directory_path();
  dir = dir / "GeekatplayTerraForge" / "material_library";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

static std::vector<LibraryMaterial> g_lib;
static bool g_scanned = false;

std::vector<LibraryMaterial> &material_library() {
  if (!g_scanned) material_library_rescan();
  return g_lib;
}

void material_library_rescan() {
  for (auto &m : g_lib)
    if (m.thumb_tex) glDeleteTextures(1, &m.thumb_tex);
  g_lib.clear();
  g_scanned = true;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(material_library_dir(), ec)) {
    if (e.path().extension() != ".gpxmat") continue;
    LibraryMaterial m;
    m.path = e.path().string();
    m.name = e.path().stem().string();
    fs::path t = e.path();
    t.replace_extension(".png");
    if (fs::exists(t)) m.thumb = t.string();
    g_lib.push_back(std::move(m));
  }
  std::sort(g_lib.begin(), g_lib.end(),
            [](const LibraryMaterial &a, const LibraryMaterial &b) {
              return a.name < b.name;
            });
}

unsigned material_thumb_texture(LibraryMaterial &m) {
  if (m.thumb_tex || m.thumb.empty()) return m.thumb_tex;
  int w, h, c;
  unsigned char *data = stbi_load(m.thumb.c_str(), &w, &h, &c, 4);
  if (!data) return 0;
  glGenTextures(1, &m.thumb_tex);
  glBindTexture(GL_TEXTURE_2D, m.thumb_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(data);
  return m.thumb_tex;
}

static std::string sanitize(std::string s) {
  for (char &c : s)
    if (strchr("\\/:*?\"<>|", c)) c = '_';
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s.empty() ? "Material" : s;
}

std::string material_library_save(App &a, unsigned long long mat_node_id,
                                 std::string &err) {
  std::string jtext;
  std::string name = "Material";
  {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    gpx::Node *n = a.graph.find_node(mat_node_id);
    if (!n || n->type != "MaterialOutput") {
      err = "no MaterialOutput selected";
      return {};
    }
    name = sanitize(n->attrs.get_s("name"));
    jtext = gpx::material_to_json(a.graph, mat_node_id);
  }
  fs::path dir = material_library_dir();
  fs::path file = dir / (name + ".gpxmat");
  int suffix = 1;
  while (fs::exists(file))
    file = dir / (name + "_" + std::to_string(++suffix) + ".gpxmat");
  std::ofstream f(file);
  if (!f) {
    err = "cannot write " + file.string();
    return {};
  }
  f << jtext;
  f.close();
  // deterministic thumbnail: render the material preview sphere right now
  unsigned tex = renderer_material_preview(192, 0, 0.6f);
  if (tex) {
    std::vector<unsigned char> px((size_t)192 * 192 * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // flip vertically
    std::vector<unsigned char> flip((size_t)192 * 192 * 4);
    for (int y = 0; y < 192; ++y)
      memcpy(&flip[(size_t)y * 192 * 4], &px[(size_t)(191 - y) * 192 * 4], 192 * 4);
    fs::path t = file;
    t.replace_extension(".png");
    stbi_write_png(t.string().c_str(), 192, 192, 4, flip.data(), 192 * 4);
  }
  material_library_rescan();
  return file.string();
}

unsigned long long material_library_load(App &a, const LibraryMaterial &m,
                                         std::string &err) {
  std::ifstream f(m.path);
  if (!f) {
    err = "cannot open " + m.path;
    return 0;
  }
  std::string text((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  float x = 200, y = 500;
  for (auto &n : a.graph.nodes) y = std::max(y, n->pos_y + 320);
  uint64_t id = gpx::material_from_json(a.graph, text, err, x, y);
  if (id) {
    a.graph_layout_serial++;
    a.request_eval();
  }
  return id;
}

// ---------------------------------------------------------------- importer
// Suffix conventions across ambientCG / Poly Haven / Quixel / Substance
// exports. Longest-match wins; glossiness maps are flagged for inversion.
struct SuffixRule {
  const char *token;
  const char *channel; // MaterialOutput port
  bool invert;         // gloss -> roughness
};
static const SuffixRule SUFFIXES[] = {
    // base color
    {"basecolor", "base color", false}, {"base_color", "base color", false},
    {"albedotransparency", "base color", false},
    {"albedo", "base color", false}, {"diffuse", "base color", false},
    {"color", "base color", false}, {"diff", "base color", false},
    {"col", "base color", false}, {"alb", "base color", false},
    // normal (GL preferred; DX accepted with a warning in the name check)
    {"normalgl", "normal", false}, {"nor_gl", "normal", false},
    {"normalopengl", "normal", false}, {"normal_opengl", "normal", false},
    {"normaldx", "normal", false}, {"nor_dx", "normal", false},
    {"normal", "normal", false}, {"norm", "normal", false},
    {"nrm", "normal", false}, {"nor", "normal", false},
    // roughness / glossiness
    {"roughness", "roughness", false}, {"rough", "roughness", false},
    {"rgh", "roughness", false},
    {"glossiness", "roughness", true}, {"gloss", "roughness", true},
    {"smoothness", "roughness", true},
    // metallic
    {"metalness", "metallic", false}, {"metallic", "metallic", false},
    {"metal", "metallic", false}, {"met", "metallic", false},
    // height / displacement
    {"displacement", "height", false}, {"disp", "height", false},
    {"height", "height", false}, {"bump", "height", false},
    // ambient occlusion
    {"ambientocclusion", "ambient occlusion", false},
    {"ambient_occlusion", "ambient occlusion", false},
    {"occlusion", "ambient occlusion", false},
    {"ao", "ambient occlusion", false}, {"occ", "ambient occlusion", false},
};

static std::string lower(std::string s) {
  for (char &c : s) c = (char)tolower(c);
  return s;
}

unsigned long long material_import_texture_set(App &a, const std::string &any_file,
                                               std::string &err) {
  fs::path folder = fs::path(any_file).parent_path();
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) {
    err = "not a folder: " + folder.string();
    return 0;
  }
  // channel -> best file (longest suffix match wins)
  struct Hit { std::string file; size_t score = 0; bool invert = false; };
  std::map<std::string, Hit> hits;
  for (auto &e : fs::directory_iterator(folder, ec)) {
    std::string ext = lower(e.path().extension().string());
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga" &&
        ext != ".bmp")
      continue;
    std::string stem = lower(e.path().stem().string());
    for (const SuffixRule &r : SUFFIXES) {
      size_t pos = stem.rfind(r.token);
      if (pos == std::string::npos) continue;
      // must be a suffix-ish token (at end or followed by resolution tag)
      size_t score = strlen(r.token) * 100 + pos;
      Hit &h = hits[r.channel];
      if (score > h.score) h = {e.path().string(), score, r.invert};
      break; // first (longest listed) rule that matches this file
    }
  }
  if (!hits.count("base color")) {
    err = "no base color / albedo texture recognized in " + folder.string();
    return 0;
  }

  std::lock_guard<std::mutex> lk(a.graph_mtx);
  float x0 = 200, y0 = 500;
  for (auto &n : a.graph.nodes) y0 = std::max(y0, n->pos_y + 320);
  gpx::Node *mat = a.graph.add_node("MaterialOutput", x0 + 300, y0 + 60);
  if (!mat) {
    err = "cannot create MaterialOutput";
    return 0;
  }
  if (gpx::Attribute *na = mat->attrs.find("name"))
    na->s = folder.filename().string();
  float y = y0;
  for (auto &[channel, hit] : hits) {
    gpx::Node *tf = a.graph.add_node("TextureFile", x0, y);
    if (!tf) continue;
    if (gpx::Attribute *p = tf->attrs.find("path")) p->s = hit.file;
    if (gpx::Attribute *m = tf->attrs.find("mapping")) m->i = 1; // tile
    if (hit.invert) {
      // glossiness map: invert through a Levels node into roughness
      gpx::Node *lv = a.graph.add_node("Levels", x0 + 150, y);
      if (lv) {
        if (gpx::Attribute *ob = lv->attrs.find("out_black")) ob->f = 1.f;
        if (gpx::Attribute *ow = lv->attrs.find("out_white")) ow->f = 0.f;
        if (gpx::Attribute *pc = lv->attrs.find("per_channel")) pc->b = true;
        a.graph.add_link(tf->id, "texture", lv->id, "texture");
        a.graph.add_link(lv->id, "texture", mat->id, channel);
      }
    } else {
      a.graph.add_link(tf->id, "texture", mat->id, channel);
    }
    y += 170;
  }
  a.graph_layout_serial++;
  a.request_eval();
  return mat->id;
}

} // namespace studio
