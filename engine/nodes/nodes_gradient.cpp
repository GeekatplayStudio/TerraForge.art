// Geekatplay TerraForge — gradients as terrain.
//
// The ramps an image editor draws - linear, reflected, circle, ellipse,
// square, diamond, angular, spiral - as heightmaps. They are the plainest
// base forms there are and the most used: a circle is a volcano's cone or an
// island's rise, a linear ramp tilts a whole map, an angular sweep drives a
// spiral ridge, and any of them multiplied into a fractal shapes it. The
// Shape primitive has bumps and cones with fixed profiles; this is the
// editor's gradient tool, with the profile, the range along the ramp, the
// repeats and a noise warp as dials.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include "gpx/gradient_shape.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

REGISTER_NODE(
    Gradient, "Primitive",
    "Gradient ramps as a heightmap: linear, reflected, circle (radial), ellipse, "
    "square, diamond, angular (conic) and spiral",
    [](Node &n) {
      n.add_out("output");
      add_choice(n.attrs, "type", "Type",
                 {"Linear", "Reflected", "Circle", "Ellipse", "Square", "Diamond",
                  "Angular", "Spiral"},
                 2)
          .tooltip = "The gradient's shape. Linear rises along the direction\n"
                     "and Reflected rises both ways from the centre line.\n"
                     "Circle, Ellipse, Square and Diamond are high in the middle\n"
                     "and fall to their edge - a hill, a plateau, a pyramid.\n"
                     "Angular sweeps once round the centre; Spiral winds out\n"
                     "from it. Invert (Output) turns any of them over.";
      add_vec2(n.attrs, "center", "Center", 0.5f, 0.5f, -1.f, 2.f, "Placement")
          .tooltip = "Where the gradient is centred, as a fraction of the tile.\n"
                     "Outside 0..1 moves the middle off the map, so only an\n"
                     "edge of the shape crosses it.";
      add_float(n.attrs, "angle", "Direction °", 0.f, -180.f, 180.f, "Placement")
          .tooltip = "Turns the gradient: the way a linear ramp rises, the\n"
                     "long axis of an ellipse or a square, where an angular\n"
                     "sweep starts.";
      add_float(n.attrs, "size", "Size", 0.5f, 0.01f, 4.f, "Placement")
          .tooltip = "How far the gradient reaches from its centre, as a\n"
                     "fraction of the tile: the radius of a circle, half the\n"
                     "side of a square, half the length of a linear ramp. At\n"
                     "0.5 a centred shape just touches the tile's edges.";
      add_float(n.attrs, "aspect", "Stretch", 1.f, 0.1f, 10.f, "Placement")
          .tooltip = "Ellipse, Square and Diamond: how much longer the shape\n"
                     "is along its direction than across it, its area kept.\n"
                     "An ellipse starts twice as long as it is wide; a square\n"
                     "at 1 is square.";
      add_range(n.attrs, "range", "Start / end", 0.f, 1.f, -1.f, 2.f, "Ramp")
          .tooltip = "Where along the gradient it begins and ends, as a\n"
                     "fraction of its size. Pulling the start up leaves a flat\n"
                     "top (a mesa for a centred shape); pulling the end in\n"
                     "gives the shape a steep skirt.";
      add_choice(n.attrs, "profile", "Profile",
                 {"Linear", "Smooth", "Smoother", "Ease in", "Ease out", "Sine",
                  "Dome", "Bell"},
                 1)
          .tooltip = "The curve from one end of the ramp to the other. Linear\n"
                     "is a straight cone; Smooth and Smoother round both ends;\n"
                     "Ease in stays low and rises late, Ease out rises early;\n"
                     "Dome is a round-topped hill with steep flanks; Bell is a\n"
                     "wide soft rise, like a gaussian.";
      add_float(n.attrs, "repeat", "Repeats", 1.f, 1.f, 32.f, "Ramp")
          .tooltip = "How many times the ramp runs over the gradient's size:\n"
                     "concentric rings for a circle, parallel ridges for a\n"
                     "linear ramp, more arms for a spiral.";
      add_choice(n.attrs, "wrap", "Past the end", {"Hold", "Repeat", "Mirror"}, 0)
          .tooltip = "What the gradient does beyond its end and between\n"
                     "repeats. Hold stays at the end's value; Repeat starts\n"
                     "again with a sudden step (terraced rings, sawtooth\n"
                     "ridges); Mirror runs back down smoothly (rolling waves).";
      add_int(n.attrs, "steps", "Terraces", 0, 0, 64, "Ramp")
          .tooltip = "Cuts the ramp into this many flat levels, like contour\n"
                     "steps or rice terraces. 0 keeps it continuous.";
      add_float(n.attrs, "turns", "Spiral turns", 1.5f, -8.f, 8.f, "Ramp")
          .tooltip = "Spiral only: how many times the spiral winds out to its\n"
                     "size. Negative winds the other way.";
      add_float(n.attrs, "roundness", "Corner rounding", 0.f, 0.f, 1.f, "Ramp")
          .tooltip = "Square and Diamond only: rounds the corners, all the way\n"
                     "to a circle (or an ellipse) at 1.";
      add_float(n.attrs, "warp", "Distortion", 0.f, 0.f, 1.f, "Distortion")
          .tooltip = "Pushes the gradient about with noise, so a circle becomes\n"
                     "an island's outline and a ramp a natural slope. 0 is the\n"
                     "exact geometric shape.";
      add_float(n.attrs, "warp_scale", "Distortion scale", 3.f, 0.25f, 32.f, "Distortion")
          .tooltip = "The size of the distortion's features: low bends the\n"
                     "whole shape, high roughens its edge.";
      add_seed(n.attrs);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      gradient::Params p;
      p.type = n.attrs.get_choice("type");
      n.attrs.get_vec2("center", p.cx, p.cy);
      p.angle_deg = n.attrs.get_f("angle", 0.f);
      p.size = n.attrs.get_f("size", 0.5f);
      p.aspect = n.attrs.get_f("aspect", 1.f);
      n.attrs.get_range("range", p.start, p.end);
      p.profile = n.attrs.get_choice("profile");
      p.repeat = n.attrs.get_f("repeat", 1.f);
      p.wrap = n.attrs.get_choice("wrap");
      p.steps = n.attrs.get_i("steps", 0);
      p.turns = n.attrs.get_f("turns", 1.5f);
      p.roundness = n.attrs.get_f("roundness", 0.f);
      const float warp = n.attrs.get_f("warp", 0.f);
      const float ws = n.attrs.get_f("warp_scale", 3.f);
      const uint32_t seed = n.attrs.get_seed("seed");
      noise::FbmParams fp;
      fp.octaves = 4;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            if (warp > 0.f) {
              u += noise::fbm(u * ws, v * ws, seed, fp) * warp * 0.35f;
              v += noise::fbm(u * ws + 17.3f, v * ws + 31.7f, seed + 7u, fp) * warp * 0.35f;
            }
            out.at(x, y) = gradient::value(p, u, v);
          }
      });
      apply_post(n, out);
    })

} // namespace gpx
