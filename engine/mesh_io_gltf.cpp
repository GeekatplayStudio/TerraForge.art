// Geekatplay TerraForge - reading binary glTF (.glb), the format every 3D
// generation service hands back. Geometry only: every mesh primitive's
// POSITION accessor and its indices, node transforms applied, welded into
// one TriMesh. Materials and textures are left to the file; the studio
// reads geometry here and wears its own materials.
//
// GLB: a 12-byte header, a JSON chunk, a BIN chunk. Accessors describe
// typed views into BIN. This reads what a mesh needs and nothing else, so
// a file from any of the providers loads without a glTF library.
#include "gpx/mesh_io.hpp"
#include <cstring>
#include <fstream>
#include <json.hpp>
#include <sstream>

using nlohmann::json;

namespace gpx {

namespace {

struct Accessor {
  const unsigned char *data = nullptr;
  size_t count = 0;
  int comp_type = 0; // 5121 u8, 5123 u16, 5125 u32, 5126 f32
  int comps = 1;
  size_t stride = 0;
};

bool accessor(const json &doc, const std::string &bin, int index, Accessor &out) {
  const json &accs = doc.value("accessors", json::array());
  const json &views = doc.value("bufferViews", json::array());
  if (index < 0 || index >= (int)accs.size()) return false;
  const json &a = accs[(size_t)index];
  int view = a.value("bufferView", -1);
  if (view < 0 || view >= (int)views.size()) return false;
  const json &v = views[(size_t)view];
  std::string type = a.value("type", "SCALAR");
  out.comps = type == "VEC3" ? 3 : type == "VEC2" ? 2 : type == "VEC4" ? 4 : 1;
  out.comp_type = a.value("componentType", 5126);
  out.count = a.value("count", 0u);
  size_t comp_size = out.comp_type == 5126 || out.comp_type == 5125 ? 4 : out.comp_type == 5123 ? 2 : 1;
  size_t offset = v.value("byteOffset", 0u) + a.value("byteOffset", 0u);
  out.stride = v.value("byteStride", 0u);
  if (!out.stride) out.stride = comp_size * out.comps;
  if (offset + out.stride * (out.count ? out.count - 1 : 0) + comp_size * out.comps > bin.size()) return false;
  out.data = (const unsigned char *)bin.data() + offset;
  return out.count > 0;
}

float read_f(const unsigned char *p) { float f; std::memcpy(&f, p, 4); return f; }
uint32_t read_index(const unsigned char *p, int comp_type) {
  if (comp_type == 5125) { uint32_t v; std::memcpy(&v, p, 4); return v; }
  if (comp_type == 5123) { uint16_t v; std::memcpy(&v, p, 2); return v; }
  return *p;
}

void mat_mul(const float *a, const float *b, float *out) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = s;
    }
}

void node_matrix(const json &n, float *m) {
  static const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  std::memcpy(m, I, sizeof I);
  if (n.contains("matrix") && n["matrix"].size() == 16) {
    for (int i = 0; i < 16; ++i) m[i] = n["matrix"][(size_t)i].get<float>();
    return;
  }
  float t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
  if (n.contains("translation")) for (int i = 0; i < 3; ++i) t[i] = n["translation"][(size_t)i].get<float>();
  if (n.contains("scale")) for (int i = 0; i < 3; ++i) s[i] = n["scale"][(size_t)i].get<float>();
  if (n.contains("rotation")) for (int i = 0; i < 4; ++i) q[i] = n["rotation"][(size_t)i].get<float>();
  float x = q[0], y = q[1], z = q[2], w = q[3];
  float R[16] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
                 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
                 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
                 0, 0, 0, 1};
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r) m[c * 4 + r] = R[c * 4 + r] * s[c];
  m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
}

void add_primitive(const json &doc, const std::string &bin, const json &prim, const float *M, TriMesh &out) {
  const json &attrs = prim.value("attributes", json::object());
  if (!attrs.contains("POSITION")) return;
  if (prim.value("mode", 4) != 4) return; // triangles only
  Accessor pos;
  if (!accessor(doc, bin, attrs["POSITION"].get<int>(), pos) || pos.comp_type != 5126 || pos.comps != 3) return;
  uint32_t base = (uint32_t)out.vert_count();
  for (size_t i = 0; i < pos.count; ++i) {
    const unsigned char *p = pos.data + i * pos.stride;
    float v[3] = {read_f(p), read_f(p + 4), read_f(p + 8)};
    float w[3];
    for (int r = 0; r < 3; ++r) w[r] = M[r] * v[0] + M[4 + r] * v[1] + M[8 + r] * v[2] + M[12 + r];
    out.v.insert(out.v.end(), w, w + 3);
  }
  if (prim.contains("indices")) {
    Accessor idx;
    if (!accessor(doc, bin, prim["indices"].get<int>(), idx)) return;
    for (size_t i = 0; i + 2 < idx.count; i += 3)
      for (int k = 0; k < 3; ++k) out.f.push_back(base + read_index(idx.data + (i + k) * idx.stride, idx.comp_type));
  } else {
    for (uint32_t i = 0; i + 2 < (uint32_t)pos.count; i += 3) {
      out.f.push_back(base + i);
      out.f.push_back(base + i + 1);
      out.f.push_back(base + i + 2);
    }
  }
}

void walk(const json &doc, const std::string &bin, int node_index, const float *parent, TriMesh &out, int depth) {
  const json &nodes = doc.value("nodes", json::array());
  if (depth > 64 || node_index < 0 || node_index >= (int)nodes.size()) return;
  const json &n = nodes[(size_t)node_index];
  float local[16], M[16];
  node_matrix(n, local);
  mat_mul(parent, local, M);
  if (n.contains("mesh")) {
    const json &meshes = doc.value("meshes", json::array());
    int mi = n["mesh"].get<int>();
    if (mi >= 0 && mi < (int)meshes.size())
      for (const json &prim : meshes[(size_t)mi].value("primitives", json::array()))
        add_primitive(doc, bin, prim, M, out);
  }
  for (const json &c : n.value("children", json::array())) walk(doc, bin, c.get<int>(), M, out, depth + 1);
}

} // namespace

bool mesh_load_glb(const std::string &path, TriMesh &out, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { err = "cannot open " + path; return false; }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string all = ss.str();
  if (all.size() < 20 || std::memcmp(all.data(), "glTF", 4) != 0) { err = "not a GLB file"; return false; }
  std::string js, bin;
  size_t pos = 12;
  while (pos + 8 <= all.size()) {
    uint32_t len, type;
    std::memcpy(&len, all.data() + pos, 4);
    std::memcpy(&type, all.data() + pos + 4, 4);
    pos += 8;
    if (pos + len > all.size()) break;
    if (type == 0x4E4F534Au) js.assign(all.data() + pos, len);       // JSON
    else if (type == 0x004E4942u) bin.assign(all.data() + pos, len); // BIN
    pos += len;
  }
  json doc = json::parse(js, nullptr, false);
  if (doc.is_discarded()) { err = "GLB has no readable JSON chunk"; return false; }
  static const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const json &scenes = doc.value("scenes", json::array());
  int scene = doc.value("scene", 0);
  if (!scenes.empty() && scene < (int)scenes.size()) {
    for (const json &n : scenes[(size_t)scene].value("nodes", json::array())) walk(doc, bin, n.get<int>(), I, out, 0);
  } else {
    // no scene: every mesh as it is
    for (const json &m : doc.value("meshes", json::array()))
      for (const json &prim : m.value("primitives", json::array())) add_primitive(doc, bin, prim, I, out);
  }
  if (out.f.empty()) { err = "GLB holds no triangle geometry"; return false; }
  return true;
}

} // namespace gpx
