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
}

} // namespace studio
