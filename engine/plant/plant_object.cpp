// Geekatplay TerraForge - the Object part: a mesh from a file grown as a
// part of the plant.
//
// A scanned pine cone, a modelled apple, a leaf somebody sculpted: anything
// the mesh readers (gpx/mesh_io.hpp) understand can hang from a twig like a
// leaf would. The file is read once per build and remembered in a memo keyed
// by its path and modification time - the one static in this file, a cache
// with no effect on the result - because a species may place the same file
// a thousand times.
//
// The file's own frame is taken as y up, standing on its lowest point: its
// x stays the part's x, its y becomes the growth axis (the frame's z) and
// its z the face (the frame's y), a rotation rather than a mirror so the
// winding survives. size_m is the longest side of its box; 0 keeps the size
// the file came in. Its materials collapse to the one material slot an
// Object has (the file's pictures are not carried: the PlantMaterial on the
// slot is what it wears). A missing or unreadable file is a warning and no
// geometry, never an error, so the rest of the plant still grows. Normals
// are geometric (the readers carry none) and the wind treats it as a rigid
// thing on its twig: it bends with the branch but does not flutter.
#include "plant/plant_internal.hpp"
#include "gpx/mesh_io.hpp"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace gpx {
namespace plant {

namespace {

float transform_block(const BuildCtx &ctx, const ParamReader &pr, Instance &inst, V3 &axis_scale) {
  float size = ctx.scale * pr.random("scale", 0.f, 0, 1.f);
  if (pr.b("scale_inherit", true)) size *= inst.scale;
  axis_scale = V3(pr.random("scale_x", 0.f, 0, 1.f), pr.random("scale_y", 0.f, 0, 1.f), pr.random("scale_z", 0.f, 0, 1.f));
  Frame &f = inst.frame;
  f.o += f.dir(V3(pr.random("offset_x"), pr.random("offset_y"), pr.random("offset_z"))) * size;
  const float rx = pr.random("rot_x"), ry = pr.random("rot_y"), rz = pr.random("rot_z");
  if (rx != 0.f) f = f.rotated(f.x, rx * DEG);
  if (ry != 0.f) f = f.rotated(f.y, ry * DEG);
  if (rz != 0.f) f = f.rotated(f.z, rz * DEG);
  return size;
}

// The memo: a loaded file with its box, or the error it gave.
struct Loaded {
  TriMesh mesh;
  std::string err;
  float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
};

std::shared_ptr<const Loaded> load_memo(const std::string &path) {
  static std::mutex mtx;
  static std::unordered_map<std::string, std::shared_ptr<const Loaded>> memo;
  std::string key = path;
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(path, ec);
  if (!ec) key += "|" + std::to_string((long long)t.time_since_epoch().count());
  {
    std::lock_guard<std::mutex> lk(mtx);
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
  }
  auto L = std::make_shared<Loaded>();
  if (!mesh_load(path, L->mesh, L->err) || L->mesh.empty()) {
    if (L->err.empty()) L->err = "no triangles";
  } else {
    for (int c = 0; c < 3; ++c) L->lo[c] = L->hi[c] = L->mesh.v[(size_t)c];
    for (size_t i = 0; i < L->mesh.vert_count(); ++i)
      for (int c = 0; c < 3; ++c) {
        const float x = L->mesh.v[i * 3 + (size_t)c];
        L->lo[c] = std::min(L->lo[c], x);
        L->hi[c] = std::max(L->hi[c], x);
      }
  }
  std::lock_guard<std::mutex> lk(mtx);
  memo[key] = L;
  return L;
}

} // namespace

void build_object(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);
  tropism_block(pr, inst.frame);
  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};

  std::string file = pr.s("file");
  if (file.empty()) {
    ctx.warnings.push_back("Object: no file named");
    return;
  }
  if (std::filesystem::path(file).is_relative() && !ctx.opt.asset_dir.empty())
    file = (std::filesystem::path(ctx.opt.asset_dir) / file).string();
  const std::shared_ptr<const Loaded> L = load_memo(file);
  if (!L->err.empty()) {
    ctx.warnings.push_back("Object: cannot read " + file + ": " + L->err);
    return;
  }

  // the size: the box's longest side becomes size_m (0 keeps the file's)
  const float ext[3] = {L->hi[0] - L->lo[0], L->hi[1] - L->lo[1], L->hi[2] - L->lo[2]};
  const float longest = std::max(ext[0], std::max(ext[1], ext[2]));
  const float size_m = pr.random("size_m");
  float k = size * (1.f - look.shrink);
  if (size_m > 0.f && longest > 1e-9f) k *= size_m / longest;
  inst.length = ext[1] * k;
  inst.radius = 0.5f * std::max(ext[0], ext[2]) * k;
  const float cx = 0.5f * (L->lo[0] + L->hi[0]), cz = 0.5f * (L->lo[2] + L->hi[2]);

  const float wind4[4] = {inst.wind_phase, inst.wind_bend, 0.f, -1.f};
  const Frame &f = inst.frame;
  const bool two_sided = pr.b("double_sided");
  const TriMesh &m = L->mesh;
  out.begin(inst, PlantPartKind::Object, ctx.material_for(n, 0), two_sided, "object");
  std::vector<uint32_t> ids(m.vert_count());
  for (size_t i = 0; i < m.vert_count(); ++i) {
    const float *v = m.vert(i);
    // file (x, y, z) -> local (x, -z, y): y up becomes the growth axis
    const V3 local((v[0] - cx) * k * axis_scale.x, -(v[2] - cz) * k * axis_scale.y, (v[1] - L->lo[1]) * k * axis_scale.z);
    const float u = m.uv.empty() ? 0.f : m.uv[i * 2], vv = m.uv.empty() ? 0.f : m.uv[i * 2 + 1];
    ids[i] = out.vertex(f.world(local), f.z, u, vv, wind4, tint4);
  }
  for (size_t t = 0; t < m.face_count(); ++t) {
    const uint32_t *fc = m.face(t);
    if (fc[0] < ids.size() && fc[1] < ids.size() && fc[2] < ids.size()) out.tri(ids[fc[0]], ids[fc[1]], ids[fc[2]]);
  }
  out.geometric_normals();
  out.end();

  sk.tip.frame = inst.frame;
  sk.tip.frame.o = f.world(V3(0.f, 0.f, inst.length));
  sk.tip.primal = 1.f;
  sk.tip.dist = inst.dist_root + inst.length;
  sk.has_tip = true;
  sk.bottom.frame = inst.frame;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
