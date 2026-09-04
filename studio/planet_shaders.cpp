// Geekatplay TerraForge - the planet and infinite-surface GLSL: the
// procedural layer maths (mirrored from gpx/planet_math.hpp), the shared
// landscape palette, the planet sphere shaders and the horizon surround
// shaders. Split from planet_renderer.cpp for the 500-line module rule; the
// logic stays there and declares these extern.
namespace studio {

// ------------------------------------------------------------------ shaders
// GLSL mirror of gpx::planet (engine/gpx/planet_math.hpp). `octf` is a FLOAT
// octave count: the top octave fades in continuously with distance, so the
// level of detail changes without a single visible pop - the flicker-free
// requirement is solved here, not by blending frames. Every function here
// has a CPU twin of the same name; planet_gpu_check.cpp measures that they
// agree.
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
// value noise with its analytic gradient: (n, dn/dx, dn/dy, dn/dz)
vec4 pl_vnoise_d(vec3 p, uint seed){
  vec3 i = floor(p), f = fract(p);
  vec3 u = f*f*(3.0-2.0*f);
  vec3 du = 6.0*f*(1.0-f);
  float c000=pl_hash(i,seed),               c100=pl_hash(i+vec3(1,0,0),seed);
  float c010=pl_hash(i+vec3(0,1,0),seed),   c110=pl_hash(i+vec3(1,1,0),seed);
  float c001=pl_hash(i+vec3(0,0,1),seed),   c101=pl_hash(i+vec3(1,0,1),seed);
  float c011=pl_hash(i+vec3(0,1,1),seed),   c111=pl_hash(i+vec3(1,1,1),seed);
  float k0 = c000, k1 = c100 - c000, k2 = c010 - c000, k3 = c001 - c000;
  float k4 = c000 - c100 - c010 + c110;
  float k5 = c000 - c010 - c001 + c011;
  float k6 = c000 - c100 - c001 + c101;
  float k7 = -c000 + c100 + c010 - c110 + c001 - c101 - c011 + c111;
  float n = k0 + k1*u.x + k2*u.y + k3*u.z + k4*u.x*u.y + k5*u.y*u.z
          + k6*u.z*u.x + k7*u.x*u.y*u.z;
  vec3 g = vec3(du.x * (k1 + k4*u.y + k6*u.z + k7*u.y*u.z),
                du.y * (k2 + k4*u.x + k5*u.z + k7*u.x*u.z),
                du.z * (k3 + k5*u.y + k6*u.x + k7*u.x*u.y));
  return vec4(n, g);
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
// eroded fBm: each octave damped by the gradient accumulated so far, so
// slopes stay smooth and flats stay busy (0..1)
float pl_fbm_eroded(vec3 p, uint seed, float octf, int ridged){
  float sum=0.0, amp=1.0, norm=0.0, lac=1.0;
  vec3 ds = vec3(0.0);
  vec3 q = p;
  for (int i = 0; i < 12; ++i){
    float w = clamp(octf - float(i), 0.0, 1.0);
    if (w <= 0.0) break;
    vec4 nd = pl_vnoise_d(q, seed + uint(i)*101u);
    float n2 = nd.x*2.0 - 1.0;
    vec3 g = nd.yzw * 2.0 * lac;
    float v;
    if (ridged != 0){
      float r = 1.0 - abs(n2);
      float s = (n2 < 0.0 ? 1.0 : -1.0) * 2.0 * r;
      v = r*r;
      g *= s;
    } else {
      v = nd.x;
      g *= 0.5;
    }
    ds += g * amp;
    float e = 1.0 / (1.0 + dot(ds, ds) * 0.35);
    sum += v * amp * w * e;
    norm += amp * w;
    amp *= 0.5;
    q *= 2.03;
    lac *= 2.03;
  }
  return norm > 0.0 ? sum/norm : 0.0;
}
float pl_terrace(float h, float stp, float soft){
  float k = h / stp;
  float f = floor(k);
  float t = k - f;
  float s = smoothstep(1.0 - soft, 1.0, t);
  return (f + s) * stp;
}
// the realistic landscape layer: (height -0.5..0.5, wetness 0..1)
vec2 pl_terrain(vec3 p, uint seed, float octf){
  float m3 = min(octf, 3.0);
  float cont = pl_fbm(p, seed ^ 0x51ed27u, m3, 0);
  float land = smoothstep(-0.05, 0.08, cont);
  float mtnm = smoothstep(0.0, 0.20,
                 pl_fbm(p * 1.3 + vec3(11.3, 4.7, -7.1), seed ^ 0x7a3c19u, m3, 0)) * land;
  float platm = smoothstep(0.08, 0.20,
                 pl_fbm(p * 1.2 + vec3(-3.9, 8.2, 2.6), seed ^ 0x2f8d5bu, m3, 0))
                * land * (1.0 - mtnm);
  float hills = pl_fbm_eroded(p * 2.6, seed ^ 0x9d1u, min(octf, 8.0), 0) - 0.5;
  float mtns = pl_fbm_eroded(p * 1.7 + vec3(5.5, 0.0, 1.5), seed ^ 0x3b7u, octf, 1);
  float base = cont * 0.35 + 0.02;
  float low = 1.0 - smoothstep(0.0, 0.25, base);
  float h = base + land * (0.04 + hills * 0.08 * (1.0 - 0.85 * low))
          + mtnm * mtns * 0.5 + (1.0 - land) * hills * 0.05;
  float ht = pl_terrace(h, 0.05, 0.4);
  h += (ht - h) * platm;
  float riv = pl_fbm(p * 2.0 + vec3(2.2, -6.4, 9.9), seed ^ 0x6e2a4cu, min(octf, 5.0), 0);
  float vall = 1.0 - smoothstep(0.0, 0.035, abs(riv));
  h -= vall * land * (0.03 + 0.06 * mtnm);
  float lakem = smoothstep(0.19, 0.25,
                  pl_fbm(p * 1.3 + vec3(-8.8, 1.9, -4.4), seed ^ 0x1c9e73u, 2.0, 0))
                * land * (1.0 - mtnm) * smoothstep(0.12, 0.03, base);
  h += ((base - 0.06) - h) * lakem;
  if (h > 0.42) h = 0.42 + (h - 0.42) * 0.3;
  float wet = max(vall * land, lakem);
  return vec2(clamp(h, -0.5, 0.5), wet);
}
uniform vec4 u_la[6];  // freq, amp, coverage, mask_scale
uniform vec4 u_lb[6];  // seed, type, octaves, -
uniform int  u_lcount;
float pl_mask(vec3 d, int i){
  float cov = u_la[i].z;
  if (cov >= 0.999) return 1.0;
  if (cov <= 0.001) return 0.0;
  float m = pl_fbm(d * u_la[i].w, uint(u_lb[i].x) ^ 0x9e3779b9u, 3.0, 0) + 0.5;
  float edge = 1.0 - cov;
  return smoothstep(edge - 0.12, edge + 0.12, m);
}
vec2 pl_layer(vec3 p, int type, uint seed, float octf){
  if (type == 3) return pl_terrain(p, seed, octf);
  return vec2(pl_fbm(p, seed, octf, type), 0.0);
}
uniform float u_fstrength; // graph-authored displacement, 0 = layers only
// (relief, wetness) of the whole layer stack plus the field graph
vec2 pl_height_w(vec3 d, float octf){
  float total = 0.0, wsum = 0.0, wett = 0.0;
  for (int i = 0; i < 6; ++i){
    if (i >= u_lcount) break;
    float amp = u_la[i].y;
    if (amp <= 0.0) continue;
    float m = pl_mask(d, i);
    wsum += amp;
    if (m <= 0.0) continue;
    float oct = min(u_lb[i].z, octf);
    vec2 hw = pl_layer(d * u_la[i].x, int(u_lb[i].y), uint(u_lb[i].x), oct);
    total += hw.x * amp * m;
    wett += hw.y * amp * m;
  }
  float h = wsum > 0.0 ? total / wsum : 0.0;
  float wet = wsum > 0.0 ? wett / wsum : 0.0;
  // A field graph the user authored, evaluated here so it reaches every
  // surface that has no heightmap: planets and the endless ground plane. It
  // gets the octave budget the camera has earned, so a graph that respects
  // `lod` stays sharp on approach and cheap far away.
  if (u_fstrength != 0.0)
    h += gpx_surface_field(d, normalize(d), h, 0.0, 0.0, 0.0, octf).x
         * u_fstrength;
  return vec2(h, wet);
}
float pl_height(vec3 d, float octf){ return pl_height_w(d, octf).x; }
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
PL_PALETTE_PLACEHOLDER
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
  vec2 hw = pl_height_w(rd, octf);
  float h0 = hw.x;
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
    // the shared landscape palette, tinted by the planet's own rock colours
    // so a red world stays red and an earth-like one stays earth-like
    float slope = clamp(1.0 - dot(N, d), 0.0, 1.0) * 6.0;
    float var = pl_vnoise(rd * 41.0, 0x5a17u);
    vec3 land = pl_palette(t, slope, abs(rd.y), hw.y, u_snow, var);
    vec3 tint = mix(u_rock_lo, u_rock_hi, t) / vec3(0.46, 0.42, 0.38);
    albedo = land * tint;
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
uniform float u_hscale, u_curve, u_amp, u_base, u_wl;
PL_FN_PLACEHOLDER
PL_SPHERE_PLACEHOLDER
out vec3 v_world;
out vec2 v_uv;
out float v_out;   // distance outside the tile, tile widths
out float v_proc;  // the ground's own height here, world units (under water too)
out float v_wet;
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
  // appears all the way round it. u_base is the level the tile was placed
  // at (studio/planet_place.cpp), the same one the tile's relief is built
  // on, so the two are one function of position at the border.
  vec2 hw = pl_height_w(vec3(uv.x, 0.37, uv.y), octf);
  float proc = hw.x * u_amp + u_base;
  float tile = texture(u_height, uvc).r * u_hscale;
  float s = smoothstep(0.0, 0.35, dout);
  float h = mix(tile, proc, s);
  v_proc = h;
  v_wet = hw.y * s;
  // the sea and the lakes: the ground never shows below the water level,
  // the surface flattens to it - the same plane the tile's water pass draws
  h = max(h, u_wl);
  vec3 p = pl_sphere_place(uv, h, u_curve);
  v_world = p; v_uv = uv; v_out = dout;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

const char *FS_INF = R"GLSL(#version 430 core
in vec3 v_world;
in vec2 v_uv;
in float v_out;
in float v_proc;
in float v_wet;
out vec4 frag;
uniform sampler2D u_height, u_albedo;
uniform int u_has_albedo;
uniform int u_textured; // 1 textured, 0 solid - see the terrain shader
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_hscale, u_amp, u_base, u_sun_i, u_ambient, u_exposure, u_sat;
uniform float u_wl, u_lat, u_snow_line, u_wclarity;
uniform vec3 u_wdeep, u_wshallow;
uniform vec3 u_grade;
PL_FN_PLACEHOLDER
PL_PALETTE_PLACEHOLDER
// the same height fog and pass outputs as the terrain tile (u_aov and
// u_object_id are declared in here), so the ground beyond the tile
// disappears into the same air the tile does - a distance fog of its own
// used to paint the surround pale right up to the tile's border
FOG_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  // the tile itself is drawn by the terrain shader - never fight it
  if (v_out <= 0.0 &&
      all(greaterThan(v_uv, vec2(0.001))) && all(lessThan(v_uv, vec2(0.999))))
    discard;
  float cam_d = max(length(u_cam - v_world), 0.02);
  float octf = clamp(10.0 - log2(cam_d * 7.0) * 1.3, 2.0, 11.0);
  float e = max(0.5 / exp2(octf), 0.0004);
  vec2 hw0 = pl_height_w(vec3(v_uv.x, 0.37, v_uv.y), octf);
  float h0 = hw0.x;
  float hx = pl_height(vec3(v_uv.x + e, 0.37, v_uv.y), octf);
  float hz = pl_height(vec3(v_uv.x, 0.37, v_uv.y + e), octf);
  vec3 N = normalize(vec3((h0-hx)*u_amp, e, (h0-hz)*u_amp));
  // water where the ground is below the water level; the vertex stage
  // already flattened the surface to it
  bool water = v_proc < u_wl - 1e-4;
  float depth = max(u_wl - v_proc, 0.0);

  // the shared landscape palette on the same altitude scale as the tile:
  // 0 at the water, 1 at the top of the tile's height range
  float t = (v_proc - u_wl) / max(u_hscale - u_wl, 0.02);
  float slope = 1.0 - N.y;
  float var = pl_vnoise(vec3(v_uv.x, 0.37, v_uv.y) * 37.0, 0x5a17u);
  vec3 alb = pl_palette(t, slope, u_lat, hw0.y, u_snow_line, var);
  if (u_has_albedo == 1){
    // near the tile, borrow the tile's own texture so a textured tile does
    // not end in a colour seam
    vec3 edge = pow(texture(u_albedo, clamp(v_uv, 0.0, 1.0)).rgb, vec3(2.2));
    alb = mix(edge, alb, smoothstep(0.0, 0.35, v_out));
  }
  // the surround has to answer the shading mode the same way the tile does,
  // or turning the texture off leaves a coloured horizon around a grey tile
  if (u_textured == 0) alb = vec3(0.58, 0.57, 0.55);
  vec3 V = normalize(u_cam - v_world);
  if (water){
    N = vec3(0.0, 1.0, 0.0);
    // the tile's water shader, without its waves: same colours, same depth law
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0) * 0.9 + 0.06;
    vec3 wc = mix(u_wshallow, u_wdeep, clamp(depth * u_wclarity, 0.0, 1.0));
    vec3 skyr = mix(u_sky_horizon, u_sky_zenith, 0.4);
    alb = mix(wc, skyr, fresnel * 0.6);
  }

  float NdL = max(dot(N, u_sun), 0.0);
  // the ambient half is skylight, so it fades with the same nightfall
  // factor the sky uses - otherwise the surround glows all night
  float day_f = clamp(u_sun.y * 4.0 + 0.35, 0.035, 1.0);
  vec3 direct = alb * u_sun_color * u_sun_i * NdL * 0.92 / 3.14159;
  vec3 ambient = alb * mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient
                     * (0.45 + 0.55*N.y) * day_f;
  vec3 col = direct + ambient;
  if (water){
    float spec = pow(max(dot(reflect(-u_sun, N), V), 0.0), 600.0);
    col += u_sun_color * spec * 1.5 * u_sun_i * 0.3;
  }
  float fog_f; vec3 fog_c;
  fog_terms(v_world, u_cam, cam_d, u_hscale, u_sun, u_sun_color, fog_f, fog_c);
  if (u_aov != 0){
    // the same quantities the terrain tile writes; the surround is terrain
    // too, so it carries the tile's object id, and its water the water's
    frag = aov_out(u_aov, cam_d, N, alb, v_world, water ? 2.0 : float(u_object_id),
                   direct, 1.0, ambient, vec3(0.0), fog_f, fog_c,
                   water ? 1.0 : 0.0, col);
    return;
  }
  col = apply_fog_terms(col, fog_f, fog_c);
  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

} // namespace studio
