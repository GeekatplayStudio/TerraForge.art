// Geekatplay TerraForge — deep space: the star field, the milky band and the
// palette every nebula is graded through.
//
// Split out of render_settings.hpp so the space backdrop has room to grow
// its own controls (the 500-line module rule). The nebulas and galaxies
// themselves are scene objects (scene.hpp NebulaData); these are the
// settings that belong to the sky as a whole.
//
// `realism` is the dial the whole look turns on: 1 is a photograph - the
// hydrogen-alpha reds and dust browns a camera records, modest saturation -
// and 0 is the picture a film paints, the Hubble palette pushed to teal and
// magenta with a glow round everything. It chooses the colours a new nebula
// is born with (space_presets.hpp) and grades what is already there.
#pragma once
#include <cstdint>

namespace studio {

struct SpaceSettings {
  bool on = true;          // draw deep space at all
  float brightness = 1.f;  // everything at once
  float realism = 0.65f;   // 1 a photograph, 0 the film
  // How far the nebula march steps: 0 draft (12), 1 normal (22), 2 fine (40),
  // 3 exhaustive (72). Only pixels inside a nebula pay for it.
  int quality = 1;
  float glow = 0.5f; // the halo round bright gas and bright stars

  // ---- the stars
  bool stars = true;
  float star_density = 0.5f;     // 0..1, how many
  float star_brightness = 1.f;
  float star_size = 1.f;         // spot size in pixels, about
  float star_temperature = 0.6f; // 0 all white, 1 the full red..blue spread
  float star_spikes = 0.45f;     // diffraction spikes on the brightest
  float star_halo = 0.6f;        // the soft ring round a bright star
  float star_clump = 0.55f;      // how strongly they gather into associations
  int star_seed = 1;
  // The spikes are the camera's, not the stars': a telescope's vanes cut the
  // same cross into every star in the frame. How many points (4, 6 or 8), the
  // angle they share, and how far each colour reaches along them - the
  // rainbow a real lens leaves on the arms.
  int star_spike_points = 4;
  float star_spike_angle = 0.f;  // degrees
  float star_spike_chroma = 0.f; // 0..1
  float star_saturation = 1.f;   // the stars' own colour, 0 grey .. 2 rich
  float star_glow = 0.f;         // the wide soft bloom round the brightest
  // How many of the stars are bright ones: 0 a very few among the faint,
  // 1 many. The magnitudes follow a steep power law (shaders_space_stars.cpp)
  // and this is its exponent, 14 down to 4; 0.5 is the 9 the sky always had.
  float star_bright_share = 0.5f;
  // Star clusters: stars born together, crowded at a core and thinning to an
  // edge - loose blue open clusters and tight yellow globulars. How many
  // (0 none), and how wide one is across, degrees.
  float star_clusters = 0.f;
  float star_cluster_size = 1.5f;

  // ---- the milky band
  bool galaxy_on = true;
  float galaxy_intensity = 0.7f;
  float galaxy_width = 14.f; // degrees, the band's half-height
  float galaxy_yaw = 35.f;   // the band's pole: heading, degrees
  float galaxy_pitch = 55.f; // and elevation
  float galaxy_core = 0.f;   // where along the band the core is, degrees
  float galaxy_dust = 0.7f;  // dark rifts, 0..1
  float galaxy_grain = 0.8f; // the unresolved stars that make the band milky
  float galaxy_color[3] = {1.f, 0.95f, 0.9f};
  int galaxy_seed = 1;
};

} // namespace studio
