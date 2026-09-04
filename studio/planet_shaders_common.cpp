// Geekatplay TerraForge - GLSL shared by every surface: the landscape
// palette (planet spheres, the horizon surround, the terrain tile without a
// material) and the tile-on-sphere placement (terrain, water, surround).
// Spliced in by inject_sky (renderer_programs.cpp) and pl_inject
// (planet_renderer.cpp) through PL_PALETTE_PLACEHOLDER / PL_SPHERE_PLACEHOLDER.
namespace studio {

// The landscape palette, shared by the planet spheres, the horizon surround
// and the terrain tile when it has no material of its own, so the three
// are one surface. Inputs: t = altitude above the water, 0 at the water and
// 1 at the top of the relief; slope = 1 - N.y; lat = |latitude| 0..1;
// wet = valley floor / lake bed 0..1; snow_line in t; var = 0..1 variation.
// Linear-light albedos.
const char *PL_PALETTE = R"GLSL(
vec3 pl_palette(float t, float slope, float lat, float wet, float snow_line, float var){
  const vec3 sand    = vec3(0.60, 0.53, 0.40);
  const vec3 grass   = vec3(0.17, 0.27, 0.08);
  const vec3 meadow  = vec3(0.32, 0.38, 0.14);
  const vec3 forest  = vec3(0.07, 0.15, 0.05);
  const vec3 scrub   = vec3(0.35, 0.31, 0.19);
  const vec3 rock    = vec3(0.33, 0.30, 0.27);
  const vec3 cliff   = vec3(0.24, 0.22, 0.21);
  const vec3 hirock  = vec3(0.44, 0.42, 0.40);
  const vec3 snow    = vec3(0.90, 0.92, 0.95);
  const vec3 wetsoil = vec3(0.13, 0.11, 0.08);
  t = clamp(t, 0.0, 1.0);
  slope = clamp(slope, 0.0, 1.0);
  vec3 c = mix(grass, meadow, var);
  c = mix(c, forest, smoothstep(0.06, 0.28, t) * (1.0 - smoothstep(0.42, 0.68, t))
                     * (0.35 + 0.65 * var));
  c = mix(c, scrub, smoothstep(0.45, 0.65, t));
  c = mix(c, hirock, smoothstep(0.65, 0.85, t));
  c = mix(sand, c, smoothstep(0.0, 0.015, t));
  c = mix(c, wetsoil, wet * 0.6 * (1.0 - smoothstep(0.5, 0.7, t)));
  c = mix(c, rock, smoothstep(0.30, 0.50, slope));
  c = mix(c, cliff, smoothstep(0.55, 0.80, slope));
  float sl = snow_line - lat * lat * 0.6 + (var - 0.5) * 0.06;
  float sn = smoothstep(sl - 0.05, sl + 0.05, t) * (1.0 - smoothstep(0.45, 0.75, slope));
  return mix(c, snow, sn);
}
)GLSL";

// The tile on its planet, mirrored from gpx::planet::sphere_place: the tile
// lies on a sphere R below its centre, wraps the globe once when the sphere
// is smaller than the tile (heights shrinking with it), and never subtracts
// R from something of size R - so a giant planet keeps its relief and a
// tiny one keeps its shape.
const char *PL_SPHERE_FN = R"GLSL(
float pl_sinc(float a){ return abs(a) < 1e-3 ? 1.0 - a*a*(1.0/6.0) : sin(a)/a; }
float pl_sphere_hscale(float R){ float c = min(R * 6.2831853, 1.0); return R <= 0.0 ? 1.0 : c * c; }
vec3 pl_sphere_place(vec2 uv, float h, float R){
  if (R <= 0.0) return vec3(uv.x, h, uv.y);
  float k = min(1.0 / R, 6.2831853);
  float kl = min(1.0 / R, 3.14159265);
  float ax = clamp((uv.x - 0.5) * k, -3.14159265, 3.14159265);
  float ay = clamp((uv.y - 0.5) * kl, -1.5707963, 1.5707963);
  h *= pl_sphere_hscale(R);
  float sx = sin(ax), cx = cos(ax), sy = sin(ay), cl = cos(ay);
  float hx = sin(ax * 0.5), hy = sin(ay * 0.5);
  float drop = 2.0 * R * hx * hx + 2.0 * R * cx * hy * hy;
  float reach_x = (uv.x - 0.5) * (k * R) * pl_sinc(ax);
  float reach_z = (uv.y - 0.5) * (kl * R) * pl_sinc(ay);
  return vec3(0.5 + (reach_x + h * sx) * cl, -drop + h * cx * cl, 0.5 + reach_z + h * sy);
}
)GLSL";


} // namespace studio
