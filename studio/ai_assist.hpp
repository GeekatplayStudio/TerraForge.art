// Geekatplay TerraForge — natural-language assistant available in every tab.
#pragma once
#include <json.hpp>
#include <string>

namespace gpx {
class Node;
}

namespace studio {
struct App;

// Which part of the app the request applies to. The assistant sends the
// model only the schema relevant to that domain, so answers stay grounded.
enum class AiDomain { Camera, World, Material, Terrain, Object, Render };

// Draws a compact prompt bar ("make a 35mm camera, 50mm lens, Kodak film").
// Accepts an optional reference image for vision models.
void ai_assist_bar(App &a, AiDomain domain, const char *hint);

// Applies a JSON action document produced by the model. Exposed so the
// scripting API and MCP server can drive exactly the same code path.
bool ai_apply_actions(App &a, const std::string &json_text, std::string &err);

// Node-level graph editing (ai_ops_graph.cpp): add/delete/connect/disconnect a
// node, set an attribute, bypass, move, choose what the viewport shows, set the
// resolution, evaluate. Returns 1 applied, 0 handled but rejected (with a
// reason in `err`), -1 when the op belongs to someone else.
int ai_graph_op(App &a, const std::string &op, const nlohmann::json &act,
                std::string &err);

// Window layouts and the viewport set (layout_store.cpp): save_layout,
// load_layout, delete_layout, list_layouts, reset_layout, add_view,
// close_view, arrange_views. Same return convention as ai_graph_op.
int ai_layout_op(App &a, const std::string &op, const nlohmann::json &act,
                 std::string &err);

// Mesh diagnosis and repair (mesh_ops.cpp): import_mesh, mesh_analyse,
// mesh_repair, mesh_reduce, mesh_export. Same return convention.
int ai_mesh_op(App &a, const std::string &op, const nlohmann::json &act,
               std::string &err);

// The Material Studio and Browser (material_ops.cpp): open_material,
// list_materials, set_material_type, save_material, load_material.
int ai_material_op(App &a, const std::string &op, const nlohmann::json &act,
                   std::string &err);

// AI generation and configuration (ai_ops_generate.cpp): ai_generate_image,
// ai_generate_texture, ai_generate_skydome, ai_generate_model, ai_ask,
// ai_jobs, ai_job_cancel, config_set_service, config_set_defaults,
// config_status, config_check_comfy.
int ai_generate_op(App &a, const std::string &op, const nlohmann::json &act,
                   std::string &err);

// The asset manager (asset_ops.cpp): asset_search, asset_open, asset_tag,
// asset_untag, asset_note, asset_trash, asset_restore, asset_rescan,
// asset_add_root, asset_remove_root, asset_roots.
int ai_asset_op(App &a, const std::string &op, const nlohmann::json &act,
                std::string &err);

// Viewport and surface quality (ai_ops_view.cpp): subdivision, per-patch
// culling, height scale, planetary radius, fractal relief, displacement
// strength, shadows, exposure. Same return convention as ai_graph_op.
int ai_view_op(App &a, const std::string &op, const nlohmann::json &act,
               std::string &err);

// Scene, animation and session ops (ai_ops_scene.cpp): set_time,
// render_sequence, camera/attribute keys, select_node, set_locked,
// open_node_editor, set_workspace, evaluate, assign_material. ai_graph_op
// falls through to it. Same return convention as ai_graph_op.
int ai_scene_op(App &a, const std::string &op, const nlohmann::json &act,
                std::string &err);

// Resolve act[key] to a node by id, numeric string, alias or type name (last
// match). Defined in ai_ops_graph.cpp, shared with ai_ops_scene.cpp.
gpx::Node *find_node(App &a, const nlohmann::json &act, const char *key);

// The JSON action schema, for prompts, docs and the MCP tool description.
std::string ai_action_schema(AiDomain domain);
} // namespace studio
