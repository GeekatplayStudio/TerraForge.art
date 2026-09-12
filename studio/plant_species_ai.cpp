// Geekatplay TerraForge - a plant species from a text model's description.
//
// Typing a plant's name already grows it without any model: the engine
// knows well over a hundred plants and every archetype
// (gpx::plant_describe_from_words). A text model is for everything that
// table cannot hold - a cultivar, a regional species, "the gnarled olive on
// the hill behind my grandmother's house", a photograph. The model is asked
// for the same PlantDescription the table fills, as JSON, and the species is
// built from it exactly as it would be from the table, so both roads lead to
// one graph the person can then edit.
//
// It runs as an AI job (ai_jobs.cpp): the request never blocks the UI, the
// answer is applied by ai_jobs_service on the UI thread, and where the plant
// is to stand rides on the job. If the answer is not a description, the
// words are grown by the table instead and the log says why.
#include "plant_species.hpp"
#include "ai_jobs.hpp"
#include "app.hpp"
#include "config.hpp"
#include "console.hpp"
#include <json.hpp>
#include <memory>
#include <mutex>

using nlohmann::json;

namespace studio {

std::string species_ai_system_prompt() {
  return std::string(
             "You are a botanist and a 3D plant modeller working in Geekatplay TerraForge. The user "
             "names or describes a plant, or attaches a photograph of one. Answer with ONLY one JSON "
             "object - no prose, no markdown fences - describing that plant with the fields below. "
             "Use real botany: the species' typical mature height, crown shape, bark, leaf shape, "
             "size and colours, whether it is evergreen, its flowers and fruit and the part of the "
             "year they show (season 0 is midwinter, 0.25 spring, 0.5 midsummer, 0.75 autumn). "
             "Honour what the user asks for: an age, a health (dry, dying, thriving), a season, a "
             "size, a weeping or windswept or pruned habit. Choose the archetype whose structure "
             "matches the plant, and set variation to how much one individual differs from the next.\n\n") +
         gpx::plant_description_schema();
}

std::string species_ai_extract_json(const std::string &reply) {
  const size_t a = reply.find('{');
  const size_t b = reply.rfind('}');
  if (a == std::string::npos || b == std::string::npos || b <= a) return "";
  return reply.substr(a, b - a + 1);
}

uint64_t species_ai_submit(const std::string &prompt, const std::string &image,
                           const SpeciesMake &mk) {
  auto job = std::make_shared<AiJob>();
  job->kind = JOB_TEXT;
  job->provider = config().ai.text_provider;
  job->negative = species_ai_system_prompt(); // the system prompt rides here
  job->prompt = prompt;
  job->image_path = image;
  job->apply.channel = "plant";
  // where the plant goes, for when the answer lands
  json w = {{"place", mk.place},
            {"at_view", mk.at.at_view},
            {"pos", {mk.at.pos[0], mk.at.pos[1], mk.at.pos[2]}},
            {"size", mk.at.size},
            {"heading", mk.at.heading_deg},
            {"scatter", mk.at.scatter},
            {"count", mk.at.count},
            {"name", mk.at.name},
            {"individuals", mk.individuals}};
  job->workflow = w.dump();
  return ai_job_submit(job);
}

bool species_ai_apply(App &a, AiJob &job, std::string &err) {
  std::string text;
  {
    std::lock_guard<std::mutex> lk(job.mtx);
    text = job.text_result;
  }
  SpeciesMake mk;
  const json w = json::parse(job.workflow, nullptr, false);
  if (w.is_object()) {
    mk.place = w.value("place", true);
    mk.at.at_view = w.value("at_view", true);
    if (w.contains("pos") && w["pos"].is_array() && w["pos"].size() >= 3)
      for (int k = 0; k < 3; ++k) mk.at.pos[k] = w["pos"][(size_t)k].get<float>();
    mk.at.size = w.value("size", 1.f);
    mk.at.heading_deg = w.value("heading", 0.f);
    mk.at.scatter = w.value("scatter", false);
    mk.at.count = w.value("count", 300);
    mk.at.name = w.value("name", std::string());
    mk.individuals = w.value("individuals", 1);
  }
  gpx::PlantDescription d;
  std::string perr;
  const std::string doc = species_ai_extract_json(text);
  if (doc.empty() || !gpx::plant_description_parse(doc, d, perr)) {
    log_error("plants", "the model did not answer with a plant description (" +
                            (perr.empty() ? std::string("no JSON") : perr) +
                            "); growing the words instead. Reply: " + text.substr(0, 1500));
    gpx::plant_describe_from_words(job.prompt, d);
    if (d.name.empty() || d.name == "Plant") d.name = job.prompt;
  }
  if (!mk.at.name.empty()) d.name = mk.at.name;
  return species_create(a, d, mk, err) != 0;
}

} // namespace studio
