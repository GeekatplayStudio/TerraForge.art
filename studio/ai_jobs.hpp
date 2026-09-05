// Geekatplay TerraForge - AI generation jobs: submitted from the UI or an
// op, run on their own thread, polled by the UI thread once a frame. A job
// knows what it is making (an image, a texture, a skydome, a 3D model),
// how far it is, and where the result landed; finished jobs are handed to
// ai_jobs_service() on the UI thread, which puts the file in the asset
// index and, when asked, into the scene.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace studio {

struct App;

enum AiJobKind { JOB_IMAGE = 0, JOB_TEXTURE, JOB_SKYDOME, JOB_MODEL, JOB_TEXT };
enum AiJobState { JOB_QUEUED = 0, JOB_RUNNING, JOB_DONE, JOB_FAILED, JOB_CANCELLED };

// What to do with the result once it exists. Empty = keep it in the library.
struct AiJobApply {
  uint64_t material = 0;      // TEXTURE: connect as this material's colour
  std::string channel;        // and which port ("base color", "normal"...)
  bool as_skydome = false;    // SKYDOME: set as the backdrop
  bool import_object = false; // MODEL: import into the scene
};

struct AiJob {
  uint64_t id = 0;
  int kind = JOB_IMAGE;
  std::string provider;
  std::string prompt;
  std::string negative;
  std::string image_path;     // a reference for image-to-3D
  int width = 1024, height = 1024;
  unsigned seed = 0;
  std::string workflow;
  AiJobApply apply;
  // progress, from the worker
  std::atomic<int> state{JOB_QUEUED};
  std::atomic<float> progress{0.f};
  std::atomic<bool> cancel{false};
  std::mutex mtx;
  std::string message;
  std::string result_path;    // set when DONE
  std::string text_result;    // TEXT jobs
  unsigned thumb_tex = 0;     // UI thread only
  bool serviced = false;      // the UI has handled the finished job
  double started = 0, finished = 0;
};

const char *ai_job_kind_name(int kind);
const char *ai_job_state_name(int state);

// Submit; the job starts immediately on a worker thread. Returns its id.
uint64_t ai_job_submit(std::shared_ptr<AiJob> job);
std::shared_ptr<AiJob> ai_job_find(uint64_t id);
std::vector<std::shared_ptr<AiJob>> ai_jobs();
void ai_job_cancel(uint64_t id);
void ai_jobs_clear_finished();

// Once a frame on the UI thread: finished jobs are indexed, applied to the
// scene as their AiJobApply asks, and announced in the status line.
void ai_jobs_service(App &a);

// Where a job's file goes, named from its prompt and the time.
std::string ai_job_output_path(int kind, const std::string &prompt, const char *ext);

// The Generate windows and the Jobs panel (panel_ai_generate.cpp).
void draw_panel_ai_generate(App &a);
void ai_generate_open_image(int kind); // JOB_IMAGE / JOB_TEXTURE / JOB_SKYDOME
void ai_generate_open_model();
void ai_generate_open_jobs();
// The two buttons on the Objects tool row.
void ai_tool_buttons();

} // namespace studio
