// Geekatplay TerraForge — local Ollama client
#pragma once
#include <string>

namespace studio {

// POST /api/generate; image_path optional (vision models). Blocking.
bool ollama_generate(const std::string &url, const std::string &model,
                     const std::string &system, const std::string &prompt,
                     const std::string &image_path, std::string &out_text,
                     std::string &err);

bool ollama_available(const std::string &url);

} // namespace studio
