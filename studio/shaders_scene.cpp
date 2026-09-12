// Geekatplay TerraForge — mesh, gizmo and helper shaders (sky lives in shaders_sky.cpp)
#include "renderer_shaders.hpp"

namespace studio {

// The shadow pass has to see the same surface the camera does. It always
// ignored the fractal micro-relief, which is fine — that detail is far below
// the shadow map's resolution. A graph displacement is not: it can move the
// surface by a large fraction of the terrain's whole height, and terrain that
// casts a shadow from where it used to be looks broken rather than subtle.
//
// The detail budget is fixed rather than camera-derived, because there is no
// camera here — the shadow map is rendered from the sun.
const char *const VS_DEPTH_SRC = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
uniform sampler2D u_height;
uniform mat4 u_light_mvp;
uniform float u_hscale;
uniform float u_field_strength;
uniform float u_height_lod_k;
uniform vec3 u_lod_cam;
uniform float u_lod_ground;
HEIGHT_SMOOTH_PLACEHOLDER
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
TILE_XFORM_PLACEHOLDER
out vec2 v_uv;
void main(){
  // the surface the view draws: the same smooth relief at the same level
  // (HEIGHT_SMOOTH_FN), or rims cast shadows onto ground that is not there
  float h = height_smooth(in_uv, relief_lod(in_uv)) * u_hscale;
  vec3 p = vec3(in_uv.x, h, in_uv.y);
  if (u_field_strength != 0.0)
    p.y += gpx_terrain_field(p, vec3(0.0,1.0,0.0), h, 1.0, 0.0, 0.0, 7.0).x *
           u_field_strength;
  p = tile_xform(p); // the same transform the view draws (terrain_xform.hpp)
  v_uv = in_uv;
  gl_Position = u_light_mvp * vec4(p, 1.0);
})GLSL";

const char *const FS_DEPTH = R"GLSL(#version 430 core
void main(){})GLSL";

// The terrain's shadow: a tile cut to an outline casts the outline's shadow.
const char *const FS_DEPTH_TERRAIN = R"GLSL(#version 430 core
in vec2 v_uv;
TILE_XFORM_FS_PLACEHOLDER
void main(){ if (tile_cut(v_uv)) discard; })GLSL";

// The water's shaders live in shaders_water.cpp.

// The model matrix is built on the CPU (scene_object_matrix) so that the
// renderer, picking and the selection outline all read one definition of
// where an object is. u_nrm is R*S^-1, so a squeezed object still shades
// correctly.
// One copy of a scattered mesh, in the model's frame: sized per axis,
// turned about its up axis, then leaned from vertical towards the ground's
// normal by the instance's lean (0 a tree, 1 a stone lying on the slope).
// Shared by the colour and the shadow pass through INSTANCE_FN_PLACEHOLDER,
// so a copy shadows exactly where it stands. The normal is turned the same
// way (a scale does not change it up to normalisation).
const char *const INSTANCE_FN = R"GLSL(
uniform float u_inst_grow; // far cells are thinned; the survivors grow to cover
vec4 instance_place(inout vec3 pos, inout vec3 nrm, vec4 I, vec4 R, vec4 A, vec4 G, mat4 model) {
  vec2 r = R.xy;
  pos = vec3(pos.x*r.x - pos.z*r.y, pos.y, pos.x*r.y + pos.z*r.x) * (A.xyz * I.w * max(u_inst_grow, 1.0));
  nrm = vec3(nrm.x*r.x - nrm.z*r.y, nrm.y, nrm.x*r.y + nrm.z*r.x);
  if (A.w > 0.0) {
    vec3 up = normalize(mix(vec3(0.0, 1.0, 0.0), G.xyz, A.w));
    // rotate y onto `up` (Rodrigues about y x up)
    vec3 ax = cross(vec3(0.0, 1.0, 0.0), up);
    float s = length(ax), c = up.y;
    if (s > 1e-5) {
      ax /= s;
      pos = pos * c + cross(ax, pos) * s + ax * dot(ax, pos) * (1.0 - c);
      nrm = nrm * c + cross(ax, nrm) * s + ax * dot(ax, nrm) * (1.0 - c);
    }
  }
  return model * vec4(pos, 1.0);
}
)GLSL";

const char *const VS_MESH = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_nrm;
// scattering: when on, each copy replaces the model's translation with its
// own position and adds a size, a turn, a lean into the ground and a tint
// of its own - the instance stream studio/app_services.cpp builds.
uniform int u_inst_on;
uniform float u_inst_sway;            // wind lean at the mesh's top
uniform float u_inst_time;
uniform vec3 u_inst_base;             // the model matrix's own translation
layout(location=2) in vec4 in_instance; // x,y,z,scale per copy
layout(location=3) in vec4 in_instance_rot; // cos(yaw), sin(yaw), tint, wind phase
layout(location=4) in vec4 in_instance_axes; // per-axis scale, lean into the surface
layout(location=5) in vec4 in_instance_ground; // the ground's normal, LOD key
layout(location=6) in vec2 in_uv;          // texture coordinates, when the model has them
// a plant's wind weights and tint (studio/scene_plants_species.cpp); the
// same plant_wind() moves the vertices on the CPU for a bake or an export
layout(location=7) in vec4 in_wind;
layout(location=8) in vec4 in_tint;
uniform int u_plant_on;
uniform float u_plant_time;
// 1 while a part of foliage is being drawn: a thin two-sided sheet whose
// outline is a cut-out. It is lit from both sides, lit through from behind,
// and its cut-out edge is resolved to a pixel (studio/renderer_meshes.cpp).
uniform int u_leaf;
uniform float u_leaf_through;
uniform vec4 u_pw_a, u_pw_b, u_pw_c;
PLANT_WIND_PLACEHOLDER
DEFORM_FN_PLACEHOLDER
WIND_GUST_PLACEHOLDER
INSTANCE_FN_PLACEHOLDER
out vec3 v_nrm;
out float v_tint;
out vec4 v_ptint;
out vec3 v_world;
out vec2 v_uv;
// Where this point sits inside the model's own bounding box, 0..1 per axis.
// The object-space mapping modes need a position that does not move when the
// object does, and that means the same thing on a pebble and on a mountain.
out vec3 v_local;
void main(){
  vec3 pos = in_pos;
  vec3 nrm = in_nrm;
  v_tint = 1.0;
  v_ptint = u_plant_on == 1 ? in_tint : vec4(1.0);
  // a plant's vertices are in a unit-height frame, so height is 1 here
  if (u_plant_on == 1) pos = plant_wind(pos, in_wind, u_plant_time, 1.0, u_pw_a, u_pw_b, u_pw_c);
  v_local = (in_pos - u_bmin) / max(u_bmax - u_bmin, vec3(1e-6));
  v_uv = in_uv; // for every path below: an unwritten varying is what flickered textured meshes
  if (u_def_on == 1) {
    // the normal follows two deformed tangents, like the CPU twin
    float eps = max(max(u_bmax.x - u_bmin.x, u_bmax.y - u_bmin.y), u_bmax.z - u_bmin.z) * 1e-3 + 1e-6;
    vec3 ax = abs(nrm.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 t1 = normalize(cross(ax, nrm)), t2 = cross(nrm, t1);
    vec3 p0 = deform(pos), p1 = deform(pos + t1 * eps), p2 = deform(pos + t2 * eps);
    vec3 nn = cross(p1 - p0, p2 - p0);
    if (dot(nn, nn) > 1e-24) nrm = normalize(nn);
    pos = p0;
  }
  if (u_inst_on == 1) {
    vec4 I = in_instance;
    v_tint = in_instance_rot.z;
    vec4 p = instance_place(pos, nrm, I, in_instance_rot, in_instance_axes, in_instance_ground, u_model);
    p.xyz += I.xyz - u_inst_base;
    if (u_inst_sway > 0.0) {
      // The gust this copy is STANDING IN, not a number of its own. Each
      // copy used to take a random phase from its position, which hid the
      // fact that the wind was one number for the whole world by making a
      // tree unrelated to the tree beside it - as wrong as the thing it hid.
      // Now neighbours share a gust and a squall crosses the wood
      // (studio/wind_field.hpp). The copy keeps its own phase for the sway
      // about that lean, because a tree answers the wind at its own rate.
      vec2 gl = wind_gust_lean(I.xz);
      float ph = u_inst_time * 1.7 + in_instance_rot.w * 6.2831853;
      float lean = u_inst_sway * max(p.y - I.y, 0.0) * (0.55 + 0.45 * sin(ph));
      p.x += gl.x * lean;
      p.z += gl.y * lean;
    }
    v_nrm = normalize(u_nrm * nrm);
    v_world = p.xyz;
    gl_Position = u_mvp * p;
    return;
  }
  vec4 p = u_model * vec4(pos, 1.0);
  v_nrm = normalize(u_nrm * nrm);
  v_world = p.xyz;
  gl_Position = u_mvp * p;
})GLSL";

const char *const FS_MESH = R"GLSL(#version 430 core
in vec3 v_nrm;
in float v_tint;
in vec4 v_ptint;
uniform int u_leaf;
uniform float u_leaf_through;
// The part's own surface: its normal map and how sharply it reflects. A leaf
// without these is lit as one flat facet whatever its colour picture shows,
// which is most of the difference between foliage that reads as a leaf and
// foliage that reads as printed paper.
uniform int u_has_part_normal;
uniform sampler2D u_part_normal;
uniform float u_part_rough;
uniform int u_has_part_rough;
uniform sampler2D u_part_rough_tex;
in vec3 v_world;
in vec2 v_uv;
in vec3 v_local;
out vec4 frag;
// the object's own frame and bounds, for the volumetric march
uniform mat4 u_model;
uniform vec3 u_bmin, u_bmax;
// the eye and the sun in the object's own space, computed on the CPU
uniform vec3 u_cam_obj, u_sun_obj;
uniform sampler2D u_albedo_tex; // the part's picture, when u_has_tex
uniform int u_has_tex;
uniform vec3 u_color, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_sun_intensity, u_ambient;
// the light the sky casts, measured (sky_light.cpp); these two shaders
// carry no SKY_FN of their own, so they declare it themselves
uniform vec3 u_sky_light;
uniform int u_light_count;
uniform vec4 u_lights[8];
uniform vec3 u_light_col[8];
uniform vec4 u_light_dir[8];
uniform int u_selected;
uniform vec3 u_cam;
uniform float u_hscale;
uniform sampler2DShadow u_shadowmap;
uniform mat4 u_light_mvp;
uniform int u_shadows;
uniform float u_shadow_soft;
float mesh_shadow(vec3 world, float ndl){
  if (u_shadows == 0) return 1.0;
  vec4 lp = u_light_mvp * vec4(world, 1.0);
  vec3 pc = lp.xyz / lp.w * 0.5 + 0.5;
  if (pc.x < 0.0 || pc.x > 1.0 || pc.y < 0.0 || pc.y > 1.0 || pc.z > 1.0) return 1.0;
  float bias = 0.003 + 0.004 * (1.0 - ndl);
  float texel = 1.0 / 2048.0 * u_shadow_soft;
  float s = 0.0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      s += texture(u_shadowmap, vec3(pc.xy + vec2(dx, dy) * texel, pc.z - bias));
  return s / 9.0;
}
FOG_FN_PLACEHOLDER
SKY_ENV_PLACEHOLDER
MATERIAL_UNIFORMS_PLACEHOLDER
const float PI = 3.14159265;
MATERIAL_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  // The object's own colour, tinted per material (Vue Color tab): no
  // picture map yet — a mesh has no UVs — but every scalar property
  // (roughness, metallic, specular, reflection, translucency, clearcoat,
  // emissive) is the terrain's own PBR pipeline, shared through
  // MATERIAL_*_PLACEHOLDER so one material means the same thing everywhere.
  vec3 N = normalize(v_nrm);
  {
    // Lit on whichever side is seen. A leaf, a frond or a blade of grass is
    // one sheet, and seen from below its normal points away from the eye, so
    // it shaded black against the sky. Mirrored across the view plane rather
    // than negated: a normal that turns away only at a closed mesh's
    // silhouette stays continuous there instead of snapping.
    vec3 Vf = normalize(u_cam - v_world);
    float nv = dot(N, Vf);
    if (nv < 0.0) N = normalize(N - 2.0 * nv * Vf);
  }
  if (u_id_mode != 0 && u_aov == 0) {
    vec3 c = id_colour(u_id_mode == 2 ? u_id_key : float(u_object_id));
    frag = vec4(c * (0.7 + 0.3 * max(N.y, 0.0)), 1.0);
    return;
  }
  if (u_v_density > 0.0) {
    // The mesh as a medium. The ray enters the object's bounding box at this
    // fragment and is marched to where it leaves, in the box's own 0..1
    // space: at each sample the extinction is Beer-Lambert in the density
    // (broken up by noise), the light in-scattered is the sun through a
    // Henyey-Greenstein phase, shadowed by a short march toward it, plus the
    // sky, times the scattering albedo; what is absorbed is tinted. The march
    // stops once 99% of the light is gone. Output is what the medium adds
    // and, in alpha, how much of what is behind it survives.
    vec3 bsz = max(u_bmax - u_bmin, vec3(1e-6));
    vec3 cam_l = (u_cam_obj - u_bmin) / bsz;
    vec3 dir_l = normalize(v_local - cam_l);
    vec3 sun_l = normalize(u_sun_obj / bsz); // a direction into box space divides by the box
    // exit of the box along dir_l from this point
    // per-axis reciprocal that never divides by zero: a direction with no
    // travel along an axis never leaves through that axis's faces
    vec3 sd = sign(dir_l);
    sd = mix(vec3(1.0), sd, abs(sd));
    vec3 inv = sd / max(abs(dir_l), vec3(1e-6));
    vec3 tfar = max((vec3(0.0) - v_local) * inv, (vec3(1.0) - v_local) * inv);
    float t_exit = max(min(min(tfar.x, tfar.y), tfar.z), 0.0);
    int steps = clamp(u_v_steps, 4, 128);
    float dt = t_exit / float(steps);
    // density is per box unit: an object of density d is 1-exp(-d) opaque
    // through its own width, whatever its size in the world. That is what a
    // material parameter should mean - the same smoke on a small cube and a
    // large one - and the offline export converts it to per world unit.
    float T = 1.0;
    vec3 S = vec3(0.0);
    float phase = fog_hg(dot(normalize(v_world - u_cam), normalize(u_sun)), u_v_g) * 12.566;
    vec3 sun_c = u_sun_color * u_sun_intensity;
    vec3 sky = u_sky_light * u_ambient;
    for (int i = 0; i < steps; ++i) {
      vec3 p = v_local + dir_l * ((float(i) + 0.5) * dt);
      float dens = u_v_density * mix(1.0, fog_noise3(p * 6.0) * 1.7, u_v_hetero);
      float ext = dens * dt;
      float od_sun = 0.0;
      for (int j = 0; j < 4; ++j) {
        vec3 q = p + sun_l * ((float(j) + 0.5) * 0.12);
        if (any(lessThan(q, vec3(0.0))) || any(greaterThan(q, vec3(1.0)))) break;
        od_sun += u_v_density * mix(1.0, fog_noise3(q * 6.0) * 1.7, u_v_hetero) * 0.12;
      }
      vec3 Li = u_v_albedo * (sky + sun_c * phase * exp(-od_sun));
      float absorbed = 1.0 - exp(-ext);
      S += T * Li * absorbed;
      T *= 1.0 - absorbed;
      if (T < 0.01) { T = 0.0; break; }
    }
    vec3 col = aces(S * u_exposure);
    col = pow(col, vec3(1.0 / 2.2));
    frag = vec4(col, clamp(1.0 - T, 0.0, 1.0));
    return;
  }
  // a plant's per-leaf tint (season, health, colour shift) and its baked
  // ambient occlusion ride in v_ptint; 1 on every other mesh
  vec3 base = u_color * v_tint * v_ptint.rgb * mix(1.0, v_ptint.a, 0.6);
  if (u_has_tex == 1) {
    // The material's mapping mode and its scale/offset/rotation, which the
    // part's own picture used to ignore entirely - it sampled raw model UVs,
    // so every Transform setting on the material did nothing here.
    vec2 auv = mat_uv3(v_uv, v_local, v_world, N);
    vec4 t = texture(u_albedo_tex, auv);
    float a = t.a;
    if (u_leaf == 1) {
      // The cut-out edge, sharpened to the width of one pixel.
      //
      // A leaf's outline is a picture's alpha tested against a half. A leaf
      // that moves - and in wind every leaf moves - drags that picture across
      // the pixel grid, and each pixel flips between leaf and gap as the
      // texel it happens to land on crosses the threshold. A crown of them
      // strobes. Dividing by how fast the alpha changes across the pixel
      // turns the test into a sub-pixel coverage: the edge lands in the same
      // place however the leaf is sampled, and the flicker goes with it.
      // (The mip chain already keeps each level's share of leaf, so the
      // crown does not thin with distance either.)
      a = clamp((a - 0.5) / max(fwidth(a), 1e-4) + 0.5, 0.0, 1.0);
      if (a < 0.5) discard;
    } else if (a < 0.5) {
      discard;
    }
    base *= pow(t.rgb, vec3(2.2));
  }
  vec3 albedo = mat_albedo(base);
  // A leaf's cuticle is waxy: its roughness belongs to the plant material,
  // not to whatever the material system last set.
  float rough = clamp(u_leaf == 1 ? u_part_rough : u_roughness, 0.03, 1.0);
  // per texel where the part has a map: the blade glossy, the veins dull, a
  // dry rim duller still. This is the glint that says "leaf" rather than
  // "green paper".
  if (u_leaf == 1 && u_has_part_rough == 1)
    rough = clamp(texture(u_part_rough_tex, mat_uv3(v_uv, v_local, v_world, N)).r, 0.03, 1.0);
  vec3 V = normalize(u_cam - v_world);
  vec3 L = normalize(u_sun);
  vec3 H = normalize(L + V);
  if (u_leaf == 1) {
    // A leaf is a sheet with no inside. Which way its normal happens to face
    // is an accident of how the card was built, and lighting it as a solid
    // surface made every leaf that turned past the sun snap to black and
    // back - the second half of the flicker. It is turned to face the
    // viewer, so both faces are lit, and what the sun puts through it from
    // behind is added below.
    if (dot(N, V) < 0.0) N = -N;
    // The midrib and the veins, from the part's normal map. A frame is built
    // about the card's own normal rather than from real tangents, which a
    // leaf card does not carry: the card is flat and its picture is axis
    // aligned, so any consistent frame reads the map the way it was drawn.
    if (u_has_part_normal == 1) {
      vec3 t = normalize(abs(N.y) < 0.9 ? cross(vec3(0.0, 1.0, 0.0), N) : cross(vec3(1.0, 0.0, 0.0), N));
      vec3 b = cross(N, t);
      vec3 m = texture(u_part_normal, mat_uv3(v_uv, v_local, v_world, N)).xyz * 2.0 - 1.0;
      N = normalize(t * m.x + b * m.y + N * m.z);
    }
  }
  float NdL = mat_ndl(dot(N, L)), NdV = max(dot(N, V), 1e-4);
  float NdH = max(dot(N, H), 0.0), VdH = max(dot(V, H), 0.0);
  vec3 F0 = mat_f0(albedo);
  float a = rough * rough, a2 = a * a;
  float dnm = (NdH * NdH * (a2 - 1.0) + 1.0);
  float D = a2 / max(PI * dnm * dnm, 1e-6);
  float k = (rough + 1.0); k = k * k / 8.0;
  float G = (NdL / (NdL * (1.0 - k) + k)) * (NdV / (NdV * (1.0 - k) + k));
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
  vec3 spec = D * G * F / max(4.0 * NdL * NdV, 1e-4);
  if (u_m_phong == 1) spec = F * mat_phong(NdH, rough);
  spec += mat_clearcoat(NdH, NdV, NdL);
  vec3 kd = (1.0 - F) * (1.0 - u_metallic);
  vec3 sun_c = u_sun_color * u_sun_intensity;
  float shadow = mesh_shadow(v_world, NdL);
  vec3 lit = (kd * albedo / PI + spec) * sun_c * NdL * shadow * (u_m_diffuse / 0.6);
  vec3 sky = u_sky_light * u_ambient;
  lit += albedo * sky * (0.45 + 0.55 * N.y) * (u_m_ambient / 0.4);
  vec3 R = reflect(-V, N);
  // the sky pass's picture, clouds and all, when the view has one
  vec3 refl = u_sky_env_on == 1 ? sky_env(R, a)
                                : mix(u_sky_horizon, u_sky_zenith, clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
  vec3 reflection = refl * u_reflection * (1.0 - rough) * mat_fresnel(NdV, F0.g);
  if (u_m_color_reflected == 1) reflection *= albedo;
  lit += reflection + mat_translucent(albedo, V, L, sun_c) + u_m_luminous;
  // Light through the leaf. A canopy with the sun behind it glows; without
  // this the shaded side of every leaf is flat ambient, and a leaf crossing
  // the terminator as it moves changes brightness in one step.
  if (u_leaf == 1) {
    float through = max(0.0, dot(-N, L));
    lit += albedo * sun_c * pow(through, 1.5) * u_leaf_through;
  }
  if (u_m_ignore_light == 1) lit = albedo + u_m_luminous;
  for (int li = 0; li < u_light_count; ++li) {
    vec3 ld = u_lights[li].xyz - v_world;
    float dist = length(ld);
    float att = clamp(1.0 - dist / max(u_lights[li].w, 1e-3), 0.0, 1.0);
    att *= att;
    vec3 l = ld / max(dist, 1e-5);
    float cone = 1.0;
    if (u_light_dir[li].w > -1.0) {
      float cd2 = dot(-l, u_light_dir[li].xyz);
      cone = smoothstep(u_light_dir[li].w,
                        mix(u_light_dir[li].w, 1.0, 0.35), cd2);
    }
    lit += albedo * u_light_col[li] * max(dot(N, l), 0.0) * att * cone;
  }
  vec3 col = lit;
  // objects sit in the same air as the ground
  float dist = length(v_world - u_cam);
  float fog_f; vec3 fog_c;
  fog_terms(v_world, u_cam, dist, u_hscale, u_sun, u_sun_color, fog_f, fog_c);
  if (u_aov != 0) {
    frag = aov_out(u_aov, dist, N, albedo, v_world, float(u_object_id),
                   albedo * sun_c * NdL * shadow, shadow, albedo * sky,
                   vec3(0.0), fog_f, fog_c, 0.0, col);
    return;
  }
  col = apply_fog_terms(col, fog_f, fog_c);
  if (u_selected == 1) col = mix(col, vec3(1.0,0.55,0.18), 0.25);
  col = aces(col * u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

const char *const VS_GIZMO = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
uniform mat4 u_mvp;
uniform vec4 u_xform; // center xyz, radius
out vec3 v_nrm;
void main(){
  vec3 p = in_pos * u_xform.w + u_xform.xyz;
  v_nrm = in_nrm;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

const char *const FS_GIZMO = R"GLSL(#version 430 core
in vec3 v_nrm;
out vec4 frag;
uniform vec3 u_color;
uniform int u_selected;
void main(){
  float rim = pow(1.0 - abs(normalize(v_nrm).z), 1.5);
  vec3 col = u_color + rim * 0.5;
  if (u_selected == 1) col = mix(col, vec3(1.0, 0.62, 0.2), 0.6);
  frag = vec4(col, 1.0);
})GLSL";

// material preview: a lit shape (sphere/cube/flat) textured with the channels
const char *const VS_MATPREV = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
layout(location=2) in vec2 in_uv;
uniform mat3 u_rot;
out vec3 v_nrm;
out vec2 v_uv;
// The preview shape is built about the origin at unit size, so its own
// bounding box is -1..1: this is the same 0..1 local position the mesh
// shader hands the mapping modes, so a material previews as it will look.
out vec3 v_local;
// and the normal before the turntable turns it: the projection has to be
// fixed to the shape, or the material would swim across a spinning preview
out vec3 v_lnrm;
void main(){
  vec3 p = u_rot * in_pos;
  v_nrm = normalize(u_rot * in_nrm);
  v_uv = in_uv;
  v_local = in_pos * 0.5 + 0.5;
  v_lnrm = normalize(in_nrm);
  gl_Position = vec4(p.xy * 0.82, p.z * 0.35 + 0.5, 1.0);
})GLSL";

const char *const FS_MATPREV = R"GLSL(#version 430 core
in vec3 v_nrm;
in vec2 v_uv;
in vec3 v_local;
in vec3 v_lnrm;
out vec4 frag;
uniform sampler2D u_albedo;
uniform sampler2D u_normal_map;
uniform sampler2D u_rough_map;
uniform int u_has_albedo, u_has_normal, u_has_rough;
MATERIAL_UNIFORMS_PLACEHOLDER
uniform vec3 u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_sun_intensity, u_ambient;
// the light the sky casts, measured (sky_light.cpp); these two shaders
// carry no SKY_FN of their own, so they declare it themselves
uniform vec3 u_sky_light;
const float PI = 3.14159265;
MATERIAL_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  vec3 N = normalize(v_nrm);
  // The preview shape stands in for an object, so the mapping is computed in
  // the shape's own space (v_local, v_lnrm) - unturned by the turntable.
  vec2 muv = mat_uv3(v_uv, v_local, v_local - 0.5, normalize(v_lnrm));
  vec3 albedo = (u_has_albedo == 1) ? pow(texture(u_albedo, muv).rgb, vec3(2.2))
                                    : vec3(0.55,0.53,0.5);
  if (u_has_normal == 1){
    vec3 nm = texture(u_normal_map, muv).xyz * 2.0 - 1.0;
    vec3 T = normalize(cross(vec3(0,1,0), N) + vec3(1e-4));
    vec3 B = cross(N, T);
    nm.xy *= u_normal_strength;
    N = normalize(T*nm.x + B*nm.y + N*max(nm.z,0.05));
  }
  albedo = mat_albedo(albedo);
  float rough = clamp(u_roughness * ((u_has_rough == 1) ?
                      texture(u_rough_map, muv).r*2.0 : 1.0), 0.03, 1.0);
  vec3 V = vec3(0,0,1);
  vec3 L = normalize(u_sun);
  vec3 H = normalize(L+V);
  float NdL = mat_ndl(dot(N,L)), NdV = max(dot(N,V),1e-4);
  float NdH = max(dot(N,H),0.0), VdH = max(dot(V,H),0.0);
  vec3 F0 = mat_f0(albedo);
  float a = rough*rough, a2 = a*a;
  float dnm = (NdH*NdH*(a2-1.0)+1.0);
  float D = a2 / max(PI*dnm*dnm, 1e-6);
  float k = (rough+1.0); k = k*k/8.0;
  float G = (NdL/(NdL*(1.0-k)+k)) * (NdV/(NdV*(1.0-k)+k));
  vec3 F = F0 + (1.0-F0)*pow(1.0-VdH,5.0);
  vec3 spec = D*G*F/max(4.0*NdL*NdV,1e-4);
  if (u_m_phong == 1) spec = F * mat_phong(NdH, rough);
  spec += mat_clearcoat(NdH, NdV, NdL);
  vec3 kd = (1.0-F)*(1.0-u_metallic);
  vec3 sky = u_sky_light * u_ambient;
  vec3 sun_c = u_sun_color * u_sun_intensity;
  vec3 col = (kd*albedo/PI + spec) * sun_c * NdL * (u_m_diffuse / 0.6)
           + albedo * sky * (0.45 + 0.55*N.y) * (u_m_ambient / 0.4);
  vec3 R = reflect(-V, N);
  vec3 refl = mix(u_sky_horizon, u_sky_zenith, clamp(R.y*0.5+0.5,0.0,1.0));
  vec3 reflection = refl * u_reflection * (1.0-rough) * mat_fresnel(NdV, F0.g);
  if (u_m_color_reflected == 1) reflection *= albedo;
  col += reflection + mat_translucent(albedo, V, L, sun_c) + u_m_luminous;
  if (u_m_ignore_light == 1) col = albedo + u_m_luminous;
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

const char *const VS_LINES = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
uniform mat4 u_mvp;
void main(){ gl_Position = u_mvp * vec4(in_pos, 1.0); })GLSL";

const char *const FS_LINES = R"GLSL(#version 430 core
out vec4 frag;
uniform vec4 u_color;
void main(){ frag = u_color; })GLSL";

const char *const VS_BG = R"GLSL(#version 430 core
out vec2 v_ndc;
void main(){
  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0-1.0;
  v_ndc = p;
  gl_Position = vec4(p, 0.99999, 1.0);
})GLSL";

const char *const FS_BG = R"GLSL(#version 430 core
in vec2 v_ndc;
out vec4 frag;
uniform vec3 u_top, u_bottom;
void main(){
  float t = v_ndc.y * 0.5 + 0.5;
  frag = vec4(mix(u_bottom, u_top, t), 1.0);
})GLSL";


} // namespace studio
