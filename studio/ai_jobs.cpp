// Geekatplay TerraForge - AI generation jobs. See ai_jobs.hpp.
#include "ai_jobs.hpp"
#include "ai_services.hpp"
#include "app.hpp"
#include "asset_store.hpp"
#include "config.hpp"
#include "ai_describe.hpp"
#include "console.hpp"
#include "material_channel_ops.hpp"
#include "mesh_object.hpp"
#include "render_settings.hpp"
#include "undo.hpp"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace studio {

namespace {

std::mutex g_mtx;
std::vector<std::shared_ptr<AiJob>> g_jobs;
uint64_t g_next = 1;

double now_s() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string slug(const std::string &s) {
  std::string o;
  for (char c : s) {
    if (isalnum((unsigned char)c)) o.push_back((char)tolower((unsigned char)c));
    else if (!o.empty() && o.back() != '_') o.push_back('_');
    if (o.size() >= 40) break;
  }
  while (!o.empty() && o.back() == '_') o.pop_back();
  return o.empty() ? "generated" : o;
}

void worker(std::shared_ptr<AiJob> job) {
  job->state.store(JOB_RUNNING);
  job->started = now_s();
  auto progress = [&](float p, const std::string &m) {
    job->progress.store(p);
    std::lock_guard<std::mutex> lk(job->mtx);
    job->message = m;
  };
  GenResult res;
  if (job->kind == JOB_MODEL) {
    GenModelRequest rq;
    rq.provider = job->provider;
    rq.prompt = job->prompt;
    rq.image_path = job->image_path;
    rq.out_dir = config_output_dir("models");
    res = ai_generate_model(rq, &job->cancel, progress);
  } else if (job->kind == JOB_TEXT) {
    std::string out, err;
    res.ok = ai_text(job->provider, job->negative /* system */, job->prompt, job->image_path, out, err);
    res.message = err;
    std::lock_guard<std::mutex> lk(job->mtx);
    job->text_result = out;
  } else {
    GenImageRequest rq;
    rq.provider = job->provider;
    rq.prompt = job->prompt;
    rq.negative = job->negative;
    rq.kind = job->kind == JOB_TEXTURE ? IMG_TEXTURE : job->kind == JOB_SKYDOME ? IMG_SKYDOME : IMG_PLAIN;
    rq.width = job->width;
    rq.height = job->height;
    rq.seed = job->seed;
    rq.workflow = job->workflow;
    rq.out_path = ai_job_output_path(job->kind, job->prompt, ".png");
    res = ai_generate_image(rq, &job->cancel, progress);
  }
  job->finished = now_s();
  {
    std::lock_guard<std::mutex> lk(job->mtx);
    job->result_path = res.path;
    job->message = res.ok ? "done" : res.message;
    if (res.seed) job->seed = res.seed;
  }
  job->progress.store(res.ok ? 1.f : job->progress.load());
  job->state.store(job->cancel.load() ? JOB_CANCELLED : res.ok ? JOB_DONE : JOB_FAILED);
  log_info("ai", std::string(ai_job_kind_name(job->kind)) + " job " + std::to_string(job->id) +
                     (res.ok ? " done: " + res.path : " failed: " + res.message));
}

} // namespace

const char *ai_job_kind_name(int kind) {
  switch (kind) {
    case JOB_TEXTURE: return "texture";
    case JOB_SKYDOME: return "skydome";
    case JOB_MODEL: return "3D model";
    case JOB_TEXT: return "text";
    default: return "image";
  }
}

const char *ai_job_state_name(int state) {
  switch (state) {
    case JOB_RUNNING: return "running";
    case JOB_DONE: return "done";
    case JOB_FAILED: return "failed";
    case JOB_CANCELLED: return "cancelled";
    default: return "queued";
  }
}

std::string ai_job_output_path(int kind, const std::string &prompt, const char *ext) {
  const char *dir_kind = kind == JOB_SKYDOME ? "skies" : kind == JOB_MODEL ? "models" : "textures";
  std::time_t t = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&t));
  return (fs::path(config_output_dir(dir_kind)) / (slug(prompt) + "_" + stamp + ext)).string();
}

uint64_t ai_job_submit(std::shared_ptr<AiJob> job) {
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    job->id = g_next++;
    g_jobs.push_back(job);
  }
  std::thread(worker, job).detach();
  return job->id;
}

std::shared_ptr<AiJob> ai_job_find(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_mtx);
  for (auto &j : g_jobs)
    if (j->id == id) return j;
  return nullptr;
}

std::vector<std::shared_ptr<AiJob>> ai_jobs() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_jobs;
}

void ai_job_cancel(uint64_t id) {
  if (auto j = ai_job_find(id)) j->cancel.store(true);
}

void ai_jobs_clear_finished() {
  std::lock_guard<std::mutex> lk(g_mtx);
  g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(),
                              [](const std::shared_ptr<AiJob> &j) {
                                int s = j->state.load();
                                return s == JOB_DONE || s == JOB_FAILED || s == JOB_CANCELLED;
                              }),
               g_jobs.end());
}

// ------------------------------------------------------------- servicing
namespace {

void apply_result(App &a, AiJob &job) {
  std::string path;
  {
    std::lock_guard<std::mutex> lk(job.mtx);
    path = job.result_path;
  }
  if (path.empty()) return;
  // into the library the asset index watches, whatever else happens
  const char *root_kind = job.kind == JOB_MODEL ? "mesh" : "texture";
  std::string root = config_output_dir(job.kind == JOB_SKYDOME ? "skies" : job.kind == JOB_MODEL ? "models" : "textures");
  std::string err;
  asset_add_root(root, root_kind, err);
  if (job.kind == JOB_TEXTURE && job.apply.material) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    gpx::Node *mat = a.graph.find_node(job.apply.material);
    if (mat) {
      undo_push_locked(a, "AI texture");
      const char *port = job.apply.channel.empty() ? "base color" : job.apply.channel.c_str();
      channel_set_mode(a.graph, mat, port, CH_PICTURE, path);
      a.graph_layout_serial++;
      a.request_eval();
      a.uploaded_serial = 0;
    }
  }
  if (job.kind == JOB_SKYDOME && job.apply.as_skydome) {
    RenderSettings &rs = render_settings();
    rs.backdrop.enabled = true;
    rs.backdrop.file = path;
    rs.backdrop.mapping = 0; // lat-long
    a.uploaded_serial = 0;
  }
  if (job.kind == JOB_MODEL && job.apply.import_object) {
    int idx = scene_import_mesh(path, err);
    if (idx < 0) a.status = "generated, but import failed: " + err;
  }
}

} // namespace

void ai_jobs_service(App &a) {
  for (auto &j : ai_jobs()) {
    int s = j->state.load();
    if (j->serviced || (s != JOB_DONE && s != JOB_FAILED && s != JOB_CANCELLED)) continue;
    j->serviced = true;
    std::string msg;
    {
      std::lock_guard<std::mutex> lk(j->mtx);
      msg = j->message;
    }
    if (s == JOB_DONE && j->kind == JOB_TEXT && j->apply.channel == "describe") {
      std::string err;
      if (ai_describe_apply(a, *j, err)) a.status = "scene built from the description";
      else a.status = "describe: " + err;
      continue;
    }
    if (s == JOB_DONE) {
      apply_result(a, *j);
      if (config().ai.notify_on_finish)
        a.status = std::string("AI ") + ai_job_kind_name(j->kind) + " ready: " + j->result_path;
    } else {
      a.status = std::string("AI ") + ai_job_kind_name(j->kind) + " " + ai_job_state_name(s) + ": " + msg;
    }
  }
}

} // namespace studio
