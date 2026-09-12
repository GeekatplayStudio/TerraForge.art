// Geekatplay TerraForge - the sea's waves, as numbers.
//
// One sea everywhere: the viewport draws it from these numbers
// (WATER_WAVES_GLSL in studio/shaders_water.cpp is the line-for-line twin),
// the offline render bakes its mesh from them (render_terrain_bake.cpp), and
// the tests pin them. Change one side, change the other, and run
// `verify_field_gpu` - it measures the two against each other.
//
// The surface is a sum of Gerstner waves whose sizes come from the
// Pierson-Moskowitz spectrum of a wind: the wind's speed sets the peak
// wavelength and how much energy each band carries, the waves travel at the
// deep-water speed (omega^2 = g k), and a longer wave follows the wind more
// closely than a short one. It is analytic, so it has exact slopes for the
// shading, an exact Jacobian for the foam, and no tile to repeat - a
// Fourier-transform ocean is a square that tiles, which is the fault this
// replaced.
//
// Sampling. A wave shorter than the grid (or the pixel) carrying it cannot
// be drawn; it aliases into a pattern that crawls. `wave_weight` fades a
// wave out between two and one sampling intervals per wavelength, and what
// it takes away from the slopes is returned as variance, which the shading
// turns into roughness - the far sea glitters instead of sparkling.
//
// Precision. Float phases lose a short wave a few kilometres out, so the
// phases are rebased: `rebase` folds the position of an origin and the clock
// into each wave's phase in double, and the shader evaluates at small
// positions relative to that origin.
#pragma once
#include <algorithm>
#include <cmath>

namespace gpx::water {

inline constexpr int MAX_WAVES = 32;
inline constexpr float GRAVITY = 9.81f;
inline constexpr float TWO_PI = 6.2831853f;

// What the Water settings say (RenderSettings water_*): Vue's Water Surface
// Options, in physical units.
struct Params {
  float wind_speed = 4.f;    // m/s: sets the peak wavelength and the energy
  float wind_dir_deg = 30.f; // the way the waves run, 0 = +x, 90 = +z
  float height = 1.f;        // multiplies every amplitude
  float scale = 1.f;         // multiplies every wavelength (amplitude with it)
  float agitation = 1.f;     // multiplies every wave's speed
  float choppiness = 0.5f;   // 0 round swells, 1 sharp crests
};

struct Wave {
  float dx = 1.f, dz = 0.f; // unit direction of travel in the flat world
  float k = 1.f;            // wavenumber, rad/m
  float amp = 0.f;          // amplitude, m
  float omega = 0.f;        // angular frequency, rad/s
  float phase = 0.f;        // phase at the world origin at t = 0, rad
  float q = 0.f;            // horizontal displacement over vertical
  float lambda = 1.f;       // wavelength, m
};

// The wave's share of a surface sampled every `min_lambda / 2` metres:
// whole at two samples per half wave and more, gone at one.
inline float wave_weight(float lambda, float min_lambda) {
  if (min_lambda <= 0.f) return 1.f;
  return std::clamp(lambda / min_lambda - 1.f, 0.f, 1.f);
}

// The wind's waves: MAX_WAVES of them, longest first. Deterministic - the
// same settings give the same sea on every run and in every renderer.
inline int build(const Params &p, Wave out[MAX_WAVES]) {
  const float U = std::max(p.wind_speed, 0.05f);
  // Pierson-Moskowitz: the peak frequency of a sea fully developed under
  // wind U, and the wavelength that frequency has in deep water
  const float wp = 0.855f * GRAVITY / U;
  const float lp = TWO_PI * GRAVITY / (wp * wp);
  // shorter than a few centimetres a ripple is surface tension, not a wave
  const float lmin = 0.04f;
  const float lmax = std::max(lp * 2.5f, lmin * 8.f);
  const int n = MAX_WAVES;
  const float r = std::pow(lmin / lmax, 1.f / float(n - 1)); // < 1
  // each wave stands for a band of frequencies; omega goes as lambda^-1/2
  const float band = std::pow(r, -0.25f) - std::pow(r, 0.25f);
  const float alpha = 8.1e-3f;
  const float wind = p.wind_dir_deg * 0.017453293f;
  const float scale = std::max(p.scale, 1e-3f);
  for (int i = 0; i < n; ++i) {
    Wave &w = out[i];
    const float lambda = lmax * std::pow(r, float(i));
    const float k = TWO_PI / lambda;
    const float omega = std::sqrt(GRAVITY * k);
    const float x = wp / omega;
    const float S = alpha * GRAVITY * GRAVITY / std::pow(omega, 5.f) *
                    std::exp(-1.25f * x * x * x * x);
    const float amp = std::sqrt(2.f * S * omega * band);
    // the long waves run with the wind, the short ones scatter round it
    const float spread = 0.45f + 0.85f * float(i) / float(n - 1);
    const float jitter = std::fmod(float(i) * 0.6180339887f + 0.31f, 1.f) * 2.f - 1.f;
    const float theta = wind + jitter * spread;
    w.dx = std::cos(theta);
    w.dz = std::sin(theta);
    w.lambda = lambda * scale;
    w.k = TWO_PI / w.lambda;
    w.amp = amp * scale * std::max(p.height, 0.f);
    w.omega = std::sqrt(GRAVITY * w.k) * std::max(p.agitation, 0.f);
    w.phase = std::fmod(float(i) * 0.7548776662f + 0.1234f, 1.f) * TWO_PI;
    // Gerstner steepness: a crest sharpens as the water under it gathers
    // in; one wave may never fold over on itself
    const float ak = w.amp * w.k;
    w.q = std::min(std::clamp(p.choppiness, 0.f, 1.f) * 2.f, ak > 0.f ? 0.6f / ak : 0.f);
  }
  return n;
}

// Each wave's phase at `origin` (metres in the flat world) and time `t`,
// folded into 0..2pi in double: the shader adds k (d . x) for x relative to
// that origin and never sees a large number.
inline void rebase(const Wave *w, int n, double ox_m, double oz_m, double t,
                   float phase_out[]) {
  const double two_pi = 6.283185307179586;
  for (int i = 0; i < n; ++i) {
    double ph = double(w[i].phase) +
                double(w[i].k) * (double(w[i].dx) * ox_m + double(w[i].dz) * oz_m) -
                double(w[i].omega) * t;
    ph = std::fmod(ph, two_pi);
    if (ph < 0.0) ph += two_pi;
    phase_out[i] = float(ph);
  }
}

struct Sample {
  float disp[3] = {0.f, 0.f, 0.f}; // the surface point's move, metres
  float dpdx[3] = {1.f, 0.f, 0.f}; // the displaced surface's tangents
  float dpdz[3] = {0.f, 0.f, 1.f};
  float lost = 0.f;                // slope variance of the waves faded out
};

// The surface at a point given relative to the rebased origin, the way the
// shader computes it (WATER_WAVES_GLSL wv_eval), same order of operations.
inline void evaluate(const Wave *w, const float *phase, int n, float x_m, float z_m,
                     float min_lambda, Sample &s) {
  s = Sample{};
  for (int i = 0; i < n; ++i) {
    const float wt = wave_weight(w[i].lambda, min_lambda);
    const float ak0 = w[i].amp * w[i].k;
    s.lost += ak0 * ak0 * 0.5f * (1.f - wt * wt);
    if (wt <= 0.f) continue;
    const float a = w[i].amp * wt;
    const float th = w[i].k * (w[i].dx * x_m + w[i].dz * z_m) + phase[i];
    const float S = std::sin(th), C = std::cos(th);
    const float qa = w[i].q * a;
    s.disp[0] += qa * w[i].dx * C;
    s.disp[1] += a * S;
    s.disp[2] += qa * w[i].dz * C;
    const float ak = a * w[i].k, qak = qa * w[i].k;
    s.dpdx[0] -= qak * w[i].dx * w[i].dx * S;
    s.dpdx[1] += ak * w[i].dx * C;
    s.dpdx[2] -= qak * w[i].dz * w[i].dx * S;
    s.dpdz[0] -= qak * w[i].dx * w[i].dz * S;
    s.dpdz[1] += ak * w[i].dz * C;
    s.dpdz[2] -= qak * w[i].dz * w[i].dz * S;
  }
}

// The unit normal of the displaced surface, +y up in the flat frame.
inline void normal(const Sample &s, float out[3]) {
  const float *a = s.dpdz, *b = s.dpdx;
  float nx = a[1] * b[2] - a[2] * b[1];
  float ny = a[2] * b[0] - a[0] * b[2];
  float nz = a[0] * b[1] - a[1] * b[0];
  const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
  out[0] = nx / l; out[1] = ny / l; out[2] = nz / l;
}

// How much the surface is squeezed together here: 1 on calm water, toward
// 0 and below where a crest gathers the water in and breaks - whitecaps.
inline float jacobian(const Sample &s) {
  return s.dpdx[0] * s.dpdz[2] - s.dpdx[2] * s.dpdz[0];
}

// How much of the breaking the wind drives, 0..1. Whitecaps are a matter of
// wind, not of wave shape: the observed coverage (Monahan and O'Muircheartaigh,
// 3.84e-6 U^3.41) is a few hundredths of a percent at a 4 m/s breeze and
// a couple of percent by 12 m/s, where this reaches 1. A lake in a breeze
// therefore shows none, whatever its crests look like.
inline float whitecap_share(float wind_speed) {
  const float U = std::max(wind_speed, 0.f);
  const float ref = 3.84e-6f * std::pow(12.f, 3.41f);
  return std::clamp(3.84e-6f * std::pow(U, 3.41f) / ref, 0.f, 1.f);
}

// Significant wave height of the sea as built: four standard deviations of
// the surface's height, metres. What a sailor would call "the waves".
inline float significant_height(const Wave *w, int n) {
  float m0 = 0.f;
  for (int i = 0; i < n; ++i) m0 += w[i].amp * w[i].amp * 0.5f;
  return 4.f * std::sqrt(m0);
}

} // namespace gpx::water
