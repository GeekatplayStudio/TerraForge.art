// Geekatplay TerraForge - the last rung of the scattered-copy ladder: a
// card facing the camera, baked from the mesh itself.
//
// Past a few kilometres a tree is a handful of pixels, and drawing even a
// sixteenth of its triangles for those pixels is waste. Vue's ladder ends
// the same way (smooth, flat, box, billboard, none). The card is the mesh
// rasterised straight on with its own pictures and their cut-outs
// (studio/mesh_thumbnail.cpp, `mesh_raster`), so a pine reads as a pine
// and a bare rock as a rock.
//
// It turns about the world's up axis only - a cylindrical billboard. A card
// that also tips towards the camera looks right from the ground and lies
// down flat when the camera climbs, which is exactly when a distant forest
// is most visible.
#include "renderer_shaders.hpp"

namespace studio {

const char *const VS_BILLBOARD = R"GLSL(#version 430 core
// no vertex buffer: the four corners come from gl_VertexID
layout(location=2) in vec4 in_instance;      // x,y,z,scale
layout(location=3) in vec4 in_instance_rot;  // cos,sin,tint,phase
layout(location=4) in vec4 in_instance_axes; // per-axis scale, lean
uniform mat4 u_mvp, u_model;
uniform vec3 u_inst_base;   // the model matrix's own translation
uniform vec3 u_cam_right;   // the camera's right, world space
uniform vec3 u_card_centre; // model-space centre the card stands about
uniform vec2 u_card_size;   // world width and height of the card at scale 1
uniform float u_inst_grow;  // thinned cells grow their survivors
uniform float u_inst_sway, u_inst_time;
out vec2 v_uv;
out float v_tint;
out vec3 v_world;
void main(){
  vec4 I = in_instance;
  vec4 A = in_instance_axes;
  float g = max(u_inst_grow, 1.0);
  // the card's centre travels exactly as the mesh's would
  vec4 p = u_model * vec4(u_card_centre * (A.xyz * I.w * g), 1.0);
  p.xyz += I.xyz - u_inst_base;
  vec2 q = vec2((gl_VertexID & 1) == 0 ? -0.5 : 0.5,
                (gl_VertexID & 2) == 0 ? -0.5 : 0.5);
  float w = u_card_size.x * A.x * I.w * g;
  float h = u_card_size.y * A.y * I.w * g;
  vec3 right = vec3(u_cam_right.x, 0.0, u_cam_right.z);
  float rl = length(right);
  right = rl > 1e-6 ? right / rl : vec3(1.0, 0.0, 0.0);
  vec3 world = p.xyz + right * (q.x * w) + vec3(0.0, q.y * h, 0.0);
  if (u_inst_sway > 0.0) {
    // the same lean the geometry has, so a copy does not jump as it
    // crosses the distance where it becomes a card
    float ph = u_inst_time * 1.7 + I.x * 37.0 + I.z * 53.0 + in_instance_rot.w * 6.2831853;
    float lean = sin(ph) * u_inst_sway * max(world.y - I.y, 0.0);
    world.x += lean;
    world.z += lean * 0.35;
  }
  v_uv = vec2(q.x + 0.5, 0.5 - q.y); // the card is stored top row first
  v_tint = in_instance_rot.z;
  v_world = world;
  gl_Position = u_mvp * vec4(world, 1.0);
})GLSL";

const char *const FS_BILLBOARD = R"GLSL(#version 430 core
in vec2 v_uv;
in float v_tint;
in vec3 v_world;
uniform sampler2D u_card;
uniform vec3 u_cam;
uniform vec3 u_sun, u_sun_color;
uniform float u_hscale;
uniform float u_exposure;
uniform vec3 u_grade;
uniform float u_sat;
// u_aov and u_object_id come from the injected pass prelude
FOG_FN_PLACEHOLDER
out vec4 frag;
void main(){
  vec4 c = texture(u_card, v_uv);
  if (c.a < 0.5) discard; // the mesh's own silhouette, cut out
  // the render passes want geometry, not a picture of it: a card writes the
  // beauty only, and the passes see the copies that are near enough to be
  // real geometry
  if (u_aov != 0) discard;
  vec3 col = c.rgb * v_tint;
  float fog_f; vec3 fog_c;
  float dist = length(v_world - u_cam);
  fog_terms(v_world, u_cam, dist, u_hscale, u_sun, u_sun_color, fog_f, fog_c);
  col = apply_fog_terms(col, fog_f, fog_c);
  col *= u_exposure;
  col = pow(max(col, 0.0), vec3(1.0 / 2.2));
  float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
  col = mix(vec3(l), col, u_sat) * u_grade;
  frag = vec4(col, 1.0);
})GLSL";

} // namespace studio
