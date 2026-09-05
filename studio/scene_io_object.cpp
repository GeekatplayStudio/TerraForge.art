// Geekatplay TerraForge - an object's placement in the scene file: position,
// size, rotation, colour, the deformers and the gizmo switch. Split from
// scene_io.cpp for the 500-line module rule; the object kinds, layers and
// environment stay there.
//
// Deform keys are written only when a deformer is set, so a file from a
// build that had none reads back unchanged and an undeformed object writes
// nothing new.
#include "scene_io.hpp"
#include "scene.hpp"

using json = nlohmann::json;

namespace studio {

namespace {
json v3(const float *v) { return json::array({v[0], v[1], v[2]}); }
void v3_from(const json &j, float *v) {
  if (j.is_array() && j.size() >= 3)
    for (int i = 0; i < 3; ++i) v[i] = j[(size_t)i].get<float>();
}
} // namespace

void object_transform_to_json(json &jo, const SceneObject &o) {
  jo["pos"] = v3(o.pos);
  jo["scale"] = o.scale;
  jo["scl"] = v3(o.scl);
  jo["yaw"] = o.yaw;
  jo["pitch"] = o.pitch;
  jo["roll"] = o.roll;
  jo["color"] = v3(o.color);
  jo["show_gizmo"] = o.show_gizmo;
  if (!o.deform.identity()) {
    jo["twist"] = v3(o.deform.twist);
    jo["bend"] = o.deform.bend;
    jo["bend_axis"] = o.deform.bend_axis;
    jo["shear"] = v3(o.deform.shear);
    jo["taper"] = o.deform.taper;
  }
  if (!o.anim.empty()) {
    json ja = json::object();
    for (const auto &kv : o.anim)
      if (!kv.second.empty() || !kv.second.modifiers.empty()) ja[kv.first] = gpx::track_to_string(kv.second);
    if (!ja.empty()) jo["anim"] = ja;
  }
}

void object_transform_from_json(const json &jo, SceneObject &o) {
  if (jo.contains("pos")) v3_from(jo["pos"], o.pos);
  o.scale = jo.value("scale", o.scale);
  if (jo.contains("scl")) v3_from(jo["scl"], o.scl);
  o.yaw = jo.value("yaw", 0.f);
  o.pitch = jo.value("pitch", 0.f);
  o.roll = jo.value("roll", 0.f);
  if (jo.contains("color")) v3_from(jo["color"], o.color);
  o.show_gizmo = jo.value("show_gizmo", true);
  if (jo.contains("twist")) v3_from(jo["twist"], o.deform.twist);
  o.deform.bend = jo.value("bend", 0.f);
  o.deform.bend_axis = jo.value("bend_axis", 0);
  if (jo.contains("shear")) v3_from(jo["shear"], o.deform.shear);
  o.deform.taper = jo.value("taper", 0.f);
  o.anim.clear();
  if (jo.contains("anim") && jo["anim"].is_object())
    for (auto it = jo["anim"].begin(); it != jo["anim"].end(); ++it)
      if (it.value().is_string()) gpx::track_from_string(o.anim[it.key()], it.value().get<std::string>());
}

// ------------------------------------------------------------ visibility
// The two three-state dots and the enable switch, beside the old plain
// "visible" flag. A file from before the dots carries only that flag; a
// hidden object there becomes an explicit red viewport dot and the flag
// goes back to true, so the one switch does not hide it twice.
void object_visibility_to_json(json &jo, const SceneObject &o) {
  jo["visible"] = o.visible;
  jo["vis_viewport"] = o.vis_viewport;
  jo["vis_render"] = o.vis_render;
  jo["enabled"] = o.enabled;
}

void object_visibility_from_json(const json &jo, SceneObject &o) {
  o.visible = jo.value("visible", true);
  if (jo.contains("vis_viewport")) {
    o.vis_viewport = jo.value("vis_viewport", 0);
    o.vis_render = jo.value("vis_render", 0);
    o.enabled = jo.value("enabled", true);
  } else if (!o.visible) {
    o.vis_viewport = 2;
    o.visible = true;
  }
}

// ---------------------------------------------------------------- layers
// A layer is a name, a switch and the colour swatch the Objects tree shows.
// A file from before the swatch gets a palette colour by index, so its
// layers look the way a new scene's would.
json layers_to_json(const SceneState &sc) {
  json layers = json::array();
  for (const SceneLayer &l : sc.layers)
    layers.push_back({{"name", l.name},
                      {"visible", l.visible},
                      {"color", {l.color[0], l.color[1], l.color[2]}}});
  return layers;
}

void layers_from_json(const json &arr, SceneState &sc) {
  sc.layers.clear();
  if (arr.is_array())
    for (const json &jl : arr) {
      SceneLayer l{jl.value("name", std::string("Layer")),
                   jl.value("visible", true)};
      scene_layer_default_color((int)sc.layers.size(), l.color);
      if (jl.contains("color") && jl["color"].is_array() &&
          jl["color"].size() >= 3)
        for (int k = 0; k < 3; ++k) l.color[k] = jl["color"][k].get<float>();
      sc.layers.push_back(l);
    }
  if (sc.layers.empty()) sc.layers.push_back({"Default", true});
}

} // namespace studio
