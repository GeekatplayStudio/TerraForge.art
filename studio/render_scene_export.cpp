// Geekatplay TerraForge - building scene.json for the offline engines:
// terrain OBJ + albedo, the sky panorama, camera, sun, fog, water, and
// every scene mesh with its scattered copies. Split from
// panel_render.cpp for the 500-line module rule.
#include "app.hpp"
#include "obj_text.hpp"
#include "render_settings.hpp"
#include "render_terrain_bake.hpp"
#include "render_water_bake.hpp"
#include "scene.hpp"
#include "terrain_relief.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/heightmap.hpp"
#include "gpx/material_params.hpp"
#include "gpx/node_graph.hpp"
#include <json.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "stb_image_write.h"

namespace studio {

namespace fs = std::filesystem;
using json = nlohmann::json;

fs::path render_workdir();
bool renderer_export_sky_hdr(const std::string &path, int w, int h,
                             const float *from);
std::shared_ptr<const gpx::TextureRGBA> app_terrain_albedo(); // app_upload.cpp
const gpx::Heightmap *app_placed_terrain();                    // app_services.cpp
void renderer_get_camera(float *eye, float *target, float *fov);
void render_set_preview_paths(const std::string &preview,
                              const std::string &progress);


namespace {

// The faces of a run of `count` vertices starting at `first`, written so each
// winds the way its own normals face. A generator or a file can wind a
// triangle against the normals it gives it: the viewport shades whichever
// side is seen and Mitsuba's two-sided materials forgive it, but Cycles
// shades from the winding and drew those meshes black (a plant's lumps and
// trunk).
void write_face_run(ObjText &f, const SceneObject &o, int first, int count, bool uv) {
  for (int i = 0; i + 2 < count; i += 3) {
    const float *a = &o.verts[(size_t)(first + i) * 6], *b = &o.verts[(size_t)(first + i + 1) * 6],
                *c = &o.verts[(size_t)(first + i + 2) * 6];
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float g[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                        e1[0] * e2[1] - e1[1] * e2[0]};
    const float facing = g[0] * (a[3] + b[3] + c[3]) + g[1] * (a[4] + b[4] + c[4]) +
                         g[2] * (a[5] + b[5] + c[5]);
    const int order[3] = {1, facing < 0.f ? 3 : 2, facing < 0.f ? 2 : 3};
    f.ch('f');
    for (int k : order) {
      const int n = i + k;
      if (uv) f.ch(' ').num(n).ch('/').num(n).ch('/').num(n);
      else f.ch(' ').num(n).text("//").num(n);
    }
    f.ch('\n');
  }
}

// A picture goes out once per content, named by it. Every render compressed
// every part's picture again, on the main thread, at the writer's slowest
// setting - over a second for one plant's leaf - so five plants froze the
// window for seven seconds, and did again on the next render of the same
// scene. A scratch file read once wants speed, not size.
std::string png_once(const fs::path &dir, const uint8_t *px, int w, int h, int comp) {
  uint64_t key = 1469598103934665603ull;
  const size_t n = (size_t)w * (size_t)h * (size_t)comp;
  for (size_t i = 0; i < n; ++i) {
    key ^= px[i];
    key *= 1099511628211ull;
  }
  key ^= ((uint64_t)w << 40) ^ ((uint64_t)h << 20) ^ (uint64_t)comp;
  char name[40];
  std::snprintf(name, sizeof name, "tex_%016llx.png", (unsigned long long)key);
  const fs::path path = dir / name;
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    const int level = stbi_write_png_compression_level;
    stbi_write_png_compression_level = 2;
    const std::string part = path.string() + ".part";
    stbi_write_png(part.c_str(), w, h, comp, px, w * comp);
    stbi_write_png_compression_level = level;
    fs::rename(part, path, ec);
  }
  return path.string();
}

// A mesh's materials, one OBJ per part, since every engine takes one material
// per shape: a plant goes out as its bark and its leaves, each in its own
// colour and picture. The whole mesh used to take the object's colour, and a
// plant's colours are its parts' - offline it was one flat white tree. Parts
// count only with texture coordinates, as in the viewport (renderer_meshes.cpp).
json export_mesh_parts(const SceneObject &o, const fs::path &dir, int mesh_index) {
  json parts = json::array();
  const size_t nv = o.verts.size() / 6;
  if (o.parts.empty() || o.uvs.size() != nv * 2) return parts;
  auto write_run = [&](const std::string &stem, int first, int count, const float *rgb,
                       const SceneObject::Part *p) {
    const fs::path op = dir / (stem + ".obj");
    ObjText f(op.string());
    for (int i = first; i < first + count; ++i) {
      const float *v = &o.verts[(size_t)i * 6];
      // v up is the picture's top in the viewport; the engines read an OBJ's
      // v from the picture's bottom
      f.line3("v", v[0], v[1], v[2]).line3("vn", v[3], v[4], v[5]);
      f.text("vt ").num(o.uvs[(size_t)i * 2]).ch(' ').num(1.f - o.uvs[(size_t)i * 2 + 1]).ch('\n');
    }
    write_face_run(f, o, first, count, true);
    json jp = {{"obj", op.string()}, {"color", {rgb[0], rgb[1], rgb[2]}}};
    if (p && !p->rgba.empty() && p->w > 0 && p->h > 0 &&
        p->rgba.size() >= (size_t)p->w * (size_t)p->h * 4) {
      jp["texture"] = png_once(dir, p->rgba.data(), p->w, p->h, 4);
      // The viewport cuts a picture at alpha 0.5 (a leaf card's edge). An
      // engine's mask reads a grey picture of its own.
      std::vector<uint8_t> alpha((size_t)p->w * (size_t)p->h);
      bool cut = false;
      for (size_t i = 0; i < alpha.size(); ++i) {
        alpha[i] = p->rgba[i * 4 + 3];
        cut = cut || alpha[i] < 128;
      }
      if (cut) jp["alpha"] = png_once(dir, alpha.data(), p->w, p->h, 1);
    }
    parts.push_back(std::move(jp));
  };
  int covered = 0;
  for (size_t k = 0; k < o.parts.size(); ++k) {
    const SceneObject::Part &p = o.parts[k];
    if (p.first < 0 || p.count <= 0 || (size_t)(p.first + p.count) > nv) continue;
    const float rgb[3] = {o.color[0] * p.color[0], o.color[1] * p.color[1], o.color[2] * p.color[2]};
    write_run("mesh_" + std::to_string(mesh_index) + "_part_" + std::to_string(k), p.first,
              p.count, rgb, &p);
    covered = std::max(covered, p.first + p.count);
  }
  // what no part claims is drawn plain, in the object's own colour
  if (!parts.empty() && (size_t)covered < nv)
    write_run("mesh_" + std::to_string(mesh_index) + "_rest", covered, (int)nv - covered,
              o.color, nullptr);
  return parts;
}

// What stands on the viewport's ground stands on the render's. The tile the
// engines get is a grid of ten-metre cells carrying the few octaves of the
// micro-relief such a grid can; the viewport's ground is the heightmap under
// all of them. A fern placed on the one stood half buried in the other. A
// point within `reach` of the viewport's ground is moved by the difference;
// anything further off it - a bird, a floating rock - is left where it is.
float seat_offset(const BakedTerrain &b, float x, float z, float y, float reach) {
  const gpx::Heightmap *hm = app_placed_terrain();
  float baked = 0.f;
  if (!hm || hm->empty() || !baked_tile_height(b, x, z, baked)) return 0.f;
  const RenderSettings &rs = render_settings();
  const float view = hm->sample(x, z) * rs.height_scale +
                     relief_at(x, z, relief_dials(rs), RELIEF_NEAR_OCTAVES);
  return std::fabs(y - view) <= reach ? baked - view : 0.f;
}

} // namespace

bool export_scene(App &a, const std::string &out_png, int width, int height,
                  int spp, const char *engine, std::string &err,
                  int cam_index, bool passes, bool panorama) {
  RenderSettings &rs = render_settings();
  fs::path dir = render_workdir();
  {
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    gpx::Node *best = nullptr;
    for (auto &n : a.graph.nodes) {
      if (n->type == "TerrainOutput") best = n.get();
      else if (!best && n->first_out(gpx::DataType::Heightmap)) best = n.get();
    }
    if (!best) {
      err = "no terrain in the graph";
      return false;
    }
    gpx::Port *ph = best->first_out(gpx::DataType::Heightmap);
    if (!ph || !ph->hmap || ph->hmap->empty()) {
      err = "terrain not computed yet";
      return false;
    }
  }

  // The world the viewport draws, as meshes and albedo maps: the tile placed
  // on its planet, the ground beyond it out to the horizon, both bent by the
  // world's curvature and painted by the same palette. What used to go out
  // was the graph's raw heightmap over one flat square, uncoloured - which
  // is why a render through a camera and the viewport through that same
  // camera were two different pictures.
  //
  // The tile's own colour is the one the viewport's tile was painted with
  // (app_upload.cpp picks it: the assigned material first), over the palette
  // by the placement's weight. The export used to pick a texture of its own -
  // whichever node in the graph came last with one, often a material's flat
  // preview - and overwrite the whole tile with it.
  // a camera-requested render frames from THAT camera's own optics; only a
  // bare panel render falls back to whatever the viewport is doing
  float eye[3], target[3], fov;
  SceneState &scn = scene();
  if (cam_index >= 0 && cam_index < (int)scn.objects.size() &&
      scn.objects[cam_index].type == SceneObject::Camera) {
    const CameraData &cd = scn.objects[cam_index].cam;
    for (int k = 0; k < 3; ++k) { eye[k] = cd.eye[k]; target[k] = cd.target[k]; }
    int nfmt = 0;
    const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&nfmt);
    int fi = std::clamp(cd.format, 0, nfmt - 1);
    fov = gpx::cam::fov_y_deg(cd.focal_mm, F[fi].height_mm);
  } else {
    renderer_get_camera(eye, target, &fov);
  }

  const std::shared_ptr<const gpx::TextureRGBA> tile_colour = app_terrain_albedo();
  const BakedTerrain baked =
      render_bake_terrain(a, dir.string(), 512, 512, 1024, tile_colour.get(), eye);
  if (!baked.ok) {
    err = baked.err;
    return false;
  }
  // The viewport's own sky and clouds, as an HDR environment map - shot
  // from the camera's own eye, because a cloud layer is at a finite
  // altitude and an environment map is only right for the point it was
  // taken at.
  std::string sky_path = (dir / "sky.hdr").string();
  bool sky_ok = renderer_export_sky_hdr(sky_path, 2048, 1024, eye);

  float sun[3];
  compute_sun_dir(rs, sun);
  // the film of the camera being rendered, or of whatever the viewport is
  // developing with when a bare panel render has no camera of its own
  float film_mult = 1.f, film_sat = 1.f;
  float film_tint[3] = {1.f, 1.f, 1.f};
  if (cam_index >= 0 && cam_index < (int)scn.objects.size() &&
      scn.objects[cam_index].type == SceneObject::Camera) {
    const CameraData &cd = scn.objects[cam_index].cam;
    int nf = 0;
    const gpx::cam::FilmStock *F = gpx::cam::film_stocks(&nf);
    const gpx::cam::FilmStock &fs = F[std::clamp(cd.film, 0, nf - 1)];
    // the same composition panel_camera.cpp hands the viewport: the camera's
    // own exposure and film, with the PostProcess grade on top
    film_mult = gpx::cam::exposure_multiplier(cd.aperture, cd.shutter, cd.iso) *
                rs.post_exposure;
    film_sat = fs.saturation * rs.post_saturation;
    for (int k = 0; k < 3; ++k) film_tint[k] = fs.tint[k] * rs.post_tint[k];
  } else {
    film_mult = rs.post_exposure;
    film_sat = rs.post_saturation;
    for (int k = 0; k < 3; ++k) film_tint[k] = rs.post_tint[k];
  }
  json j;
  j["engine"] = engine;
  j["width"] = width;
  j["height"] = height;
  j["spp"] = spp;
  j["passes"] = passes;
  j["panorama"] = panorama;
  j["output"] = out_png;
  std::string preview_path = (dir / "preview.png").string();
  std::string progress_path = (dir / "progress.txt").string();
  std::error_code rm;
  fs::remove(preview_path, rm);
  fs::remove(progress_path, rm);
  render_set_preview_paths(preview_path, progress_path);
  j["preview"] = preview_path;
  j["progress_file"] = progress_path;
  j["terrain_obj"] = baked.tile_obj;
  j["albedo"] = baked.tile_albedo;
  j["surround_obj"] = baked.surround_obj;
  j["surround_albedo"] = baked.surround_albedo;
  j["ground_extent"] = baked.extent;
  j["sky_hdr"] = sky_ok ? sky_path : "";
  // How the frame is developed: the camera's own exposure from its
  // aperture, shutter and ISO, and the film's tint and saturation. The
  // viewport has always applied these - a camera view is exposed by its
  // optics - and the export sent the bare scene exposure, so a render came
  // out developed differently from the picture the camera was showing.
  j["exposure"] = rs.exposure * film_mult;
  j["grade"] = {film_tint[0], film_tint[1], film_tint[2]};
  j["saturation"] = film_sat;
  j["camera"] = {{"eye", {eye[0], eye[1], eye[2]}},
                 {"target", {target[0], target[1], target[2]}},
                 {"fov", fov}};
  j["sun"] = {{"dir", {sun[0], sun[1], sun[2]}},
              {"color", {rs.sun_color[0], rs.sun_color[1], rs.sun_color[2]}},
              {"intensity", rs.sun_intensity}};
  j["sky"] = {{"zenith", {rs.sky_zenith[0], rs.sky_zenith[1], rs.sky_zenith[2]}},
              {"horizon", {rs.sky_horizon[0], rs.sky_horizon[1], rs.sky_horizon[2]}},
              {"ambient", rs.ambient_intensity}};
  j["material"] = {{"roughness", rs.mat_roughness},
                   {"metallic", rs.mat_metallic},
                   {"specular", rs.mat_specular},
                   {"transparency", rs.mat_transparency}};
  j["fog"] = {{"type", rs.fog_type},
              {"density", rs.fog_density},
              {"level", rs.fog_level * rs.height_scale * 4.f},
              {"falloff", rs.fog_falloff / std::max(rs.height_scale, 1e-3f)},
              {"color", {rs.fog_color[0], rs.fog_color[1], rs.fog_color[2]}},
              {"absorb", {rs.absorption_color[0], rs.absorption_color[1],
                          rs.absorption_color[2]}},
              {"scatter", rs.fog_sun_scatter},
              {"albedo", rs.fog_albedo},
              {"anisotropy", rs.fog_anisotropy},
              {"heterogeneity", rs.fog_heterogeneity},
              {"steps", rs.fog_steps}};
  // The sea the camera sees: one surface about the camera, as wide as the
  // ground it lies in, carrying the viewport's own waves (gpx/water_waves.hpp)
  // and the roughness of those too small for the mesh.
  const BakedWater sea = rs.show_water
                             ? render_bake_water(rs, eye, (dir / "water.obj").string(), baked.extent)
                             : BakedWater{};
  j["water"] = {{"enabled", rs.show_water},
                {"extent", baked.extent},
                {"mesh", sea.ok ? sea.obj : std::string()},
                {"level", rs.water_level * rs.height_scale},
                {"roughness", sea.roughness},
                {"deep", {rs.water_deep_color[0], rs.water_deep_color[1],
                          rs.water_deep_color[2]}}};
  // scene meshes, scattered copies included, so the offline engines see the
  // same world the viewport draws (multi-engine parity rule)
  {
    scene_rebuild_scatter_instances(a); // a scripted batch may not have ticked
    json meshes = json::array();
    SceneState &sc = scene();
    int mi = 0;
    for (const SceneObject &o : sc.objects) {
      if (o.type != SceneObject::Mesh || !sc.object_visible(o) ||
          o.verts.empty())
        continue;
      const int mesh_index = mi++;
      fs::path mp = dir / ("mesh_" + std::to_string(mesh_index) + ".obj");
      {
        ObjText mf(mp.string());
        const size_t nv = o.verts.size() / 6;
        for (size_t i = 0; i < nv; ++i) {
          const float *v = &o.verts[i * 6];
          mf.line3("v", v[0], v[1], v[2]).line3("vn", v[3], v[4], v[5]);
        }
        write_face_run(mf, o, 0, (int)nv, false);
      }
      float model[16], nrm9[9];
      scene_object_matrix(o, rs.height_scale, model, nrm9);
      // standing on the ground: within its own height of it, or two metres
      const float reach = std::max((o.bmax[1] - o.bmin[1]) * o.scale * o.scl[1],
                                   2.f / std::max(rs.terrain_size_m, 1.f));
      const float seat = o.inst.empty() ? seat_offset(baked, o.pos[0], o.pos[2], model[13], reach) : 0.f;
      model[13] += seat;
      json jm;
      jm["obj"] = mp.string();
      jm["color"] = {o.color[0], o.color[1], o.color[2]};
      json parts = export_mesh_parts(o, dir, mesh_index);
      if (!parts.empty()) jm["parts"] = std::move(parts);
      // A volumetric material makes the mesh a medium rather than a surface:
      // the engines get its extinction, absorption colour, scattering albedo
      // and phase, and draw it as one (Mitsuba: a homogeneous medium inside a
      // null surface, with the volumetric path tracer).
      if (o.material_node) {
        if (gpx::Node *mn = a.graph.find_node(o.material_node)) {
          const gpx::MaterialParams mp2 = gpx::material_params_from(mn->attrs);
          // density is per box unit in the material (see FS_MESH); the
          // engines want it per world unit, so divide by the object's width
          const float ext = std::max({(o.bmax[0] - o.bmin[0]) * o.scale * o.scl[0],
                                      (o.bmax[1] - o.bmin[1]) * o.scale * o.scl[1],
                                      (o.bmax[2] - o.bmin[2]) * o.scale * o.scl[2], 1e-6f});
          if (mp2.vol_density > 0.f)
            jm["volume"] = {{"density", mp2.vol_density / ext},
                            {"density_per_box", mp2.vol_density},
                            {"absorb", {mp2.vol_absorb[0], mp2.vol_absorb[1], mp2.vol_absorb[2]}},
                            {"albedo", mp2.vol_albedo},
                            {"anisotropy", mp2.vol_anisotropy},
                            {"steps", mp2.vol_steps}};
        }
      }
      jm["model"] = json::array();
      for (int k = 0; k < 16; ++k) jm["model"].push_back(model[k]);
      // decomposed placement too, for engines that would rather compose
      // their own transforms (Blender's axis conventions, mainly)
      jm["position"] = {o.pos[0], o.pos[1] * rs.height_scale + seat, o.pos[2]};
      jm["scale"] = o.scale;
      jm["scl"] = {o.scl[0], o.scl[1], o.scl[2]};
      jm["ypr"] = {o.yaw, o.pitch, o.roll};
      if (!o.inst.empty()) {
        json inst = json::array();
        const size_t per = SceneObject::INST_FLOATS;
        for (size_t i = 0; i + per <= o.inst.size(); i += per) {
          const float *s = o.inst.data() + i;
          // x, y, z, scale, yaw (radians, from the stored cos/sin), then the
          // per-axis scale, the lean into the ground and the ground's normal
          const float y = s[1] + seat_offset(baked, s[0], s[2], s[1], reach * std::max(s[3], 1.f));
          inst.push_back({s[0], y, s[2], s[3], std::atan2(s[5], s[4]),
                          s[8], s[9], s[10], s[11], s[12], s[13], s[14]});
        }
        jm["instances"] = std::move(inst);
      }
      meshes.push_back(std::move(jm));
    }
    j["meshes"] = std::move(meshes);
  }
  // scene point lights, for engine parity with the viewport
  {
    json lights = json::array();
    for (const SceneObject &o : scene().objects) {
      if (o.type != SceneObject::Light || !scene().object_visible(o)) continue;
      float lyaw = o.yaw * 0.017453293f, lpit = o.pitch * 0.017453293f;
      lights.push_back(
          {{"position", {o.pos[0], o.pos[1] * rs.height_scale, o.pos[2]}},
           {"color", {o.color[0], o.color[1], o.color[2]}},
           {"intensity", o.light_intensity},
           {"reach", o.light_radius},
           {"type", o.light_type == 1 ? "spot" : "point"},
           {"cone_deg", o.light_cone},
           {"direction",
            {std::cos(lpit) * std::sin(lyaw), std::sin(lpit),
             std::cos(lpit) * std::cos(lyaw)}}});
    }
    j["lights"] = std::move(lights);
  }
  std::ofstream sj(dir / "scene.json");
  sj << j.dump(2);
  return true;
}

} // namespace studio
