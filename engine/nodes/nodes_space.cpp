// Geekatplay TerraForge — deep space nodes (the Objects workspace).
//
// A Nebula node is the network view of a Nebula scene object (studio
// scene.hpp NebulaData): a nebula or a galaxy at infinity, placed in the sky
// by direction and drawn by studio/shaders_space.cpp. As with the Planet
// node, studio/scene_nodes_objects.cpp keeps one object per node in step
// with its attributes, so a sky can be assembled in the graph, saved with
// it, driven by the AI and undone.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

namespace {

REGISTER_NODE(
    Nebula, "Scene",
    "A nebula or a galaxy in deep space: a glowing cloud, a dark cloud, a spiral or "
    "elliptical galaxy, or a planetary nebula, placed in the sky by direction",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Nebula", "Nebula")
          .tooltip = "Which scene object this node drives.";
      add_choice(n.attrs, "kind", "Kind",
                 {"Nebula", "Dark nebula", "Spiral galaxy", "Elliptical galaxy", "Planetary nebula"},
                 0, "Nebula")
          .tooltip = "A cloud of glowing gas with filaments and young stars; a cloud\n"
                     "of dust that hides the stars behind it; a spiral galaxy with\n"
                     "arms, a bulge and dust lanes; a smooth elliptical galaxy; a\n"
                     "ring of gas round a dying star.";
      add_float(n.attrs, "azimuth", "Azimuth", 40.f, -360.f, 360.f, "Place")
          .tooltip = "Compass heading in the sky, degrees, the sun's frame.";
      add_float(n.attrs, "elevation", "Elevation", 35.f, -90.f, 90.f, "Place")
          .tooltip = "Degrees above the horizon.";
      add_float(n.attrs, "size_deg", "Size", 24.f, 0.2f, 180.f, "Place")
          .tooltip = "Angular diameter in degrees. The full moon is half a degree,\n"
                     "the Andromeda galaxy three, the Orion nebula one.";
      add_float(n.attrs, "tilt_deg", "Tilt", 50.f, 0.f, 85.f, "Place")
          .tooltip = "Galaxies: how far the disc is turned from face-on.";
      add_float(n.attrs, "rotation_deg", "Rotation", 0.f, -180.f, 180.f, "Place")
          .tooltip = "Turned in the sky, degrees.";
      add_seed(n.attrs, "seed", "Seed", 1, "Look").tooltip =
          "Another seed is another cloud or galaxy of the same kind.";
      add_float(n.attrs, "brightness", "Brightness", 1.f, 0.f, 4.f, "Look")
          .tooltip = "How bright it glows.";
      add_float(n.attrs, "density", "Density", 0.5f, 0.f, 1.f, "Look")
          .tooltip = "Clouds: how much of the footprint is cloud. Galaxies: how\n"
                     "dark the dust lanes are.";
      add_float(n.attrs, "detail", "Detail", 0.5f, 0.f, 1.f, "Look")
          .tooltip = "Fractal fineness, soft to wispy.";
      add_int(n.attrs, "arms", "Arms", 2, 1, 6, "Look")
          .tooltip = "Spiral galaxies: how many arms.";
      // The cloud kinds' own shape and light (NebulaData, marched as volumes
      // in shaders_space_neb.cpp). The node never set them, so a nebula made
      // from the graph could not be given what one made in the tree could.
      add_float(n.attrs, "dust", "Dust", 0.55f, 0.f, 1.f, "Cloud")
          .tooltip = "Clouds: how much dark dust threads through the gas and\n"
                     "hides it - the black lanes across a bright nebula.";
      add_float(n.attrs, "warp", "Warp", 0.6f, 0.f, 1.5f, "Cloud")
          .tooltip = "Clouds: how far the shape is pulled out of a ball into\n"
                     "billows, pillars and tails.";
      add_float(n.attrs, "glow", "Glow", 0.4f, 0.f, 2.f, "Cloud")
          .tooltip = "Clouds: the soft halo the gas throws around itself.";
      add_int(n.attrs, "sources", "Hot stars", 3, 1, 4, "Cloud")
          .tooltip = "Clouds: how many hot young stars inside light the gas.\n"
                     "Their glare decides where it glows in the bright colour\n"
                     "and where in the cool one.";
      add_float(n.attrs, "source_stars", "Hot stars shown", 0.6f, 0.f, 2.f, "Cloud")
          .tooltip = "Clouds: how bright those stars are drawn. 0 leaves the gas\n"
                     "lit by stars you cannot see.";
      add_float(n.attrs, "core_glow", "Core glow", 0.f, 0.f, 2.f, "Cloud")
          .tooltip = "Clouds: the gas round the hot stars burned out toward\n"
                     "white, as a long exposure records a nebula's heart.";
      add_float(n.attrs, "turbulence", "Turbulence", 0.f, 0.f, 1.f, "Cloud")
          .tooltip = "Clouds: tears the gas into filaments and tendrils with a\n"
                     "second, finer warp.";
      add_float(n.attrs, "lanes", "Dust lanes", 0.f, 0.f, 1.f, "Cloud")
          .tooltip = "Clouds: thin dark ridges of dust laid across the glow.";
      add_color(n.attrs, "color1", "Bright gas / core", 0.95f, 0.38f, 0.48f, 1.f, "Look")
          .tooltip = "The bright gas of a cloud, the core of a galaxy.";
      add_color(n.attrs, "color2", "Cool gas / arms", 0.30f, 0.55f, 0.95f, 1.f, "Look")
          .tooltip = "The cool gas of a cloud, the arms of a galaxy.";
      add_bool(n.attrs, "visible", "Visible", true, "Look")
          .tooltip = "Drawn at all.";
      n.add_out("nebula");
    },
    [](Node &) {})

} // namespace

} // namespace gpx
