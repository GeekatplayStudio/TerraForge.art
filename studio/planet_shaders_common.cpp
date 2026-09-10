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
// The palette's variation. It is not decoration: `var` picks between grass
// and meadow outright and weights the forest, so it *is* the ground's
// colour, and if the two sides of a tile's border compute it differently the
// mottling stops dead at the border and draws the square. The surround has
// the planet's own noise (PL_FN); the tile shader does not, and used to make
// do with a different fractal entirely - two octaves at five per tile
// against value noise at thirty-seven, which is broad smooth blotches inside
// the square and fine mottle outside it. So the grain lives here, with the
// palette it feeds, and both shaders call this one. Its own copy of the
// hash, under its own name, because the shaders that carry PL_FN would
// otherwise have two definitions of it.
float pv_hash(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return float(h & 0xffffffu) / 16777215.0;
}
float pv_noise(vec3 p, uint seed){
  vec3 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  float c000=pv_hash(i,seed),               c100=pv_hash(i+vec3(1,0,0),seed);
  float c010=pv_hash(i+vec3(0,1,0),seed),   c110=pv_hash(i+vec3(1,1,0),seed);
  float c001=pv_hash(i+vec3(0,0,1),seed),   c101=pv_hash(i+vec3(1,0,1),seed);
  float c011=pv_hash(i+vec3(0,1,1),seed),   c111=pv_hash(i+vec3(1,1,1),seed);
  float x00=mix(c000,c100,f.x), x10=mix(c010,c110,f.x);
  float x01=mix(c001,c101,f.x), x11=mix(c011,c111,f.x);
  return mix(mix(x00,x10,f.y), mix(x01,x11,f.y), f.z);
}
// on the ground, from the flat world position in tile units - the same
// number the surround indexes by, so the grain crosses the border unbroken
float pl_palette_var(vec2 xz){
  return pv_noise(vec3(xz.x, 0.37, xz.y) * 37.0, 0x5a17u);
}
// and on a world seen whole, by direction: thirty-seven per tile is speckle
// from thousands of tiles off, and a grain scaled by distance smears into
// stripes along the view
float pl_palette_var_dir(vec3 dir){
  return pv_noise(dir * 40.0, 0x5a17u);
}
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
// The world's shape (gpx::planet::Shape, studio/world_shape.hpp): x = 1 when
// the tile's x axis is flat, y = 1 when its z axis is flat, z = 1 when the
// centre is above the tile (a ring world, a Dyson sphere), w = 1 for the
// other face of the same shell (heights the other way). All zero - the
// value a uniform never set reads as - is the globe every scene had.
// u_world_thick is the shell's thickness (Shape::thick): the other face lies
// that much deeper, at its own radius; 0 (never set) is the skin it was.
const char *PL_SPHERE_FN = R"GLSL(
uniform vec4 u_world_shape;
uniform float u_world_thick;
float pl_sinc(float a){ return abs(a) < 1e-3 ? 1.0 - a*a*(1.0/6.0) : sin(a)/a; }
float pl_sphere_hscale(float R){ float c = min(R * 6.2831853, 1.0); return R <= 0.0 ? 1.0 : c * c; }
bool pl_world_flat(){ return u_world_shape.x > 0.5 && u_world_shape.y > 0.5; }
// the tile's two angles round the world, 0 along a flat axis
vec2 pl_sphere_angles(vec2 uv, float R){
  if (R <= 0.0) return vec2(0.0); // a flat world has no angles
  float k = u_world_shape.x > 0.5 ? 0.0 : min(1.0 / R, 6.2831853);
  float kl = u_world_shape.y > 0.5 ? 0.0 : min(1.0 / R, 3.14159265);
  return vec2(clamp((uv.x - 0.5) * k, -3.14159265, 3.14159265),
              clamp((uv.y - 0.5) * kl, -1.5707963, 1.5707963));
}
vec3 pl_sphere_place(vec2 uv, float h, float R){
  // a flat world: the other face is its underside, the thickness below
  if (R <= 0.0 || pl_world_flat()) return vec3(uv.x, u_world_shape.w > 0.5 ? -h - u_world_thick : h, uv.y);
  float k = min(1.0 / R, 6.2831853);
  float kl = min(1.0 / R, 3.14159265);
  // a flat axis has no angle and its reach is the tile's own distance
  float wrap_x = u_world_shape.x > 0.5 ? 1.0 : k * R;
  float wrap_z = u_world_shape.y > 0.5 ? 1.0 : kl * R;
  if (u_world_shape.x > 0.5) k = 0.0;
  if (u_world_shape.y > 0.5) kl = 0.0;
  float ax = clamp((uv.x - 0.5) * k, -3.14159265, 3.14159265);
  float ay = clamp((uv.y - 0.5) * kl, -1.5707963, 1.5707963);
  h *= pl_sphere_hscale(R);
  if (u_world_shape.w > 0.5) h = -h; // the other face: heights the other way
  // inside: the centre is R above the tile, the drop is a rise and the
  // height leans toward the centre
  float s = u_world_shape.z > 0.5 ? -1.0 : 1.0;
  // the other face of a thick shell: the same angles at its own radius,
  // the thickness below the world's ground (gpx::planet::sphere_place)
  float Rf = R, dy = 0.0, ratio = 1.0;
  if (u_world_shape.w > 0.5 && u_world_thick > 0.0){
    Rf = max(R - s * u_world_thick, R * 1e-3);
    dy = -u_world_thick;
    ratio = Rf / R;
  }
  float sx = sin(ax), cx = cos(ax), sy = sin(ay), cl = cos(ay);
  float hx = sin(ax * 0.5), hy = sin(ay * 0.5);
  float drop = 2.0 * Rf * hx * hx + 2.0 * Rf * cx * hy * hy;
  float reach_x = (uv.x - 0.5) * wrap_x * pl_sinc(ax);
  float reach_z = (uv.y - 0.5) * wrap_z * pl_sinc(ay);
  if (ratio != 1.0){
    if (u_world_shape.x < 0.5) reach_x *= ratio;
    if (u_world_shape.y < 0.5) reach_z *= ratio;
  }
  return vec3(0.5 + (reach_x + s * h * sx) * cl, -s * drop + h * cx * cl + dy, 0.5 + reach_z + s * h * sy);
}
// the local east/up/north at a tile point, for a normal built in the flat
// tile's frame (gpx::planet::sphere_frame)
void pl_sphere_frame(vec2 uv, float R, out vec3 east, out vec3 up, out vec3 north){
  vec2 a = pl_sphere_angles(uv, R);
  float s = u_world_shape.z > 0.5 ? -1.0 : 1.0;
  float sx = sin(a.x), cx = cos(a.x), sy = sin(a.y), cl = cos(a.y);
  float f = u_world_shape.w > 0.5 ? -1.0 : 1.0; // the other face looks the other way
  up = vec3(s * sx * cl, cx * cl, s * sy) * f;
  east = vec3(cx, -s * sx, 0.0);
  north = normalize(cross(east, up));
}
// the world's centre (a globe, a Dyson sphere) or the point of its axis (a
// ring) nearest a world point: R above the tile when the centre is above,
// R below it otherwise
vec3 pl_world_centre_at(vec3 world, float R){
  return vec3(0.5, u_world_shape.z > 0.5 ? R : -R, u_world_shape.y > 0.5 ? world.z : 0.5);
}
// the direction a sun inside the world shines from, at a world point
vec3 pl_world_up_at(vec3 world, float R){
  return normalize(pl_world_centre_at(world, R) - world);
}
// The altitude of a world point above the world's own surface, and the
// surface's up there (gpx::planet::world_alt / world_up): the air, the
// clouds and their shadows are layers on the world, whatever its shape. A
// flat world, and a radius past float precision, measure world y.
float pl_world_alt(vec3 p, float R){
  if (R <= 0.0 || R > 1.0e5 || pl_world_flat()) return p.y;
  float d = length(p - pl_world_centre_at(p, R));
  return u_world_shape.z > 0.5 ? R - d : d - R;
}
vec3 pl_world_up(vec3 p, float R){
  if (R <= 0.0 || R > 1.0e5 || pl_world_flat()) return vec3(0.0, 1.0, 0.0);
  vec3 d = p - pl_world_centre_at(p, R);
  float l = length(d);
  if (l < 1e-12) return vec3(0.0, 1.0, 0.0);
  return u_world_shape.z > 0.5 ? -d / l : d / l;
}
)GLSL";


} // namespace studio
