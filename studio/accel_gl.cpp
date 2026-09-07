// Geekatplay TerraForge — the GL implementation of gpx::Accelerator.
//
// Installed by the studio at start-up. The CLI, the tests and every
// regression golden install nothing and run the CPU path unchanged, which is
// what keeps bakes bit-identical and deterministic while an interactive drag
// gets the GPU.
#include "accel_gl_fractal.hpp"
#include "gpu_compute.hpp"
#include "console.hpp"
#include "gpx/accel.hpp"
#include "gpx/heightmap.hpp"
#include <glad/gl.h>
#include <atomic>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

namespace studio {

namespace {

// What the GPU path may be trusted with.
//
// Not a list of what is hard to write - the shader implements all of it - but
// a list of where a floating-point difference of about 1e-7 between the two
// paths stops being 1e-7 in the answer. The agreement check
// (studio/accel_check.cpp) measured every one of these; the mean difference
// in each is the same 3e-6 as in the cases that agree, and it is only the
// worst pixel that runs away. Four operations do that, and they do it for the
// same reason: they are discontinuous, or their derivative is unbounded.
//
//   combine MAX_ABS / MIN_ABS   picks an octave by magnitude. Two octaves a
//                               hair apart, and the two paths choose
//                               differently; the answer then differs by the
//                               gap between them, not by 1e-7. Measured
//                               9.5e-3.
//   profile TERRACES            floor(). A value a millionth from a step
//                               boundary lands on the far side of it, and the
//                               output moves by a whole step. Measured 0.16.
//   gain != 1                   pow(|v|, 1/gain). Near v = 0 the slope is
//                               infinite. Measured 5.6e-3 at gain 3.
//   filter_steepness != 1       the same pow, per octave. Measured 6.9e-4.
//
// So the CPU keeps them. All four default to their neutral value, so this
// costs the fast path almost nothing, and it buys the guarantee that matters:
// what the accelerator returns is the terrain the CPU would have drawn.
bool supported(const gpx::fractal::Params &P) {
  using namespace gpx::fractal;
  if (P.base == CELL_F1 || P.base == CELL_EDGES) return false; // no worley yet
  if (P.combine == MAX_ABS || P.combine == MIN_ABS) return false;
  if (P.profile == P_TERRACE) return false;
  if (P.gain != 1.f) return false;
  if (P.filter_steepness != 1.f) return false;
  return true;
}

class GlAccelerator : public gpx::Accelerator {
public:
  bool available() const override { return gpu_compute_available() && !dead; }
  const char *name() const override { return "OpenGL 4.3 compute"; }

  void stats(uint64_t &t, uint64_t &d) const override {
    t = taken.load();
    d = declined.load();
  }

  bool fractal(const gpx::fractal::Params &P, uint32_t seed, gpx::Heightmap &out,
               gpx::Heightmap &rough) override {
    if (!supported(P)) {
      declined.fetch_add(1);
      return false;
    }
    const int w = out.w, h = out.h;
    if (w <= 0 || h <= 0 || (int)out.v.size() < w * h) {
      declined.fetch_add(1);
      return false;
    }
    // Below this the readback and the dispatch cost more than the arithmetic
    // saved. Measured rather than guessed - see docs/GPU_USE.md.
    if ((int64_t)w * h < 128 * 128) {
      declined.fetch_add(1);
      return false;
    }
    if (!gpu_compute_bind_thread()) {
      declined.fetch_add(1);
      return false;
    }
    const unsigned prog = gpu_compute_program("fractal", fractal_compute_source());
    if (!prog) {
      dead = true; // it will not compile next frame either
      declined.fetch_add(1);
      return false;
    }

    const size_t n = (size_t)w * h;
    GLuint bufs[2] = {0, 0};
    glGenBuffers(2, bufs);
    for (GLuint b : bufs) {
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
      glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(n * sizeof(float)),
                   nullptr, GL_STREAM_READ);
    }
    glUseProgram(prog);
    auto i1 = [&](const char *k, int v) {
      glUniform1i(glGetUniformLocation(prog, k), v);
    };
    auto f1 = [&](const char *k, float v) {
      glUniform1f(glGetUniformLocation(prog, k), v);
    };
    glUniform2i(glGetUniformLocation(prog, "u_size"), w, h);
    glUniform1ui(glGetUniformLocation(prog, "u_seed"), seed);
    i1("u_base", P.base);
    i1("u_landscape", P.landscape);
    i1("u_combine", P.combine);
    i1("u_octaves", P.octaves);
    i1("u_profile", P.profile);
    i1("u_rotate", P.rotate ? 1 : 0);
    i1("u_double_noise", P.double_noise ? 1 : 0);
    f1("u_wavelength", P.wavelength);
    f1("u_stretch_x", P.stretch_x);
    f1("u_stretch_y", P.stretch_y);
    f1("u_stretch_damping", P.stretch_damping);
    f1("u_scale_ratio", P.scale_ratio);
    f1("u_amp_ratio", P.amp_ratio);
    f1("u_roughness", P.roughness);
    f1("u_gain", P.gain);
    f1("u_smooth_level", P.smooth_level);
    f1("u_influence", P.influence);
    f1("u_local_influence", P.local_influence);
    f1("u_distortion", P.distortion);
    f1("u_distortion_scale", P.distortion_scale);
    f1("u_filter_steepness", P.filter_steepness);
    f1("u_variation_strength", P.variation_strength);
    f1("u_variation_roughness", P.variation_roughness);
    f1("u_smooth_altitude", P.smooth_altitude);
    f1("u_blend", P.blend);
    f1("u_ridge_smooth", P.ridge_smooth);
    f1("u_bump_surge", P.bump_surge);
    f1("u_profile_steps", P.profile_steps);
    f1("u_creep_in", P.creep_in);
    f1("u_filter_min", P.filter_min);
    f1("u_filter_max", P.filter_max);
    f1("u_amplitude", P.amplitude);
    f1("u_offset", P.offset);
    f1("u_rough_ref", P.rough_ref);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufs[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bufs[1]);
    const GLuint gx = (GLuint)((w + 7) / 8), gy = (GLuint)((h + 7) / 8);
    glDispatchCompute(gx, gy, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    bool ok = true;
    for (int k = 0; k < 2 && ok; ++k) {
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[k]);
      const void *p = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                                       (GLsizeiptr)(n * sizeof(float)),
                                       GL_MAP_READ_BIT);
      if (!p) {
        ok = false;
        break;
      }
      gpx::Heightmap &dst = k == 0 ? out : rough;
      if ((int)dst.v.size() >= (int)n)
        std::memcpy(dst.v.data(), p, n * sizeof(float));
      else
        ok = false;
      glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDeleteBuffers(2, bufs);
    glUseProgram(0);
    // A GL error here means the result in `out` cannot be trusted, and a
    // half-written heightmap is worse than a slow one. Say so and let the
    // caller recompute.
    if (GLenum e = glGetError(); e != GL_NO_ERROR) {
      log_error("gpu", "fractal dispatch left GL error 0x" +
                           std::to_string((unsigned)e) + "; falling back");
      ok = false;
    }
    (ok ? taken : declined).fetch_add(1);
    return ok;
  }

private:
  mutable std::atomic<uint64_t> taken{0}, declined{0};
  bool dead = false; // a shader that would not compile is not tried again
};

GlAccelerator g_gl;

} // namespace

void accel_gl_install() {
  // A way to rule the GPU out without a rebuild. When a terrain looks wrong
  // the first question is always "is it the new fast path", and an answer
  // that costs a restart rather than a recompile is worth one line.
  if (const char *off = std::getenv("GPX_NO_GPU"); off && *off && *off != '0') {
    log_info("accel", "GPU acceleration disabled by GPX_NO_GPU");
    return;
  }
  if (!gpu_compute_available()) {
    log_info("accel", std::string("GPU acceleration off: ") +
                          gpu_compute_status());
    return;
  }
  gpx::set_accelerator(&g_gl);
  log_info("accel", "GPU acceleration on (OpenGL 4.3 compute)");
}

void accel_gl_stats(unsigned long long &taken,
                    unsigned long long &declined) {
  g_gl.stats(taken, declined);
}

} // namespace studio
