// Geekatplay TerraForge - animation through the action schema: what the
// assistant, the Python API and MCP can do to keys, the clock and the
// range. One implementation behind every door; the Timeline's own gestures
// call the same engine operations.
//
// Track addressing: {"object":"Rock","prop":"pos"} (comp -1 = all, or
// "comp":0..2 / "x"), {"world":"sun_azimuth"}, {"node":..,"attr":..}, or a
// raw {"track":"o:3:pos.x"} id from `keys`.
#include "ai_assist.hpp"
#include "anim_targets.hpp"
#include "anim_tracks.hpp"
#include "anim_widgets.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

int comp_of(const json &act) {
  if (act.contains("comp")) {
    const json &c = act["comp"];
    if (c.is_number()) return c.get<int>();
    if (c.is_string()) { std::string s = c.get<std::string>(); if (s == "x" || s == "r") return 0; if (s == "y" || s == "g") return 1; if (s == "z" || s == "b") return 2; }
  }
  return -1;
}

float time_of(App &a, const json &act) {
  gpx::Timeline &tl = scene().timeline;
  if (act.contains("frame")) return tl.time_of(act["frame"].get<float>());
  if (act.contains("time")) return act["time"].get<float>();
  return a.graph.time;
}

// Resolve the tracks an action addresses (one per component), creating them
// when `create` and the property exists. Returns false with err on failure.
bool tracks_of(App &a, const json &act, bool create, std::vector<TrackRef> &out, std::string &err) {
  out.clear();
  int comp = comp_of(act);
  if (act.contains("track") && act["track"].is_string()) {
    TrackRef r;
    if (!anim_resolve(a, act["track"].get<std::string>(), &r)) { err = "no such track"; return false; }
    out.push_back(r);
    return true;
  }
  if (act.contains("world")) {
    std::string path = act["world"].get<std::string>();
    const AnimProp *p = anim_find_world_prop(path);
    if (!p) { err = "no world property '" + path + "'"; return false; }
    for (int c = 0; c < p->comps; ++c) {
      if (comp >= 0 && c != comp) continue;
      if (create) anim_get(scene().world_anim, *p, c);
      TrackRef r;
      if (anim_resolve(a, "w:" + anim_key(*p, c), &r)) out.push_back(r);
      else { r.kind = TrackRef::World; r.id = "w:" + anim_key(*p, c); r.prop = p; r.comp = p->comps > 1 ? c : -1; out.push_back(r); }
    }
    return true;
  }
  if (act.contains("object")) {
    std::string name = act["object"].get<std::string>();
    SceneState &sc = scene();
    int idx = -1;
    for (size_t i = 0; i < sc.objects.size(); ++i) if (sc.objects[i].name == name) idx = (int)i;
    if (idx < 0 && act["object"].is_number()) idx = act["object"].get<int>();
    if (idx < 0 || idx >= (int)sc.objects.size()) { err = "no object '" + name + "'"; return false; }
    std::string path = act.value("prop", std::string("pos"));
    const AnimProp *p = anim_find_prop(sc.objects[(size_t)idx], path);
    if (!p) { err = "'" + path + "' is not an animatable property of " + name; return false; }
    for (int c = 0; c < p->comps; ++c) {
      if (comp >= 0 && c != comp) continue;
      if (create) anim_get(sc.objects[(size_t)idx].anim, *p, c);
      TrackRef r;
      if (anim_resolve(a, "o:" + std::to_string(idx) + ":" + anim_key(*p, c), &r)) out.push_back(r);
      else { r.kind = TrackRef::Object; r.id = "o:" + std::to_string(idx) + ":" + anim_key(*p, c); r.object = idx; r.prop = p; r.comp = p->comps > 1 ? c : -1; out.push_back(r); }
    }
    return true;
  }
  if (act.contains("node")) {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) { err = "no such node"; return false; }
    std::string key = act.value("attr", "");
    gpx::Attribute *at = n->attrs.find(key);
    if (!at) { err = "no attribute '" + key + "' on " + n->type; return false; }
    int nc = at->type == gpx::AttrType::Color ? 3 : (at->type == gpx::AttrType::Vec2 || at->type == gpx::AttrType::Range) ? 2 : 1;
    for (int c = 0; c < nc; ++c) {
      if (comp >= 0 && c != comp) continue;
      if (nc > 1 && create) at->anim_comp(c);
      std::string id = "n:" + std::to_string(n->id) + ":" + key + (nc > 1 ? ":" + std::to_string(c) : "");
      TrackRef r;
      if (anim_resolve(a, id, &r)) out.push_back(r);
    }
    return !out.empty() || (err = "no track", false);
  }
  err = "say which track: object+prop, world, node+attr, or track";
  return false;
}

gpx::Interp interp_of(const std::string &s) {
  if (s == "linear") return gpx::Interp::Linear;
  if (s == "step" || s == "constant" || s == "hold") return gpx::Interp::Constant;
  if (s == "smooth") return gpx::Interp::Smooth;
  return gpx::Interp::Bezier;
}
gpx::Extrapolate extrap_of(const std::string &s) {
  if (s == "linear") return gpx::Extrapolate::Linear;
  if (s == "cycle") return gpx::Extrapolate::Cycle;
  if (s == "cycle_offset" || s == "offset") return gpx::Extrapolate::CycleOffset;
  if (s == "pingpong" || s == "ping-pong") return gpx::Extrapolate::PingPong;
  return gpx::Extrapolate::Constant;
}

json key_json(const gpx::Track &t, const gpx::Key &k, const gpx::Timeline &tl) {
  const char *in[] = {"linear", "smooth", "step", "bezier"};
  gpx::Interp e = t.effective(k);
  return {{"time", k.time}, {"frame", tl.frame_of(k.time)}, {"value", k.value},
          {"interp", in[std::min((int)e, 3)]}, {"tan_in", k.tan_in}, {"tan_out", k.tan_out}};
}

} // namespace

int ai_anim_op(App &a, const std::string &op, const json &act, std::string &err) {
  gpx::Timeline &tl = scene().timeline;
  std::vector<TrackRef> trs;

  if (op == "set_frame") { anim_set_time(a, tl.time_of(act.value("frame", 0.f))); return 1; }
  if (op == "play") { a.anim_playing = act.value("playing", true); return 1; }
  if (op == "stop") { a.anim_playing = false; anim_set_time(a, tl.play_start()); return 1; }
  if (op == "set_range") {
    if (act.contains("fps")) tl.fps = std::max(act["fps"].get<float>(), 1.f);
    if (act.contains("start")) tl.start = tl.time_of(act["start"].get<float>());
    if (act.contains("end")) tl.end = tl.time_of(act["end"].get<float>());
    if (act.contains("start_s")) tl.start = act["start_s"].get<float>();
    if (act.contains("end_s")) tl.end = act["end_s"].get<float>();
    if (act.contains("preview_start")) { tl.preview_start = tl.time_of(act["preview_start"].get<float>()); tl.preview = true; }
    if (act.contains("preview_end")) { tl.preview_end = tl.time_of(act["preview_end"].get<float>()); tl.preview = true; }
    if (act.contains("preview")) tl.preview = act["preview"].get<bool>();
    if (act.contains("loop")) { std::string l = act["loop"].get<std::string>(); tl.loop = l == "once" ? gpx::LoopMode::Once : l == "pingpong" ? gpx::LoopMode::PingPong : gpx::LoopMode::Loop; }
    if (act.contains("autokey")) tl.autokey = act["autokey"].get<bool>();
    if (act.contains("display")) { std::string d = act["display"].get<std::string>(); tl.display = d == "timecode" ? gpx::TimeDisplay::Timecode : d == "seconds" ? gpx::TimeDisplay::Seconds : gpx::TimeDisplay::Frames; }
    if (tl.end <= tl.start) tl.end = tl.start + tl.time_of(1);
    a.status = "range " + tl.format(tl.start) + " .. " + tl.format(tl.end) + " at " + std::to_string((int)tl.fps) + " fps";
    return 1;
  }
  if (op == "add_marker") { tl.markers.push_back({time_of(a, act), act.value("name", std::string("Marker"))}); return 1; }
  if (op == "remove_marker") {
    float t = time_of(a, act);
    std::string name = act.value("name", std::string());
    size_t before = tl.markers.size();
    tl.markers.erase(std::remove_if(tl.markers.begin(), tl.markers.end(), [&](const gpx::Marker &m) { return name.empty() ? std::fabs(m.time - t) < 1e-4f : m.name == name; }), tl.markers.end());
    if (tl.markers.size() == before) { err = "no such marker"; return 0; }
    return 1;
  }
  if (op == "key_transform") { anim_key_selection_transform(a); return 1; }

  if (op == "set_key" && !act.contains("node")) {
    if (!tracks_of(a, act, true, trs, err)) return 0;
    float t = time_of(a, act);
    for (TrackRef &r : trs) {
      float v = 0.f;
      if (act.contains("value")) {
        const json &jv = act["value"];
        if (jv.is_array()) { int c = std::max(r.comp, 0); v = c < (int)jv.size() ? jv[(size_t)c].get<float>() : 0.f; }
        else if (jv.is_boolean()) v = jv.get<bool>() ? 1.f : 0.f;
        else v = jv.get<float>();
        // a given value is also written to the property, so the key and
        // the scene agree even with the clock elsewhere
        if (r.kind == TrackRef::Object && r.prop) {
          SceneObject &o = scene().objects[(size_t)r.object];
          if (r.prop->boolean) { if (bool *b = anim_bool_ptr(o, *r.prop)) *b = v >= 0.5f; }
          else if (float *f = anim_ptr(o, *r.prop, std::max(r.comp, 0))) *f = v;
        } else if (r.kind == TrackRef::World && r.prop) {
          if (float *f = anim_world_ptr(render_settings(), *r.prop, std::max(r.comp, 0))) *f = v;
        }
      } else if (!anim_current_value(a, r, v)) continue;
      gpx::Track *tr_ = r.track ? r.track : anim_resolve(a, r.id);
      if (!tr_) continue;
      tr_->set_key(t, v);
      std::vector<int> idx{tr_->index_at(t)};
      if (act.contains("interp")) gpx::anim::set_interp(*tr_, idx, interp_of(act["interp"].get<std::string>()));
      if (act.contains("ease")) {
        std::string e = act["ease"].get<std::string>();
        gpx::anim::set_ease(*tr_, idx, e == "in" ? gpx::anim::Ease::In : e == "out" ? gpx::anim::Ease::Out : e == "linear" ? gpx::anim::Ease::Linear : e == "hold" ? gpx::anim::Ease::Hold : gpx::anim::Ease::Easy);
      }
      anim_touched(a, r);
    }
    a.status = "key at " + tl.format(t);
    return 1;
  }
  if (op == "remove_key") {
    if (!tracks_of(a, act, false, trs, err)) return 0;
    float t = time_of(a, act);
    int n = 0;
    for (TrackRef &r : trs) if (r.track && r.track->remove_key(t)) { ++n; anim_touched(a, r); }
    // drop empty object/world tracks so the property is static again
    for (SceneObject &o : scene().objects) for (auto it = o.anim.begin(); it != o.anim.end();) it = (it->second.empty() && it->second.modifiers.empty()) ? o.anim.erase(it) : std::next(it);
    for (auto it = scene().world_anim.begin(); it != scene().world_anim.end();) it = (it->second.empty() && it->second.modifiers.empty()) ? scene().world_anim.erase(it) : std::next(it);
    if (!n) { err = "no key at " + tl.format(t); return 0; }
    return 1;
  }
  if (op == "remove_track" || op == "remove_animation") {
    if (!tracks_of(a, act, false, trs, err)) return 0;
    for (TrackRef &r : trs) if (r.track) { r.track->clear(); anim_touched(a, r); }
    for (SceneObject &o : scene().objects) for (auto it = o.anim.begin(); it != o.anim.end();) it = (it->second.empty() && it->second.modifiers.empty()) ? o.anim.erase(it) : std::next(it);
    for (auto it = scene().world_anim.begin(); it != scene().world_anim.end();) it = (it->second.empty() && it->second.modifiers.empty()) ? scene().world_anim.erase(it) : std::next(it);
    return 1;
  }
  if (op == "keys") {
    // list: every track when nothing is addressed
    json out = json::array();
    std::vector<TrackRef> list;
    if (act.contains("object") || act.contains("world") || act.contains("node") || act.contains("track")) { if (!tracks_of(a, act, false, list, err)) return 0; }
    else list = anim_collect(a, true);
    for (TrackRef &r : list) {
      if (!r.track) continue;
      json jt = {{"track", r.id}, {"owner", r.owner}, {"group", r.group}, {"label", r.label}, {"comp", r.comp},
                 {"pre", (int)r.track->pre}, {"post", (int)r.track->post}, {"expr", r.track->expr}, {"modifiers", r.track->modifiers.size()}};
      json ks = json::array();
      for (const gpx::Key &k : r.track->keys) ks.push_back(key_json(*r.track, k, tl));
      jt["keys"] = ks;
      out.push_back(jt);
    }
    a.status = "animation: " + std::to_string(out.size()) + " track(s)";
    a.api_reply = out.dump(); // published with the next state as "reply"
    return 1;
  }
  if (op == "set_extrapolation") {
    if (!tracks_of(a, act, false, trs, err)) return 0;
    for (TrackRef &r : trs) if (r.track) {
      if (act.contains("pre")) r.track->pre = extrap_of(act["pre"].get<std::string>());
      if (act.contains("post")) r.track->post = extrap_of(act["post"].get<std::string>());
      if (act.contains("mode")) r.track->pre = r.track->post = extrap_of(act["mode"].get<std::string>());
      anim_touched(a, r);
    }
    return 1;
  }
  if (op == "set_ease" || op == "set_interp") {
    if (!tracks_of(a, act, false, trs, err)) return 0;
    for (TrackRef &r : trs) if (r.track) {
      std::vector<int> idx;
      if (act.contains("frame") || act.contains("time")) { int i = r.track->index_at(time_of(a, act)); if (i >= 0) idx.push_back(i); }
      else for (int i = 0; i < (int)r.track->keys.size(); ++i) idx.push_back(i);
      if (act.contains("interp")) { gpx::anim::set_interp(*r.track, idx, interp_of(act["interp"].get<std::string>())); gpx::anim::set_tangent_mode(*r.track, idx, gpx::TangentMode::Auto); }
      if (act.contains("ease")) { std::string e = act["ease"].get<std::string>(); gpx::anim::set_ease(*r.track, idx, e == "in" ? gpx::anim::Ease::In : e == "out" ? gpx::anim::Ease::Out : e == "linear" ? gpx::anim::Ease::Linear : e == "hold" ? gpx::anim::Ease::Hold : gpx::anim::Ease::Easy); }
      anim_touched(a, r);
    }
    return 1;
  }
  if (op == "bake" || op == "simplify" || op == "snap_keys" || op == "mirror_keys" || op == "retime") {
    if (!tracks_of(a, act, false, trs, err)) return 0;
    for (TrackRef &r : trs) if (r.track) {
      std::vector<int> all; for (int i = 0; i < (int)r.track->keys.size(); ++i) all.push_back(i);
      if (op == "bake") gpx::anim::bake(*r.track, tl.fps);
      else if (op == "simplify") gpx::anim::simplify(*r.track, act.value("tolerance", 0.01f), tl.fps);
      else if (op == "snap_keys") gpx::anim::snap_to_frames(*r.track, tl.fps);
      else if (op == "mirror_keys") gpx::anim::mirror(*r.track, all);
      else gpx::anim::retime(*r.track, all, act.contains("pivot") ? tl.time_of(act["pivot"].get<float>()) : r.track->first_time(), act.value("factor", 1.f));
      anim_touched(a, r);
    }
    return 1;
  }
  if (op == "set_expression") {
    if (!tracks_of(a, act, true, trs, err)) return 0;
    std::string e = act.value("expr", std::string());
    if (!e.empty()) { float v; std::string perr; gpx::ExprContext ctx = anim_expr_context(a.graph.time); if (!gpx::expr_eval(e, ctx, v, &perr) && perr.rfind("unknown name", 0) != 0) { err = "expression: " + perr; return 0; } }
    for (TrackRef &r : trs) { gpx::Track *t = r.track ? r.track : anim_resolve(a, r.id); if (t) { t->expr = e; anim_touched(a, r); } }
    return 1;
  }
  if (op == "add_modifier") {
    if (!tracks_of(a, act, true, trs, err)) return 0;
    std::string ty = act.value("type", std::string("noise"));
    gpx::Modifier m;
    m.type = ty == "oscillator" ? gpx::ModType::Oscillator : ty == "offset" ? gpx::ModType::Offset : ty == "limit" ? gpx::ModType::Limit : ty == "smooth" ? gpx::ModType::Smooth : gpx::ModType::Noise;
    m.a = act.value("amplitude", act.value("a", m.type == gpx::ModType::Limit ? 0.f : 0.1f));
    m.b = act.value("frequency", act.value("b", 1.f));
    m.c = act.value("phase", act.value("c", 0.f));
    m.shape = act.value("shape", 0);
    m.octaves = act.value("octaves", 1);
    m.seed = (uint32_t)act.value("seed", 1);
    if (act.contains("min")) m.a = act["min"].get<float>();
    if (act.contains("max")) m.b = act["max"].get<float>();
    if (act.contains("window")) m.a = act["window"].get<float>();
    for (TrackRef &r : trs) { gpx::Track *t = r.track ? r.track : anim_resolve(a, r.id); if (t) { t->modifiers.push_back(m); anim_touched(a, r); } }
    return 1;
  }
  if (op == "clear_modifiers") {
    if (!tracks_of(a, act, false, trs, err)) return 0;
    for (TrackRef &r : trs) if (r.track) { r.track->modifiers.clear(); anim_touched(a, r); }
    return 1;
  }
  if (op == "playblast") {
    // viewport capture of the play range, one image per frame
    a.seq_dir = act.value("dir", std::string("playblast"));
    std::error_code ec;
    std::filesystem::create_directories(a.seq_dir, ec);
    a.seq_fps = tl.fps;
    a.seq_w = act.value("width", 1280);
    a.seq_h = act.value("height", 720);
    float s = act.contains("start") ? tl.time_of(act["start"].get<float>()) : tl.play_start();
    float e = act.contains("end") ? tl.time_of(act["end"].get<float>()) : tl.play_end();
    a.anim_start = s; a.anim_end = e;
    a.seq_total = (int)std::max((e - s) * tl.fps + 0.5f, 1.f) + 1;
    a.seq_frame = 0;
    a.seq_cam_path = 0; a.seq_sun_sweep = false;
    a.anim_playing = false;
    a.graph.time = s;
    a.seq_active = true;
    a.request_eval();
    a.status = "playblast: " + std::to_string(a.seq_total) + " frames to " + a.seq_dir;
    return 1;
  }
  return -1;
}

} // namespace studio
