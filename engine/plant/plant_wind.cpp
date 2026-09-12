// Geekatplay TerraForge - wind weights and the wind displacement, CPU and
// GLSL (gpx/plant.hpp, plant_internal.hpp).
//
// A plant does not simulate its wind; it is baked with four numbers per
// vertex (phase, bend, flutter, height) and a small function moves the
// vertex at time t. The studio splices the GLSL below into the mesh vertex
// shader and the bake/export runs the CPU twin. The two must be the same
// arithmetic line for line, or a rendered plant leans differently from
// the one exported at the same second - so plant_wind_vertex is written
// in the GLSL's order with the GLSL's constants, and the string is kept
// beside it. Change one, change the other.
//
// The bend weight grows along the wood as a cantilever does, and how much
// of it there is comes from how thick the wood is: the slenderness L/r,
// cubed, which is what a beam's deflection under a wind load actually goes
// as (see wind_bend_at). It is clamped at 4 so a hair-thin twig cannot fly
// off, and the slenderness itself at 120, past which a branch is a whip.
//
// The phase is where the gust has got to: it travels out from the foot
// along the wood, so a vertex's phase is a function of how far it stands
// from the root through the plant and nothing else. That is what holds the
// plant together. The displacement at a point depends on its phase, so two
// vertices in the same place with different phases move apart - and a leaf
// drawing its own phase, which is what this used to do, swung out of step
// with the twig it is nailed to and appeared to float free of it, jittering.
// Distance is continuous across every joint (a child starts at the distance
// its socket sits at), so the parent and everything growing on it agree
// exactly, while two branches whose paths differ still sway independently -
// which was the point of drawing a phase in the first place.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

// The CPU twin. Keep in step with plant_wind_glsl() below.
void plant_wind_vertex(const PlantWind &w, float t, const float pos[3], const float wind4[4],
                       float height_m, float out[3]) {
  const float phase = wind4[0], bend = wind4[1], flutter_w = wind4[2], hf = wind4[3];
  const float gust = 1.f + w.gust * (0.5f + 0.5f * std::sin(t * w.gust_frequency * 6.2831853f + phase * 3.f));
  const float sway = w.strength * gust *
                     (0.6f * std::sin(t * w.breeze_speed * 1.3f + phase * 6.283f) +
                      0.4f * std::sin(t * w.breeze_speed * 2.9f + phase * 12.566f)) * w.breeze;
  const float lean = w.strength * w.strength * 0.15f;
  const float amount = (lean * hf * hf + sway * bend * 0.05f) * height_m * (bend > 0.f ? 1.f : 0.f);
  const float dx = w.dir[0] * amount, dz = w.dir[1] * amount;
  const float f = flutter_w * w.flutter * w.strength *
                  std::sin(t * w.flutter_speed * 6.283f + phase * 20.f + pos[0] * 7.f + pos[2] * 5.f) * 0.02f *
                  height_m;
  out[0] = pos[0] + dx + w.dir[1] * f;
  out[1] = pos[1] + 0.4f * f;
  out[2] = pos[2] + dz - w.dir[0] * f;
}

const char *plant_wind_glsl() {
  // The same function as plant_wind_vertex, line for line. u_pw_a =
  // (strength, dir.x, dir.z, breeze), u_pw_b = (breeze_speed, flutter,
  // flutter_speed, gust), u_pw_c = (gust_frequency, 0, 0, 0).
  return R"GLSL(
vec3 plant_wind(vec3 pos, vec4 w, float t, float height, vec4 u_pw_a, vec4 u_pw_b, vec4 u_pw_c) {
  float phase = w.x, bend = w.y, flutter_w = w.z, hf = w.w;
  float strength = u_pw_a.x, breeze = u_pw_a.w;
  vec2 dir = vec2(u_pw_a.y, u_pw_a.z);
  float breeze_speed = u_pw_b.x, flutter = u_pw_b.y, flutter_speed = u_pw_b.z, gust_amp = u_pw_b.w;
  float gust_frequency = u_pw_c.x;
  float gust = 1.0 + gust_amp * (0.5 + 0.5 * sin(t * gust_frequency * 6.2831853 + phase * 3.0));
  float sway = strength * gust *
               (0.6 * sin(t * breeze_speed * 1.3 + phase * 6.283) +
                0.4 * sin(t * breeze_speed * 2.9 + phase * 12.566)) * breeze;
  float lean = strength * strength * 0.15;
  float amount = (lean * hf * hf + sway * bend * 0.05) * height * (bend > 0.0 ? 1.0 : 0.0);
  float dx = dir.x * amount, dz = dir.y * amount;
  float f = flutter_w * flutter * strength *
            sin(t * flutter_speed * 6.283 + phase * 20.0 + pos.x * 7.0 + pos.z * 5.0) * 0.02 * height;
  return vec3(pos.x + dx + dir.y * f, pos.y + 0.4 * f, pos.z + dz - dir.x * f);
}
)GLSL";
}

void plant_wind_apply(const PlantMesh &m, const PlantWind &w, float t, std::vector<float> &pos_out) {
  pos_out.resize(m.pos.size());
  const size_t n = m.vertex_count();
  for (size_t i = 0; i < n; ++i) {
    const float *p = &m.pos[i * 3];
    float w4[4] = {0, 0, 0, 0};
    if (m.wind.size() >= (i + 1) * 4)
      for (int c = 0; c < 4; ++c) w4[c] = m.wind[i * 4 + (size_t)c];
    plant_wind_vertex(w, t, p, w4, m.height_m, &pos_out[i * 3]);
  }
}

namespace plant {

// One turn of phase every WAVE_M metres of wood: a 20 m tree carries about
// a turn and a half from its foot to its tips, so the crown never moves as
// one slab, and two parts a hand's breadth apart are within a percent of a
// cycle of each other.
static const float WAVE_M = 13.f;

float wind_phase_at(const BuildCtx &ctx, float dist_m) {
  const float p = ctx.wind_phase_base + dist_m / WAVE_M;
  return p - std::floor(p);
}

float wind_bend_at(const BuildCtx &ctx, const Instance &parent, float primal, float flexibility) {
  if (parent.length <= 0.f) return parent.wind_bend;
  const Node *r = ctx.root;
  const float boost_thin = r ? r->attrs.get_f("boost_thin", 0.5f) : 0.5f;
  const float boost_long = r ? r->attrs.get_f("boost_long", 0.5f) : 0.5f;
  const float influence = r ? r->attrs.get_f("seg_wind_influence", 1.f) : 1.f;
  const float radius = std::max(parent.radius, 1e-4f);
  const float flex = flexibility * (1.f + boost_thin * (0.1f / std::max(radius, 0.01f))) *
                     (1.f + boost_long * parent.length / 5.f) * influence;

  // A branch bends like a beam clamped at one end, and how far it bends
  // follows from how thick it is.
  //
  // Its stiffness is EI with I = pi r^4 / 4, and the wind's push along it
  // goes with the area it presents, which for a round branch is its
  // diameter: w ~ r. A uniformly loaded cantilever's tip moves by
  // w L^4 / (8 EI), so how far it swings compared with its own length goes
  // as (L/r)^3 - its slenderness, cubed. That is what flexibility from
  // thickness means: it is why a twig whips in air that leaves the trunk
  // holding it standing still, and it falls out of the geometry rather than
  // being dialled in. Past a slenderness of about a hundred a branch is a
  // whip and bends as far as it is going to, so the ratio is held there -
  // the alternative is a number that runs to six figures on the thinnest
  // twigs and pins everything above a branch at the clamp.
  const float slender = std::min(parent.length / radius, 120.f);
  const float rel = slender / 40.f; // 40 is an ordinary branch
  const float amount = rel * rel * rel * 0.074f;

  // Along the branch, the cantilever's own shape: 1 at the tip, and - the
  // part that matters at a joint - leaving the base with no slope at all.
  // The p^1.5 this used before rose vertically out of the joint, so a branch
  // appeared to shear away from the parent at the very point it is fixed to.
  const float p = clampf(primal, 0.f, 1.f);
  const float shape = p * p * (6.f - 4.f * p + p * p) / 3.f;

  const float b = parent.wind_bend + flex * amount * shape;
  return std::min(b, 4.f);
}

PlantWind plant_wind_from_root(const Node &root) {
  PlantWind w;
  const AttrSet &a = root.attrs;
  if (!a.get_b("receive_wind", true)) return w;
  w.strength = a.get_f("wind_strength", 0.3f);
  const float d = a.get_f("wind_direction", 0.f) * DEG;
  w.dir[0] = std::cos(d);
  w.dir[1] = std::sin(d);
  w.breeze = a.get_f("seg_breeze_influence", 0.5f);
  w.breeze_speed = a.get_f("seg_breeze_speed", 1.f);
  w.breeze_randomness = a.get_f("seg_breeze_randomness", 0.5f);
  w.flutter = a.get_f("leaf_flutter_influence", 0.5f);
  w.flutter_speed = a.get_f("leaf_flutter_speed", 3.f);
  w.gust = a.get_f("gust_amplitude", 0.3f);
  w.gust_frequency = a.get_f("gust_frequency", 0.15f);
  return w;
}

} // namespace plant
} // namespace gpx
