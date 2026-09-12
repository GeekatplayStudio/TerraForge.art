// Geekatplay TerraForge — the terrain's relief as a smooth surface, shared by
// the stages that place the terrain (shaders_terrain.cpp, shaders_scene.cpp).
#include "renderer_shaders.hpp"

namespace studio {

// The relief as a smooth surface, shared by every stage that places the
// terrain - the view's vertices and the shadow map's - so the shadow is cast
// by the surface that is drawn. Needs u_height, u_height_lod_k, u_lod_cam and
// u_lod_ground declared before it.
const char *const HEIGHT_SMOOTH_FN = R"GLSL(
uniform float u_height_top; // the height texture's last mip level
// The heightmap through a cubic B-spline over the texels of one mip level,
// from four filtered fetches (Sigg and Hadwiger, GPU Gems 2 ch. 20). Read
// bilinearly the relief is flat facets meeting at creases, and a crater's
// rim or a ridge drawn that way is a staircase however finely the
// tessellation cuts it.
float height_bspline(float level, vec2 uv){
  vec2 size = vec2(textureSize(u_height, int(level)));
  vec2 p = uv * size - 0.5;
  vec2 f = fract(p);
  vec2 i = p - f;
  vec2 f2 = f * f, f3 = f2 * f;
  vec2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
  vec2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
  vec2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
  vec2 w3 = f3 / 6.0;
  vec2 g0 = w0 + w1;
  vec2 g1 = w2 + w3;
  vec2 t0 = (i - 1.0 + w1 / max(g0, vec2(1e-6)) + 0.5) / size;
  vec2 t1 = (i + 1.0 + w3 / max(g1, vec2(1e-6)) + 0.5) / size;
  float a = textureLod(u_height, t0, level).r;
  float b = textureLod(u_height, vec2(t1.x, t0.y), level).r;
  float c = textureLod(u_height, vec2(t0.x, t1.y), level).r;
  float d = textureLod(u_height, t1, level).r;
  return mix(mix(d, c, g0.x), mix(b, a, g0.x), g0.y);
}
// between two levels, blended, so the surface never steps as the camera moves
float height_smooth(vec2 uv, float lod){
  float top = max(u_height_top, 0.0);
  lod = clamp(lod, 0.0, top);
  float l0 = floor(lod);
  float h0 = height_bspline(l0, uv);
  float t = lod - l0;
  return (t > 0.001 && l0 < top) ? mix(h0, height_bspline(l0 + 1.0, uv), t) : h0;
}
// the level of detail at a tile point, from the camera's distance to it
float relief_lod(vec2 uv){
  if (u_height_lod_k <= 0.0) return 0.0;
  float dl = length(u_lod_cam - vec3(uv.x, u_lod_ground, uv.y));
  return clamp(log2(max(dl * u_height_lod_k, 1.0)), 0.0, 6.0);
}
)GLSL";

} // namespace studio
