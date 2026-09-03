// Geekatplay TerraForge — the render editor's nodes (the Render workspace).
//
// RenderCamera and RenderQuality (nodes_atmosphere.cpp) were the first two.
// This file adds the rest of a render network: the master output with its
// file format, the pass list (G-buffer channels written beside the beauty),
// the HDR backdrop dome, and post-processing. studio/scene_nodes.cpp copies
// them into RenderSettings after every evaluation, so when the nodes are
// present they are the source of truth and the Render panel mirrors them.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

namespace {
void planned(Node &n, const char *what, const char *phase) {
  add_text(n.attrs, "plan", "Planned", what, "Roadmap").tooltip =
      "This node is a placeholder: it documents a capability on the roadmap\n"
      "so the module is not forgotten. It has no effect on the scene yet.";
  add_text(n.attrs, "phase", "Roadmap phase", phase, "Roadmap");
}
} // namespace

REGISTER_NODE(
    RenderOutput, "Render",
    "Master output: file, format, size, engine and samples of the render",
    [](Node &n) {
      add_filename(n.attrs, "path", "Output file", "terraforge_render.png", "Output");
      add_choice(n.attrs, "format", "Beauty format",
                 {"PNG 8-bit (tone mapped)", "EXR float (linear)", "HDR Radiance (linear)"},
                 0, "Output")
          .tooltip = "Passes are always written as linear float EXR beside the\n"
                     "beauty, whatever this is.";
      add_int(n.attrs, "width", "Width", 1920, 64, 8192, "Output");
      add_int(n.attrs, "height", "Height", 1080, 64, 8192, "Output");
      add_choice(n.attrs, "engine", "Engine",
                 {"Mitsuba 3", "Blender Cycles", "LuxCoreRender", "appleseed",
                  "OpenGL viewport"},
                 4, "Engine");
      add_int(n.attrs, "samples", "Samples", 128, 8, 4096, "Engine");
    },
    [](Node &) {})

REGISTER_NODE(
    RenderPasses, "Render",
    "Which channels the render writes beside the beauty: depth, normal, id, light, atmosphere...",
    [](Node &n) {
      const char *G = "Geometry";
      add_bool(n.attrs, "depth", "Depth (metres)", true, G);
      add_bool(n.attrs, "normal", "World normal", true, G);
      add_bool(n.attrs, "position", "World position", false, G);
      add_bool(n.attrs, "object_id", "Object id", true, G);
      add_bool(n.attrs, "water_mask", "Water mask", false, G);
      const char *L = "Light";
      add_bool(n.attrs, "albedo", "Albedo", true, L);
      add_bool(n.attrs, "direct", "Direct sun light", false, L);
      add_bool(n.attrs, "shadow", "Shadow mask", false, L);
      add_bool(n.attrs, "ambient", "Sky / ambient light", false, L);
      add_bool(n.attrs, "specular", "Specular / reflection", false, L);
      const char *A = "Atmosphere";
      add_bool(n.attrs, "atmosphere", "Fog & haze (rgb + transmittance)", false, A);
      add_bool(n.attrs, "environment", "Sky & backdrop only", false, A);
    },
    [](Node &) {})

REGISTER_NODE(
    RenderBackdrop, "Render",
    "An HDR image dome at infinity behind the scene, hazed and clouded by the atmosphere",
    [](Node &n) {
      add_bool(n.attrs, "enabled", "Enabled", true, "Image");
      add_filename(n.attrs, "file", "HDR image (.hdr / .exr / .png / .jpg)", "", "Image");
      add_choice(n.attrs, "mapping", "Mapping",
                 {"Equirectangular (lat-long)", "Angular map (light probe)",
                  "Mirror ball", "Cube map cross", "Cylindrical panorama",
                  "Sky dome (hemisphere)", "Planar backdrop"},
                 0, "Image")
          .tooltip = "How pixels map onto directions. Lat-long is what HDRI\n"
                     "libraries ship; a cross is detected as horizontal or\n"
                     "vertical by its aspect; cylindrical and planar use the\n"
                     "vertical field of view below.";
      add_float(n.attrs, "vfov", "Vertical field of view °", 90.f, 5.f, 179.f, "Image")
          .tooltip = "Cylindrical panorama and planar backdrop only.";
      add_bool(n.attrs, "flip", "Mirror horizontally", false, "Image");
      add_float(n.attrs, "yaw", "Rotate °", 0.f, -180.f, 180.f, "Placement");
      add_float(n.attrs, "pitch", "Tilt °", 0.f, -90.f, 90.f, "Placement");
      add_float(n.attrs, "exposure", "Exposure (EV)", 0.f, -10.f, 10.f, "Look");
      add_color(n.attrs, "tint", "Tint", 1.f, 1.f, 1.f, 1.f, "Look");
      add_float(n.attrs, "blend", "Blend over the sky", 1.f, 0.f, 1.f, "Look").tooltip =
          "1 replaces the procedural sky with the image; lower values mix.\n"
          "Where a mapping has no pixel (below a sky dome, outside a planar\n"
          "backdrop) the procedural sky shows through.";
      add_float(n.attrs, "haze", "Atmosphere on the dome", 1.f, 0.f, 1.f, "Look").tooltip =
          "How much horizon haze and fog the dome receives, as if at\n"
          "infinite distance. Clouds always draw in front of it.";
      add_bool(n.attrs, "hide_sun", "Hide the sun disc", true, "Look").tooltip =
          "An HDRI usually contains its own sun.";
    },
    [](Node &) {})

REGISTER_NODE(
    PostProcess, "Render",
    "Image finishing after tone mapping: exposure, saturation, colour tint",
    [](Node &n) {
      add_float(n.attrs, "exposure", "Exposure multiplier", 1.f, 0.1f, 10.f, "Grade", true);
      add_float(n.attrs, "saturation", "Saturation", 1.f, 0.f, 2.f, "Grade");
      add_color(n.attrs, "tint", "Tint", 1.f, 1.f, 1.f, 1.f, "Grade");
      add_float(n.attrs, "vignette", "Vignette", 0.f, 0.f, 1.f, "Lens").tooltip =
          "Recorded for the offline post pass; the viewport ignores it for\n"
          "now (roadmap P6 post-render options).";
    },
    [](Node &) {})

REGISTER_NODE(
    RenderRegion, "Render",
    "[Planned] Render only a rectangle of the frame",
    [](Node &n) {
      planned(n,
              "A crop rectangle for test renders and re-renders of one area,\n"
              "in the viewport engine and exported to the offline engines.",
              "P6 Render (Vue p351-368 render area)");
    },
    [](Node &) {})

REGISTER_NODE(
    RenderLayers, "Render",
    "[Planned] Objects and lights sorted into render layers for compositing",
    [](Node &n) {
      planned(n,
              "Layer membership per object with holdouts, so foreground,\n"
              "terrain and sky can be rendered and composited separately.",
              "P6 Render (Vue p369-378 multi-pass)");
    },
    [](Node &) {})

} // namespace gpx
