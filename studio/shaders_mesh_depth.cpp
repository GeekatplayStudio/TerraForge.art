// Geekatplay TerraForge - the deformer GLSL every mesh program shares, and
// the mesh depth shader for the sun's shadow map. Split from
// shaders_scene.cpp for the 500-line module rule.
#include "renderer_shaders.hpp"

namespace studio {

// The deformers in GLSL, shared by the colour and the shadow pass through
// DEFORM_FN_PLACEHOLDER (inject_sky) so a bent tree shadows as a bent tree.
const char *const DEFORM_FN_GLSL = R"GLSL(
// deformers in the object's own space: the GLSL twin of gpx/deform.hpp
uniform int u_def_on, u_def_bend_axis;
uniform vec3 u_def_twist, u_def_shear, u_bmin, u_bmax;
uniform float u_def_bend, u_def_taper;
float def_frac(float x, float lo, float hi) { float d = hi - lo; return d > 1e-9 ? (x - lo) / d : 0.0; }
vec3 def_rotate(vec3 p, int axis, float rad, vec3 pivot) {
  int u = (axis + 1) % 3, v = (axis + 2) % 3;
  float c = cos(rad), s = sin(rad);
  float a = p[u] - pivot[u], b = p[v] - pivot[v];
  p[u] = pivot[u] + a * c - b * s;
  p[v] = pivot[v] + a * s + b * c;
  return p;
}
vec3 deform(vec3 p) {
  vec3 centre = (u_bmin + u_bmax) * 0.5;
  float ty = def_frac(p.y, u_bmin.y, u_bmax.y);
  float tx = def_frac(p.x, u_bmin.x, u_bmax.x);
  if (u_def_taper != 0.0) {
    float k = max(1.0 + u_def_taper * ty, 0.0);
    p.x = centre.x + (p.x - centre.x) * k;
    p.z = centre.z + (p.z - centre.z) * k;
  }
  p.x += u_def_shear.x * ty * (u_bmax.x - u_bmin.x);
  p.z += u_def_shear.z * ty * (u_bmax.z - u_bmin.z);
  p.y += u_def_shear.y * tx * (u_bmax.y - u_bmin.y);
  for (int a = 0; a < 3; ++a)
    if (u_def_twist[a] != 0.0) {
      float t = def_frac(p[a], u_bmin[a], u_bmax[a]);
      p = def_rotate(p, a, radians(u_def_twist[a]) * t, centre);
    }
  if (u_def_bend != 0.0) {
    int up = u_def_bend_axis == 1 ? 0 : 1;
    float t = def_frac(p[up], u_bmin[up], u_bmax[up]);
    vec3 pivot = centre;
    pivot[up] = u_bmin[up];
    p = def_rotate(p, u_def_bend_axis, radians(u_def_bend) * t, pivot);
  }
  return p;
}
)GLSL";

// Meshes in the sun's shadow map: the same placement, deformers and
// instance stream as VS_MESH, projected by the light.
const char *const VS_DEPTH_MESH = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
layout(location=2) in vec4 in_instance;
layout(location=3) in vec4 in_instance_rot;
uniform mat4 u_light_mvp, u_model;
uniform int u_inst_on;
uniform float u_inst_sway, u_inst_time;
uniform vec3 u_inst_base;
DEFORM_FN_PLACEHOLDER
void main(){
  vec3 pos = in_pos;
  if (u_def_on == 1) pos = deform(pos);
  vec4 p;
  if (u_inst_on == 1) {
    vec4 I = in_instance;
    vec2 r = in_instance_rot.xy;
    pos = vec3(pos.x*r.x - pos.z*r.y, pos.y, pos.x*r.y + pos.z*r.x) * I.w;
    p = u_model * vec4(pos, 1.0);
    p.xyz += I.xyz - u_inst_base;
    if (u_inst_sway > 0.0) {
      float ph = u_inst_time * 1.7 + I.x * 37.0 + I.z * 53.0;
      float lean = sin(ph) * u_inst_sway * max(p.y - I.y, 0.0);
      p.x += lean;
      p.z += lean * 0.35;
    }
  } else p = u_model * vec4(pos, 1.0);
  gl_Position = u_light_mvp * p;
})GLSL";

} // namespace studio
