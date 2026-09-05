// Geekatplay TerraForge - the timeline and the world's animation tracks in
// the scene file. Split from scene_io.cpp for the 500-line module rule.
//
// Written only when they differ from a fresh document, so a project that
// never touched the timeline reads back byte-identical.
#include "scene_io.hpp"
#include "scene.hpp"

using json = nlohmann::json;

namespace studio {

void scene_anim_to_json(json &j, const SceneState &sc) {
  const gpx::Timeline &tl = sc.timeline;
  json jt;
  jt["fps"] = tl.fps;
  jt["start"] = tl.start;
  jt["end"] = tl.end;
  jt["preview"] = tl.preview;
  jt["preview_start"] = tl.preview_start;
  jt["preview_end"] = tl.preview_end;
  jt["display"] = (int)tl.display;
  jt["loop"] = (int)tl.loop;
  jt["autokey"] = tl.autokey;
  jt["all_frames"] = tl.all_frames;
  jt["snap"] = tl.snap;
  if (!tl.markers.empty()) {
    json m = json::array();
    for (const gpx::Marker &mk : tl.markers) m.push_back({{"t", mk.time}, {"name", mk.name}});
    jt["markers"] = m;
  }
  j["timeline"] = jt;
  if (!sc.world_anim.empty()) {
    json ja = json::object();
    for (const auto &kv : sc.world_anim)
      if (!kv.second.empty() || !kv.second.modifiers.empty())
        ja[kv.first] = gpx::track_to_string(kv.second);
    if (!ja.empty()) j["world_anim"] = ja;
  }
}

void scene_anim_from_json(const json &j, SceneState &sc) {
  sc.timeline = gpx::Timeline{};
  sc.world_anim.clear();
  if (j.contains("timeline") && j["timeline"].is_object()) {
    const json &jt = j["timeline"];
    gpx::Timeline &tl = sc.timeline;
    tl.fps = jt.value("fps", tl.fps);
    if (tl.fps <= 0.f) tl.fps = 30.f;
    tl.start = jt.value("start", tl.start);
    tl.end = jt.value("end", tl.end);
    tl.preview = jt.value("preview", false);
    tl.preview_start = jt.value("preview_start", tl.start);
    tl.preview_end = jt.value("preview_end", tl.end);
    tl.display = (gpx::TimeDisplay)std::clamp(jt.value("display", 0), 0, 2);
    tl.loop = (gpx::LoopMode)std::clamp(jt.value("loop", 1), 0, 2);
    tl.autokey = jt.value("autokey", false);
    tl.all_frames = jt.value("all_frames", false);
    tl.snap = jt.value("snap", true);
    if (jt.contains("markers") && jt["markers"].is_array())
      for (const json &m : jt["markers"])
        tl.markers.push_back({m.value("t", 0.f), m.value("name", std::string())});
  }
  if (j.contains("world_anim") && j["world_anim"].is_object())
    for (auto it = j["world_anim"].begin(); it != j["world_anim"].end(); ++it)
      if (it.value().is_string())
        gpx::track_from_string(sc.world_anim[it.key()], it.value().get<std::string>());
}

} // namespace studio
