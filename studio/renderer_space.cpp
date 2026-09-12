// Geekatplay TerraForge - deep space uniforms (renderer_space.hpp): the
// backdrop's own settings and the star field from SpaceSettings, the milky
// band's frame worked out from its pole, and the nebulas from the scene's
// Nebula objects. The noise volume the nebula march samples is bound here
// too, on unit 10.
#include "uniform_cache.hpp"
#include "renderer_space.hpp"
#include "cloud_noise.hpp"
#include "perf.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "space_noise.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

namespace studio {

void nebula_direction(float azimuth_deg, float elevation_deg, float out[3]) {
  const float az = azimuth_deg * 0.017453293f, el = elevation_deg * 0.017453293f;
  out[0] = std::cos(el) * std::sin(az);
  out[1] = std::sin(el);
  out[2] = std::cos(el) * std::cos(az);
}

int space_march_steps(int quality) {
  switch (std::clamp(quality, 0, 3)) {
    case 0: return 12;
    case 2: return 40;
    case 3: return 72;
    default: return 22;
  }
}

int space_view_steps(bool finished) {
  const SpaceSettings &sp = render_settings().space;
  return space_march_steps(finished ? sp.quality + 1
                                    : std::min(sp.quality, perf_quality().space_quality_cap));
}

void upload_space_uniforms(unsigned prog, bool finished) {
  const RenderSettings &rs = render_settings();
  const SpaceSettings &sp = rs.space;
  auto u1 = [&](const char *n, float v) { glUniform1f(uniform_location(prog, n), v); };
  auto ui = [&](const char *n, int v) { glUniform1i(uniform_location(prog, n), v); };
  auto u3 = [&](const char *n, const float *v) { glUniform3fv(uniform_location(prog, n), 1, v); };
  // the backdrop as a whole
  ui("u_sp_on", sp.on ? 1 : 0);
  u1("u_sp_bright", sp.brightness);
  u1("u_sp_realism", sp.realism);
  u1("u_sp_glow", sp.glow);
  // under the governor's cap, as the clouds are: a nebula filling the frame is
  // a march per pixel too. The finished picture takes a step finer instead.
  ui("u_sp_steps", space_view_steps(finished));
  // the stars
  ui("u_sp_stars", sp.stars ? 1 : 0);
  ui("u_sp_star_seed", sp.star_seed);
  u1("u_sp_star_density", sp.star_density);
  u1("u_sp_star_bright", sp.star_brightness);
  u1("u_sp_star_size", sp.star_size);
  u1("u_sp_star_temp", sp.star_temperature);
  u1("u_sp_star_spikes", sp.star_spikes);
  u1("u_sp_star_halo", sp.star_halo);
  u1("u_sp_star_clump", sp.star_clump);
  ui("u_sp_spike_points", std::clamp(sp.star_spike_points, 4, 8));
  u1("u_sp_spike_angle", sp.star_spike_angle * 0.017453293f);
  u1("u_sp_spike_chroma", std::clamp(sp.star_spike_chroma, 0.f, 1.f));
  u1("u_sp_star_sat", std::clamp(sp.star_saturation, 0.f, 2.f));
  u1("u_sp_star_glow", std::max(sp.star_glow, 0.f));
  u1("u_sp_mag_slope", 14.f - 10.f * std::clamp(sp.star_bright_share, 0.f, 1.f));
  u1("u_sp_clusters", std::clamp(sp.star_clusters, 0.f, 1.f));
  u1("u_sp_cluster_size", std::clamp(sp.star_cluster_size, 0.2f, 5.f));
  // the volume the nebula march samples (space_noise.hpp), and its dither
  glActiveTexture(GL_TEXTURE10);
  glBindTexture(GL_TEXTURE_3D, space_noise_texture());
  ui("u_sp_vol", 10);
  glActiveTexture(GL_TEXTURE9);
  glBindTexture(GL_TEXTURE_2D, blue_noise_texture());
  ui("u_sp_blue", 9);
  glActiveTexture(GL_TEXTURE0);
  // the galaxy band: its pole from a heading and an elevation, a direction
  // in its plane turned round the pole to where the core is
  {
    float ax[3];
    nebula_direction(sp.galaxy_yaw, sp.galaxy_pitch, ax);
    const float yaw = sp.galaxy_yaw * 0.017453293f;
    float z0[3] = {std::cos(yaw), 0.f, -std::sin(yaw)}; // horizontal, perpendicular to the pole
    const float core = sp.galaxy_core * 0.017453293f;
    const float c = std::cos(core), s = std::sin(core);
    const float cr[3] = {ax[1] * z0[2] - ax[2] * z0[1], ax[2] * z0[0] - ax[0] * z0[2],
                         ax[0] * z0[1] - ax[1] * z0[0]};
    float zero[3];
    for (int k = 0; k < 3; ++k) zero[k] = z0[k] * c + cr[k] * s;
    ui("u_sp_gal", sp.galaxy_on ? 1 : 0);
    ui("u_sp_gal_seed", sp.galaxy_seed);
    u1("u_sp_gal_int", sp.galaxy_intensity);
    u1("u_sp_gal_width", sp.galaxy_width);
    u1("u_sp_gal_dust", sp.galaxy_dust);
    u1("u_sp_gal_grain", sp.galaxy_grain);
    u3("u_sp_gal_axis", ax);
    u3("u_sp_gal_zero", zero);
    u3("u_sp_gal_color", sp.galaxy_color);
  }
  // the nebulas: every visible Nebula object, eight at most
  {
    float d[8][4] = {}, a[8][4] = {}, b[8][4] = {}, e[8][4] = {}, fl[8][4] = {};
    float c1[8][3] = {}, c2[8][3] = {}, c3[8][3] = {};
    int n = 0;
    const SceneState &sc = scene();
    for (const SceneObject &o : sc.objects) {
      if (o.type != SceneObject::Nebula || n >= 8) continue;
      if (!sc.object_visible(o)) continue;
      const NebulaData &N = o.nebula;
      nebula_direction(N.azimuth, N.elevation, d[n]);
      d[n][3] = std::max(N.size_deg, 0.1f) * 0.5f * 0.017453293f;
      a[n][0] = (float)N.type;
      a[n][1] = (float)(N.seed % 10007u);
      a[n][2] = N.brightness;
      a[n][3] = N.density;
      b[n][0] = N.tilt_deg * 0.017453293f;
      b[n][1] = N.rotation_deg * 0.017453293f;
      b[n][2] = std::clamp(N.detail, 0.f, 1.f);
      b[n][3] = (float)std::clamp(N.arms, 1, 6);
      // A dark nebula is the same march with nothing glowing in it, so its
      // Density - the only dial that means anything for one - is what fills
      // it with dust.
      e[n][0] = N.type == 1 ? std::max(N.dust, 0.35f + N.density * 0.65f)
                            : std::clamp(N.dust, 0.f, 1.f);
      e[n][1] = std::clamp(N.warp, 0.f, 1.5f);
      e[n][2] = std::max(N.glow, 0.f);
      e[n][3] = (float)std::clamp(N.sources, 1, 4);
      // the finer look (NebulaData): turbulence, lanes, core glow, and how
      // bright the hot stars are drawn
      fl[n][0] = std::clamp(N.turbulence, 0.f, 1.f);
      fl[n][1] = std::clamp(N.lanes, 0.f, 1.f);
      fl[n][2] = std::max(N.core_glow, 0.f);
      fl[n][3] = std::max(N.source_stars, 0.f);
      for (int k = 0; k < 3; ++k) {
        c1[n][k] = N.color1[k];
        c2[n][k] = N.color2[k];
        // an outskirts colour never set is the cool gas's own
        c3[n][k] = N.color3[0] >= 0.f ? N.color3[k] : N.color2[k];
      }
      ++n;
    }
    ui("u_neb_n", n);
    if (n) {
      glUniform4fv(uniform_location(prog, "u_neb_d"), n, &d[0][0]);
      glUniform4fv(uniform_location(prog, "u_neb_a"), n, &a[0][0]);
      glUniform4fv(uniform_location(prog, "u_neb_b"), n, &b[0][0]);
      glUniform4fv(uniform_location(prog, "u_neb_e"), n, &e[0][0]);
      glUniform3fv(uniform_location(prog, "u_neb_c1"), n, &c1[0][0]);
      glUniform3fv(uniform_location(prog, "u_neb_c2"), n, &c2[0][0]);
      glUniform4fv(uniform_location(prog, "u_neb_f"), n, &fl[0][0]);
      glUniform3fv(uniform_location(prog, "u_neb_c3"), n, &c3[0][0]);
    }
  }
}

} // namespace studio
