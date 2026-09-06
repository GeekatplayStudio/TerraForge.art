// Geekatplay TerraForge - reading binary FBX.
//
// The format is a tree of records - name, typed properties, children - and
// nothing more; the meaning is in the names. This reads the tree, then
// takes from it what a static model is: every Geometry's vertices and
// polygons, its texture coordinates (one uv per polygon corner, so a
// vertex is split where the uvs differ), the material of each polygon, the
// diffuse picture of each material (a file beside the model, or embedded
// in a Video record), and the transform of the Model each geometry hangs
// from, up its parent chain. Deflated arrays are inflated with miniz, which
// the engine already carries for the material library. ASCII FBX is not
// read: every exporter writes binary, and the ASCII form is a debugging
// aid.
//
// Written from the layout of the format itself, which is public knowledge;
// no FBX SDK, no licence.
#include "gpx/mesh_io.hpp"
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <vector>
#include "miniz/miniz.h"

namespace gpx {

namespace {

struct Prop {
  char type = 0;              // Y C I F D L S R, or lower case for arrays
  int64_t i = 0;
  double d = 0;
  std::string s;              // S and R
  std::vector<double> nums;   // f d i l b arrays, as doubles
};

struct Rec {
  std::string name;
  std::vector<Prop> props;
  std::vector<std::unique_ptr<Rec>> kids;
  const Rec *child(const char *n) const {
    for (const auto &k : kids)
      if (k->name == n) return k.get();
    return nullptr;
  }
};

struct Reader {
  const unsigned char *p;
  size_t n, at = 0;
  bool big; // version >= 7500: 64-bit offsets
  bool ok = true;
  template <class T> T rd() {
    T v{};
    if (at + sizeof(T) > n) { ok = false; at = n; return v; }
    std::memcpy(&v, p + at, sizeof(T));
    at += sizeof(T);
    return v;
  }
  uint64_t rd_off() { return big ? rd<uint64_t>() : rd<uint32_t>(); }
  std::string rd_str(size_t len) {
    if (at + len > n) { ok = false; at = n; return ""; }
    std::string s((const char *)p + at, len);
    at += len;
    return s;
  }
};

bool read_array(Reader &r, Prop &pr, size_t elem) {
  uint32_t len = r.rd<uint32_t>(), enc = r.rd<uint32_t>(), clen = r.rd<uint32_t>();
  if (!r.ok) return false;
  std::vector<unsigned char> raw;
  if (enc == 0) {
    if (r.at + (size_t)len * elem > r.n) return false;
    raw.assign(r.p + r.at, r.p + r.at + (size_t)len * elem);
    r.at += (size_t)len * elem;
  } else {
    if (r.at + clen > r.n) return false;
    raw.resize((size_t)len * elem);
    mz_ulong out_len = (mz_ulong)raw.size();
    if (mz_uncompress(raw.data(), &out_len, r.p + r.at, clen) != MZ_OK) return false;
    r.at += clen;
  }
  pr.nums.resize(len);
  for (uint32_t k = 0; k < len; ++k) {
    const unsigned char *q = raw.data() + (size_t)k * elem;
    switch (pr.type) {
      case 'f': { float v; std::memcpy(&v, q, 4); pr.nums[k] = v; break; }
      case 'd': { double v; std::memcpy(&v, q, 8); pr.nums[k] = v; break; }
      case 'i': { int32_t v; std::memcpy(&v, q, 4); pr.nums[k] = v; break; }
      case 'l': { int64_t v; std::memcpy(&v, q, 8); pr.nums[k] = (double)v; break; }
      default: pr.nums[k] = q[0]; break;
    }
  }
  return true;
}

bool read_prop(Reader &r, Prop &pr) {
  pr.type = (char)r.rd<uint8_t>();
  switch (pr.type) {
    case 'Y': pr.i = r.rd<int16_t>(); pr.d = (double)pr.i; return r.ok;
    case 'C': pr.i = r.rd<uint8_t>(); pr.d = (double)pr.i; return r.ok;
    case 'I': pr.i = r.rd<int32_t>(); pr.d = (double)pr.i; return r.ok;
    case 'F': pr.d = r.rd<float>(); pr.i = (int64_t)pr.d; return r.ok;
    case 'D': pr.d = r.rd<double>(); pr.i = (int64_t)pr.d; return r.ok;
    case 'L': pr.i = r.rd<int64_t>(); pr.d = (double)pr.i; return r.ok;
    case 'S': case 'R': { uint32_t len = r.rd<uint32_t>(); pr.s = r.rd_str(len); return r.ok; }
    case 'f': case 'i': return read_array(r, pr, 4);
    case 'd': case 'l': return read_array(r, pr, 8);
    case 'b': return read_array(r, pr, 1);
    default: return false;
  }
}

// one record and its children; false at the null record that ends a list
bool read_rec(Reader &r, Rec &out, int depth) {
  const size_t start = r.at;
  uint64_t end = r.rd_off();
  uint64_t nprops = r.rd_off();
  uint64_t plen = r.rd_off();
  uint8_t nlen = r.rd<uint8_t>();
  if (!r.ok) return false;
  if (end == 0) return false; // the null record
  (void)plen;
  out.name = r.rd_str(nlen);
  for (uint64_t k = 0; k < nprops && r.ok; ++k) {
    Prop pr;
    if (!read_prop(r, pr)) { r.ok = false; break; }
    out.props.push_back(std::move(pr));
  }
  if (!r.ok || end > r.n || end < start) return false;
  if (depth < 64)
    while (r.at < end) {
      auto k = std::make_unique<Rec>();
      if (!read_rec(r, *k, depth + 1)) break;
      out.kids.push_back(std::move(k));
    }
  r.at = (size_t)end;
  return true;
}

// ---- the scene graph in the records -----------------------------------
struct Conn { int64_t child, parent; std::string prop; };

const Rec *prop70(const Rec &obj, const char *name) {
  const Rec *p70 = obj.child("Properties70");
  if (!p70) return nullptr;
  for (const auto &p : p70->kids)
    if (p->name == "P" && !p->props.empty() && p->props[0].s == name) return p.get();
  return nullptr;
}
void prop70_vec3(const Rec &obj, const char *name, double *v, double def) {
  v[0] = v[1] = v[2] = def;
  const Rec *p = prop70(obj, name);
  if (p && p->props.size() >= 7)
    for (int k = 0; k < 3; ++k) v[k] = p->props[(size_t)4 + k].d;
}

void mat_mul(const double *a, const double *b, double *o) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      double s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
}
void identity(double *m) { for (int i = 0; i < 16; ++i) m[i] = i % 5 == 0 ? 1.0 : 0.0; }

// R(z) * R(y) * R(x) for degrees, column-major 3x3
void rot_xyz(const double *r, double *R) {
  const double D = 3.14159265358979323846 / 180.0;
  double cx = std::cos(r[0] * D), sx = std::sin(r[0] * D);
  double cy = std::cos(r[1] * D), sy = std::sin(r[1] * D);
  double cz = std::cos(r[2] * D), sz = std::sin(r[2] * D);
  const double M[9] = {cy * cz, cy * sz, -sy,
                       sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy,
                       cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy};
  for (int i = 0; i < 9; ++i) R[i] = M[i];
}
void trs(const double *t, const double *r, const double *s, double *m) {
  double R[9];
  rot_xyz(r, R);
  identity(m);
  for (int c = 0; c < 3; ++c)
    for (int rr = 0; rr < 3; ++rr) m[c * 4 + rr] = R[c * 3 + rr] * s[c];
  m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
}

// T * Rpre * R * S * (Tg * Rg * Sg): FBX's default XYZ order, degrees.
// PreRotation is where an exporter bakes a Z-up model's turn to Y-up - a
// model read without it lies on its side; the Geometric* terms move the
// mesh alone, not the node's children.
void local_matrix(const Rec &model, double *m) {
  double t[3], r[3], s[3], pre[3], gt[3], gr[3], gs[3];
  prop70_vec3(model, "Lcl Translation", t, 0.0);
  prop70_vec3(model, "Lcl Rotation", r, 0.0);
  prop70_vec3(model, "Lcl Scaling", s, 1.0);
  prop70_vec3(model, "PreRotation", pre, 0.0);
  prop70_vec3(model, "GeometricTranslation", gt, 0.0);
  prop70_vec3(model, "GeometricRotation", gr, 0.0);
  prop70_vec3(model, "GeometricScaling", gs, 1.0);
  const double zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
  double T[16], P[16], RS[16], G[16], tmp[16];
  trs(t, zero, one, T);
  trs(zero, pre, one, P);
  trs(zero, r, s, RS);
  trs(gt, gr, gs, G);
  mat_mul(T, P, tmp);
  mat_mul(tmp, RS, m);
  mat_mul(m, G, tmp);
  for (int i = 0; i < 16; ++i) m[i] = tmp[i];
}

std::string basename_of(const std::string &p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? p : p.substr(s + 1);
}

} // namespace

bool mesh_load_fbx(const std::string &path, TriMesh &out, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { err = "cannot open " + path; return false; }
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string all = ss.str();
  if (all.size() < 27 || std::memcmp(all.data(), "Kaydara FBX Binary", 18) != 0) {
    err = all.rfind("; FBX", 0) == 0 || all.find("FBXHeaderExtension") != std::string::npos
              ? "ASCII FBX is not read; export the model as binary FBX (or glTF)"
              : "not a binary FBX file";
    return false;
  }
  uint32_t version;
  std::memcpy(&version, all.data() + 23, 4);
  Reader r{(const unsigned char *)all.data(), all.size(), 27, version >= 7500};
  Rec root;
  while (r.at < r.n) {
    auto k = std::make_unique<Rec>();
    if (!read_rec(r, *k, 0)) break;
    root.kids.push_back(std::move(k));
  }
  // the file's up axis: 1 = Y (ours), 2 = Z (Max, Blender exports) - a Z-up
  // model is turned to stand up, (x, y, z) -> (x, z, -y), sign respected
  bool z_up = false;
  double up_sign = 1.0;
  if (const Rec *gs = root.child("GlobalSettings")) {
    if (const Rec *ua = prop70(*gs, "UpAxis"); ua && ua->props.size() >= 5)
      z_up = (ua->props[4].d != 0 ? ua->props[4].d : (double)ua->props[4].i) == 2.0;
    if (const Rec *us = prop70(*gs, "UpAxisSign"); us && us->props.size() >= 5)
      up_sign = (us->props[4].d != 0 ? us->props[4].d : (double)us->props[4].i) < 0 ? -1.0 : 1.0;
  }
  const Rec *objects = root.child("Objects");
  if (!objects) { err = "FBX has no Objects section"; return false; }

  // index everything by id, and the connections both ways
  std::map<int64_t, const Rec *> by_id;
  for (const auto &o : objects->kids)
    if (!o->props.empty() && (o->props[0].type == 'L' || o->props[0].type == 'I')) by_id[o->props[0].i] = o.get();
  std::vector<Conn> conns;
  if (const Rec *cs = root.child("Connections"))
    for (const auto &c : cs->kids)
      if (c->name == "C" && c->props.size() >= 3)
        conns.push_back({c->props[1].i, c->props[2].i, c->props.size() >= 4 ? c->props[3].s : ""});
  auto parents_of = [&](int64_t id, const char *want_name) {
    std::vector<int64_t> v;
    for (const Conn &c : conns)
      if (c.child == id) {
        auto it = by_id.find(c.parent);
        if (it != by_id.end() && (!want_name || it->second->name == want_name)) v.push_back(c.parent);
      }
    return v;
  };
  auto children_of = [&](int64_t id, const char *want_name) {
    std::vector<int64_t> v;
    for (const Conn &c : conns)
      if (c.parent == id) {
        auto it = by_id.find(c.child);
        if (it != by_id.end() && (!want_name || it->second->name == want_name)) v.push_back(c.child);
      }
    return v;
  };

  // the world matrix of a Model, up its parent chain
  std::map<int64_t, std::vector<double>> world_cache;
  std::function<const double *(int64_t, int)> world_of = [&](int64_t id, int depth) -> const double * {
    auto it = world_cache.find(id);
    if (it != world_cache.end()) return it->second.data();
    std::vector<double> m(16);
    identity(m.data());
    auto obj = by_id.find(id);
    if (obj != by_id.end() && obj->second->name == "Model") {
      double local[16];
      local_matrix(*obj->second, local);
      std::vector<int64_t> ps = depth < 64 ? parents_of(id, "Model") : std::vector<int64_t>();
      if (!ps.empty()) mat_mul(world_of(ps[0], depth + 1), local, m.data());
      else std::memcpy(m.data(), local, sizeof local);
    }
    return world_cache.emplace(id, std::move(m)).first->second.data();
  };

  // the picture of a material: Texture -> its file, or the Video's bytes
  const std::string dir = [&] {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string() : path.substr(0, s);
  }();
  // material id -> TriMesh image, for the colour picture and, separately,
  // for the opacity picture a leaf card's material carries
  std::map<std::pair<int64_t, bool>, int> image_of_material;
  auto material_image = [&](int64_t mat_id, bool alpha) -> int {
    auto it = image_of_material.find({mat_id, alpha});
    if (it != image_of_material.end()) return it->second;
    int result = -1;
    std::vector<int64_t> texs;
    if (alpha) {
      // only a texture wired to the material's transparency counts
      for (const Conn &c : conns)
        if (c.parent == mat_id && by_id.count(c.child) && by_id[c.child]->name == "Texture" &&
            (c.prop == "TransparentColor" || c.prop == "TransparencyFactor" || c.prop == "Opacity"))
          texs.push_back(c.child);
    } else {
      // prefer the texture wired to DiffuseColor; else any texture on it
      // that is not an opacity, normal or specular map
      for (const Conn &c : conns)
        if (c.parent == mat_id && by_id.count(c.child) && by_id[c.child]->name == "Texture" &&
            (c.prop == "DiffuseColor" || c.prop.empty()))
          texs.push_back(c.child);
      for (const Conn &c : conns)
        if (c.parent == mat_id && by_id.count(c.child) && by_id[c.child]->name == "Texture" &&
            c.prop != "TransparentColor" && c.prop != "TransparencyFactor" && c.prop != "Opacity" &&
            c.prop != "NormalMap" && c.prop != "Bump" && c.prop != "SpecularColor" && c.prop != "ShininessExponent")
          texs.push_back(c.child);
    }
    for (int64_t tid : texs) {
      const Rec *tex = by_id[tid];
      MeshImage mi;
      if (const Rec *rf = tex->child("RelativeFilename"); rf && !rf->props.empty()) mi.name = rf->props[0].s;
      if (const Rec *fn = tex->child("FileName"); fn && !fn->props.empty() && mi.name.empty()) mi.name = fn->props[0].s;
      // embedded bytes live on the Video the texture points at
      for (int64_t vid : children_of(tid, "Video")) {
        const Rec *video = by_id[vid];
        if (const Rec *content = video->child("Content"); content && !content->props.empty() &&
            content->props[0].type == 'R' && !content->props[0].s.empty()) {
          const std::string &b = content->props[0].s;
          mi.bytes.assign(b.begin(), b.end());
          break;
        }
      }
      if (mi.bytes.empty() && !mi.name.empty()) {
        // the file beside the model, by its relative name, then by its base name
        std::string rel = mi.name;
        for (char &c : rel) if (c == '\\') c = '/';
        std::string cand = dir.empty() ? rel : dir + "/" + rel;
        if (std::ifstream(cand, std::ios::binary)) mi.path = cand;
        else {
          std::string cand2 = dir.empty() ? basename_of(rel) : dir + "/" + basename_of(rel);
          if (std::ifstream(cand2, std::ios::binary)) mi.path = cand2;
        }
      }
      if (!mi.bytes.empty() || !mi.path.empty()) {
        mi.name = basename_of(mi.name);
        out.images.push_back(std::move(mi));
        result = (int)out.images.size() - 1;
        break;
      }
    }
    image_of_material[{mat_id, alpha}] = result;
    return result;
  };

  // ---- every geometry ----------------------------------------------
  for (const auto &obj : objects->kids) {
    if (obj->name != "Geometry" || obj->props.size() < 3 || obj->props[2].s != "Mesh") continue;
    const Rec *verts = obj->child("Vertices");
    const Rec *polys = obj->child("PolygonVertexIndex");
    if (!verts || !polys || verts->props.empty() || polys->props.empty()) continue;
    const std::vector<double> &V = verts->props[0].nums;
    const std::vector<double> &P = polys->props[0].nums;
    if (V.size() < 9 || P.size() < 3) continue;
    const int64_t gid = obj->props[0].i;

    // the model it hangs from, for the transform and the materials
    std::vector<int64_t> models = parents_of(gid, "Model");
    const double *M = models.empty() ? nullptr : world_of(models[0], 0);
    std::vector<int64_t> mats = models.empty() ? std::vector<int64_t>() : children_of(models[0], "Material");

    // uv: per polygon corner (ByPolygonVertex) or per control point
    const Rec *uvl = obj->child("LayerElementUV");
    const std::vector<double> *UV = nullptr, *UVI = nullptr;
    bool uv_by_corner = true;
    if (uvl) {
      if (const Rec *u = uvl->child("UV"); u && !u->props.empty()) UV = &u->props[0].nums;
      if (const Rec *ui = uvl->child("UVIndex"); ui && !ui->props.empty()) UVI = &ui->props[0].nums;
      if (const Rec *mt = uvl->child("MappingInformationType"); mt && !mt->props.empty())
        uv_by_corner = mt->props[0].s != "ByControlPoint";
    }
    // material per polygon (ByPolygon) or one for all
    const Rec *matl = obj->child("LayerElementMaterial");
    const std::vector<double> *MI = nullptr;
    bool mat_by_poly = false;
    if (matl) {
      if (const Rec *mm = matl->child("Materials"); mm && !mm->props.empty()) MI = &mm->props[0].nums;
      if (const Rec *mt = matl->child("MappingInformationType"); mt && !mt->props.empty())
        mat_by_poly = mt->props[0].s == "ByPolygon";
    }

    // corners -> vertices, split by (control point, uv)
    const bool have_uv = UV && UV->size() >= 2;
    if (have_uv && out.uv.size() != out.vert_count() * 2) out.uv.resize(out.vert_count() * 2, 0.f);
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> split;
    auto emit_vertex = [&](uint32_t cp, uint32_t uvi) -> uint32_t {
      auto key = std::make_pair(cp, have_uv ? uvi : 0u);
      auto it = split.find(key);
      if (it != split.end()) return it->second;
      double v[3] = {V[cp * 3], V[cp * 3 + 1], V[cp * 3 + 2]};
      double w[3] = {v[0], v[1], v[2]};
      if (M)
        for (int rr = 0; rr < 3; ++rr) w[rr] = M[rr] * v[0] + M[4 + rr] * v[1] + M[8 + rr] * v[2] + M[12 + rr];
      if (z_up) { double y = w[1]; w[1] = w[2] * up_sign; w[2] = -y * up_sign; }
      uint32_t idx = (uint32_t)out.vert_count();
      out.v.push_back((float)w[0]);
      out.v.push_back((float)w[1]);
      out.v.push_back((float)w[2]);
      if (have_uv) {
        size_t k = (size_t)uvi * 2;
        float u = k + 1 < UV->size() ? (float)(*UV)[k] : 0.f;
        float t = k + 1 < UV->size() ? (float)(*UV)[k + 1] : 0.f;
        out.uv.push_back(u);
        out.uv.push_back(1.f - t); // FBX's v runs up; ours runs down the picture
      } else if (!out.uv.empty()) {
        out.uv.push_back(0.f);
        out.uv.push_back(0.f);
      }
      split.emplace(key, idx);
      return idx;
    };

    // walk polygons; faces are grouped into parts by material as they come
    std::vector<uint32_t> poly;
    std::vector<uint32_t> poly_uv;
    size_t poly_index = 0, corner = 0;
    int cur_mat = -2;
    const uint32_t ncp = (uint32_t)(V.size() / 3);
    auto flush_poly = [&]() {
      if (poly.size() < 3) { poly.clear(); poly_uv.clear(); return; }
      int mat_slot = MI && !MI->empty() ? (int)(*MI)[mat_by_poly ? std::min(poly_index, MI->size() - 1) : 0] : 0;
      if (mat_slot != cur_mat) {
        cur_mat = mat_slot;
        MeshPart part;
        part.first_face = (uint32_t)out.face_count();
        if (mat_slot >= 0 && mat_slot < (int)mats.size()) {
          const Rec *m = by_id[mats[(size_t)mat_slot]];
          if (m->props.size() >= 2) part.name = m->props[1].s;
          double dc[3];
          prop70_vec3(*m, "DiffuseColor", dc, 1.0);
          for (int k = 0; k < 3; ++k) part.color[k] = (float)dc[k];
          part.image = material_image(mats[(size_t)mat_slot], false);
          part.alpha_image = material_image(mats[(size_t)mat_slot], true);
        }
        out.parts.push_back(part);
      }
      for (size_t i = 1; i + 1 < poly.size(); ++i) {
        out.f.push_back(emit_vertex(poly[0], poly_uv[0]));
        out.f.push_back(emit_vertex(poly[i], poly_uv[i]));
        out.f.push_back(emit_vertex(poly[i + 1], poly_uv[i + 1]));
      }
      out.parts.back().face_count = (uint32_t)out.face_count() - out.parts.back().first_face;
      poly.clear();
      poly_uv.clear();
      ++poly_index;
    };
    for (size_t i = 0; i < P.size(); ++i, ++corner) {
      int64_t raw = (int64_t)P[i];
      bool last = raw < 0;
      uint32_t cp = (uint32_t)(last ? ~raw : raw);
      if (cp >= ncp) { poly.clear(); poly_uv.clear(); if (last) ++poly_index; continue; }
      uint32_t uvi = 0;
      if (have_uv) {
        if (uv_by_corner) uvi = UVI && !UVI->empty() ? (corner < UVI->size() ? (uint32_t)(*UVI)[corner] : 0u) : (uint32_t)corner;
        else uvi = UVI && !UVI->empty() ? (cp < UVI->size() ? (uint32_t)(*UVI)[cp] : 0u) : cp;
      }
      poly.push_back(cp);
      poly_uv.push_back(uvi);
      if (last) flush_poly();
    }
    flush_poly();
  }
  if (out.f.empty()) { err = "FBX holds no polygon geometry"; return false; }
  if (!out.uv.empty() && out.uv.size() != out.vert_count() * 2) out.uv.resize(out.vert_count() * 2, 0.f);
  return true;
}

} // namespace gpx
