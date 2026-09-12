// Geekatplay TerraForge - a grown plant written out as a mesh file.
//
// A species is rules, but the rest of the world wants triangles: this
// writes the mesh one seed grew as OBJ with its MTL, the format every DCC
// still reads first, and hands ".glb"/".gltf" to plant_export_gltf.cpp.
// Pictures a material made from rules live in memory (PlantMaterial::rgba);
// they are written as PNG beside the mesh, named with the tags a texture
// loader looks for - `_color`, `_alpha`, `_normal`, `_roughness` - so the
// files fall into the right channels when another tool opens the folder.
// Pictures that were files already are referenced by their path.
//
// OBJ's texture origin is the bottom-left corner while the plant's
// pictures (and glTF) count v from the top row, so v is flipped on the way
// out; glTF takes it as it is. Every part is a `g` with its own `usemtl`,
// so a part-per-node export survives the round trip.
//
// The helpers at the top (a PNG in memory, a safe file stem, the tagged
// picture names) are shared with the glTF writer, which declares them
// itself: the two exporters are the only files of this seam.
#include "gpx/plant.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "stb_image_write.h"

namespace gpx {

namespace plant_export_detail {

void png_sink(void *ctx, void *data, int size) {
  auto *v = (std::vector<uint8_t> *)ctx;
  v->insert(v->end(), (uint8_t *)data, (uint8_t *)data + size);
}
bool png_bytes(const std::vector<uint8_t> &rgba, int w, int h, std::vector<uint8_t> &out) {
  out.clear();
  if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return false;
  return stbi_write_png_to_func(png_sink, &out, w, h, 4, rgba.data(), w * 4) != 0;
}
// The alpha channel as a grey picture, for loaders that want opacity on
// its own (OBJ's map_d).
void alpha_as_grey(const std::vector<uint8_t> &rgba, std::vector<uint8_t> &grey) {
  grey.resize(rgba.size());
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
    grey[i] = grey[i + 1] = grey[i + 2] = rgba[i + 3];
    grey[i + 3] = 255;
  }
}
bool has_cutout(const PlantMaterial &m) {
  if (m.alpha_cutout || !m.alpha_map.empty()) return true;
  for (size_t i = 3; i < m.rgba.size(); i += 4)
    if (m.rgba[i] < 255) return true;
  return false;
}
std::string safe_name(const std::string &s) {
  std::string out;
  for (char c : s) out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out.empty() ? "material" : out;
}
// Materials named alike get numbered so their pictures do not overwrite.
std::vector<std::string> material_stems(const PlantMesh &m) {
  std::vector<std::string> names;
  for (size_t i = 0; i < m.materials.size(); ++i) {
    std::string n = safe_name(m.materials[i].name);
    if (std::find(names.begin(), names.end(), n) != names.end()) n += "_" + std::to_string(i);
    names.push_back(n);
  }
  return names;
}
std::string file_stem(const std::string &path) {
  std::filesystem::path p(path);
  return p.stem().string();
}
std::string dir_of(const std::string &path) {
  std::filesystem::path p(path);
  return p.has_parent_path() ? p.parent_path().string() : std::string();
}
std::string join(const std::string &dir, const std::string &name) {
  return dir.empty() ? name : dir + "/" + name;
}
bool write_bytes(const std::string &path, const std::vector<uint8_t> &bytes) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write((const char *)bytes.data(), (std::streamsize)bytes.size());
  return (bool)f;
}
bool read_bytes(const std::string &path, std::vector<uint8_t> &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

// The picture files one material contributes beside a mesh at `path`:
// written when they were made from rules, named when they were files.
// Names are relative to the mesh's folder.
struct MaterialFiles {
  std::string color, alpha, normal, roughness;
};
bool write_material_pictures(const PlantMesh &m, int index, const std::string &path, bool want_alpha_file,
                             MaterialFiles &out, std::string &err) {
  const PlantMaterial &mat = m.materials[(size_t)index];
  std::string stem = file_stem(path) + "_" + material_stems(m)[(size_t)index];
  std::string dir = dir_of(path);
  if (!mat.rgba.empty() && mat.w > 0 && mat.h > 0) {
    out.color = stem + "_color.png";
    if (!plant_texture_write_png(join(dir, out.color), mat.rgba, mat.w, mat.h)) {
      err = "cannot write " + out.color;
      return false;
    }
    if (want_alpha_file && has_cutout(mat)) {
      std::vector<uint8_t> grey;
      alpha_as_grey(mat.rgba, grey);
      out.alpha = stem + "_alpha.png";
      if (!plant_texture_write_png(join(dir, out.alpha), grey, mat.w, mat.h)) {
        err = "cannot write " + out.alpha;
        return false;
      }
    }
  } else {
    out.color = mat.color_map;
    out.alpha = mat.alpha_map;
  }
  if (!mat.normal_rgba.empty() && mat.w > 0 && mat.h > 0) {
    out.normal = stem + "_normal.png";
    if (!plant_texture_write_png(join(dir, out.normal), mat.normal_rgba, mat.w, mat.h)) {
      err = "cannot write " + out.normal;
      return false;
    }
  } else {
    out.normal = mat.normal_map;
  }
  out.roughness = mat.roughness_map;
  return true;
}

} // namespace plant_export_detail

using namespace plant_export_detail;

// plant_export_gltf.cpp
bool plant_export_gltf(const PlantMesh &m, const std::string &path, std::string &err);

namespace {

bool write_obj(const PlantMesh &m, const std::string &path, std::string &err) {
  const size_t nv = m.vertex_count();
  if (nv == 0 || m.idx.empty()) {
    err = "the plant has no geometry";
    return false;
  }
  const bool have_n = m.nrm.size() == nv * 3, have_uv = m.uv.size() == nv * 2;
  std::string dir = dir_of(path), stem = file_stem(path);
  if (!dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
  }
  // the MTL first, so a picture that fails to write stops before the mesh
  std::string mtl_name = stem + ".mtl";
  std::vector<std::string> mat_names = material_stems(m);
  {
    std::ofstream f(join(dir, mtl_name));
    if (!f) {
      err = "cannot write " + mtl_name;
      return false;
    }
    f << "# TerraForge plant materials\n";
    for (size_t i = 0; i < m.materials.size(); ++i) {
      const PlantMaterial &mat = m.materials[i];
      MaterialFiles files;
      if (!write_material_pictures(m, (int)i, path, true, files, err)) return false;
      char buf[256];
      f << "\nnewmtl " << mat_names[i] << "\n";
      std::snprintf(buf, sizeof buf, "Ka 0.1 0.1 0.1\nKd %.4f %.4f %.4f\nKs 0.02 0.02 0.02\n", mat.color[0], mat.color[1], mat.color[2]);
      f << buf;
      // Ns from roughness: shiny is a high exponent
      std::snprintf(buf, sizeof buf, "Ns %.1f\nd 1.0\nillum 2\nPr %.3f\nPm %.3f\n", (1.f - mat.roughness) * 200.f + 5.f, mat.roughness, mat.metallic);
      f << buf;
      if (mat.two_sided) f << "# two-sided\n";
      if (!files.color.empty()) f << "map_Kd " << files.color << "\n";
      if (!files.alpha.empty()) f << "map_d " << files.alpha << "\n";
      if (!files.normal.empty()) f << "norm " << files.normal << "\nmap_bump " << files.normal << "\n";
      if (!files.roughness.empty()) f << "map_Pr " << files.roughness << "\n";
    }
  }
  std::ofstream f(path);
  if (!f) {
    err = "cannot write " + path;
    return false;
  }
  f << "# TerraForge plant, metres, y up\nmtllib " << mtl_name << "\n";
  char buf[128];
  for (size_t i = 0; i < nv; ++i) {
    std::snprintf(buf, sizeof buf, "v %.5f %.5f %.5f\n", m.pos[i * 3], m.pos[i * 3 + 1], m.pos[i * 3 + 2]);
    f << buf;
  }
  if (have_uv)
    for (size_t i = 0; i < nv; ++i) {
      std::snprintf(buf, sizeof buf, "vt %.5f %.5f\n", m.uv[i * 2], 1.f - m.uv[i * 2 + 1]);
      f << buf;
    }
  if (have_n)
    for (size_t i = 0; i < nv; ++i) {
      std::snprintf(buf, sizeof buf, "vn %.4f %.4f %.4f\n", m.nrm[i * 3], m.nrm[i * 3 + 1], m.nrm[i * 3 + 2]);
      f << buf;
    }
  auto face = [&](uint32_t a, uint32_t b, uint32_t c) {
    uint32_t v[3] = {a + 1, b + 1, c + 1};
    f << 'f';
    for (uint32_t k : v) {
      if (have_uv && have_n) std::snprintf(buf, sizeof buf, " %u/%u/%u", k, k, k);
      else if (have_uv) std::snprintf(buf, sizeof buf, " %u/%u", k, k);
      else if (have_n) std::snprintf(buf, sizeof buf, " %u//%u", k, k);
      else std::snprintf(buf, sizeof buf, " %u", k);
      f << buf;
    }
    f << '\n';
  };
  // parts in order; triangles no part claims go into a last group
  std::vector<uint8_t> claimed(m.idx.size() / 3, 0);
  for (size_t p = 0; p < m.parts.size(); ++p) {
    const PlantPart &part = m.parts[p];
    if (part.index_count == 0 || (size_t)part.first_index + part.index_count > m.idx.size()) continue;
    std::string gname = safe_name(part.name.empty() ? "part_" + std::to_string(p) : part.name);
    f << "g " << gname << "_" << p << "\n";
    if (part.material >= 0 && part.material < (int)mat_names.size()) f << "usemtl " << mat_names[(size_t)part.material] << "\n";
    for (uint32_t i = part.first_index; i + 2 < part.first_index + part.index_count; i += 3) {
      face(m.idx[i], m.idx[i + 1], m.idx[i + 2]);
      claimed[i / 3] = 1;
    }
  }
  bool loose = false;
  for (size_t t = 0; t < claimed.size(); ++t) {
    if (claimed[t]) continue;
    if (!loose) { f << "g loose\n"; loose = true; }
    face(m.idx[t * 3], m.idx[t * 3 + 1], m.idx[t * 3 + 2]);
  }
  if (!f) {
    err = "writing " + path + " failed";
    return false;
  }
  return true;
}

} // namespace

bool plant_mesh_write(const PlantMesh &m, const std::string &path, std::string &err) {
  err.clear();
  try {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char &c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".obj") return write_obj(m, path, err);
    if (ext == ".glb" || ext == ".gltf") return plant_export_gltf(m, path, err);
    err = "unknown mesh format '" + ext + "' (use .obj, .glb or .gltf)";
    return false;
  } catch (const std::exception &e) {
    err = e.what();
    return false;
  } catch (...) {
    err = "export failed";
    return false;
  }
}

} // namespace gpx
