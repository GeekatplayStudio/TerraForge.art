// Geekatplay TerraForge - the planet and infinite-surface GLSL: the
// procedural layer maths (mirrored from gpx/planet_math.hpp), the
// planet sphere shaders and the horizon surround shaders. Split from
// planet_renderer.cpp for the 500-line module rule; the logic stays
// there and declares these extern.
namespace studio {

// ------------------------------------------------------------------ shaders
// GLSL mirror of gpx::planet (engine/gpx/planet_math.hpp). `octf` is a FLOAT
// octave count: the top octave fades in continuously with distance, so the
// level of detail changes without a single visible pop — the flicker-free
// requirement is solved here, not by blending frames.
const char *PL_FN = R"GLSL(
float pl_hash(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return float(h & 0xffffffu) / 16777215.0;
}
float pl_vnoise(vec3 p, uint seed){
  vec3 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  float c000=pl_hash(i,seed),               c100=pl_hash(i+vec3(1,0,0),seed);
  float c010=pl_hash(i+vec3(0,1,0),seed),   c110=pl_hash(i+vec3(1,1,0),seed);
  float c001=pl_hash(i+vec3(0,0,1),seed),   c101=pl_hash(i+vec3(1,0,1),seed);
  float c011=pl_hash(i+vec3(0,1,1),seed),   c111=pl_hash(i+vec3(1,1,1),seed);
  float x00=mix(c000,c100,f.x), x10=mix(c010,c110,f.x);
  float x01=mix(c001,c101,f.x), x11=mix(c011,c111,f.x);
  return mix(mix(x00,x10,f.y), mix(x01,x11,f.y), f.z);
}
float pl_fbm(vec3 p, uint seed, float octf, int type){
  float sum=0.0, amp=1.0, norm=0.0;
  vec3 q = p;
  for (int i = 0; i < 12; ++i){
    float w = clamp(octf - float(i), 0.0, 1.0);
    if (w <= 0.0) break;
    float n = pl_vnoise(q, seed + uint(i)*101u);
    if (type == 1) n = 1.0 - abs(n*2.0-1.0);
    else if (type == 2) n = abs(n*2.0-1.0);
    sum += n * amp * w;
    norm += amp * w;
    amp *= 0.5;
    q *= 2.03;
  }
  float v = norm > 0.0 ? sum/norm : 0.0;
  if (type == 1) v = v*v;
  return v - 0.5;
}
uniform vec4 u_la[6];  // freq, amp, coverage, mask_scale
uniform vec4 u_lb[6];  // seed, type, -, -
uniform int  u_lcount;
float pl_mask(vec3 d, int i){
  float cov = u_la[i].z;
  if (cov >= 0.999) return 1.0;
  if (cov <= 0.001) return 0.0;
  float m = pl_fbm(d * u_la[i].w, uint(u_lb[i].x) ^ 0x9e3779b9u, 3.0, 0) + 0.5;
  float edge = 1.0 - cov;
  return smoothstep(edge - 0.12, edge + 0.12, m);
}
uniform float u_fstrength; // graph-authored displacement, 0 = layers only
float pl_height(vec3 d, float octf){
  float total = 0.0, wsum = 0.0;
  for (int i = 0; i < 6; ++i){
    if (i >= u_lcount) break;
    float amp = u_la[i].y;
    if (amp <= 0.0) continue;
    float m = pl_mask(d, i);
    wsum += amp;
    if (m <= 0.0) continue;
    total += pl_fbm(d * u_la[i].x, uint(u_lb[i].x), octf, int(u_lb[i].y)) * amp * m;
  }
  float h = wsum > 0.0 ? total / wsum : 0.0;
  // A field graph the user authored, evaluated here so it reaches every
  // surface that has no heightmap: planets and the endless ground plane. It
  // gets the octave budget the camera has earned, so a graph that respects
  // `lod` stays sharp on approach and cheap far away.
  if (u_fstrength != 0.0)
    h += gpx_surface_field(d, normalize(d), h, 0.0, 0.0, 0.0, octf).x
         * u_fstrength;
  return h;
}
)GLSL";

// What pl_height calls when no graph is wired up. Same signature as anything
// the transpiler emits, so the shader source never has to change shape.
const char *PL_FIELD_STUB =
    "vec4 gpx_surface_field(vec3 P, vec3 N, float alt, float slope,\n"
    "                       float orient, float t, float lod){\n"
    "  return vec4(0.0);\n}\n";

const char *VS_PLANET = R"GLSL(#version 430 core
layout(location=0) in vec3 in_dir;
uniform mat4 u_mvp;
uniform vec3 u_center;
uniform float u_radius, u_relief, u_octf, u_sea;
uniform vec2 u_spin; // cos, sin of the planet's static rotation
PL_FN_PLACEHOLDER
out vec3 v_dir;
out vec3 v_world;
void main(){
  vec3 d = normalize(in_dir);
  vec3 rd = vec3(d.x*u_spin.x - d.z*u_spin.y, d.y, d.x*u_spin.y + d.z*u_spin.x);
  float h = pl_height(rd, u_octf);
  // the ocean is a smooth sphere: submerged land never dents the water
  float hs = max(h, u_sea - 0.5);
  vec3 w = u_center + d * (u_radius * (1.0 + hs * u_relief));
  v_dir = d;
  v_world = w;
  gl_Position = u_mvp * vec4(w, 1.0);
  // planets are a sky layer: clamp depth just inside the far plane so no
  // planet is ever frustum-clipped, however far out the camera zooms
  gl_Position.z = min(gl_Position.z, gl_Position.w * 0.99999);
})GLSL";

const char *FS_PLANET = R"GLSL(#version 430 core
in vec3 v_dir;
in vec3 v_world;
out vec4 frag;
uniform vec3 u_center, u_cam, u_sun;
uniform float u_radius, u_relief, u_octf, u_sea, u_snow;
uniform vec2 u_spin;
uniform vec3 u_rock_lo, u_rock_hi, u_water_c, u_atmo_c;
uniform float u_atmo, u_sun_i, u_exposure, u_sat;
uniform vec3 u_grade;
uniform int u_aov, u_object_id; // render pass being drawn, 0 = picture
PL_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  vec3 d = normalize(v_dir);
  vec3 rd = vec3(d.x*u_spin.x - d.z*u_spin.y, d.y, d.x*u_spin.y + d.z*u_spin.x);
  // shading re-evaluates the surface two octaves beyond the geometry (the
  // Vue manual's trick): detail too fine for the mesh still shows as shading
  float octf = min(u_octf + 2.0, 12.0);
  vec3 t1 = normalize(cross(d, abs(d.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0)));
  vec3 t2 = cross(d, t1);
  float e = max(0.35 / exp2(octf), 0.00012);
  float h0 = pl_height(rd, octf);
  vec3 o1 = normalize(d + t1*e), o2 = normalize(d + t2*e);
  vec3 r1 = vec3(o1.x*u_spin.x - o1.z*u_spin.y, o1.y, o1.x*u_spin.y + o1.z*u_spin.x);
  vec3 r2 = vec3(o2.x*u_spin.x - o2.z*u_spin.y, o2.y, o2.x*u_spin.y + o2.z*u_spin.x);
  float h1 = pl_height(r1, octf);
  float h2 = pl_height(r2, octf);
  float g = u_relief / e;
  vec3 N = normalize(d - t1*(h1-h0)*g - t2*(h2-h0)*g);

  float sea = u_sea - 0.5;
  bool water = u_sea > 0.001 && h0 < sea;
  vec3 albedo;
  if (water){
    N = d;
    float depth = clamp((sea - h0) * 6.0, 0.0, 1.0);
    albedo = mix(u_water_c * 1.6, u_water_c * 0.55, depth);
  } else {
    float t = clamp((h0 - sea) / max(0.5 - sea, 0.05), 0.0, 1.0);
    albedo = mix(u_rock_lo, u_rock_hi, t);
    // polar caps + high-altitude snow, both softened by fractal variation
    float lat = abs(rd.y);
    float snow = smoothstep(u_snow - 0.08, u_snow + 0.08, t)
               + smoothstep(0.82, 0.95, lat + h0*0.3);
    albedo = mix(albedo, vec3(0.92, 0.93, 0.95), clamp(snow, 0.0, 1.0));
  }

  float NdL = max(dot(N, u_sun), 0.0);
  float day = smoothstep(-0.15, 0.25, dot(d, u_sun));
  vec3 direct = albedo * NdL * u_sun_i * 0.9 * max(day, 0.06);
  vec3 col = albedo * (NdL * u_sun_i * 0.9 + 0.035) * max(day, 0.06);
  if (u_aov != 0){
    // a planet is a distant body: no fog, no water mask, its own object id
    if (u_aov == 1) frag = vec4(length(u_cam - v_world), 0.0, 0.0, 1.0);
    else if (u_aov == 2) frag = vec4(N, 1.0);
    else if (u_aov == 3) frag = vec4(v_world, 1.0);
    else if (u_aov == 4) frag = vec4(float(u_object_id), 0.0, 0.0, 1.0);
    else if (u_aov == 6) frag = vec4(albedo, 1.0);
    else if (u_aov == 7) frag = vec4(direct, 1.0);
    else if (u_aov == 8) frag = vec4(1.0, 0.0, 0.0, 1.0);
    else if (u_aov == 9) frag = vec4(col - direct, 1.0);
    else if (u_aov == 11) frag = vec4(0.0, 0.0, 0.0, 1.0);
    else if (u_aov == 13) frag = vec4(col, 1.0);
    else frag = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  if (water){
    vec3 V = normalize(u_cam - v_world);
    vec3 H = normalize(V + u_sun);
    col += vec3(1.0, 0.97, 0.9) * pow(max(dot(N, H), 0.0), 180.0) * u_sun_i * day;
  }
  // atmosphere: limb glow, stronger on the lit side
  vec3 V = normalize(u_cam - v_world);
  float rim = pow(1.0 - clamp(dot(V, d), 0.0, 1.0), 2.6) * u_atmo;
  col = mix(col, u_atmo_c * (0.25 + 0.85*day) * u_sun_i, clamp(rim, 0.0, 0.85));

  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

// -------- home ground plane extended to the horizon -------------------------
const char *VS_INF = R"GLSL(#version 430 core
layout(location=0) in vec2 in_p; // -1..1 param, concentrated near the tile
uniform mat4 u_mvp;
uniform sampler2D u_height;
uniform vec3 u_cam;
uniform float u_hscale, u_curve, u_amp, u_base;
PL_FN_PLACEHOLDER
out vec3 v_world;
out vec2 v_uv;
out float v_out;   // distance outside the tile, tile widths
void main(){
  // cubic concentration: vertex density is spent near the tile where the
  // blend must be exact, the far ring reaches ~30 tiles = the horizon
  vec2 off = in_p * (0.5 + 30.0 * in_p*in_p*in_p*in_p);
  vec2 uv = vec2(0.5) + off;
  vec2 uvc = clamp(uv, 0.0, 1.0);
  float dout = length(uv - uvc);
  float cam_d = max(length(u_cam.xz - uv) * 0.15, 0.02);
  float octf = clamp(9.0 - log2(cam_d) * 1.2, 2.0, 10.0);
  // The surround has to meet the tile at the tile's own level, or a step
  // appears all the way round it. This used to be a hardcoded fraction of the
  // height scale, which only looked right while every terrain was normalised
  // to fill 0..1; u_base is the tile's actual mean height.
  float proc = pl_height(vec3(uv.x, 0.37, uv.y), octf) * u_amp + u_base;
  float tile = texture(u_height, uvc).r * u_hscale;
  float s = smoothstep(0.0, 0.35, dout);
  float h = mix(tile, proc, s);
  vec3 p = vec3(uv.x, h, uv.y);
  if (u_curve > 0.0){
    // the same sphere the tile lies on (see gpx_sphere_place in the terrain
    // shader): centre R below the tile's centre, arc length -> angle,
    // clamped so nothing wraps past the far side of the globe
    float k = min(1.0 / u_curve, 6.2831853);
    float kl = min(1.0 / u_curve, 3.14159265);
    vec2 a = vec2(clamp((uv.x - 0.5) * k, -3.14159265, 3.14159265),
                  clamp((uv.y - 0.5) * kl, -1.5707963, 1.5707963));
    float cl = cos(a.y);
    vec3 d = vec3(sin(a.x) * cl, cos(a.x) * cl, sin(a.y));
    p = vec3(0.5, -u_curve, 0.5) + d * (u_curve + h);
  }
  v_world = p; v_uv = uv; v_out = dout;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

const char *FS_INF = R"GLSL(#version 430 core
in vec3 v_world;
in vec2 v_uv;
in float v_out;
out vec4 frag;
uniform sampler2D u_height, u_albedo;
uniform int u_has_albedo, u_fog_type;
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon, u_fog_color;
uniform float u_hscale, u_amp, u_sun_i, u_ambient, u_exposure, u_sat, u_fogd;
uniform vec3 u_grade;
uniform int u_aov; // render pass being drawn (renderer_aov.cpp), 0 = picture
PL_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  // the tile itself is drawn by the terrain shader — never fight it
  if (v_out <= 0.0 &&
      all(greaterThan(v_uv, vec2(0.001))) && all(lessThan(v_uv, vec2(0.999))))
    discard;
  float cam_d = max(length(u_cam - v_world), 0.02);
  float octf = clamp(10.0 - log2(cam_d * 7.0) * 1.3, 2.0, 11.0);
  float e = max(0.5 / exp2(octf), 0.0004);
  float h0 = pl_height(vec3(v_uv.x, 0.37, v_uv.y), octf);
  float hx = pl_height(vec3(v_uv.x + e, 0.37, v_uv.y), octf);
  float hz = pl_height(vec3(v_uv.x, 0.37, v_uv.y + e), octf);
  vec3 N = normalize(vec3((h0-hx)*u_amp, e, (h0-hz)*u_amp));

  // palette: valley grass-rock into high rock into snow, steered by the
  // procedural height and slope; near the tile edge, borrow the tile's own
  // albedo so the seam is invisible
  float t = clamp(h0 + 0.5, 0.0, 1.0);
  float slope = 1.0 - N.y;
  vec3 col_lo = vec3(0.30, 0.31, 0.24);
  vec3 col_hi = vec3(0.46, 0.43, 0.40);
  vec3 alb = mix(col_lo, col_hi, clamp(t*1.3 + slope*2.0 - 0.35, 0.0, 1.0));
  alb = mix(alb, vec3(0.90, 0.91, 0.93),
            smoothstep(0.72, 0.85, t) * smoothstep(0.35, 0.15, slope));
  if (u_has_albedo == 1){
    vec3 edge = texture(u_albedo, clamp(v_uv, 0.0, 1.0)).rgb;
    alb = mix(edge, alb, smoothstep(0.0, 1.2, v_out));
  }

  float NdL = max(dot(N, u_sun), 0.0);
  // the ambient half is skylight, so it fades with the same nightfall
  // factor the sky uses - otherwise the surround glows all night
  float day_f = clamp(u_sun.y * 4.0 + 0.35, 0.035, 1.0);
  vec3 direct = alb * u_sun_color * u_sun_i * NdL * 0.8 / 3.14159;
  vec3 ambient = alb * mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient
                     * (0.45 + 0.55*N.y) * day_f;
  vec3 col = direct + ambient;
  float fog_f = 0.0;
  if (u_fog_type > 0) fog_f = clamp(1.0 - exp(-cam_d * u_fogd * 0.35), 0.0, 1.0);
  if (u_aov != 0){
    // the same quantities the terrain tile writes (aov_out in FOG_FN); the
    // surround is terrain too, so it carries object id 1
    if (u_aov == 1) frag = vec4(cam_d, 0.0, 0.0, 1.0);
    else if (u_aov == 2) frag = vec4(N, 1.0);
    else if (u_aov == 3) frag = vec4(v_world, 1.0);
    else if (u_aov == 4) frag = vec4(1.0, 0.0, 0.0, 1.0);
    else if (u_aov == 6) frag = vec4(alb, 1.0);
    else if (u_aov == 7) frag = vec4(direct, 1.0);
    else if (u_aov == 8) frag = vec4(1.0, 0.0, 0.0, 1.0);
    else if (u_aov == 9) frag = vec4(ambient, 1.0);
    else if (u_aov == 11) frag = vec4(u_fog_color * fog_f, 1.0 - fog_f);
    else if (u_aov == 13) frag = vec4(mix(col, u_fog_color, fog_f), 1.0);
    else frag = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  col = mix(col, u_fog_color, fog_f);
  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

} // namespace studio
