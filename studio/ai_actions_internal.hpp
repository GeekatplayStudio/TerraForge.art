// Geekatplay TerraForge — declarations shared between ai_actions.cpp and the
// files split from it (ai_actions_camera.cpp, ai_actions_scene.cpp) for the
// 500-line module rule. Internal to the studio AI action dispatcher.
#pragma once
#include <json.hpp>
#include <string>

namespace studio {
struct App;
struct CameraData;

// ai_actions_camera.cpp: JSON field readers and camera/render helpers.
bool read_vec3(const nlohmann::json &j, const char *key, float *out);
void apply_camera_fields(CameraData &cd, const nlohmann::json &j);
int engine_index(const std::string &name);
unsigned long long planet_surface_node_of(App &a, const nlohmann::json &v);

// ai_actions_scene.cpp: the scene-object ops (select, place_object,
// add/set_light, add_primitive, import_object, set_scatter,
// add/set_planet, add_infinite_terrain). Returns true when `op` was one of
// them; the branch bodies are unchanged and count into `applied` / set `err`
// exactly as they did inside ai_apply_actions.
bool ai_scene_object_op(App &a, const std::string &op, const nlohmann::json &act,
                        int &applied, std::string &err);
} // namespace studio
