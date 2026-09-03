// Geekatplay TerraForge — camera nodes (the Cameras workspace).
//
// Cameras are scene objects with real optics (focal length, sensor format,
// aperture, shutter, ISO, film stock). SceneCamera is the network view of one
// of them: studio/scene_nodes_objects.cpp keeps a scene camera in step with
// the node, so a shot can be laid out in the graph, saved with it, keyed by
// the timeline and driven by the AI. CameraPath is live too - it feeds the
// sequence renderer the path a camera rides. The planned nodes record the
// rest of Vue's camera chapter so the module is not lost.
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
    SceneCamera, "Camera",
    "A camera in the scene: position, aim, lens, exposure triangle, film",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Camera", "Camera").tooltip =
          "Name of the scene camera this node drives. Created when missing;\n"
          "an existing camera of that name is adopted.";
      add_bool(n.attrs, "active", "Look through it", false, "Camera").tooltip =
          "Makes this the active camera: the perspective views and the\n"
          "render use it.";
      add_float(n.attrs, "eye_x_m", "Eye X (m)", 2500.f, -100000.f, 100000.f, "Position");
      add_float(n.attrs, "eye_y_m", "Eye height (m)", 2250.f, -10000.f, 100000.f, "Position");
      add_float(n.attrs, "eye_z_m", "Eye Z (m)", 8500.f, -100000.f, 100000.f, "Position");
      add_float(n.attrs, "target_x_m", "Target X (m)", 2500.f, -100000.f, 100000.f, "Aim");
      add_float(n.attrs, "target_y_m", "Target height (m)", 500.f, -10000.f, 100000.f, "Aim");
      add_float(n.attrs, "target_z_m", "Target Z (m)", 2500.f, -100000.f, 100000.f, "Aim");
      add_float(n.attrs, "focal_mm", "Focal length (mm)", 35.f, 8.f, 800.f, "Lens", true);
      add_float(n.attrs, "aperture", "Aperture f/", 8.f, 1.2f, 22.f, "Lens");
      add_float(n.attrs, "shutter_inv", "Shutter 1/x s", 125.f, 0.5f, 8000.f, "Exposure", true);
      add_float(n.attrs, "iso", "ISO", 100.f, 25.f, 25600.f, "Exposure", true);
      add_int(n.attrs, "film", "Film stock", 0, 0, 7, "Exposure").tooltip =
          "Index into the film stock list (see the Camera properties).";
    },
    [](Node &) {})

REGISTER_NODE(
    CameraPath, "Camera",
    "Fly a camera along a path for the rendered sequence",
    [](Node &n) {
      n.add_in("path", DataType::Points);
      add_float(n.attrs, "height_m", "Height above ground (m)", 400.f, 0.f, 50000.f,
                "Flight", true);
      add_bool(n.attrs, "enabled", "Ride the path", true, "Flight").tooltip =
          "When on, the sequence renderer moves the active camera along the\n"
          "connected path over the length of the animation.";
    },
    [](Node &) {})

REGISTER_NODE(
    CameraTarget, "Camera",
    "[Planned] Aim a camera at a scene object and keep tracking it",
    [](Node &n) {
      planned(n,
              "Look-at tracking: the camera keeps a chosen object framed while\n"
              "either of them moves, with look-ahead on animated paths.",
              "P7 Animation (Vue p1164-1167 tracking, look-ahead)");
    },
    [](Node &) {})

REGISTER_NODE(
    DepthOfField, "Camera",
    "[Planned] Focus distance and bokeh from the camera's aperture",
    [](Node &n) {
      planned(n,
              "Thin-lens depth of field from the camera's aperture and a focus\n"
              "distance or focus object; exported to the path tracers, with a\n"
              "post-process approximation in the viewport.",
              "P6 Render (Vue p349-461 rendering)");
    },
    [](Node &) {})

REGISTER_NODE(
    MotionBlur, "Camera",
    "[Planned] Shutter-time motion blur for camera and object movement",
    [](Node &n) {
      planned(n,
              "Blur from the shutter time already on the camera: camera moves,\n"
              "animated objects and moving clouds smear across the exposure.",
              "P7 Animation (Vue p1147)");
    },
    [](Node &) {})

REGISTER_NODE(
    CameraSwitch, "Camera",
    "[Planned] Cut between cameras over the timeline",
    [](Node &n) {
      planned(n,
              "A cut list: which camera renders which frame range, so one\n"
              "sequence can be shot from several viewpoints.",
              "P7 Animation (Vue p1145 camera switching)");
    },
    [](Node &) {})

} // namespace gpx
