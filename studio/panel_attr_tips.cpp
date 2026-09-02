// Geekatplay TerraForge - the shared hover explanations for common
// attribute keys (per-node tooltips override these). Split from
// panel_properties_node.cpp for the 500-line module rule.
#include <map>
#include <string>

namespace studio {

// hover explanations for common attribute keys (per-node tooltips override)
const char *attr_tooltip(const std::string &key) {
  static const std::map<std::string, const char *> tips = {
      {"seed", "Random seed: same seed reproduces the exact same result.\nUse the dice to try variations."},
      {"octaves", "Number of noise layers. More octaves = finer detail,\nslower compute."},
      {"lacunarity", "Frequency multiplier between octaves. 2.0 is standard;\nhigher packs detail tighter."},
      {"gain", "Amplitude falloff per octave. Lower = smoother,\nhigher = rougher surfaces."},
      {"kw", "Wavenumber: how many noise features fit across the terrain\nin X and Y."},
      {"offset", "Shifts the noise pattern across the terrain."},
      {"angle", "Rotation or direction in degrees."},
      {"type", "Algorithm variant. Each option changes which parameters matter."},
      {"method", "Simulation method. Droplets are fast and detailed;\nshallow water is more physical."},
      {"particles", "How many erosion droplets to simulate (thousands).\nMore = deeper carving, slower."},
      {"lifetime", "Steps each droplet lives. Longer = longer valleys."},
      {"inertia", "How much droplets keep their direction. Low = follows\nterrain tightly, high = straighter paths."},
      {"capacity", "How much sediment a droplet can carry. Higher = deeper cuts."},
      {"erode_rate", "Fraction of free capacity eroded per step."},
      {"deposit_rate", "Fraction of excess sediment dropped per step."},
      {"evaporation", "Water loss per step; ends droplet life sooner."},
      {"gravity", "Acceleration downhill; affects droplet speed and carving."},
      {"brush", "Radius of terrain affected by each erosion step."},
      {"iterations", "Simulation steps. More = stronger effect, slower."},
      {"talus", "Angle of repose: slopes steeper than this shed material."},
      {"rate", "Material transport speed per iteration."},
      {"k_erode", "Erodibility: how fast rivers cut into rock."},
      {"m_exp", "Drainage-area exponent in E = K*A^m*S^n."},
      {"n_exp", "Slope exponent in E = K*A^m*S^n."},
      {"dt", "Implicit solver timestep. Large values stay stable."},
      {"uplift_rate", "Tectonic uplift added each step (implicit method).\nGrows mountains against erosion."},
      {"smooth", "Hillslope diffusion; softens sharp edges between steps."},
      {"level", "Threshold height (normalized 0..1)."},
      {"softness", "Width of the soft transition edge."},
      {"radius", "Effect radius as a fraction of terrain size."},
      {"factor", "Blend amount between inputs."},
      {"mode", "How the two inputs are combined."},
      {"smoothing", "Softness of the selection edges."},
      {"invert", "Flips the result (selected becomes unselected)."},
      {"post_remap", "Rescale output into the range below after computing."},
      {"post_range", "Output range after remapping."},
      {"post_invert", "Flip the output upside down."},
      {"post_gain", "Gamma curve on the output; >1 darkens lows."},
      {"tiles", "How many times the texture repeats across the terrain."},
      {"mapping", "Stretch = one copy over the whole terrain.\nTile = repeat with the count below."},
      {"asset", "ambientCG asset ID, e.g. Rock035, Grass004, Snow010.\nBrowse ambientcg.com for the catalog (CC0)."},
      {"resolution", "Source texture resolution. 8K sets are ~400MB downloads."},
      {"amplitude", "Strength of the warp displacement."},
      {"strength", "Effect intensity."},
      {"path", "Output/input file path."},
      {"auto_export", "Write the file every time this node recomputes\n(enabled automatically by Bake)."},
  };
  auto it = tips.find(key);
  return it == tips.end() ? nullptr : it->second;
}

} // namespace studio
