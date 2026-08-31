// Geekatplay TerraForge — natural-language assistant available in every tab.
#pragma once
#include <json.hpp>
#include <string>

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

// Viewport and surface quality (ai_ops_view.cpp): subdivision, per-patch
// culling, height scale, planetary radius, fractal relief, displacement
// strength, shadows, exposure. Same return convention as ai_graph_op.
int ai_view_op(App &a, const std::string &op, const nlohmann::json &act,
               std::string &err);

// The JSON action schema, for prompts, docs and the MCP tool description.
std::string ai_action_schema(AiDomain domain);
} // namespace studio
