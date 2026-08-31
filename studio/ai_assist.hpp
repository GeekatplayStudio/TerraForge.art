// Geekatplay TerraForge — natural-language assistant available in every tab.
#pragma once
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

// The JSON action schema, for prompts, docs and the MCP tool description.
std::string ai_action_schema(AiDomain domain);
} // namespace studio
