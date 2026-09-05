// Geekatplay TerraForge - per-frame services split from app.cpp for the
// 500-line module rule: the points overlay, scatter instance rebuild,
// and the PNG-sequence capture (with its optional camera fly-through).
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace studio {

bool renderer_render_to_file(const std::string &path, int w, int h);

// the last heightmap handed to the renderer, kept so overlays and
// camera paths can sample real elevations without re-walking the graph
static std::shared_ptr<gpx::Heightmap> g_overlay_terrain;
static uint64_t g_overlay_revision = 0;

void app_set_overlay_terrain(std::shared_ptr<gpx::Heightmap> hm) {
  g_overlay_terrain = std::move(hm);
  ++g_overlay_revision;
}


// Rebuild every scattered object's copy list from its bound Points node.
// Called when the evaluation moves, and again by the render exporter so a
// scripted set_scatter -> render in one batch never ships an empty forest.
void scene_rebuild_scatter_instances(App &a) {
  static unsigned long long revision = 0;
  const gpx::Heightmap *hm = g_overlay_terrain && !g_overlay_terrain->empty()
                                 ? g_overlay_terrain.get()
                                 : nullptr;
  float hs = render_settings().height_scale;
  for (SceneObject &o : scene().objects) {
    o.inst_revision = ++revision;
    if (o.type != SceneObject::Mesh || !o.scatter_node) {
      o.inst.clear();
      continue;
    }
    gpx::Node *sn = a.graph.find_node(o.scatter_node);
    const gpx::PointCloud *pc = nullptr;
    if (sn)
      for (const gpx::Port &p : sn->ports)
        if (p.dir == gpx::PortDir::Out && p.type == gpx::DataType::Points &&
            p.pts && p.pts->size()) {
          pc = p.pts.get();
          break;
        }
    o.inst.clear();
    if (!pc) continue;
    size_t count = std::min(pc->size(), (size_t)4096);
    o.inst.reserve(count * 8);
    for (size_t i = 0; i < count; ++i) {
      float px = pc->x[i], pz = pc->y[i], py = 0.f;
      if (hm) {
        int ix = std::clamp((int)(px * hm->w), 0, hm->w - 1);
        int iy = std::clamp((int)(pz * hm->h), 0, hm->h - 1);
        py = hm->v[(size_t)iy * hm->w + ix] * hs;
      }
      uint32_t hb = gpx::planet::pl_hash_bits((int)i, 11, 0, o.scatter_seed);
      float yaw = (hb & 0xffffu) / 65535.f * 6.2831853f;
      float sj =
          1.f + (((hb >> 16) & 0xffu) / 255.f - 0.5f) * o.scatter_jitter;
      float sc = o.scatter_scale * sj;
      // point values scale the copies when asked: a power-law cloud then
      // reads as many saplings and a few grown trees
      if (o.scatter_value_size > 0.f) {
        float vv = std::clamp(pc->v[i], 0.f, 2.f);
        sc *= 1.f + (vv - 1.f) * o.scatter_value_size;
        sc = std::max(sc, 0.02f * o.scatter_scale);
      }
      o.inst.push_back(px);
      o.inst.push_back(py);
      o.inst.push_back(pz);
      o.inst.push_back(sc);
      o.inst.push_back(std::cos(yaw));
      o.inst.push_back(std::sin(yaw));
      // a per-copy brightness so a stand of one mesh reads as many
      o.inst.push_back(0.85f + ((hb >> 24) & 0xffu) / 255.f * 0.3f);
      o.inst.push_back(0.f);
    }
  }
}

// Keyed cameras follow their tracks at the graph clock. Empty tracks leave
// the camera exactly where the user put it.
void app_service_camera_anim(App &a) {
  for (SceneObject &o : scene().objects) {
    if (o.type != SceneObject::Camera) continue;
    CameraData &cd = o.cam;
    for (int k = 0; k < 3; ++k) {
      if (!cd.anim_eye[k].empty())
        cd.eye[k] = cd.anim_eye[k].sample(a.graph.time);
      if (!cd.anim_target[k].empty())
        cd.target[k] = cd.anim_target[k].sample(a.graph.time);
    }
  }
}

void app_service_points_overlay(App &a) {
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (!lk.owns_lock() || a.eval.running.load() || a.uploaded_serial != a.eval_serial) return;
    // points overlay: whenever the selection or the evaluation moves, hand
    // the renderer the selected node's point cloud (if it has one) with
    // heights sampled from the current terrain
    {
      static uint64_t last_sel = ~0ull, last_ser = ~0ull;
      static uint64_t last_terrain = ~0ull;
      if (last_sel != a.selected_node || last_ser != a.uploaded_serial || last_terrain != g_overlay_revision) {
        last_terrain = g_overlay_revision;
        last_sel = a.selected_node;
        last_ser = a.uploaded_serial;
        std::vector<float> xyz;
        gpx::Node *sn = a.graph.find_node(a.selected_node);
        const gpx::PointCloud *pc = nullptr;
        if (sn)
          for (const gpx::Port &p : sn->ports)
            if (p.dir == gpx::PortDir::Out &&
                p.type == gpx::DataType::Points && p.pts && p.pts->size()) {
              pc = p.pts.get();
              break;
            }
        if (pc) {
          const gpx::Heightmap *hm =
              g_overlay_terrain && !g_overlay_terrain->empty()
                  ? g_overlay_terrain.get()
                  : nullptr;
          float hs = render_settings().height_scale;
          size_t count = std::min(pc->size(), (size_t)20000);
          xyz.reserve(count * 3);
          for (size_t i = 0; i < count; ++i) {
            float hx = pc->x[i], hz = pc->y[i], hy = 0.f;
            if (hm) {
              int ix = std::clamp((int)(hx * hm->w), 0, hm->w - 1);
              int iy = std::clamp((int)(hz * hm->h), 0, hm->h - 1);
              hy = hm->v[(size_t)iy * hm->w + ix] * hs;
            }
            xyz.push_back(hx);
            xyz.push_back(hy);
            xyz.push_back(hz);
          }
        }
        renderer_set_points_overlay(xyz);
      }
    }
}

void app_service_scatter(App &a) {
  static uint64_t last_serial = ~0ull, last_terrain = ~0ull;
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock() || a.eval.running.load() || a.uploaded_serial != a.eval_serial) return;
  if (last_serial == a.uploaded_serial && last_terrain == g_overlay_revision) return;
  scene_rebuild_scatter_instances(a);
  last_serial = a.uploaded_serial;
  last_terrain = g_overlay_revision;
}

void app_service_sequence(App &a) {
    // PNG-sequence capture: whenever the evaluation for the current frame's
    // time has landed and been uploaded, save the frame and step the clock.
    // Riding the async pipeline this way means a heavy graph just takes
    // longer per frame - nothing blocks and nothing tears.
    if (a.seq_active && !a.eval.running.load() &&
        a.uploaded_serial == a.eval_serial) {
      // day cycle: sweep the sun across the sequence
      if (a.seq_sun_sweep) {
        float s = a.seq_total > 1
                      ? (float)a.seq_frame / (float)(a.seq_total - 1)
                      : 0.f;
        RenderSettings &rs = render_settings();
        rs.sun_mode = 0;
        rs.sun_azimuth = a.seq_sun[0] + (a.seq_sun[2] - a.seq_sun[0]) * s;
        rs.sun_altitude = a.seq_sun[1] + (a.seq_sun[3] - a.seq_sun[1]) * s;
      }

      // fly-through: put the active camera on its path for this frame
      if (a.seq_cam_path) {
        gpx::Node *pn = a.graph.find_node(a.seq_cam_path);
        const gpx::PointCloud *pc = nullptr;
        if (pn)
          for (const gpx::Port &p : pn->ports)
            if (p.dir == gpx::PortDir::Out &&
                p.type == gpx::DataType::Points && p.pts && p.pts->size() > 1) {
              pc = p.pts.get();
              break;
            }
        int cam = scene_active_camera();
        if (pc && cam >= 0 && cam < (int)scene().objects.size() &&
            scene().objects[cam].type == SceneObject::Camera) {
          auto sample_path = [&](float s, float *out3) {
            s = std::clamp(s, 0.f, 1.f);
            float fi = s * (float)(pc->size() - 1);
            size_t i0 = (size_t)fi;
            size_t i1 = std::min(i0 + 1, pc->size() - 1);
            float u = fi - (float)i0;
            float px = pc->x[i0] + (pc->x[i1] - pc->x[i0]) * u;
            float pz = pc->y[i0] + (pc->y[i1] - pc->y[i0]) * u;
            float py = a.seq_cam_height;
            if (g_overlay_terrain && !g_overlay_terrain->empty()) {
              const gpx::Heightmap *hm = g_overlay_terrain.get();
              int ix = std::clamp((int)(px * hm->w), 0, hm->w - 1);
              int iy = std::clamp((int)(pz * hm->h), 0, hm->h - 1);
              py += hm->v[(size_t)iy * hm->w + ix] *
                    render_settings().height_scale;
            }
            out3[0] = px;
            out3[1] = py;
            out3[2] = pz;
          };
          float s = a.seq_total > 1
                        ? (float)a.seq_frame / (float)(a.seq_total - 1)
                        : 0.f;
          CameraData &cd = scene().objects[cam].cam;
          sample_path(s, cd.eye);
          sample_path(std::min(s + 0.04f, 1.f), cd.target);
        }
      }
      char name[64];
      snprintf(name, sizeof name, "frame_%04d.png", a.seq_frame);
      std::string path = a.seq_dir + "/" + name;
      renderer_render_to_file(path, a.seq_w, a.seq_h);
      ++a.seq_frame;
      if (a.seq_frame >= a.seq_total) {
        a.seq_active = false;
        a.status = "sequence done: " + std::to_string(a.seq_total) +
                   " frames in " + a.seq_dir;
      } else {
        a.graph.time =
            a.anim_start + (float)a.seq_frame / std::max(a.seq_fps, 1.f);
        a.request_eval();
        a.status = "sequence frame " + std::to_string(a.seq_frame) + "/" +
                   std::to_string(a.seq_total);
      }
    }
}

} // namespace studio
