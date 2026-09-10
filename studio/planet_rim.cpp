// Geekatplay TerraForge - the rim of a thick world (world_shape.hpp).
//
// A world with a thickness is a body. A ring world has its ground on the
// inside and its other face the thickness further from the axis, and at
// its two edges a wall the thickness tall runs the whole way round; a flat
// world has its underside the thickness below and, round its disc or its
// square, a wall from the ground down to it. The two faces are the
// surround, drawn once per face (planet_renderer.cpp infinite_draw); this
// is the wall between them.
//
// The wall's top follows the ground's own relief at the edge - the same
// layers the surround and the far shell build it from, at the same level,
// never below the water - so it meets the ground and not a step; its
// bottom is the other face's level. It is bare rock in strata, lit by the
// same sun (a body inside the world shines on it from the axis), fogged by
// the same air and writing the same pass outputs as the ground it belongs
// to. One program, the built-in layers; a field graph shaping the surround
// is not applied to the wall's top.
#include "uniform_cache.hpp"
#include "console.hpp"
#include "planet_renderer.hpp"
#include "render_settings.hpp"
#include "world_shape.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <string>

namespace studio {

extern int g_aov; // renderer_aov.cpp: which render pass is being drawn
// planet_renderer.cpp: the splice, the link and the layer upload the
// surround itself uses, so the wall is built from the very same functions
std::string pl_inject(const char *src, const std::string &glsl);
unsigned pl_link_checked(const char *vs, const char *fs, const std::string &glsl,
                         std::string &err);
int upload_layers(unsigned prog, int planet_idx, float amp_scale, float field_strength,
                  int side);
void upload_fog_uniforms(unsigned prog, const RenderSettings &RS, bool atmosphere);

namespace {

const char *VS_RIM = R"GLSL(#version 430 core
layout(location=0) in vec2 in_p; // -1..1: x along the edge, y down the wall
uniform mat4 u_mvp;
uniform vec3 u_cam;
uniform float u_curve, u_shell_w, u_amp, u_base, u_wl;
uniform int u_shell;    // 1: past the surround the relief is the far shell's
uniform int u_rim_kind; // 0 and 1 a ring's two rims, 2 a disc's edge, 3 a square's
PL_FN_PLACEHOLDER
PL_SPHERE_PLACEHOLDER
PL_SHELL_PLACEHOLDER
out vec3 v_world;
out vec3 v_n;
out vec2 v_uv;
out float v_down;
void main(){
  float d = in_p.y * 0.5 + 0.5; // 0 at the ground, 1 at the other face
  vec2 uv; vec3 n;
  if (u_rim_kind <= 1){
    // the whole way round along x, at the ring's rim north or south
    float R = max(u_curve, 1e-6);
    float side = u_rim_kind == 0 ? 1.0 : -1.0;
    uv = vec2(0.5 + in_p.x * 3.14159265 * R, 0.5 + side * u_shell_w * 0.5);
    n = vec3(0.0, 0.0, side);
  } else if (u_rim_kind == 2){
    float a = in_p.x * 3.14159265, r = u_shell_w * 0.5;
    uv = vec2(0.5 + r * cos(a), 0.5 + r * sin(a));
    n = vec3(cos(a), 0.0, sin(a));
  } else {
    // four sides, a quarter of the grid each, corners on grid lines
    float t = clamp((in_p.x * 0.5 + 0.5) * 4.0, 0.0, 3.9999);
    int s = int(floor(t));
    float f = t - float(s), hw = u_shell_w * 0.5, w = u_shell_w;
    if (s == 0){ uv = vec2(0.5 - hw + f * w, 0.5 - hw); n = vec3(0.0, 0.0, -1.0); }
    else if (s == 1){ uv = vec2(0.5 + hw, 0.5 - hw + f * w); n = vec3(1.0, 0.0, 0.0); }
    else if (s == 2){ uv = vec2(0.5 + hw - f * w, 0.5 + hw); n = vec3(0.0, 0.0, 1.0); }
    else { uv = vec2(0.5 - hw, 0.5 + hw - f * w); n = vec3(-1.0, 0.0, 0.0); }
  }
  // the top is the ground's own level at the edge, never below the water;
  // the bottom is the other face
  float top = max(pl_relief_w(uv, 4.0).x * u_amp + u_base, u_wl);
  float h = mix(top, -u_world_thick, d);
  vec3 p = pl_sphere_place(uv, h, u_curve);
  vec3 east, up, north;
  pl_sphere_frame(uv, u_curve, east, up, north);
  v_n = normalize(east * n.x + up * n.y + north * n.z);
  v_world = p; v_uv = uv; v_down = d;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

const char *FS_RIM = R"GLSL(#version 430 core
in vec3 v_world;
in vec3 v_n;
in vec2 v_uv;
in float v_down;
out vec4 frag;
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_sun_i, u_ambient, u_exposure, u_sat, u_hscale, u_lat, u_curve;
uniform vec3 u_grade;
uniform int u_sun_mode, u_textured;
PL_FN_PLACEHOLDER
PL_PALETTE_PLACEHOLDER
PL_SPHERE_PLACEHOLDER
FOG_FN_PLACEHOLDER
SKY_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  vec3 N = normalize(v_n);
  // a sun inside the world shines on the wall from the axis or the centre
  vec3 sun = u_sun_mode == 1 ? pl_world_up_at(v_world, u_curve) : u_sun;
  float cam_d = max(length(u_cam - v_world), 0.02);
  // bare rock in strata down the wall: the palette's cliff, banded
  float band = pl_vnoise(vec3(v_uv.x * 3.0, v_down * 40.0, v_uv.y * 3.0), 0x7a3bu);
  vec3 alb = pl_palette(0.5 + 0.3 * band, 1.0, u_lat, 0.0, 2.0, band);
  if (u_textured == 0) alb = vec3(0.58, 0.57, 0.55);
  float NdL = max(dot(N, sun), 0.0);
  float sun_elev = u_sun_mode == 1 ? 1.0 : sun.y;
  float day_f = clamp(sun_elev * 4.0 + 0.35, 0.035, 1.0);
  vec3 direct = alb * u_sun_color * u_sun_i * NdL * 0.92 / 3.14159;
  vec3 ambient = alb * mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient * 0.6 * day_f;
  vec3 col = direct + ambient;
  float fog_f; vec3 fog_c;
  fog_terms(v_world, u_cam, cam_d, u_hscale, u_sun_mode == 1 ? vec3(0.0, 1.0, 0.0) : sun,
            u_sun_color, fog_f, fog_c);
  if (u_aov != 0){
    frag = aov_out(u_aov, cam_d, N, alb, v_world, float(u_object_id), direct, 1.0, ambient,
                   vec3(0.0), fog_f, fog_c, 0.0, col);
    return;
  }
  col = apply_fog_terms(col, fog_f, fog_c);
  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

GLuint g_prog = 0;
bool g_tried = false;

void puni1(GLuint p, const char *n, float v) { glUniform1f(uniform_location(p, n), v); }
void puni3(GLuint p, const char *n, const float *v) { glUniform3fv(uniform_location(p, n), 1, v); }
void punii(GLuint p, const char *n, int v) { glUniform1i(uniform_location(p, n), v); }

} // namespace

void planet_rim_draw(const InfiniteFrame &f, unsigned grid_vao, int grid_count) {
  const RenderSettings &rs = render_settings();
  if (!world_has_body(rs)) return;
  if (!g_prog && !g_tried) {
    g_tried = true;
    std::string err;
    g_prog = pl_link_checked(VS_RIM, FS_RIM, "", err);
    if (!g_prog) log_error("shader", "world rim program: " + err);
  }
  if (!g_prog) return;
  const bool flat = world_is_flat(rs);
  const gpx::planet::Shape S = world_shape(rs); // the world's own face
  glUseProgram(g_prog);
  glUniformMatrix4fv(uniform_location(g_prog, "u_mvp"), 1, GL_FALSE, f.mvp);
  upload_world_shape(g_prog, S);
  punii(g_prog, "u_aov", g_aov);
  punii(g_prog, "u_object_id", 1); // the ground's id: the wall is the world
  punii(g_prog, "u_textured", f.textured ? 1 : 0);
  puni3(g_prog, "u_cam", f.eye);
  puni3(g_prog, "u_sun", f.sun);
  puni3(g_prog, "u_sun_color", f.sun_color);
  puni1(g_prog, "u_sun_i", f.sun_intensity);
  puni1(g_prog, "u_ambient", f.ambient);
  puni3(g_prog, "u_sky_zenith", f.sky_zenith);
  puni3(g_prog, "u_sky_horizon", f.sky_horizon);
  puni1(g_prog, "u_exposure", f.exposure);
  puni3(g_prog, "u_grade", f.grade);
  puni1(g_prog, "u_sat", f.saturation);
  puni1(g_prog, "u_hscale", f.height_scale);
  puni1(g_prog, "u_curve", f.planet_radius);
  puni1(g_prog, "u_lat", f.latitude);
  upload_fog_uniforms(g_prog, rs, f.atmosphere);
  // the ground's own relief and level at the edge, the surround's numbers
  const float amp = f.height_scale * 1.2f;
  upload_layers(g_prog, -1, amp / std::max(f.height_scale, 1e-4f), 0.f,
                S.inside ? SIDE_INSIDE : SIDE_OUTSIDE);
  puni1(g_prog, "u_amp", amp);
  puni1(g_prog, "u_base", f.base_height);
  puni1(g_prog, "u_wl", f.water_level);
  puni1(g_prog, "u_shell_w", rs.world_width);
  punii(g_prog, "u_shell", flat ? 0 : 1);
  punii(g_prog, "u_sun_mode", (rs.world_sun_inside && S.inside) ? 1 : 0);
  glBindVertexArray(grid_vao);
  if (flat) {
    punii(g_prog, "u_rim_kind", rs.world_outline == OUTLINE_SQUARE ? 3 : 2);
    glDrawElements(GL_TRIANGLES, grid_count, GL_UNSIGNED_INT, nullptr);
  } else {
    for (int kind : {0, 1}) {
      punii(g_prog, "u_rim_kind", kind);
      glDrawElements(GL_TRIANGLES, grid_count, GL_UNSIGNED_INT, nullptr);
    }
  }
}

} // namespace studio
