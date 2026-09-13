// Geekatplay TerraForge - what each kind of rock is a recipe for.
//
// The numbers are read off real stone, not invented to look different from
// each other. Where a figure is a measurement it says so; where it is a
// judgement it says that too.
#include "gpx/rock.hpp"
#include <algorithm>
#include <cctype>

namespace gpx {

const char *rock_type_name(RockType t) {
  switch (t) {
    case RockType::Angular: return "Angular";
    case RockType::Sharp: return "Sharp";
    case RockType::River: return "River";
    case RockType::Boulder: return "Boulder";
    case RockType::Slab: return "Slab";
    case RockType::Columnar: return "Columnar";
    case RockType::Weathered: return "Weathered";
    case RockType::Volcanic: return "Volcanic";
    case RockType::Outcrop: return "Outcrop";
    case RockType::Pebble: return "Pebble";
    default: return "Angular";
  }
}

const char *rock_type_note(RockType t) {
  switch (t) {
    case RockType::Angular:
      return "Freshly broken along its joints. Flat faces meeting at sharp "
             "edges, because that is what rock does when it parts: it splits "
             "on planes it already had. Fresh scree, a quarry floor, the foot "
             "of a cliff after a frost.";
    case RockType::Sharp:
      return "Frost-shattered: water froze in a crack, widened it, and did it "
             "again. Thin splintered plates with edges you would not sit on. "
             "Alpine scree above the treeline, and the block fields on a "
             "summit plateau.";
    case RockType::River:
      return "Carried by water and worn at its corners, because that is where "
             "the stress falls. Ellipsoidal and smooth, with the old fracture "
             "facets still faintly readable under the rounding. A stream bed, "
             "a shingle bar, a beach above the tide.";
    case RockType::Boulder:
      return "Large and long weathered: rounded but not tumbled, its corners "
             "taken by centuries of frost and rain rather than by a river. "
             "Glacial erratics, moorland tors, the stones in a field wall.";
    case RockType::Slab:
      return "Split along its bedding, so it is far wider than it is thick. "
             "Sandstone flags, shale, slate - anything laid down in layers "
             "parts into sheets, and a slab lies flat because it has no other "
             "way to sit.";
    case RockType::Columnar:
      return "A cooling joint. Lava contracting as it solidified cracked into "
             "a honeycomb from the surface down, and the columns are five- or "
             "six-sided because that is how a shrinking sheet tessellates. "
             "Basalt pavements, and the broken columns at their feet.";
    case RockType::Weathered:
      return "Taken apart at the surface by salt and wind: pits, hollows and "
             "flutes, deeper where the rock was weaker. Tafoni and honeycomb "
             "weathering on a desert scarp or a sea cliff.";
    case RockType::Volcanic:
      return "Full of the gas that was in it when it froze. Vesicles all "
             "through, so it is light for its size and its surface is holes. "
             "Scoria and pumice, on a cinder cone or a fresh flow.";
    case RockType::Outcrop:
      return "Bedrock, not a loose stone: the top of something much larger, "
             "standing out of the ground with a flat base and no underside "
             "worth drawing. It never moved, so it is not rounded on the "
             "buried side.";
    case RockType::Pebble:
      return "Small and thoroughly rounded - far enough down the river that "
             "nothing is left of the shape it broke off with. Shingle, "
             "gravel, the stones in the bed of a stream.";
    default: return "A stone.";
  }
}

RockRecipe rock_recipe(RockType t) {
  RockRecipe r;
  switch (t) {
    case RockType::Angular:
      // Six to nine faces: a block bounded by three joint sets, two faces
      // each, and the count is what makes it read as broken rather than
      // carved.
      r.cuts = 8; r.cut_flat = 0.92f; r.round = 0.08f;
      r.bump = 0.035f; r.bump_freq = 4.f;
      r.flat = 0.82f; r.elong = 1.15f; r.detail = 3;
      break;
    case RockType::Sharp:
      // A splinter: many cuts, nearly dead flat, and thin. Frost-shattered
      // plates are commonly three to eight times as long as they are thick.
      r.cuts = 11; r.cut_flat = 0.98f; r.round = 0.02f;
      r.bump = 0.02f; r.bump_freq = 5.f;
      // parted along the crack it split on, not squashed
      r.bed = 0.26f; r.flat = 1.f; r.elong = 1.55f; r.detail = 3;
      break;
    case RockType::River:
      // Abrasion takes the corners: few cuts left readable, high roundness,
      // and the surface itself smooth - a river stone has no grain to speak
      // of, which is most of why it reads as wet even when it is dry.
      r.cuts = 6; r.cut_flat = 0.62f; r.round = 0.82f;
      r.bump = 0.012f; r.bump_freq = 2.5f;
      r.flat = 0.66f; r.elong = 1.3f; r.detail = 3;
      break;
    case RockType::Boulder:
      // Rounded by weather rather than tumbling, so the rounding is uneven
      // and the surface keeps a coarse grain.
      r.cuts = 5; r.cut_flat = 0.55f; r.round = 0.6f;
      r.bump = 0.055f; r.bump_freq = 2.2f;
      r.flat = 0.8f; r.elong = 1.12f; r.detail = 4;
      break;
    case RockType::Slab:
      // Bedding. The thickness is the thing: a flag is a tenth of its width
      // or less, and its edges are broken while its faces are not.
      r.cuts = 7; r.cut_flat = 0.95f; r.round = 0.12f;
      r.bump = 0.02f; r.bump_freq = 3.f;
      r.bed = 0.11f; r.flat = 1.f; r.elong = 1.25f; r.detail = 3;
      r.flat_bottom = true;
      break;
    case RockType::Columnar:
      // Five or six sides; six is the ideal a shrinking sheet tends to and
      // five is what it commonly gets. Tall, with broken ends.
      r.sides = 6; r.cuts = 2; r.cut_flat = 0.9f; r.round = 0.06f;
      r.bump = 0.03f; r.bump_freq = 3.5f;
      // `flat` is the y scale: a column is stretched along it, not squashed
      r.flat = 2.8f; r.elong = 1.f; r.detail = 3;
      break;
    case RockType::Weathered:
      // The pits are the point: deep, at several sizes, and the rock between
      // them stands proud.
      r.cuts = 5; r.cut_flat = 0.6f; r.round = 0.35f;
      r.bump = 0.09f; r.bump_freq = 2.8f;
      r.pit = 0.16f; r.pit_count = 22;
      r.flat = 0.85f; r.elong = 1.1f; r.detail = 4;
      break;
    case RockType::Volcanic:
      // Vesicles everywhere and small: gas bubbles frozen in place.
      r.cuts = 6; r.cut_flat = 0.7f; r.round = 0.2f;
      r.bump = 0.07f; r.bump_freq = 6.f;
      r.pit = 0.085f; r.pit_count = 60;
      r.flat = 0.88f; r.elong = 1.08f; r.detail = 4;
      break;
    case RockType::Outcrop:
      // Bedrock: flat underneath because the rest of it is still down there.
      r.cuts = 7; r.cut_flat = 0.85f; r.round = 0.18f;
      r.bump = 0.06f; r.bump_freq = 2.f;
      r.bed = 0.5f; r.flat = 1.f; r.elong = 1.35f; r.detail = 4;
      r.flat_bottom = true;
      break;
    case RockType::Pebble:
      r.cuts = 2; r.cut_flat = 0.3f; r.round = 0.95f;
      r.bump = 0.008f; r.bump_freq = 2.f;
      r.flat = 0.6f; r.elong = 1.22f; r.detail = 2;
      break;
    default: break;
  }
  return r;
}

RockType rock_type_from_words(const std::string &words) {
  std::string w;
  for (char c : words) w.push_back((char)std::tolower((unsigned char)c));
  auto has = [&](const char *k) { return w.find(k) != std::string::npos; };
  // most specific first: "river boulder" is a river stone, not a boulder
  if (has("pebble") || has("shingle") || has("gravel")) return RockType::Pebble;
  if (has("river") || has("stream") || has("cobble") || has("tumbl") ||
      has("beach") || has("rounded"))
    return RockType::River;
  if (has("column") || has("basalt") || has("hexagon")) return RockType::Columnar;
  if (has("slab") || has("flag") || has("shale") || has("slate") ||
      has("plate") || has("bedding") || has("flat"))
    return RockType::Slab;
  if (has("scoria") || has("pumice") || has("volcan") || has("lava") ||
      has("vesic") || has("bubble"))
    return RockType::Volcanic;
  if (has("tafoni") || has("honeycomb") || has("pitted") || has("weather") ||
      has("eroded") || has("desert"))
    return RockType::Weathered;
  if (has("outcrop") || has("bedrock") || has("tor") || has("crag"))
    return RockType::Outcrop;
  if (has("sharp") || has("splinter") || has("shatter") || has("scree") ||
      has("jagged") || has("spiky") || has("frost"))
    return RockType::Sharp;
  if (has("boulder") || has("erratic") || has("glacial")) return RockType::Boulder;
  if (has("angular") || has("broken") || has("block") || has("quarry") ||
      has("rubble") || has("rock") || has("stone"))
    return RockType::Angular;
  return RockType::Count;
}

} // namespace gpx
