// Geekatplay TerraForge — deep space presets and Fill the sky
// (space_presets.hpp).
#include "space_presets.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace studio {

namespace {

uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
float frand(uint32_t &s) {
  s = mix32(s + 0x9E3779B9u);
  return s * (1.f / 4294967296.f);
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }

// The two ends of the dial, per kind of object. The natural column is what
// a camera records: hydrogen's crimson with doubly-ionised oxygen's teal
// where the gas is hardest lit, a galaxy's old yellow core against its young
// blue arms. The film column is the same objects with the palette pushed -
// cyan, magenta and gold - which is what a science-fiction sky is made of.
struct Pair {
  float hot[3], cool[3];
};
struct Palette {
  Pair natural[3];
  Pair film[3];
};
const Palette PALETTES[5] = {
    // 0 emission cloud
    {{{{0.58f, 0.93f, 0.96f}, {0.95f, 0.22f, 0.20f}},
      {{0.78f, 0.95f, 0.86f}, {0.92f, 0.34f, 0.26f}},
      {{0.52f, 0.80f, 1.00f}, {0.88f, 0.45f, 0.38f}}},
     {{{0.28f, 0.96f, 1.00f}, {1.00f, 0.16f, 0.72f}},
      {{0.45f, 0.85f, 1.00f}, {0.98f, 0.35f, 0.20f}},
      {{0.95f, 0.78f, 0.25f}, {0.72f, 0.20f, 1.00f}}}},
    // 1 dark cloud: it emits nothing, but the dial still tints what the
    // dust scatters back
    {{{{0.45f, 0.52f, 0.68f}, {0.30f, 0.24f, 0.20f}},
      {{0.40f, 0.48f, 0.62f}, {0.26f, 0.21f, 0.18f}},
      {{0.50f, 0.55f, 0.70f}, {0.32f, 0.26f, 0.22f}}},
     {{{0.55f, 0.45f, 0.95f}, {0.22f, 0.14f, 0.30f}},
      {{0.35f, 0.70f, 0.95f}, {0.18f, 0.16f, 0.28f}},
      {{0.85f, 0.45f, 0.80f}, {0.24f, 0.14f, 0.26f}}}},
    // 2 spiral galaxy
    {{{{1.00f, 0.90f, 0.72f}, {0.62f, 0.74f, 1.00f}},
      {{1.00f, 0.86f, 0.64f}, {0.70f, 0.80f, 1.00f}},
      {{0.98f, 0.92f, 0.80f}, {0.55f, 0.68f, 0.98f}}},
     {{{1.00f, 0.82f, 0.35f}, {0.45f, 0.45f, 1.00f}},
      {{1.00f, 0.62f, 0.30f}, {0.30f, 0.85f, 1.00f}},
      {{0.95f, 0.85f, 0.55f}, {0.85f, 0.35f, 0.95f}}}},
    // 3 elliptical galaxy
    {{{{0.98f, 0.87f, 0.70f}, {0.75f, 0.66f, 0.55f}},
      {{1.00f, 0.90f, 0.76f}, {0.70f, 0.62f, 0.54f}},
      {{0.96f, 0.84f, 0.66f}, {0.66f, 0.60f, 0.56f}}},
     {{{1.00f, 0.78f, 0.45f}, {0.90f, 0.40f, 0.80f}},
      {{1.00f, 0.85f, 0.55f}, {0.55f, 0.45f, 0.95f}},
      {{0.98f, 0.70f, 0.40f}, {0.80f, 0.55f, 0.95f}}}},
    // 4 planetary nebula
    {{{{0.35f, 0.95f, 0.80f}, {1.00f, 0.35f, 0.30f}},
      {{0.45f, 0.92f, 0.88f}, {0.95f, 0.42f, 0.35f}},
      {{0.30f, 0.88f, 0.95f}, {1.00f, 0.30f, 0.45f}}},
     {{{0.20f, 1.00f, 0.95f}, {1.00f, 0.20f, 0.80f}},
      {{0.35f, 0.95f, 1.00f}, {0.95f, 0.45f, 0.15f}},
      {{0.55f, 1.00f, 0.70f}, {0.90f, 0.15f, 0.95f}}}},
};

// Nebulas made by Fill the sky carry this name, so filling again replaces
// them and leaves anything placed by hand alone.
const char *SCATTER_PREFIX = "Sky ";
bool is_scattered(const SceneObject &o) {
  return o.type == SceneObject::Nebula &&
         o.name.compare(0, std::strlen(SCATTER_PREFIX), SCATTER_PREFIX) == 0;
}

} // namespace

void space_nebula_colors(int type, float realism, uint32_t seed, float c1[3], float c2[3]) {
  const Palette &P = PALETTES[std::clamp(type, 0, 4)];
  const int v = (int)(mix32(seed * 2654435761u) % 3u);
  const float t = std::clamp(realism, 0.f, 1.f);
  for (int k = 0; k < 3; ++k) {
    c1[k] = lerp(P.film[v].hot[k], P.natural[v].hot[k], t);
    c2[k] = lerp(P.film[v].cool[k], P.natural[v].cool[k], t);
  }
}

const std::vector<SpacePreset> &space_presets() {
  static const std::vector<SpacePreset> P = {
      {"night", "A dark night sky",
       "What the eye sees from a dark place: stars, the Milky Way\n"
       "overhead, and nothing else. No nebula is bright enough to\n"
       "show a colour to an eye."},
      {"hubble", "Hubble",
       "A telescope's picture: a few great clouds of gas, their\n"
       "hearts teal where the hot stars have ionised them twice\n"
       "over, hydrogen's crimson around the outside, dust lanes\n"
       "across the front."},
      {"cinema", "Science fiction",
       "The sky a film paints: cyan and magenta gas, gold cores,\n"
       "a glow around everything and stars with spikes. Not what a\n"
       "camera records - what an audience remembers."},
      {"deep_field", "Deep field",
       "The view out of the galaxy: a quiet star field and a\n"
       "scattering of far galaxies, small and faint, the way the\n"
       "Hubble Deep Field found them."},
      {"nursery", "Star nursery",
       "One great emission nebula filling half the sky, lit from\n"
       "inside, with pillars of dust standing in its light."},
      {"void", "Empty",
       "Stars alone: no band, no nebulas. The backdrop for a scene\n"
       "that wants nothing behind it."},
  };
  return P;
}

bool space_preset_apply(const std::string &key, std::string &err) {
  RenderSettings &rs = render_settings();
  SpaceSettings &s = rs.space;
  auto clear = [] {
    SceneState &sc = scene();
    for (int i = (int)sc.objects.size() - 1; i >= 0; --i)
      if (is_scattered(sc.objects[(size_t)i])) scene_delete_subtree(i);
  };
  std::string ignore;
  s.on = true;
  if (key == "night") {
    s.realism = 1.f;
    s.brightness = 1.f;
    s.glow = 0.25f;
    s.stars = true;
    s.star_density = 0.62f;
    s.star_brightness = 1.f;
    s.star_spikes = 0.f;
    s.star_halo = 0.35f;
    s.star_clump = 0.6f;
    s.galaxy_on = true;
    s.galaxy_intensity = 0.85f;
    s.galaxy_grain = 0.9f;
    s.galaxy_dust = 0.75f;
    clear();
    return true;
  }
  if (key == "hubble") {
    s.realism = 0.85f;
    s.brightness = 1.15f;
    s.glow = 0.45f;
    s.stars = true;
    s.star_density = 0.55f;
    s.star_spikes = 0.75f;
    s.star_halo = 0.6f;
    s.star_clump = 0.5f;
    s.galaxy_on = true;
    s.galaxy_intensity = 0.45f;
    s.galaxy_grain = 0.8f;
    return space_populate(3, 7, "nebulas", ignore) >= 0;
  }
  if (key == "cinema") {
    s.realism = 0.05f;
    s.brightness = 1.35f;
    s.glow = 0.9f;
    s.stars = true;
    s.star_density = 0.7f;
    s.star_brightness = 1.2f;
    s.star_spikes = 0.85f;
    s.star_halo = 0.85f;
    s.star_clump = 0.65f;
    s.galaxy_on = true;
    s.galaxy_intensity = 0.9f;
    s.galaxy_grain = 0.85f;
    return space_populate(5, 11, "mixed", ignore) >= 0;
  }
  if (key == "deep_field") {
    s.realism = 1.f;
    s.brightness = 1.f;
    s.glow = 0.3f;
    s.stars = true;
    s.star_density = 0.32f;
    s.star_spikes = 0.6f;
    s.star_halo = 0.5f;
    s.galaxy_on = false;
    return space_populate(8, 23, "galaxies", ignore) >= 0;
  }
  if (key == "nursery") {
    s.realism = 0.7f;
    s.brightness = 1.2f;
    s.glow = 0.6f;
    s.stars = true;
    s.star_density = 0.6f;
    s.star_spikes = 0.5f;
    s.galaxy_on = true;
    s.galaxy_intensity = 0.35f;
    clear();
    SceneState &sc = scene();
    const int idx = scene_add_nebula(std::string(SCATTER_PREFIX) + "nursery", 0);
    if (idx < 0) {
      err = "could not add the nebula";
      return false;
    }
    NebulaData &N = sc.objects[(size_t)idx].nebula;
    N.azimuth = 25.f;
    N.elevation = 30.f;
    N.size_deg = 130.f;
    N.seed = 4211u;
    N.brightness = 1.5f;
    N.density = 0.62f;
    N.detail = 0.8f;
    N.dust = 0.7f;
    N.warp = 0.85f;
    N.glow = 0.55f;
    N.sources = 3;
    space_nebula_colors(0, s.realism, N.seed, N.color1, N.color2);
    return true;
  }
  if (key == "void") {
    s.realism = 1.f;
    s.brightness = 1.f;
    s.glow = 0.2f;
    s.stars = true;
    s.star_density = 0.45f;
    s.star_spikes = 0.3f;
    s.galaxy_on = false;
    clear();
    return true;
  }
  err = "no space preset called '" + key + "'";
  return false;
}

int space_populate(int count, int seed, const std::string &style, std::string &err) {
  count = std::clamp(count, 0, 8);
  SceneState &sc = scene();
  for (int i = (int)sc.objects.size() - 1; i >= 0; --i)
    if (is_scattered(sc.objects[(size_t)i])) scene_delete_subtree(i);
  int already = 0;
  for (const SceneObject &o : sc.objects)
    if (o.type == SceneObject::Nebula) ++already;
  const int room = std::max(0, 8 - already);
  if (count > room) count = room;
  const float realism = render_settings().space.realism;
  uint32_t s = (uint32_t)seed * 2246822519u + 1u;
  int made = 0;
  for (int i = 0; i < count; ++i) {
    // A golden-angle spiral over the sphere spreads them without any two
    // landing on each other, and a little jitter keeps the pattern from
    // reading as a pattern.
    const float k = (i + 0.5f) / (float)std::max(count, 1);
    const float el = std::asin(std::clamp(2.f * k - 1.f, -1.f, 1.f)) * 57.29578f +
                     (frand(s) - 0.5f) * 22.f;
    const float az = std::fmod(i * 137.508f + frand(s) * 40.f, 360.f);
    int type = 0;
    if (style == "galaxies") type = frand(s) < 0.7f ? 2 : 3;
    else if (style == "dark") type = 1;
    else if (style != "nebulas") {
      const float r = frand(s);
      type = r < 0.45f ? 0 : (r < 0.60f ? 1 : (r < 0.82f ? 2 : (r < 0.93f ? 3 : 4)));
    }
    const int idx = scene_add_nebula(
        std::string(SCATTER_PREFIX) + std::to_string(made + 1), type);
    if (idx < 0) {
      err = "could not add a nebula";
      return made > 0 ? made : -1;
    }
    NebulaData &N = sc.objects[(size_t)idx].nebula;
    N.azimuth = az;
    N.elevation = std::clamp(el, -85.f, 85.f);
    N.seed = mix32(s + (uint32_t)i * 7919u) % 100003u + 1u;
    // one large piece to build the picture round, then smaller ones
    const bool hero = i == 0 && style != "galaxies" && style != "deep_field";
    // A backdrop wants the sky covered, not eight dots on it: the clouds
    // are big enough to overlap and meet, and only the galaxies - which are
    // whole other galaxies, a long way off - stay small.
    if (type == 2 || type == 3) N.size_deg = lerp(4.f, 26.f, frand(s) * frand(s));
    else if (type == 4) N.size_deg = lerp(1.5f, 7.f, frand(s));
    else N.size_deg = hero ? lerp(80.f, 115.f, frand(s)) : lerp(34.f, 72.f, frand(s));
    N.rotation_deg = frand(s) * 360.f - 180.f;
    N.tilt_deg = frand(s) * 78.f;
    N.brightness = lerp(0.7f, 1.6f, frand(s)) * (hero ? 1.15f : 1.f);
    N.density = lerp(0.35f, 0.72f, frand(s));
    N.detail = lerp(0.45f, 0.9f, frand(s));
    N.dust = lerp(0.3f, 0.85f, frand(s));
    N.warp = lerp(0.35f, 1.0f, frand(s));
    N.glow = lerp(0.2f, 0.65f, frand(s));
    N.sources = 2 + (int)(frand(s) * 3.f);
    N.arms = 2 + (int)(frand(s) * 3.f);
    space_nebula_colors(type, realism, N.seed, N.color1, N.color2);
    ++made;
  }
  return made;
}

} // namespace studio
