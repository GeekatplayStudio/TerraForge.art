// Geekatplay TerraForge - the Rock node.
//
// A rock is grown from a seed the way a plant is, not loaded from a library:
// see engine/gpx/rock.hpp for why, and for what the three processes are. The
// node holds the recipe; studio/scene_rocks.cpp builds the mesh from it and
// keeps a scene object in step, exactly as a PlantSpecies node drives its
// individual.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/rock.hpp"
#include <string>
#include <vector>

namespace gpx {

REGISTER_NODE(
    Rock, "Rock",
    "A rock grown from fracture, abrasion and weathering: ten kinds, any seed",
    [](Node &n) {
      std::vector<std::string> kinds;
      for (int i = 0; i < (int)RockType::Count; ++i)
        kinds.push_back(rock_type_name((RockType)i));
      add_choice(n.attrs, "type", "Kind", kinds, 0, "Rock")
          .tooltip = "What made this stone. Each kind is a recipe over the same\n"
                     "three processes - how it broke, how far it has been worn,\n"
                     "and what the weather has taken off it - so the kinds are\n"
                     "different rocks rather than different names.";
      add_seed(n.attrs, "seed", "Seed", 1u, "Rock")
          .tooltip = "One seed is one stone. Change it for another rock of the\n"
                     "same kind: this is the whole of the variation, so a\n"
                     "hillside of two hundred is two hundred different stones\n"
                     "and not one model turned twenty ways.";
      add_float(n.attrs, "size_m", "Size (m)", 1.f, 0.01f, 200.f, "Rock")
          .tooltip = "The longest axis, in metres. A pebble is 0.05, a stone you\n"
                     "could lift 0.3, a boulder 2, a tor 15.";
      add_int(n.attrs, "detail", "Detail", 3, 1, 5, "Rock")
          .tooltip = "How many triangles to spend: each step is four times the\n"
                     "last, from 320 faces to 20,480. Three is right for a rock\n"
                     "you walk past, five for one the camera rests on.";
      // the dials, each multiplying what the kind already asks for
      add_float(n.attrs, "fracture", "Fracture", 1.f, 0.f, 2.f, "Shape")
          .tooltip = "How broken it is: how many flat faces, and how flat. 0 is\n"
                     "a lump that never split, 2 is all faces and edges.";
      add_float(n.attrs, "roundness", "Roundness", 1.f, 0.f, 3.f, "Shape")
          .tooltip = "How far wear has taken the corners. Abrasion works on the\n"
                     "edges and not the faces, which is why a worn stone keeps\n"
                     "its facets faintly while its edges go.";
      add_float(n.attrs, "weathering", "Weathering", 1.f, 0.f, 3.f, "Shape")
          .tooltip = "Pits, flutes and surface grain - what frost, salt and wind\n"
                     "take off the outside.";
      add_float(n.attrs, "flatness", "Flatness", 1.f, 0.1f, 3.f, "Shape")
          .tooltip = "Thinner or thicker than the kind usually is. Below 1 makes\n"
                     "a plate, above it a block.";
      add_float(n.attrs, "elongation", "Elongation", 1.f, 0.1f, 4.f, "Shape")
          .tooltip = "Longer or stubbier than the kind usually is.";
      // where it stands, in metres, like every other placed thing
      add_float(n.attrs, "x_m", "X (m)", 0.f, -1e7f, 1e7f, "Place")
          .tooltip = "Where it lies, east of the world's centre, in metres.";
      add_float(n.attrs, "y_m", "Y (m)", 0.f, -1e7f, 1e7f, "Place")
          .tooltip = "Its height in metres, used only when it is not sitting on\n"
                     "the ground - for a stone on a ledge, or one falling.";
      add_float(n.attrs, "z_m", "Z (m)", 0.f, -1e7f, 1e7f, "Place")
          .tooltip = "Where it lies, north of the world's centre, in metres.";
      add_float(n.attrs, "heading", "Heading", 0.f, 0.f, 360.f, "Place")
          .tooltip = "Which way it is turned. A stone has no front, so this is\n"
                     "only ever about which face you are shown.";
      add_bool(n.attrs, "sit", "Sit on the ground", true, "Place")
          .tooltip = "Drops it onto the terrain under it rather than leaving it\n"
                     "at the height given. A rock resting half in the ground is\n"
                     "the commonest way a scene gives itself away.";
      add_color(n.attrs, "color", "Colour", 0.62f, 0.60f, 0.57f, 1.f, "Place")
          .tooltip = "The stone's own colour, before any material is assigned.\n"
                     "Grey with a little warmth in it: most rock is.";
      add_text(n.attrs, "object", "Object name", "", "Place")
          .tooltip = "The scene object this node drives. Left empty it names\n"
                     "itself after the kind.";
    },
    [](Node &n) { (void)n; })

} // namespace gpx
