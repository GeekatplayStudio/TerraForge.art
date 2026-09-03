// Geekatplay TerraForge — terrain fragment shader (lighting, fog, material, AO, cloud shadows). Split from shaders_terrain.cpp for the 500-line module rule.
//
// FS_TERRAIN_SRC is a template: renderer_programs.cpp substitutes the
// FRACTAL_FN / GPX_* / SKY_FN placeholders before compiling.
#include "renderer_shaders.hpp"

namespace studio {

const char *const FS_TERRAIN_SRC = R"GLSL(#version 430 core
in vec2 v_uv;
in vec3 v_world;
out vec4 frag;
uniform sampler2D u_height;
uniform sampler2D u_albedo;
uniform sampler2D u_normal_map;
uniform sampler2D u_rough_map;
uniform sampler2DShadow u_shadowmap;
uniform sampler3D u_cl_shape;
uniform mat4 u_light_mvp;
uniform int u_has_albedo, u_has_normal, u_has_rough, u_shadows, u_quality;
uniform float u_shadow_soft, u_hscale, u_texel, u_exposure;
uniform vec3 u_sun, u_sun_color, u_cam, u_sky_zenith, u_sky_horizon;
uniform float u_sun_intensity, u_ambient, u_atmo;
// scene point lights: xyz + radius, color premultiplied by intensity
uniform int u_light_count;
uniform vec4 u_lights[8];
uniform vec3 u_light_col[8];
uniform vec4 u_light_dir[8]; // spot axis xyz + cos(half cone); w<-1 = point
// material
uniform float u_roughness, u_metallic, u_specular, u_reflection;
uniform float u_translucency, u_transparency, u_normal_strength;
uniform float u_planet_radius; // the sphere the tile lies on, 0 = flat
// fog
uniform int u_fog_type;
uniform float u_fog_density, u_fog_level, u_fog_falloff, u_fog_scatter;
uniform vec3 u_fog_color, u_absorb;
// sculpt brush cursor: xy = terrain uv, z = radius (<=0 hides), w = erase flag
uniform vec4 u_brush;
// cloud shadows
uniform int u_cloud_shadows;
uniform vec3 u_cam_unused_marker;
uniform float u_frac_amount;
uniform float u_frac_scale;
in float v_detail;
uniform float u_cl_cov, u_cl_alt, u_cl_thick, u_cl_time;
uniform vec2 u_cl_wind;
const float PI = 3.14159265;
uniform float u_field_strength;
uniform int u_surface_on;
uniform int u_surf_rough_on;
uniform int u_surf_bump_on;
uniform float u_surf_bump_strength;
uniform float u_surf_bump_scale;
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
GPX_SURFACE_PLACEHOLDER
GPX_ROUGH_PLACEHOLDER
GPX_BUMP_PLACEHOLDER
SKY_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}

vec3 get_normal(vec2 uv){
  float e = u_texel;
  float hl = texture(u_height, uv - vec2(e,0)).r;
  float hr = texture(u_height, uv + vec2(e,0)).r;
  float hd = texture(u_height, uv - vec2(0,e)).r;
  float hu = texture(u_height, uv + vec2(0,e)).r;
  vec3 n = normalize(vec3((hl-hr)*u_hscale, 2.0*e, (hd-hu)*u_hscale));
  // The graph-authored displacement moved the geometry, so the normal has to
  // move with it or the lighting describes a surface that is not there.
  // Central differences, matching FieldComputeNormal on the CPU.
  if (u_field_strength != 0.0){
    float lodf = gp_octavesf(length(u_cam - v_world), 9.0);
    vec3 pc = v_world;
    float fxp = gpx_terrain_field(pc + vec3(e,0,0), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float fxm = gpx_terrain_field(pc - vec3(e,0,0), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float fzp = gpx_terrain_field(pc + vec3(0,0,e), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float fzm = gpx_terrain_field(pc - vec3(0,0,e), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float k = u_field_strength / max(2.0*e, 1e-5);
    n = normalize(n + vec3(-(fxp-fxm)*k, 0.0, -(fzp-fzm)*k));
  }
  // fractal detail continues below the heightmap's resolution: perturb the
  // normal with the same octaves the vertex stage used, plus finer ones
  if (u_frac_amount > 0.0){
    float dist = length(u_cam - v_world);
    int oct = gp_octaves(dist, 11.0);
    if (oct > 0){
      float e2 = max(u_texel * 0.35, 1e-5);
      float c  = gp_detail(uv, u_frac_scale, oct, 0.5);
      float dx = gp_detail(uv + vec2(e2,0), u_frac_scale, oct, 0.5) - c;
      float dy = gp_detail(uv + vec2(0,e2), u_frac_scale, oct, 0.5) - c;
      float k = u_frac_amount / max(e2, 1e-5) * 0.25;
      n = normalize(n + vec3(-dx * k, 0.0, -dy * k));
    }
  }
  return n;
}

float shadow_factor(vec3 world){
  if (u_shadows == 0) return 1.0;
  vec4 lp = u_light_mvp * vec4(world, 1.0);
  vec3 pc = lp.xyz / lp.w * 0.5 + 0.5;
  if (pc.x < 0.0 || pc.x > 1.0 || pc.y < 0.0 || pc.y > 1.0 || pc.z > 1.0) return 1.0;
  float bias = 0.004;
  float s = 0.0;
  float texel = 1.0 / 2048.0 * u_shadow_soft;
  int R = (u_quality == 1) ? 2 : 1;
  float cnt = 0.0;
  for (int dy = -R; dy <= R; ++dy)
    for (int dx = -R; dx <= R; ++dx){
      s += texture(u_shadowmap, vec3(pc.xy + vec2(dx,dy)*texel, pc.z - bias));
      cnt += 1.0;
    }
  return s / cnt;
}

// horizon-based ambient occlusion marched against the heightfield
float terrain_ao(vec2 uv, float h){
  if (u_quality == 0) return 1.0;
  float occ = 0.0;
  const int DIRS = 6;
  for (int d = 0; d < DIRS; ++d){
    float a = float(d) * (6.2831853/float(DIRS));
    vec2 dir = vec2(cos(a), sin(a));
    float horiz = 0.0;
    for (int s = 1; s <= 4; ++s){
      float r = float(s) * u_texel * 5.0;
      float hs = texture(u_height, uv + dir*r).r;
      horiz = max(horiz, (hs - h) * u_hscale / r);
    }
    occ += horiz / (1.0 + horiz);
  }
  return clamp(1.0 - occ / float(DIRS) * 1.3, 0.15, 1.0);
}

float cloud_shadow(vec3 world){
  if (u_cloud_shadows == 0) return 1.0;
  // Project the point up to the *middle* of the cloud slab along the sun
  // direction, and sample the shape volume at the frequency the sky march
  // uses.
  //
  // Both were wrong. The projection landed on the slab's base, where the
  // height gradient puts density at zero, and the sample was taken at
  // wp * 0.35 against the march's wp * 0.18 - so the shadows on the ground
  // were a different pattern, at roughly half the scale, from the clouds
  // casting them. They now line up because they are the same lookup.
  float dy = max(u_cl_alt + u_cl_thick * 0.5 - world.y, 0.0);
  if (u_sun.y < 0.05) return 1.0;
  vec3 p = world + u_sun * (dy / max(u_sun.y, 0.05));
  vec3 wp = p; wp.xz += u_cl_wind * u_cl_time;
  vec4 sn = texture(u_cl_shape, wp * 0.18);
  float fbm = sn.g*0.625 + sn.b*0.25 + sn.a*0.125;
  float shape = clamp((sn.r - (fbm - 1.0)) / max(2.0 - fbm, 1e-3), 0.0, 1.0);
  float d = clamp((shape - (1.0 - u_cl_cov)) / max(u_cl_cov, 1e-3), 0.0, 1.0);
  return 1.0 - d * 0.65;
}

vec3 apply_fog(vec3 col, vec3 world, vec3 cam, float dist){
  if (u_fog_type == 0 || u_fog_density <= 0.0) return col;
  float level = u_fog_level * u_hscale * 4.0;
  float falloff = u_fog_falloff / max(u_hscale, 1e-3);
  float fy0 = cam.y - level, fy1 = world.y - level;
  float dY = fy1 - fy0;
  float a = exp(-falloff * max(fy0, 0.0));
  float b = exp(-falloff * max(fy1, 0.0));
  float od = (abs(falloff*dY) < 1e-3) ? dist * a
                                      : abs(dist * (a - b) / (falloff * dY));
  float dens = u_fog_density * (u_fog_type == 1 ? 0.35 : (u_fog_type == 2 ? 1.0 : 1.8));
  float f = clamp(1.0 - exp(-od * dens), 0.0, 1.0);
  float sunward = pow(max(dot(normalize(world - cam), u_sun), 0.0), 6.0);
  vec3 fogc = u_fog_color * mix(vec3(1.0), u_sun_color * 1.6, sunward * u_fog_scatter);
  if (u_fog_type == 3) fogc *= vec3(0.85, 0.75, 0.6);
  col *= mix(vec3(1.0), u_absorb, f);
  return mix(col, fogc, f);
}

void main(){
  vec3 N = get_normal(v_uv);
  // the normal was built in the flat tile's frame; on a planet that frame is
  // rotated to the sphere's local east/up/north at this point
  if (u_planet_radius > 0.0){
    float k = min(1.0 / u_planet_radius, 6.2831853);
    float kl = min(1.0 / u_planet_radius, 3.14159265);
    vec2 a = vec2((v_uv.x - 0.5) * k, (v_uv.y - 0.5) * kl);
    float cl = cos(a.y), sl = sin(a.y);
    vec3 up = vec3(sin(a.x) * cl, cos(a.x) * cl, sl);
    vec3 east = vec3(cos(a.x), -sin(a.x), 0.0);
    vec3 north = normalize(cross(east, up));
    N = normalize(east * N.x + up * N.y + north * N.z);
  }
  if (u_has_normal == 1){
    vec3 nm = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;
    nm.xy *= u_normal_strength;
    // terrain tangent frame: +X tangent, +Z bitangent
    vec3 T = normalize(vec3(1.0, 0.0, 0.0) - N * N.x);
    vec3 B = cross(N, T);
    N = normalize(T * nm.x + B * nm.y + N * max(nm.z, 0.05));
  }
  float h = texture(u_height, v_uv).r;
  vec3 albedo;
  if (u_surface_on == 1) {
    // A colour field shades the terrain per pixel. Slope and facing come from
    // the shaded normal, so a distribution keyed on steepness follows detail
    // finer than the heightmap carrying it.
    float sl = N.y;
    float orient = atan(N.x, N.z) / PI;
    albedo = gpx_terrain_surface(v_world, N, v_world.y, sl, orient, 0.0,
                                 gp_octavesf(length(u_cam - v_world), 11.0)).rgb;
  } else if (u_has_albedo == 1) {
    albedo = pow(texture(u_albedo, v_uv).rgb, vec3(2.2));
  } else {
    float slope = 1.0 - N.y;
    vec3 rock = vec3(0.30,0.27,0.25), grass = vec3(0.13,0.20,0.08);
    vec3 snow = vec3(0.90,0.91,0.95), dirt = vec3(0.24,0.19,0.14);
    albedo = mix(grass, dirt, smoothstep(0.05,0.25,slope));
    albedo = mix(albedo, rock, smoothstep(0.18,0.45,slope));
    albedo = mix(albedo, snow, smoothstep(0.62,0.72,h)*(1.0-smoothstep(0.25,0.5,slope)));
  }
  float rough = clamp(u_roughness * (u_has_rough == 1 ?
                      texture(u_rough_map, v_uv).r * 2.0 : 1.0), 0.03, 1.0);
  if (u_surf_rough_on == 1){
    float lodr = gp_octavesf(length(u_cam - v_world), 11.0);
    rough = clamp(gpx_terrain_rough(v_world, N, v_world.y, N.y,
                                    atan(N.x, N.z) / PI, 0.0, lodr).x,
                  0.03, 1.0);
  }
  // A bump field tilts the normal without moving the geometry. Central
  // differences of the same function, so it describes the surface the colour
  // graph describes.
  if (u_surf_bump_on == 1){
    float e = u_surf_bump_scale;
    float lodb = gp_octavesf(length(u_cam - v_world), 11.0);
    float o = atan(N.x, N.z) / PI;
    float bx = gpx_terrain_bump(v_world + vec3(e,0,0), N, v_world.y, N.y, o, 0.0, lodb).x
             - gpx_terrain_bump(v_world - vec3(e,0,0), N, v_world.y, N.y, o, 0.0, lodb).x;
    float bz = gpx_terrain_bump(v_world + vec3(0,0,e), N, v_world.y, N.y, o, 0.0, lodb).x
             - gpx_terrain_bump(v_world - vec3(0,0,e), N, v_world.y, N.y, o, 0.0, lodb).x;
    float k = u_surf_bump_strength / max(2.0*e, 1e-5);
    N = normalize(N + vec3(-bx * k, 0.0, -bz * k));
  }
  vec3 V = normalize(u_cam - v_world);
  vec3 L = u_sun;
  vec3 H = normalize(L + V);
  float NdL = max(dot(N, L), 0.0);
  float NdV = max(dot(N, V), 1e-4);
  float NdH = max(dot(N, H), 0.0);
  float VdH = max(dot(V, H), 0.0);

  float shadow = shadow_factor(v_world + N * 0.002) * cloud_shadow(v_world);
  float ao = terrain_ao(v_uv, h);

  vec3 F0 = mix(vec3(0.08 * u_specular), albedo, u_metallic);
  float a = rough * rough;
  float a2 = a * a;
  float dnm = (NdH*NdH*(a2-1.0)+1.0);
  float D = a2 / max(PI * dnm * dnm, 1e-6);
  float k = (rough + 1.0); k = k*k/8.0;
  float G = (NdL/(NdL*(1.0-k)+k)) * (NdV/(NdV*(1.0-k)+k));
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
  vec3 spec = D * G * F / max(4.0 * NdL * NdV, 1e-4);

  vec3 sun_c = u_sun_color * u_sun_intensity;
  vec3 kd = (1.0 - F) * (1.0 - u_metallic);
  vec3 direct = (kd * albedo / PI + spec) * sun_c * NdL * shadow;

  vec3 sky_amb = mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient;
  vec3 ambient = albedo * sky_amb * (0.5 + 0.5*N.y) * ao;
  ambient += albedo * vec3(0.25,0.22,0.18) * 0.25 * u_ambient * (1.0 - N.y) * ao;

  // sky reflection
  vec3 R = reflect(-V, N);
  vec3 refl = sky_color(R, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  float fres = F0.g + (1.0 - F0.g) * pow(1.0 - NdV, 5.0);
  vec3 reflection = refl * fres * u_reflection * (1.0 - rough) * ao;

  // translucency (light bleeding through thin material toward the viewer)
  float trans_term = pow(max(dot(V, -L), 0.0), 3.0);
  vec3 translucent = albedo * u_translucency * trans_term * sun_c * 0.6;

  // scene point lights: diffuse with a smooth radius falloff; cheap and
  // enough for lanterns, windows and fill lights
  for (int li = 0; li < u_light_count; ++li) {
    vec3 lp = u_lights[li].xyz;
    float lr = max(u_lights[li].w, 1e-3);
    vec3 ld = lp - v_world;
    float dist = length(ld);
    float att = clamp(1.0 - dist / lr, 0.0, 1.0);
    att *= att;
    vec3 l = ld / max(dist, 1e-5);
    float nl = max(dot(N, l), 0.0);
    float cone = 1.0;
    if (u_light_dir[li].w > -1.0) {
      float cd2 = dot(-l, u_light_dir[li].xyz);
      cone = smoothstep(u_light_dir[li].w,
                        mix(u_light_dir[li].w, 1.0, 0.35), cd2);
    }
    direct += kd * albedo / PI * u_light_col[li] * nl * att * cone;
  }

  vec3 col = direct + ambient + reflection + translucent;
  float d = length(v_world - u_cam);
  col = apply_fog(col, v_world, u_cam, d);
  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  // sculpt brush ring, drawn on the surface after grading so it stays legible
  if (u_brush.z > 0.0) {
    float bd = distance(v_uv, u_brush.xy);
    float ring = abs(bd - u_brush.z);
    float px = fwidth(bd) * 1.5;
    float line = 1.0 - smoothstep(px, px * 2.5, ring);
    vec3 bcol = u_brush.w > 0.5 ? vec3(0.85, 0.30, 0.22) : vec3(0.90, 0.55, 0.20);
    col = mix(col, bcol, line * 0.85);
    // soft fill hinting at falloff
    float fill = 1.0 - smoothstep(0.0, u_brush.z, bd);
    col = mix(col, bcol, fill * fill * 0.10);
  }
  frag = vec4(col, 1.0 - u_transparency);
})GLSL";

} // namespace studio
