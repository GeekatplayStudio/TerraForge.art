// Geekatplay TerraForge — deep space: the palette the realism dial hands
// out, the named skies, and Fill the sky (studio/space_presets.hpp).
//
// None of it needs a GL context - it is settings and scene objects - so it
// links into undo_tests with the rest of the studio-state batteries.
#include "space_presets.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace studio;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

static int count_nebulas() {
  int n = 0;
  for (const SceneObject &o : scene().objects)
    if (o.type == SceneObject::Nebula) ++n;
  return n;
}

// The dial has to actually move the colours, and it has to land somewhere
// sensible at both ends: a photograph's nebula is crimson where the gas is
// only once ionised and teal where it is twice, and the film's is not.
static void test_realism_dial_picks_the_palette() {
  std::printf("space: the realism dial...\n");
  float hot_real[3], cool_real[3], hot_film[3], cool_film[3];
  space_nebula_colors(0, 1.f, 7u, hot_real, cool_real);
  space_nebula_colors(0, 0.f, 7u, hot_film, cool_film);
  bool moved = false;
  for (int k = 0; k < 3; ++k)
    if (std::fabs(hot_real[k] - hot_film[k]) > 0.05f ||
        std::fabs(cool_real[k] - cool_film[k]) > 0.05f)
      moved = true;
  CHECK(moved, "1 and 0 on the dial are not the same colours");
  // hydrogen: red the strongest channel of the cool gas, on a photograph
  CHECK(cool_real[0] > cool_real[1] && cool_real[0] > cool_real[2],
        "a photograph's cool gas is hydrogen's crimson");
  // doubly ionised oxygen: the hot gas leans green-blue
  CHECK(hot_real[1] > hot_real[0] && hot_real[2] > hot_real[0],
        "a photograph's ionised heart is oxygen's teal");
  for (int k = 0; k < 3; ++k) {
    CHECK(hot_real[k] >= 0.f && hot_real[k] <= 1.f, "colours stay in range");
    CHECK(cool_film[k] >= 0.f && cool_film[k] <= 1.f, "film colours stay in range");
  }
  // the same seed always gives the same colours, and different seeds vary
  float a[3], b[3], c[3], d[3];
  space_nebula_colors(0, 0.5f, 11u, a, b);
  space_nebula_colors(0, 0.5f, 11u, c, d);
  CHECK(a[0] == c[0] && a[1] == c[1] && a[2] == c[2], "a seed always gives the same palette");
  bool varies = false;
  for (uint32_t s = 1; s < 40 && !varies; ++s) {
    float x[3], y[3];
    space_nebula_colors(0, 0.5f, s, x, y);
    if (std::fabs(x[0] - a[0]) > 1e-4f) varies = true;
  }
  CHECK(varies, "different seeds do not all give one palette");
  // every kind has a palette at both ends, and none of them is black
  for (int type = 0; type < 5; ++type) {
    float h[3], k2[3];
    space_nebula_colors(type, 0.f, 3u, h, k2);
    CHECK(h[0] + h[1] + h[2] > 0.2f, "every kind is lit at the film end");
    space_nebula_colors(type, 1.f, 3u, h, k2);
    CHECK(h[0] + h[1] + h[2] > 0.2f, "every kind is lit at the photograph end");
  }
}

static void test_presets() {
  std::printf("space: the named skies...\n");
  CHECK(space_presets().size() >= 5, "there are presets to choose from");
  std::string err;
  for (const SpacePreset &p : space_presets()) {
    scene() = SceneState{};
    render_settings().space = SpaceSettings{};
    err.clear();
    CHECK(space_preset_apply(p.key, err), (std::string("preset ") + p.key + " applies").c_str());
    CHECK(err.empty(), "a preset that applies says nothing");
    CHECK(render_settings().space.on, "a preset turns space on");
    CHECK(render_settings().space.realism >= 0.f && render_settings().space.realism <= 1.f,
          "a preset leaves the dial in range");
  }
  err.clear();
  CHECK(!space_preset_apply("no-such-sky", err), "an unknown preset is refused");
  CHECK(!err.empty(), "and says which name it did not know");
}

static void test_fill_the_sky() {
  std::printf("space: filling the sky...\n");
  scene() = SceneState{};
  render_settings().space = SpaceSettings{};
  std::string err;
  CHECK(space_populate(4, 1, "mixed", err) == 4, "four asked for, four made");
  CHECK(count_nebulas() == 4, "and four are in the scene");
  // Filling again replaces what the last fill made rather than piling more
  // on: eight is all the sky can hold, and a second press must not spend
  // them all.
  CHECK(space_populate(3, 2, "mixed", err) == 3, "a second fill makes what it says");
  CHECK(count_nebulas() == 3, "and replaces the first, rather than adding to it");
  // one placed by hand survives a fill
  const int hand = scene_add_nebula("My own", 0);
  CHECK(hand >= 0, "a nebula can be added by hand");
  CHECK(space_populate(2, 3, "mixed", err) == 2, "a fill beside a hand-placed one");
  CHECK(count_nebulas() == 3, "which is left where it was");
  bool kept = false;
  for (const SceneObject &o : scene().objects)
    if (o.type == SceneObject::Nebula && o.name == "My own") kept = true;
  CHECK(kept, "the hand-placed one is still there by name");
  // the ceiling
  scene() = SceneState{};
  CHECK(space_populate(20, 4, "mixed", err) == 8, "eight is the ceiling");
  scene() = SceneState{};
  CHECK(space_populate(0, 5, "mixed", err) == 0, "none asked for, none made");
  // what each style makes
  scene() = SceneState{};
  space_populate(5, 6, "galaxies", err);
  bool all_discs = true;
  for (const SceneObject &o : scene().objects)
    if (o.type == SceneObject::Nebula && o.nebula.type != 2 && o.nebula.type != 3)
      all_discs = false;
  CHECK(all_discs, "the galaxies style makes only galaxies");
  scene() = SceneState{};
  space_populate(5, 6, "dark", err);
  bool all_dark = true;
  for (const SceneObject &o : scene().objects)
    if (o.type == SceneObject::Nebula && o.nebula.type != 1) all_dark = false;
  CHECK(all_dark, "the dark style makes only dark clouds");
  // the same seed lays them out the same way, a different one does not
  scene() = SceneState{};
  space_populate(4, 9, "mixed", err);
  float first_az = 0.f;
  for (const SceneObject &o : scene().objects)
    if (o.type == SceneObject::Nebula) { first_az = o.nebula.azimuth; break; }
  scene() = SceneState{};
  space_populate(4, 9, "mixed", err);
  float again_az = 0.f;
  for (const SceneObject &o : scene().objects)
    if (o.type == SceneObject::Nebula) { again_az = o.nebula.azimuth; break; }
  CHECK(first_az == again_az, "the same seed gives the same arrangement");
  // and every one of them is somewhere a sky can hold
  scene() = SceneState{};
  space_populate(8, 12, "mixed", err);
  for (const SceneObject &o : scene().objects) {
    if (o.type != SceneObject::Nebula) continue;
    const NebulaData &N = o.nebula;
    CHECK(N.elevation >= -90.f && N.elevation <= 90.f, "elevation is on the sphere");
    CHECK(N.size_deg > 0.f && N.size_deg <= 180.f, "the size is an angle");
    CHECK(N.sources >= 1 && N.sources <= 4, "the hot stars are between one and four");
    CHECK(N.brightness > 0.f, "nothing is scattered dark");
  }
  scene() = SceneState{};
  render_settings().space = SpaceSettings{};
}

int test_space_run() {
  g_fail = 0;
  test_realism_dial_picks_the_palette();
  test_presets();
  test_fill_the_sky();
  if (g_fail == 0) std::printf("  space: all good\n");
  return g_fail;
}
