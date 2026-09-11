// Geekatplay TerraForge - building scene.json for the offline engines:
// terrain OBJ + albedo, the sky panorama, camera, sun, fog, water, and
// every scene mesh with its scattered copies. Split from
// panel_render.cpp for the 500-line module rule.
#include "app.hpp"
#include "render_settings.hpp"
#include "render_terrain_bake.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/heightmap.hpp"
#include "gpx/material_params.hpp"
#include "gpx/node_graph.hpp"
#include <json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "stb_image_write.h"

namespace studio {

namespace fs = std::filesystem;
using json = nlohmann::json;

fs::path render_workdir();
bool renderer_export_sky_hdr(const std::string &path, int w, int h,
                             const float *from);
void renderer_get_camera(float *eye, float *target, float *fov);
void render_set_preview_paths(const std::string &preview,
                              const std::string &progress);


bool export_scene(App &a, const std::string &out_png, int width, int height,
                  int spp, const char *engine, std::string &err,
                  int cam_index, bool passes, bool panorama) {
  RenderSettings &rs = render_settings();
  fs::path dir = render_workdir();
  gpx::Heightmap hm;
  std::vector<uint8_t> albedo_u8;
  int alb_w = 0;
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
    hm = *ph->hmap;
    if (rs.terrain_material_mode == 2) {
      gpx::Node *mn = a.graph.find_node(rs.terrain_material_node);
      gpx::Port *mp = mn ? mn->first_out(gpx::DataType::Texture) : nullptr;
      if (mp && mp->tex && !mp->tex->empty()) {
        albedo_u8 = mp->tex->to_u8();
        alb_w = mp->tex->w;
      }
    } else if (rs.terrain_material_mode == 0) {
      for (auto &n : a.graph.nodes) {
        if (n->type == "Splatmap" || n->type == "NormalMap" ||
            n->type == "AlbedoToPBR")
          continue;
        gpx::Port *pt = n->first_out(gpx::DataType::Texture);
        if (pt && pt->tex && !pt->tex->empty()) {
          albedo_u8 = pt->tex->to_u8();
          alb_w = pt->tex->w;
        }
      }
    }
  }

  // The world the viewport draws, as meshes and albedo maps: the tile placed
  // on its planet, the ground beyond it out to the horizon, both bent by the
  // world's curvature and painted by the same palette. What used to go out
  // was the graph's raw heightmap over one flat square, uncoloured - which
  // is why a render through a camera and the viewport through that same
  // camera were two different pictures.
  const BakedTerrain baked =
      render_bake_terrain(a, dir.string(), 512, 384, 1024);
  if (!baked.ok) {
    err = baked.err;
    return false;
  }
  // A material assigned to the terrain still wins over the palette: that is
  // the picture the viewport shows too.
  if (!albedo_u8.empty())
    stbi_write_png((dir / "albedo.png").string().c_str(), alb_w, alb_w, 4,
                   albedo_u8.data(), alb_w * 4);

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
  j["water"] = {{"enabled", rs.show_water},
                // as wide as the ground it lies in, not the one tile it used
                // to be: a sea that stops at the tile's edge is a puddle
                {"extent", baked.extent},
                {"mesh", baked.water_obj},
                {"level", rs.water_level * rs.height_scale},
                {"roughness", std::max(rs.mat_roughness * 0.05f, 0.01f)},
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
      fs::path mp = dir / ("mesh_" + std::to_string(mi++) + ".obj");
      std::ofstream mf(mp);
      const size_t nv = o.verts.size() / 6;
      for (size_t i = 0; i < nv; ++i)
        mf << "v " << o.verts[i * 6] << ' ' << o.verts[i * 6 + 1] << ' '
           << o.verts[i * 6 + 2] << "\nvn " << o.verts[i * 6 + 3] << ' '
           << o.verts[i * 6 + 4] << ' ' << o.verts[i * 6 + 5] << '\n';
      for (size_t i = 0; i + 2 < nv; i += 3)
        mf << "f " << i + 1 << "//" << i + 1 << ' ' << i + 2 << "//" << i + 2
           << ' ' << i + 3 << "//" << i + 3 << '\n';
      float model[16], nrm9[9];
      scene_object_matrix(o, rs.height_scale, model, nrm9);
      json jm;
      jm["obj"] = mp.string();
      jm["color"] = {o.color[0], o.color[1], o.color[2]};
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
      jm["position"] = {o.pos[0], o.pos[1] * rs.height_scale, o.pos[2]};
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
          inst.push_back({s[0], s[1], s[2], s[3], std::atan2(s[5], s[4]),
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
