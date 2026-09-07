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
      add_filename(n.attrs, "path", "Output file", "terraforge_render.png", "Output")
          .tooltip = "Where the finished image is written.";
      add_choice(n.attrs, "format", "Beauty format",
                 {"PNG 8-bit (tone mapped)", "EXR float (linear)", "HDR Radiance (linear)"},
                 0, "Output")
          .tooltip = "Passes are always written as linear float EXR beside the\n"
                     "beauty, whatever this is.";
      add_int(n.attrs, "width", "Width", 1920, 64, 8192, "Output")
          .tooltip = "The width of the rendered image, in pixels.";
      add_int(n.attrs, "height", "Height", 1080, 64, 8192, "Output")
          .tooltip = "The height of the rendered image, in pixels.";
      add_choice(n.attrs, "engine", "Engine",
                 {"Mitsuba 3", "Blender Cycles", "LuxCoreRender", "appleseed",
                  "OpenGL viewport"},
                 4, "Engine")
          .tooltip = "Which renderer produces the frame. The viewport engine is\n"
                     "immediate; the path tracer is slower and resolves real\n"
                     "light transport.";
      add_int(n.attrs, "samples", "Samples", 128, 8, 4096, "Engine")
          .tooltip = "How many samples each pixel gets. This is the direct trade\n"
                     "between noise and time - noise falls with the square root,\n"
                     "so four times the samples is half the noise.";
    },
    [](Node &) {})

REGISTER_NODE(
    RenderPasses, "Render",
    "Which channels the render writes beside the beauty: depth, normal, id, light, atmosphere...",
    [](Node &n) {
      const char *G = "Geometry";
      add_bool(n.attrs, "depth", "Depth (metres)", true, G)
          .tooltip = "Writes a linear EXR of distance from the camera to the\n"
                     "surface beside the beauty pass, for compositing.";
      add_bool(n.attrs, "normal", "World normal", true, G)
          .tooltip = "Writes a linear EXR of the direction each surface faces\n"
                     "beside the beauty pass, for compositing.";
      add_bool(n.attrs, "position", "World position", false, G)
          .tooltip = "Writes a linear EXR of the world position of each surface\n"
                     "point beside the beauty pass, for compositing.";
      add_bool(n.attrs, "object_id", "Object id", true, G)
          .tooltip = "Writes a linear EXR of which object each pixel belongs to\n"
                     "beside the beauty pass, for compositing.";
      add_bool(n.attrs, "water_mask", "Water mask", false, G)
          .tooltip = "Writes a linear EXR of where water covers the frame beside\n"
                     "the beauty pass, for compositing.";
      const char *L = "Light";
      add_bool(n.attrs, "albedo", "Albedo", true, L)
          .tooltip = "Writes a linear EXR of the surface colour with no lighting\n"
                     "on it beside the beauty pass, for compositing.";
      add_bool(n.attrs, "direct", "Direct sun light", false, L)
          .tooltip = "Writes a linear EXR of light arriving straight from a\n"
                     "source beside the beauty pass, for compositing.";
      add_bool(n.attrs, "shadow", "Shadow mask", false, L)
          .tooltip = "Writes a linear EXR of where light is blocked beside the\n"
                     "beauty pass, for compositing.";
      add_bool(n.attrs, "ambient", "Sky / ambient light", false, L)
          .tooltip = "Writes a linear EXR of light arriving from the sky as a\n"
                     "whole beside the beauty pass, for compositing.";
      add_bool(n.attrs, "specular", "Specular / reflection", false, L)
          .tooltip = "Writes a linear EXR of the highlights alone beside the\n"
                     "beauty pass, for compositing.";
      const char *A = "Atmosphere";
      add_bool(n.attrs, "atmosphere", "Fog & haze (rgb + transmittance)", false, A)
          .tooltip = "Writes a linear EXR of the haze and fog between camera and\n"
                     "surface beside the beauty pass, for compositing.";
      add_bool(n.attrs, "environment", "Sky & backdrop only", false, A)
          .tooltip = "Writes a linear EXR of light arriving from the backdrop\n"
                     "beside the beauty pass, for compositing.";
    },
    [](Node &) {})

REGISTER_NODE(
    RenderBackdrop, "Render",
    "An HDR image dome at infinity behind the scene, hazed and clouded by the atmosphere",
    [](Node &n) {
      add_bool(n.attrs, "enabled", "Enabled", true, "Image")
          .tooltip = "Turns the backdrop off without losing its settings.";
      add_filename(n.attrs, "file", "HDR image (.hdr / .exr / .png / .jpg)", "", "Image")
          .tooltip = "The panoramic image behind the scene. It lights nothing -\n"
                     "this is what the camera sees past the terrain, not an\n"
                     "environment light.";
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
      add_bool(n.attrs, "flip", "Mirror horizontally", false, "Image")
          .tooltip = "Mirrors the panorama, for when it was shot or stored the\n"
                     "other way round.";
      add_float(n.attrs, "yaw", "Rotate °", 0.f, -180.f, 180.f, "Placement")
          .tooltip = "Turns the backdrop about the vertical, to put its\n"
                     "interesting part behind the shot.";
      add_float(n.attrs, "pitch", "Tilt °", 0.f, -90.f, 90.f, "Placement")
          .tooltip = "Tilts the backdrop, to raise or drop its horizon against\n"
                     "the terrain's.";
      add_float(n.attrs, "exposure", "Exposure (EV)", 0.f, -10.f, 10.f, "Look")
          .tooltip = "How bright the backdrop is, in stops. Matching it to the\n"
                     "scene's own exposure is what stops the join being visible.";
      add_color(n.attrs, "tint", "Tint", 1.f, 1.f, 1.f, 1.f, "Look")
          .tooltip = "A colour cast over the backdrop alone, for matching it to\n"
                     "the scene's light.";
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
      add_float(n.attrs, "exposure", "Exposure multiplier", 1.f, 0.1f, 10.f, "Grade", true)
          .tooltip = "How much light the picture is given, in stops.";
      add_float(n.attrs, "saturation", "Saturation", 1.f, 0.f, 2.f, "Grade")
          .tooltip = "How strong the colour is in the finished frame.";
      add_color(n.attrs, "tint", "Tint", 1.f, 1.f, 1.f, 1.f, "Grade")
          .tooltip = "A colour cast over the whole frame - the grade, not the\n"
                     "lighting.";
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
