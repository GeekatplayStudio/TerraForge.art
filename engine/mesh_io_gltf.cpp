// Geekatplay TerraForge - reading glTF: binary .glb (what the 3D generation
// services hand back) and JSON .gltf with its .bin and images beside it.
//
// Geometry, texture coordinates and the base-colour picture of each
// material: every mesh primitive's POSITION and TEXCOORD_0 accessors and its
// indices, node transforms applied, welded into one TriMesh with one part
// per primitive. Images are kept as they are in the file (PNG/JPG bytes
// from a buffer view or a data: URI, or the path of a file next to the
// .gltf); decoding is the caller's job, so this stays free of any image
// library.
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

struct Doc {
  json j;
  std::vector<std::string> buffers; // buffer bytes, by index
  std::string dir;                  // folder of a .gltf, for uris
};

bool accessor(const Doc &d, int index, Accessor &out) {
  const json &accs = d.j.value("accessors", json::array());
  const json &views = d.j.value("bufferViews", json::array());
  if (index < 0 || index >= (int)accs.size()) return false;
  const json &a = accs[(size_t)index];
  int view = a.value("bufferView", -1);
  if (view < 0 || view >= (int)views.size()) return false;
  const json &v = views[(size_t)view];
  int buf = v.value("buffer", 0);
  if (buf < 0 || buf >= (int)d.buffers.size()) return false;
  const std::string &bin = d.buffers[(size_t)buf];
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
// a normalised integer texture coordinate, or a float one
float read_uv(const unsigned char *p, int comp_type) {
  if (comp_type == 5126) return read_f(p);
  if (comp_type == 5123) { uint16_t v; std::memcpy(&v, p, 2); return v / 65535.f; }
  return *p / 255.f;
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

// ---- images ---------------------------------------------------------------
int base64_val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
std::vector<uint8_t> base64_decode(const std::string &s) {
  std::vector<uint8_t> out;
  int val = 0, bits = -8;
  for (char c : s) {
    int d = base64_val(c);
    if (d < 0) continue;
    val = (val << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out.push_back((uint8_t)((val >> bits) & 0xff));
      bits -= 8;
    }
  }
  return out;
}

// glTF image index -> TriMesh image index, loading once
int image_of(const Doc &d, int gltf_image, TriMesh &out, std::vector<int> &cache) {
  const json &images = d.j.value("images", json::array());
  if (gltf_image < 0 || gltf_image >= (int)images.size()) return -1;
  if (cache.size() != images.size()) cache.assign(images.size(), -2);
  if (cache[(size_t)gltf_image] != -2) return cache[(size_t)gltf_image];
  const json &im = images[(size_t)gltf_image];
  MeshImage mi;
  mi.name = im.value("name", "image " + std::to_string(gltf_image));
  int result = -1;
  if (im.contains("bufferView")) {
    const json &views = d.j.value("bufferViews", json::array());
    int view = im.value("bufferView", -1);
    if (view >= 0 && view < (int)views.size()) {
      const json &v = views[(size_t)view];
      int buf = v.value("buffer", 0);
      size_t off = v.value("byteOffset", 0u), len = v.value("byteLength", 0u);
      if (buf >= 0 && buf < (int)d.buffers.size() && off + len <= d.buffers[(size_t)buf].size()) {
        const std::string &bin = d.buffers[(size_t)buf];
        mi.bytes.assign(bin.begin() + (long)off, bin.begin() + (long)(off + len));
      }
    }
  } else if (im.contains("uri")) {
    std::string uri = im["uri"].get<std::string>();
    if (uri.rfind("data:", 0) == 0) {
      size_t comma = uri.find(',');
      if (comma != std::string::npos) mi.bytes = base64_decode(uri.substr(comma + 1));
    } else {
      mi.path = d.dir.empty() ? uri : d.dir + "/" + uri;
    }
  }
  if (!mi.bytes.empty() || !mi.path.empty()) {
    out.images.push_back(std::move(mi));
    result = (int)out.images.size() - 1;
  }
  cache[(size_t)gltf_image] = result;
  return result;
}

void add_primitive(const Doc &d, const json &prim, const float *M, TriMesh &out, std::vector<int> &img_cache) {
  const json &attrs = prim.value("attributes", json::object());
  if (!attrs.contains("POSITION")) return;
  if (prim.value("mode", 4) != 4) return; // triangles only
  Accessor pos;
  if (!accessor(d, attrs["POSITION"].get<int>(), pos) || pos.comp_type != 5126 || pos.comps != 3) return;
  // texture coordinates line up with the positions, one per vertex
  Accessor tc;
  const bool have_uv = attrs.contains("TEXCOORD_0") && accessor(d, attrs["TEXCOORD_0"].get<int>(), tc) &&
                       tc.comps == 2 && tc.count == pos.count;
  // a mesh that had uvs so far must keep them per vertex; pad where a
  // primitive has none
  if (have_uv && out.uv.size() != out.vert_count() * 2) out.uv.resize(out.vert_count() * 2, 0.f);
  uint32_t base = (uint32_t)out.vert_count();
  for (size_t i = 0; i < pos.count; ++i) {
    const unsigned char *p = pos.data + i * pos.stride;
    float v[3] = {read_f(p), read_f(p + 4), read_f(p + 8)};
    float w[3];
    for (int r = 0; r < 3; ++r) w[r] = M[r] * v[0] + M[4 + r] * v[1] + M[8 + r] * v[2] + M[12 + r];
    out.v.insert(out.v.end(), w, w + 3);
    if (have_uv) {
      const unsigned char *q = tc.data + i * tc.stride;
      size_t cs = tc.comp_type == 5126 ? 4 : tc.comp_type == 5123 ? 2 : 1;
      out.uv.push_back(read_uv(q, tc.comp_type));
      out.uv.push_back(read_uv(q + cs, tc.comp_type));
    } else if (!out.uv.empty()) {
      out.uv.push_back(0.f);
      out.uv.push_back(0.f);
    }
  }
  MeshPart part;
  part.first_face = (uint32_t)out.face_count();
  if (prim.contains("indices")) {
    Accessor idx;
    if (!accessor(d, prim["indices"].get<int>(), idx)) return;
    for (size_t i = 0; i + 2 < idx.count; i += 3)
      for (int k = 0; k < 3; ++k) out.f.push_back(base + read_index(idx.data + (i + k) * idx.stride, idx.comp_type));
  } else {
    for (uint32_t i = 0; i + 2 < (uint32_t)pos.count; i += 3) {
      out.f.push_back(base + i);
      out.f.push_back(base + i + 1);
      out.f.push_back(base + i + 2);
    }
  }
  part.face_count = (uint32_t)out.face_count() - part.first_face;
  // the material: base colour, and its picture
  const json &mats = d.j.value("materials", json::array());
  int mat = prim.value("material", -1);
  if (mat >= 0 && mat < (int)mats.size()) {
    const json &m = mats[(size_t)mat];
    part.name = m.value("name", "");
    const json &pbr = m.value("pbrMetallicRoughness", json::object());
    if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].size() >= 3)
      for (int k = 0; k < 4 && k < (int)pbr["baseColorFactor"].size(); ++k)
        part.color[k] = pbr["baseColorFactor"][(size_t)k].get<float>();
    if (pbr.contains("baseColorTexture")) {
      int tex = pbr["baseColorTexture"].value("index", -1);
      const json &texs = d.j.value("textures", json::array());
      if (tex >= 0 && tex < (int)texs.size())
        part.image = image_of(d, texs[(size_t)tex].value("source", -1), out, img_cache);
    }
  }
  if (part.face_count) out.parts.push_back(part);
}

void walk(const Doc &d, int node_index, const float *parent, TriMesh &out, std::vector<int> &img_cache, int depth) {
  const json &nodes = d.j.value("nodes", json::array());
  if (depth > 64 || node_index < 0 || node_index >= (int)nodes.size()) return;
  const json &n = nodes[(size_t)node_index];
  float local[16], M[16];
  node_matrix(n, local);
  mat_mul(parent, local, M);
  if (n.contains("mesh")) {
    const json &meshes = d.j.value("meshes", json::array());
    int mi = n["mesh"].get<int>();
    if (mi >= 0 && mi < (int)meshes.size())
      for (const json &prim : meshes[(size_t)mi].value("primitives", json::array()))
        add_primitive(d, prim, M, out, img_cache);
  }
  for (const json &c : n.value("children", json::array())) walk(d, c.get<int>(), M, out, img_cache, depth + 1);
}

bool read_file(const std::string &path, std::string &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool load_doc(Doc &d, TriMesh &out, std::string &err) {
  static const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  std::vector<int> img_cache;
  const json &scenes = d.j.value("scenes", json::array());
  int scene = d.j.value("scene", 0);
  if (!scenes.empty() && scene < (int)scenes.size()) {
    for (const json &n : scenes[(size_t)scene].value("nodes", json::array()))
      walk(d, n.get<int>(), I, out, img_cache, 0);
  } else {
    // no scene: every mesh as it is
    for (const json &m : d.j.value("meshes", json::array()))
      for (const json &prim : m.value("primitives", json::array())) add_primitive(d, prim, I, out, img_cache);
  }
  if (out.f.empty()) { err = "glTF holds no triangle geometry"; return false; }
  // a mesh where only some primitives had uvs still has one uv per vertex
  if (!out.uv.empty() && out.uv.size() != out.vert_count() * 2) out.uv.resize(out.vert_count() * 2, 0.f);
  return true;
}

} // namespace

bool mesh_load_glb(const std::string &path, TriMesh &out, std::string &err) {
  std::string all;
  if (!read_file(path, all)) { err = "cannot open " + path; return false; }
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
  Doc d;
  d.j = json::parse(js, nullptr, false);
  if (d.j.is_discarded()) { err = "GLB has no readable JSON chunk"; return false; }
  d.buffers.push_back(std::move(bin));
  size_t slash = path.find_last_of("/\\");
  d.dir = slash == std::string::npos ? "" : path.substr(0, slash);
  return load_doc(d, out, err);
}

bool mesh_load_gltf(const std::string &path, TriMesh &out, std::string &err) {
  std::string text;
  if (!read_file(path, text)) { err = "cannot open " + path; return false; }
  Doc d;
  d.j = json::parse(text, nullptr, false);
  if (d.j.is_discarded() || !d.j.is_object()) { err = "not a glTF JSON file"; return false; }
  size_t slash = path.find_last_of("/\\");
  d.dir = slash == std::string::npos ? "" : path.substr(0, slash);
  for (const json &b : d.j.value("buffers", json::array())) {
    std::string bytes;
    std::string uri = b.value("uri", "");
    if (uri.rfind("data:", 0) == 0) {
      size_t comma = uri.find(',');
      std::vector<uint8_t> dec = comma == std::string::npos ? std::vector<uint8_t>() : base64_decode(uri.substr(comma + 1));
      bytes.assign(dec.begin(), dec.end());
    } else if (!uri.empty()) {
      if (!read_file(d.dir.empty() ? uri : d.dir + "/" + uri, bytes)) {
        err = "glTF buffer missing: " + uri;
        return false;
      }
    }
    d.buffers.push_back(std::move(bytes));
  }
  return load_doc(d, out, err);
}

} // namespace gpx
