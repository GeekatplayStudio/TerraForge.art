// Geekatplay TerraForge — deep space, part 1 of 4: what the star field, the
// milky band and the nebulas all share, and the entry the sky calls.
//
// Everything here is a function of a direction and a seed, so it is the same
// picture from every camera and in every export, it costs no memory, and it
// holds its detail at any focal length - there is no backdrop image to run
// out of pixels. The other three parts are spliced in after this one, in
// order, because GLSL has no forward declarations:
//
//   shaders_space.cpp        this: noise, colour, the cube grid, the grade
//   shaders_space_stars.cpp  the star field
//   shaders_space_gal.cpp    the milky band and the galaxy discs
//   shaders_space_neb.cpp    the nebulas, marched as volumes
//   SPACE_ENTRY_FN           space_color(), at the end of this file
//
// renderer_programs.cpp splices the five through SPACE_FN_PLACEHOLDER;
// renderer_space.cpp uploads the uniforms.
#include "renderer_shaders.hpp"

namespace studio {

const char *const SPACE_COMMON_FN = R"GLSL(
// ---- the backdrop as a whole
uniform int u_sp_on;
uniform float u_sp_bright, u_sp_realism, u_sp_glow;

// The four fields the nebulas are marched through and the milky band is
// built from (space_noise.hpp): billow, ridged, cellular, fine. One
// filtered texture read stands in for a fractal's worth of hashing, and it
// carries none of the axis-aligned structure a lattice of value noise
// shows when it is stretched across a sky.
uniform sampler3D u_sp_vol;
vec4 sp_vol(vec3 p){ return texture(u_sp_vol, p); }

float sp_hash(vec3 p, float seed){
  p = fract(p * 0.1031 + seed * 0.0173);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}
vec3 sp_hash3(vec3 p, float seed){
  vec3 q = fract(p * vec3(0.1031, 0.1030, 0.0973) + seed * 0.0131);
  q += dot(q, q.yxz + 33.33);
  return fract((q.xxy + q.yxx) * q.zyx);
}
// Value noise sits on a cubic lattice, and a cubic interpolant leaves its
// second derivative jumping at every cell wall - which the eye reads as
// boxes wherever the noise is stretched over a large piece of sky. The
// quintic curve is smooth to the second derivative and costs two multiplies.
float sp_vnoise(vec3 p, float seed){
  vec3 i = floor(p), f = fract(p);
  f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  return mix(mix(mix(sp_hash(i, seed), sp_hash(i + vec3(1,0,0), seed), f.x),
                 mix(sp_hash(i + vec3(0,1,0), seed), sp_hash(i + vec3(1,1,0), seed), f.x), f.y),
             mix(mix(sp_hash(i + vec3(0,0,1), seed), sp_hash(i + vec3(1,0,1), seed), f.x),
                 mix(sp_hash(i + vec3(0,1,1), seed), sp_hash(i + vec3(1,1,1), seed), f.x), f.y), f.z);
}
// A turn between octaves, not only a shift. Octaves that all share the
// lattice's axes stack their cell walls on top of each other and the grid
// comes through; turning each one scatters them.
const mat3 SP_ROT = mat3(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);
float sp_fbm(vec3 p, float seed, int oct){
  float s = 0.0, a = 0.5, n = 0.0;
  for (int i = 0; i < 8; ++i){
    if (i >= oct) break;
    s += sp_vnoise(p, seed + float(i) * 3.7) * a;
    n += a;
    a *= 0.5;
    p = SP_ROT * p * 2.03 + vec3(1.7, 9.2, 4.1);
  }
  return n > 0.0 ? s / n : 0.0;
}
// ridged: the crests of each octave folded into creases, which is what
// draws the filaments through gas and the rifts through a dust lane
float sp_ridge(vec3 p, float seed, int oct){
  float s = 0.0, a = 0.5, n = 0.0;
  for (int i = 0; i < 8; ++i){
    if (i >= oct) break;
    s += (1.0 - abs(sp_vnoise(p, seed + float(i) * 5.1) * 2.0 - 1.0)) * a;
    n += a;
    a *= 0.55;
    p = SP_ROT * p * 2.11 + vec3(5.3, 1.9, 7.7);
  }
  return n > 0.0 ? s / n : 0.0;
}

// A star's colour along the Planckian locus, in linear light: 0 is about
// 2500 K (a red dwarf), 0.5 is the sun's white, 1 is about 20000 K (a blue
// giant). Normalised so no temperature is dimmer than another - a star's
// brightness is its magnitude, not its colour.
vec3 sp_star_col(float t){
  t = clamp(t, 0.0, 1.0);
  vec3 c = mix(vec3(1.00, 0.46, 0.16), vec3(1.00, 0.79, 0.59), smoothstep(0.0, 0.30, t));
  c = mix(c, vec3(1.00, 0.96, 0.94), smoothstep(0.24, 0.55, t));
  c = mix(c, vec3(0.82, 0.87, 1.00), smoothstep(0.50, 0.78, t));
  return mix(c, vec3(0.64, 0.75, 1.00), smoothstep(0.74, 1.0, t));
}

// ---- the sky as a grid of cells
// Stars are laid out on a cube-mapped grid rather than on the sphere's own
// angles, which would crowd them at the poles. A direction picks its face
// and its place on it; a cell picks the direction at its middle.
int sp_face(vec3 d, out vec2 uv){
  vec3 a = abs(d);
  int f; vec2 st; float m;
  if (a.x >= a.y && a.x >= a.z){ f = d.x > 0.0 ? 0 : 1; st = d.yz; m = a.x; }
  else if (a.y >= a.z){ f = d.y > 0.0 ? 2 : 3; st = d.xz; m = a.y; }
  else { f = d.z > 0.0 ? 4 : 5; st = d.xy; m = a.z; }
  uv = st / m * 0.5 + 0.5;
  return f;
}
vec3 sp_face_dir(int f, vec2 uv){
  vec2 st = uv * 2.0 - 1.0;
  if (f == 0) return normalize(vec3(1.0, st));
  if (f == 1) return normalize(vec3(-1.0, st));
  if (f == 2) return normalize(vec3(st.x, 1.0, st.y));
  if (f == 3) return normalize(vec3(st.x, -1.0, st.y));
  if (f == 4) return normalize(vec3(st, 1.0));
  return normalize(vec3(st, -1.0));
}

// A falloff that is 1 at the middle and exactly 0 at `1`, with no corner at
// either end. Every glow drawn from a cell grid uses this, because a
// falloff that has not reached zero by the edge of the cells the shader
// searches is cut off there - and a cut-off glow is a square. That square
// around every star is what this file was rewritten to be rid of.
float sp_falloff(float x){
  float t = clamp(1.0 - x, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// Scattered points in a flat cell grid - the young stars in a nebula, the
// knots along a galaxy's arms. One point at a hashed place in each cell,
// drawn as a spot; the cell itself is never filled, which the old code did.
float sp_points(vec2 uv, float n, float prob, float rad, float seed){
  vec2 g = uv * n;
  vec2 cell = floor(g);
  float s = 0.0;
  float r = min(rad, 0.9) / n;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i){
      vec2 c = cell + vec2(float(i), float(j));
      vec3 h = sp_hash3(vec3(c, 17.0), seed);
      if (h.x > prob) continue;
      vec2 p = (c + 0.15 + h.yz * 0.7) / n;
      float rr = r * (0.35 + 0.65 * h.x / max(prob, 1e-4));
      float dd = length(uv - p);
      s += sp_falloff(min(dd / max(rr, 1e-6), 1.0)) * (0.4 + 0.6 * h.z);
    }
  return s;
}
)GLSL";

// The entry the sky calls, spliced last: everything beyond the air, in the
// order light reaches the eye. Dust in front of a nebula dims what is
// behind it, so the nebulas hand back a transmittance as well as their own
// glow, and the stars and the band are multiplied by it.
const char *const SPACE_ENTRY_FN = R"GLSL(
// The realism dial (space_settings.hpp): a photograph keeps the colours the
// camera recorded; a film pushes them. This is the only place the dial
// touches what is already drawn - elsewhere it chooses the palette a new
// nebula is born with.
vec3 sp_grade(vec3 c){
  float k = 1.0 - clamp(u_sp_realism, 0.0, 1.0);
  float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(lum), c, 1.0 + 0.6 * k);
  // the film also lifts the faint end, the way a long exposure does
  c += c * c * k * 0.35;
  return max(c, vec3(0.0)) * u_sp_bright;
}
vec3 space_color(vec3 d){
  if (u_sp_on == 0) return vec3(0.0);
  float pix = max(length(fwidth(d)), 4.0e-4);
  vec3 behind = sp_stars(d, pix) + sp_galaxy(d, pix);
  vec3 trans = vec3(1.0);
  vec3 add = sp_nebulas(d, pix, trans);
  return sp_grade(behind * trans + add);
}
)GLSL";

} // namespace studio
