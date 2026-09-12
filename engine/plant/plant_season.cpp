// Geekatplay TerraForge - seasons, health and age on a part
// (plant_internal.hpp: season_look, hls_shift).
//
// A part's Seasons group is three presence curves and two tint gradients.
// Presence is their product: the share of these parts that exist at this
// season, at this health, at this maturity. The walker keeps a child with
// that probability (one hashed draw per instance, so the same leaves fall
// every autumn on the same seed). The tint is the two gradients multiplied
// and goes into the instance's tint; the builder applies it. Droop and
// shrink are what a dry leaf does: they scale with (1 - health), and only
// the leaf-like builders use them.
//
// hls_shift is the colour shift a material's seasonal look and a leaf's
// colour-shift group apply: hue in turns, luminosity and saturation added.
// It runs in HSV through engine/gpx/color_math.hpp's conversions so the
// engine has one hue arithmetic; "luminosity" is HSV's value here, which
// is what a lighter or darker leaf means in practice.
#include "plant/plant_internal.hpp"
#include "gpx/color_math.hpp"

namespace gpx {
namespace plant {

SeasonLook season_look(const BuildCtx &ctx, const ParamReader &pr) {
  SeasonLook s;
  const float season = clampf(ctx.season, 0.f, 1.f), health = clampf(ctx.health, 0.f, 1.f);
  const float maturity = clampf(ctx.maturity, 0.f, 1.f);
  float p = 1.f;
  if (pr.attr("presence_season")) p *= clampf(pr.curve_at("presence_season", season), 0.f, 1.f);
  if (pr.attr("presence_health")) p *= clampf(pr.curve_at("presence_health", health), 0.f, 1.f);
  if (pr.attr("presence_age")) p *= clampf(pr.curve_at("presence_age", maturity), 0.f, 1.f);
  s.presence = p;
  float a[3] = {1, 1, 1}, b[3] = {1, 1, 1};
  if (pr.attr("tint_season")) pr.gradient("tint_season", season, a);
  if (pr.attr("tint_health")) pr.gradient("tint_health", health, b);
  for (int c = 0; c < 3; ++c) s.tint[c] = a[c] * b[c];
  const float dry = 1.f - health;
  s.droop_deg = pr.random("droop_health", 0.f, 0, 0.f) * dry;
  s.shrink = clampf(pr.random("shrink_health", 0.f, 0, 0.f) * dry, 0.f, 0.95f);
  return s;
}

void hls_shift(float rgb[3], float hue, float lum, float sat) {
  if (hue == 0.f && lum == 0.f && sat == 0.f) return;
  float hsv[3];
  rgb_to_hsv(rgb, hsv);
  hsv[0] += hue;
  hsv[0] -= std::floor(hsv[0]);
  hsv[1] = clampf(hsv[1] + sat, 0.f, 1.f);
  hsv[2] = clampf(hsv[2] + lum, 0.f, 4.f);
  hsv_to_rgb(hsv, rgb);
}

} // namespace plant
} // namespace gpx
