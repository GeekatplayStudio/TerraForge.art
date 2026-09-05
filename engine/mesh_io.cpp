// Geekatplay TerraForge - mesh file formats. See gpx/mesh_io.hpp.
#include "gpx/mesh_io.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gpx {

namespace {

std::string lower_ext(const std::string &path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string e = path.substr(dot + 1);
  for (char &c : e) c = (char)tolower((unsigned char)c);
  return e;
}

// A polygon of n vertices becomes n-2 triangles, fanned from the first.
void add_polygon(TriMesh &m, const std::vector<uint32_t> &poly) {
  for (size_t i = 1; i + 1 < poly.size(); ++i) {
    m.f.push_back(poly[0]);
    m.f.push_back(poly[i]);
    m.f.push_back(poly[i + 1]);
  }
}

bool load_obj(const std::string &path, TriMesh &m, std::string &err) {
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (line.size() < 2) continue;
    if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
      std::istringstream ss(line.substr(1));
      float x = 0, y = 0, z = 0;
      ss >> x >> y >> z;
      m.v.push_back(x);
      m.v.push_back(y);
      m.v.push_back(z);
    } else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
      std::istringstream ss(line.substr(1));
      std::string tok;
      std::vector<uint32_t> poly;
      while (ss >> tok) {
        // "v", "v/vt", "v//vn" and "v/vt/vn" all start with the position.
        long idx = strtol(tok.c_str(), nullptr, 10);
        if (idx == 0) continue;
        // OBJ indices are 1-based, and negative means "counting back from
        // the end of the vertices read so far".
        size_t have = m.vert_count();
        long resolved = idx > 0 ? idx - 1 : (long)have + idx;
        if (resolved < 0 || (size_t)resolved >= have) continue;
        poly.push_back((uint32_t)resolved);
      }
      if (poly.size() >= 3) add_polygon(m, poly);
    }
  }
  if (m.v.empty()) {
    err = "no vertices in " + path;
    return false;
  }
  return true;
}

bool stl_is_ascii(std::ifstream &f) {
  char head[6] = {0};
  f.read(head, 5);
  f.clear();
  f.seekg(0);
  return std::strncmp(head, "solid", 5) == 0;
}

bool load_stl(const std::string &path, TriMesh &m, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  bool ascii = stl_is_ascii(f);
  if (ascii) {
    // Some binary files start with the word "solid" too, so an ascii parse
    // that finds no facet falls back to binary rather than failing.
    std::string word;
    size_t before = m.v.size();
    while (f >> word) {
      if (word != "vertex") continue;
      float x, y, z;
      f >> x >> y >> z;
      m.v.push_back(x);
      m.v.push_back(y);
      m.v.push_back(z);
    }
    if (m.v.size() > before) {
      for (uint32_t i = 0; i + 2 < m.vert_count(); i += 3) {
        m.f.push_back(i);
        m.f.push_back(i + 1);
        m.f.push_back(i + 2);
      }
      return true;
    }
    m.v.resize(before);
    f.clear();
  }
  f.clear();
  f.seekg(0, std::ios::end);
  std::streamoff size = f.tellg();
  f.seekg(80);
  uint32_t count = 0;
  f.read((char *)&count, 4);
  if (!f || (std::streamoff)count * 50 + 84 > size + 4) {
    err = "not a readable STL: " + path;
    return false;
  }
  m.v.reserve((size_t)count * 9);
  m.f.reserve((size_t)count * 3);
  for (uint32_t i = 0; i < count; ++i) {
    float tri[12];
    f.read((char *)tri, sizeof tri);
    if (!f) break;
    uint16_t attr = 0;
    f.read((char *)&attr, 2);
    uint32_t base = (uint32_t)m.vert_count();
    for (int k = 3; k < 12; ++k) m.v.push_back(tri[k]);
    m.f.push_back(base);
    m.f.push_back(base + 1);
    m.f.push_back(base + 2);
  }
  if (m.f.empty()) {
    err = "no triangles in " + path;
    return false;
  }
  return true;
}

bool load_ply(const std::string &path, TriMesh &m, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::string line;
  std::getline(f, line);
  if (line.rfind("ply", 0) != 0) {
    err = "not a PLY file: " + path;
    return false;
  }
  bool binary_le = false, binary_be = false;
  size_t n_vert = 0, n_face = 0;
  // Vertex properties are counted so a file carrying normals or colours is
  // read at the right stride instead of coming out as noise.
  int vprops = 0;
  bool in_vertex = false;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
      line.pop_back();
    if (line.rfind("format ", 0) == 0) {
      binary_le = line.find("binary_little_endian") != std::string::npos;
      binary_be = line.find("binary_big_endian") != std::string::npos;
    } else if (line.rfind("element vertex", 0) == 0) {
      n_vert = strtoul(line.c_str() + 14, nullptr, 10);
      in_vertex = true;
      vprops = 0;
    } else if (line.rfind("element face", 0) == 0) {
      n_face = strtoul(line.c_str() + 12, nullptr, 10);
      in_vertex = false;
    } else if (line.rfind("property ", 0) == 0) {
      if (in_vertex && line.rfind("property list", 0) != 0) ++vprops;
    } else if (line == "end_header") {
      break;
    }
  }
  if (binary_be) {
    err = "big-endian PLY is not supported: " + path;
    return false;
  }
  if (!binary_le) {
    for (size_t i = 0; i < n_vert; ++i) {
      std::getline(f, line);
      std::istringstream ss(line);
      float x = 0, y = 0, z = 0;
      ss >> x >> y >> z;
      m.v.push_back(x);
      m.v.push_back(y);
      m.v.push_back(z);
    }
    for (size_t i = 0; i < n_face; ++i) {
      std::getline(f, line);
      std::istringstream ss(line);
      int n = 0;
      ss >> n;
      std::vector<uint32_t> poly;
      for (int k = 0; k < n; ++k) {
        uint32_t idx = 0;
        ss >> idx;
        if (idx < m.vert_count()) poly.push_back(idx);
      }
      if (poly.size() >= 3) add_polygon(m, poly);
    }
  } else {
    // Binary little-endian, floats throughout - the shape every scanner and
    // every mesh tool writes.
    std::vector<float> row((size_t)std::max(vprops, 3));
    for (size_t i = 0; i < n_vert; ++i) {
      f.read((char *)row.data(), (std::streamsize)row.size() * 4);
      if (!f) break;
      m.v.push_back(row[0]);
      m.v.push_back(row[1]);
      m.v.push_back(row[2]);
    }
    for (size_t i = 0; i < n_face; ++i) {
      uint8_t n = 0;
      f.read((char *)&n, 1);
      if (!f) break;
      std::vector<uint32_t> poly((size_t)n);
      f.read((char *)poly.data(), (std::streamsize)n * 4);
      std::vector<uint32_t> valid;
      for (uint32_t idx : poly)
        if (idx < m.vert_count()) valid.push_back(idx);
      if (valid.size() >= 3) add_polygon(m, valid);
    }
  }
  if (m.v.empty()) {
    err = "no vertices in " + path;
    return false;
  }
  return true;
}

bool load_off(const std::string &path, TriMesh &m, std::string &err) {
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::string tag;
  f >> tag;
  if (tag != "OFF") {
    err = "not an OFF file: " + path;
    return false;
  }
  size_t nv = 0, nf = 0, ne = 0;
  f >> nv >> nf >> ne;
  for (size_t i = 0; i < nv; ++i) {
    float x = 0, y = 0, z = 0;
    f >> x >> y >> z;
    m.v.push_back(x);
    m.v.push_back(y);
    m.v.push_back(z);
  }
  for (size_t i = 0; i < nf; ++i) {
    int n = 0;
    f >> n;
    std::vector<uint32_t> poly;
    for (int k = 0; k < n; ++k) {
      uint32_t idx = 0;
      f >> idx;
      if (idx < m.vert_count()) poly.push_back(idx);
    }
    if (poly.size() >= 3) add_polygon(m, poly);
  }
  return !m.v.empty();
}

bool save_stl(const std::string &path, const TriMesh &m, bool ascii,
              std::string &err) {
  if (ascii) {
    std::ofstream f(path);
    if (!f) {
      err = "cannot write " + path;
      return false;
    }
    f << "solid geekatplay\n";
    for (size_t i = 0; i < m.face_count(); ++i) {
      const uint32_t *fc = m.face(i);
      const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
      double u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
      double v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
      double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                     u[0] * v[1] - u[1] * v[0]};
      double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (len > 0)
        for (int k = 0; k < 3; ++k) n[k] /= len;
      f << "  facet normal " << n[0] << ' ' << n[1] << ' ' << n[2] << "\n";
      f << "    outer loop\n";
      for (const float *p : {p0, p1, p2})
        f << "      vertex " << p[0] << ' ' << p[1] << ' ' << p[2] << "\n";
      f << "    endloop\n  endfacet\n";
    }
    f << "endsolid geekatplay\n";
    return (bool)f;
  }
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot write " + path;
    return false;
  }
  char header[80] = {0};
  std::snprintf(header, sizeof header, "Geekatplay TerraForge binary STL");
  f.write(header, 80);
  uint32_t count = (uint32_t)m.face_count();
  f.write((const char *)&count, 4);
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
    float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0]};
    float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0)
      for (int k = 0; k < 3; ++k) n[k] /= len;
    f.write((const char *)n, 12);
    f.write((const char *)p0, 12);
    f.write((const char *)p1, 12);
    f.write((const char *)p2, 12);
    uint16_t attr = 0;
    f.write((const char *)&attr, 2);
  }
  return (bool)f;
}

bool save_obj(const std::string &path, const TriMesh &m, std::string &err) {
  std::ofstream f(path);
  if (!f) {
    err = "cannot write " + path;
    return false;
  }
  f << "# Geekatplay TerraForge\n";
  for (size_t i = 0; i < m.vert_count(); ++i)
    f << "v " << m.v[i * 3] << ' ' << m.v[i * 3 + 1] << ' ' << m.v[i * 3 + 2]
      << '\n';
  for (size_t i = 0; i < m.face_count(); ++i)
    f << "f " << m.f[i * 3] + 1 << ' ' << m.f[i * 3 + 1] + 1 << ' '
      << m.f[i * 3 + 2] + 1 << '\n';
  return (bool)f;
}

bool save_ply(const std::string &path, const TriMesh &m, std::string &err) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot write " + path;
    return false;
  }
  f << "ply\nformat binary_little_endian 1.0\n";
  f << "comment Geekatplay TerraForge\n";
  f << "element vertex " << m.vert_count() << "\n";
  f << "property float x\nproperty float y\nproperty float z\n";
  f << "element face " << m.face_count() << "\n";
  f << "property list uchar int vertex_indices\nend_header\n";
  f.write((const char *)m.v.data(), (std::streamsize)m.v.size() * 4);
  for (size_t i = 0; i < m.face_count(); ++i) {
    uint8_t n = 3;
    f.write((const char *)&n, 1);
    f.write((const char *)&m.f[i * 3], 12);
  }
  return (bool)f;
}

bool save_off(const std::string &path, const TriMesh &m, std::string &err) {
  std::ofstream f(path);
  if (!f) {
    err = "cannot write " + path;
    return false;
  }
  f << "OFF\n" << m.vert_count() << ' ' << m.face_count() << " 0\n";
  for (size_t i = 0; i < m.vert_count(); ++i)
    f << m.v[i * 3] << ' ' << m.v[i * 3 + 1] << ' ' << m.v[i * 3 + 2] << '\n';
  for (size_t i = 0; i < m.face_count(); ++i)
    f << "3 " << m.f[i * 3] << ' ' << m.f[i * 3 + 1] << ' ' << m.f[i * 3 + 2]
      << '\n';
  return (bool)f;
}

} // namespace

bool mesh_load(const std::string &path, TriMesh &out, std::string &err) {
  out.clear();
  const std::string ext = lower_ext(path);
  bool ok;
  if (ext == "obj")
    ok = load_obj(path, out, err);
  else if (ext == "stl")
    ok = load_stl(path, out, err);
  else if (ext == "ply")
    ok = load_ply(path, out, err);
  else if (ext == "off")
    ok = load_off(path, out, err);
  else if (ext == "glb")
    ok = mesh_load_glb(path, out, err); // mesh_io_gltf.cpp
  else {
    err = "unsupported mesh format '." + ext + "' (OBJ, STL, PLY, OFF and GLB are read)";
    return false;
  }
  if (ok && out.f.empty()) {
    err = "no faces in " + path;
    return false;
  }
  return ok;
}

bool mesh_save(const std::string &path, const TriMesh &m, std::string &err,
               bool ascii_stl) {
  if (m.empty()) {
    err = "nothing to write: the mesh has no triangles";
    return false;
  }
  const std::string ext = lower_ext(path);
  if (ext == "stl") return save_stl(path, m, ascii_stl, err);
  if (ext == "obj") return save_obj(path, m, err);
  if (ext == "ply") return save_ply(path, m, err);
  if (ext == "off") return save_off(path, m, err);
  err = "unsupported mesh format '." + ext + "' (STL, OBJ, PLY and OFF are written)";
  return false;
}

const std::vector<std::string> &mesh_load_formats() {
  static const std::vector<std::string> f = {"obj", "stl", "ply", "off"};
  return f;
}

const std::vector<std::string> &mesh_save_formats() {
  static const std::vector<std::string> f = {"stl", "obj", "ply", "off"};
  return f;
}

} // namespace gpx
