// Geekatplay TerraForge - natural language for the whole scene, the terrain
// or the atmosphere: a planner prompt over every action schema, an answer
// that is a JSON list of actions, applied through ai_apply_actions.
#pragma once
#include <cstdint>
#include <string>

namespace studio {

struct App;
struct AiJob;

enum DescribeScope { DESCRIBE_SCENE = 0, DESCRIBE_TERRAIN, DESCRIBE_ATMOSPHERE };

// The system prompt for a scope: rules plus the schemas that scope covers.
std::string ai_describe_system_prompt(int scope);
// The JSON array inside a model's reply, fences and chatter stripped; ""
// when there is none.
std::string ai_describe_extract_actions(const std::string &reply);

// Start a description job; the answer is applied by ai_jobs_service.
uint64_t ai_describe_submit(const std::string &prompt, const std::string &image, int scope);
bool ai_describe_apply(App &a, AiJob &job, std::string &err);

void ai_describe_open(int scope);
void draw_panel_ai_describe(App &a);

} // namespace studio
