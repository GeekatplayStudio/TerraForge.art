// Geekatplay TerraForge - playback and the per-frame apply.
//
// Once a frame, before anything is drawn: advance the clock if playing
// (real time, dropping frames to keep up, or every frame when the timeline
// says so; once / loop / ping-pong), write every keyed scene and world
// property for the current time, then the legacy camera pose tracks. The
// graph's own tracks are applied by Graph::evaluate at the same clock.
#include "anim_targets.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace studio {

void app_service_camera_anim(App &a);

namespace {
float g_dir = 1.f; // ping-pong direction
}

void anim_service(App &a) {
  SceneState &sc = scene();
  gpx::Timeline &tl = sc.timeline;
  if (a.anim_playing && !a.seq_active) {
    float dt = tl.all_frames ? 1.f / std::max(tl.fps, 1.f) : ImGui::GetIO().DeltaTime;
    if (tl.all_frames) dt = 1.f / std::max(tl.fps, 1.f);
    float t = a.graph.time + dt * g_dir;
    float s = tl.play_start(), e = tl.play_end();
    if (e <= s) e = s + 1.f / std::max(tl.fps, 1.f);
    if (t > e || t < s) {
      switch (tl.loop) {
        case gpx::LoopMode::Once: t = g_dir > 0 ? e : s; a.anim_playing = false; break;
        case gpx::LoopMode::Loop: t = g_dir > 0 ? s + std::fmod(t - s, e - s) : e - std::fmod(s - t, e - s); break;
        case gpx::LoopMode::PingPong:
          if (t > e) { t = e - (t - e); g_dir = -1.f; } else { t = s + (s - t); g_dir = 1.f; }
          t = std::clamp(t, s, e);
          break;
      }
    }
    // when playing every frame, land exactly on frames
    if (tl.all_frames) t = std::round(t * tl.fps) / tl.fps;
    a.graph.time = t;
    a.request_eval();
  }
  if (!a.anim_playing) g_dir = 1.f;
  bool sc_changed = false, w_changed = false;
  anim_apply(sc, render_settings(), a.graph.time, sc_changed, w_changed);
  if (sc_changed) a.scene_selection_serial++;
  app_service_camera_anim(a);
}

} // namespace studio
