// Geekatplay TerraForge - natural language through whichever model is
// configured: a local Ollama, OpenAI chat completions, Anthropic messages,
// Google Gemini. One function to call, four wire formats behind it, each
// built and parsed by a pure function the tests can feed canned JSON.
#include "ai_services.hpp"
#include "config.hpp"
#include "http_client.hpp"
#include "ollama.hpp"
#include "prefs.hpp"
#include <fstream>
#include <json.hpp>
#include <sstream>

using nlohmann::json;

namespace studio {

namespace {

std::string read_file_b64(const std::string &path) {
  if (path.empty()) return "";
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return base64_encode(ss.str());
}

std::string mime_of(const std::string &path) {
  std::string l = path;
  for (char &c : l) c = (char)tolower(c);
  if (l.size() > 4 && (l.compare(l.size() - 4, 4, ".jpg") == 0 || l.compare(l.size() - 5, 5, ".jpeg") == 0))
    return "image/jpeg";
  return "image/png";
}

} // namespace

// ---------------------------------------------------------------- builders
std::string openai_chat_json(const std::string &model, const std::string &system,
                             const std::string &prompt, const std::string &img) {
  json content = json::array();
  content.push_back({{"type", "text"}, {"text", prompt}});
  if (!img.empty())
    content.push_back({{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64," + img}}}});
  json j = {{"model", model},
            {"messages", json::array({json{{"role", "system"}, {"content", system}},
                                      json{{"role", "user"}, {"content", content}}})},
            {"temperature", 0.2}};
  return j.dump();
}

std::string anthropic_messages_json(const std::string &model, const std::string &system,
                                    const std::string &prompt, const std::string &img) {
  json content = json::array();
  if (!img.empty())
    content.push_back({{"type", "image"},
                       {"source", {{"type", "base64"}, {"media_type", "image/png"}, {"data", img}}}});
  content.push_back({{"type", "text"}, {"text", prompt}});
  json j = {{"model", model},
            {"max_tokens", 4096},
            {"system", system},
            {"messages", json::array({json{{"role", "user"}, {"content", content}}})}};
  return j.dump();
}

std::string gemini_generate_json(const std::string &prompt, const std::string &img,
                                 bool want_image, const std::string &aspect) {
  json parts = json::array();
  parts.push_back({{"text", prompt}});
  if (!img.empty()) parts.push_back({{"inline_data", {{"mime_type", "image/png"}, {"data", img}}}});
  json j = {{"contents", json::array({json{{"parts", parts}}})}};
  if (want_image)
    j["generationConfig"] = {{"responseModalities", json::array({"IMAGE"})},
                             {"imageConfig", {{"aspectRatio", aspect}}}};
  else
    j["generationConfig"] = {{"temperature", 0.2}};
  return j.dump();
}

// ----------------------------------------------------------------- parsers
bool parse_openai_chat(const std::string &body, std::string &text, std::string &err) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded()) { err = "not JSON"; return false; }
  if (j.contains("error")) { err = j["error"].value("message", "error"); return false; }
  if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
    err = "no choices in the reply";
    return false;
  }
  const json &m = j["choices"][0].value("message", json::object());
  if (!m.contains("content") || !m["content"].is_string()) { err = "no content"; return false; }
  text = m["content"].get<std::string>();
  return true;
}

bool parse_anthropic_message(const std::string &body, std::string &text, std::string &err) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded()) { err = "not JSON"; return false; }
  if (j.contains("error")) { err = j["error"].value("message", "error"); return false; }
  if (!j.contains("content") || !j["content"].is_array()) { err = "no content"; return false; }
  text.clear();
  for (const json &c : j["content"])
    if (c.value("type", "") == "text") text += c.value("text", "");
  if (text.empty()) { err = "no text in the reply"; return false; }
  return true;
}

bool parse_gemini_text(const std::string &body, std::string &text, std::string &err) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded()) { err = "not JSON"; return false; }
  if (j.contains("error")) { err = j["error"].value("message", "error"); return false; }
  if (!j.contains("candidates") || !j["candidates"].is_array() || j["candidates"].empty()) {
    err = "no candidates";
    return false;
  }
  text.clear();
  const json &parts = j["candidates"][0].value("content", json::object()).value("parts", json::array());
  for (const json &p : parts)
    if (p.contains("text") && p["text"].is_string()) text += p["text"].get<std::string>();
  if (text.empty()) { err = "no text in the reply"; return false; }
  return true;
}

// --------------------------------------------------------------------- ask
bool ai_text(const std::string &provider_in, const std::string &system, const std::string &prompt,
             const std::string &image_path, std::string &out, std::string &err) {
  std::string provider = provider_in.empty() ? config().ai.text_provider : provider_in;
  ServiceConfig s = service_resolved(provider);
  if (!service_ready(provider)) {
    err = provider + " is not configured (Settings > AI services)";
    return false;
  }
  if (provider == "ollama") {
    std::string model = image_path.empty() ? s.model : prefs().vision_model;
    return ollama_generate(s.endpoint, model, system, prompt, image_path, out, err);
  }
  std::string img = read_file_b64(image_path);
  HttpResponse r;
  if (provider == "openai") {
    r = http_post_json(s.endpoint + "/chat/completions", openai_chat_json(s.model, system, prompt, img),
                       {{"Authorization", "Bearer " + s.key}});
    if (r.status == 0) { err = r.error; return false; }
    return parse_openai_chat(r.body, out, err);
  }
  if (provider == "anthropic") {
    r = http_post_json(s.endpoint + "/messages", anthropic_messages_json(s.model, system, prompt, img),
                       {{"x-api-key", s.key}, {"anthropic-version", "2023-06-01"}});
    if (r.status == 0) { err = r.error; return false; }
    return parse_anthropic_message(r.body, out, err);
  }
  if (provider == "google") {
    std::string url = s.endpoint + "/models/" + s.model + ":generateContent?key=" + s.key;
    // Gemini has no system role in this shape; the system text leads the prompt
    r = http_post_json(url, gemini_generate_json(system + "\n\n" + prompt, img, false, ""));
    if (r.status == 0) { err = r.error; return false; }
    return parse_gemini_text(r.body, out, err);
  }
  err = "no text adapter for " + provider;
  return false;
}

} // namespace studio
