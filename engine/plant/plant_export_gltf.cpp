// Geekatplay TerraForge - a grown plant written as glTF 2.0.
//
// glTF is what game engines, web viewers and the 3D generation services
// speak, and a binary .glb carries the pictures inside it, so a plant made
// from rules travels as one file with nothing to lose on the way. This is
// a minimal writer, not a library: one buffer holding the positions, the
// normals, the texture coordinates, then one index run per part, then the
// PNG bytes of every rule-made picture; one mesh whose primitives are the
// parts; a material per PlantMaterial with the metallic-roughness model,
// its base-colour and normal pictures, alphaMode MASK at half opacity for
// cut-outs, doubleSided when the part is. Metres, y up, as the plant is.
// The reader in engine/mesh_io_gltf.cpp reads this back (the throwaway
// check in the module's history did exactly that) and so does Blender.
//
// ".gltf" writes the same document as JSON with the buffer in a sibling
// .bin and the pictures as PNG files beside it, tagged `_color` and
// `_normal` so a texture loader sorts them. The JSON is built by hand: the
// document is small and regular, and the plant engine keeps its one JSON
// dependency to the species file and the descriptions.
//
// Pictures that were files already are embedded as their raw bytes in a
// .glb when they are PNG or JPEG (glTF accepts nothing else) and named by
// uri in a .gltf. The picture helpers are shared with plant_export.cpp,
// which owns them; they are declared again here because the two exporters
// are the only files of this seam.
#include "gpx/plant.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gpx {

namespace plant_export_detail {
bool png_bytes(const std::vector<uint8_t> &rgba, int w, int h, std::vector<uint8_t> &out);
bool has_cutout(const PlantMaterial &m);
std::vector<std::string> material_stems(const PlantMesh &m);
std::string file_stem(const std::string &path);
std::string dir_of(const std::string &path);
std::string join(const std::string &dir, const std::string &name);
bool write_bytes(const std::string &path, const std::vector<uint8_t> &bytes);
bool read_bytes(const std::string &path, std::vector<uint8_t> &out);
struct MaterialFiles {
  std::string color, alpha, normal, roughness;
};
bool write_material_pictures(const PlantMesh &m, int index, const std::string &path, bool want_alpha_file,
                             MaterialFiles &out, std::string &err);
} // namespace plant_export_detail

using namespace plant_export_detail;

namespace {

std::string num(float f) {
  char b[32];
  std::snprintf(b, sizeof b, "%.7g", (double)f);
  std::string s = b;
  // JSON has no inf/nan; a broken vertex must not break the file
  if (s.find("inf") != std::string::npos || s.find("nan") != std::string::npos) return "0";
  return s;
}
std::string jstr(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((unsigned char)c < 0x20) out += ' ';
    else out += c;
  }
  return out + "\"";
}
void pad4(std::vector<uint8_t> &b, uint8_t fill = 0) {
  while (b.size() % 4) b.push_back(fill);
}
void append(std::vector<uint8_t> &b, const void *data, size_t n) {
  b.insert(b.end(), (const uint8_t *)data, (const uint8_t *)data + n);
}

struct View {
  size_t offset = 0, length = 0;
  int target = 0; // 34962 vertex data, 34963 indices, 0 an image
};

// The glTF picture of one material channel: a buffer view (embedded) or a
// uri (a file beside a .gltf); -1 when the material has none.
struct Picture {
  int view = -1;
  std::string uri, mime;
};

std::string view_json(const View &v) {
  std::string s = "{\"buffer\":0,\"byteOffset\":" + std::to_string(v.offset) + ",\"byteLength\":" + std::to_string(v.length);
  if (v.target) s += ",\"target\":" + std::to_string(v.target);
  return s + "}";
}

std::string mime_of(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  for (char &c : ext) c = (char)std::tolower((unsigned char)c);
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  return "";
}

// A picture from a file path: embedded raw when binary, named when text.
Picture picture_from_path(const std::string &file, bool binary, const std::string &out_dir,
                          std::vector<uint8_t> &bin, std::vector<View> &views) {
  Picture p;
  if (file.empty()) return p;
  p.mime = mime_of(file);
  if (p.mime.empty()) return p;
  if (binary) {
    std::vector<uint8_t> bytes;
    if (!read_bytes(file, bytes) || bytes.empty()) return p;
    pad4(bin);
    View v;
    v.offset = bin.size();
    v.length = bytes.size();
    append(bin, bytes.data(), bytes.size());
    views.push_back(v);
    p.view = (int)views.size() - 1;
  } else {
    // relative when the picture lies in the mesh's folder, else its path
    std::filesystem::path fp(file), rel;
    std::error_code ec;
    rel = std::filesystem::relative(fp, out_dir.empty() ? "." : out_dir, ec);
    std::string u = (ec || rel.empty() || rel.string().rfind("..", 0) == 0) ? fp.generic_string() : rel.generic_string();
    p.uri = u;
  }
  return p;
}

} // namespace

bool plant_export_gltf(const PlantMesh &m, const std::string &path, std::string &err) {
  const size_t nv = m.vertex_count();
  if (nv == 0 || m.idx.empty()) {
    err = "the plant has no geometry";
    return false;
  }
  const bool binary = [&] {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char &c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".glb";
  }();
  const bool have_n = m.nrm.size() == nv * 3, have_uv = m.uv.size() == nv * 2;
  const std::string dir = dir_of(path), stem = file_stem(path);
  if (!dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
  }

  // ---- the buffer: vertex arrays, index runs, pictures
  std::vector<uint8_t> bin;
  std::vector<View> views;
  std::vector<std::string> accessors;
  auto add_view = [&](const void *data, size_t bytes, int target) {
    pad4(bin);
    View v;
    v.offset = bin.size();
    v.length = bytes;
    v.target = target;
    append(bin, data, bytes);
    views.push_back(v);
    return (int)views.size() - 1;
  };
  float mn[3] = {m.pos[0], m.pos[1], m.pos[2]}, mx[3] = {m.pos[0], m.pos[1], m.pos[2]};
  for (size_t i = 0; i < nv; ++i)
    for (int k = 0; k < 3; ++k) {
      mn[k] = std::min(mn[k], m.pos[i * 3 + k]);
      mx[k] = std::max(mx[k], m.pos[i * 3 + k]);
    }
  int pos_view = add_view(m.pos.data(), nv * 12, 34962);
  accessors.push_back("{\"bufferView\":" + std::to_string(pos_view) + ",\"componentType\":5126,\"count\":" + std::to_string(nv) +
                      ",\"type\":\"VEC3\",\"min\":[" + num(mn[0]) + "," + num(mn[1]) + "," + num(mn[2]) + "],\"max\":[" +
                      num(mx[0]) + "," + num(mx[1]) + "," + num(mx[2]) + "]}");
  const int acc_pos = 0;
  int acc_nrm = -1, acc_uv = -1;
  if (have_n) {
    int v = add_view(m.nrm.data(), nv * 12, 34962);
    accessors.push_back("{\"bufferView\":" + std::to_string(v) + ",\"componentType\":5126,\"count\":" + std::to_string(nv) + ",\"type\":\"VEC3\"}");
    acc_nrm = (int)accessors.size() - 1;
  }
  if (have_uv) {
    int v = add_view(m.uv.data(), nv * 8, 34962);
    accessors.push_back("{\"bufferView\":" + std::to_string(v) + ",\"componentType\":5126,\"count\":" + std::to_string(nv) + ",\"type\":\"VEC2\"}");
    acc_uv = (int)accessors.size() - 1;
  }
  // one index run per part (the whole mesh as one primitive when no part
  // claims anything)
  struct Prim {
    int accessor, material;
    bool double_sided;
    std::string name;
  };
  std::vector<Prim> prims;
  std::vector<PlantPart> parts = m.parts;
  parts.erase(std::remove_if(parts.begin(), parts.end(),
                             [&](const PlantPart &p) {
                               return p.index_count == 0 || (size_t)p.first_index + p.index_count > m.idx.size();
                             }),
              parts.end());
  if (parts.empty()) {
    PlantPart all;
    all.name = "plant";
    all.index_count = (uint32_t)m.idx.size();
    parts.push_back(all);
  }
  for (const PlantPart &p : parts) {
    uint32_t count = p.index_count - p.index_count % 3;
    int v = add_view(m.idx.data() + p.first_index, count * 4, 34963);
    accessors.push_back("{\"bufferView\":" + std::to_string(v) + ",\"componentType\":5125,\"count\":" + std::to_string(count) + ",\"type\":\"SCALAR\"}");
    prims.push_back({(int)accessors.size() - 1, p.material, p.double_sided, p.name});
  }
  // which materials are double-sided: any part wearing it that is
  std::vector<uint8_t> mat_two_sided(m.materials.size(), 0);
  for (size_t i = 0; i < m.materials.size(); ++i) mat_two_sided[i] = m.materials[i].two_sided;
  for (const Prim &p : prims)
    if (p.double_sided && p.material >= 0 && p.material < (int)mat_two_sided.size()) mat_two_sided[(size_t)p.material] = 1;

  // ---- pictures and materials
  std::vector<std::string> images, textures, materials;
  auto add_image = [&](const Picture &p, const std::string &name) {
    if (p.view < 0 && p.uri.empty()) return -1;
    std::string s = "{\"name\":" + jstr(name) + ",\"mimeType\":" + jstr(p.mime);
    if (p.view >= 0) s += ",\"bufferView\":" + std::to_string(p.view);
    else s += ",\"uri\":" + jstr(p.uri);
    images.push_back(s + "}");
    textures.push_back("{\"sampler\":0,\"source\":" + std::to_string(images.size() - 1) + "}");
    return (int)textures.size() - 1;
  };
  std::vector<std::string> stems = material_stems(m);
  for (size_t i = 0; i < m.materials.size(); ++i) {
    const PlantMaterial &mat = m.materials[i];
    Picture color, normal;
    if (binary) {
      if (!mat.rgba.empty() && mat.w > 0 && mat.h > 0) {
        std::vector<uint8_t> png;
        if (png_bytes(mat.rgba, mat.w, mat.h, png)) {
          color.view = add_view(png.data(), png.size(), 0);
          color.mime = "image/png";
        }
      } else {
        color = picture_from_path(mat.color_map, true, dir, bin, views);
      }
      if (!mat.normal_rgba.empty() && mat.w > 0 && mat.h > 0) {
        std::vector<uint8_t> png;
        if (png_bytes(mat.normal_rgba, mat.w, mat.h, png)) {
          normal.view = add_view(png.data(), png.size(), 0);
          normal.mime = "image/png";
        }
      } else {
        normal = picture_from_path(mat.normal_map, true, dir, bin, views);
      }
    } else {
      MaterialFiles files;
      if (!write_material_pictures(m, (int)i, path, false, files, err)) return false;
      color = picture_from_path(files.color.empty() ? "" : (mat.rgba.empty() ? files.color : join(dir, files.color)), false, dir, bin, views);
      normal = picture_from_path(files.normal.empty() ? "" : (mat.normal_rgba.empty() ? files.normal : join(dir, files.normal)), false, dir, bin, views);
    }
    int tex_color = add_image(color, stems[i] + "_color");
    int tex_normal = add_image(normal, stems[i] + "_normal");
    std::string s = "{\"name\":" + jstr(stems[i]) + ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" + num(mat.color[0]) + "," +
                    num(mat.color[1]) + "," + num(mat.color[2]) + ",1],\"metallicFactor\":" + num(std::clamp(mat.metallic, 0.f, 1.f)) +
                    ",\"roughnessFactor\":" + num(std::clamp(mat.roughness, 0.f, 1.f));
    if (tex_color >= 0) s += ",\"baseColorTexture\":{\"index\":" + std::to_string(tex_color) + "}";
    s += "}";
    if (tex_normal >= 0) s += ",\"normalTexture\":{\"index\":" + std::to_string(tex_normal) + "}";
    s += has_cutout(mat) ? ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5" : ",\"alphaMode\":\"OPAQUE\"";
    s += std::string(",\"doubleSided\":") + (mat_two_sided[i] ? "true" : "false") + "}";
    materials.push_back(s);
  }
  if (materials.empty()) materials.push_back("{\"name\":\"plant\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.5,0.5,1],\"metallicFactor\":0,\"roughnessFactor\":0.8},\"doubleSided\":true}");

  // ---- the document
  auto list = [](const std::vector<std::string> &v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s + "]";
  };
  std::vector<std::string> prim_json;
  for (const Prim &p : prims) {
    std::string s = "{\"attributes\":{\"POSITION\":" + std::to_string(acc_pos);
    if (acc_nrm >= 0) s += ",\"NORMAL\":" + std::to_string(acc_nrm);
    if (acc_uv >= 0) s += ",\"TEXCOORD_0\":" + std::to_string(acc_uv);
    s += "},\"indices\":" + std::to_string(p.accessor) + ",\"mode\":4";
    int mat = p.material >= 0 && p.material < (int)materials.size() ? p.material : 0;
    s += ",\"material\":" + std::to_string(mat) + "}";
    prim_json.push_back(s);
  }
  std::vector<std::string> view_json_list;
  for (const View &v : views) view_json_list.push_back(view_json(v));
  std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Geekatplay TerraForge\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                     "\"nodes\":[{\"mesh\":0,\"name\":" + jstr(stem) + "}],"
                     "\"meshes\":[{\"name\":" + jstr(stem) + ",\"primitives\":" + list(prim_json) + "}],"
                     "\"materials\":" + list(materials) + ",";
  if (!images.empty())
    json += "\"images\":" + list(images) + ",\"textures\":" + list(textures) +
            ",\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],";
  json += "\"accessors\":" + list(accessors) + ",\"bufferViews\":" + list(view_json_list) + ",\"buffers\":[{\"byteLength\":" +
          std::to_string(bin.size());
  if (!binary) json += ",\"uri\":" + jstr(stem + ".bin");
  json += "}]}";

  if (binary) {
    // header, JSON chunk (padded with spaces), BIN chunk (padded with zeros)
    std::vector<uint8_t> js(json.begin(), json.end());
    pad4(js, ' ');
    pad4(bin);
    std::vector<uint8_t> out;
    uint32_t total = (uint32_t)(12 + 8 + js.size() + 8 + bin.size());
    uint32_t hdr[3] = {0x46546C67u, 2u, total};
    append(out, hdr, 12);
    uint32_t jc[2] = {(uint32_t)js.size(), 0x4E4F534Au};
    append(out, jc, 8);
    append(out, js.data(), js.size());
    uint32_t bc[2] = {(uint32_t)bin.size(), 0x004E4942u};
    append(out, bc, 8);
    append(out, bin.data(), bin.size());
    if (!write_bytes(path, out)) {
      err = "cannot write " + path;
      return false;
    }
    return true;
  }
  if (!write_bytes(join(dir, stem + ".bin"), bin)) {
    err = "cannot write " + stem + ".bin";
    return false;
  }
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot write " + path;
    return false;
  }
  f << json;
  return (bool)f;
}

} // namespace gpx
