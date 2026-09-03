// Geekatplay TerraForge — cloud nodes beyond the single CloudLayer.
//
// CloudLayer (nodes_atmosphere.cpp) drives the volumetric layer the viewport
// ray-marches. Vue's cloud chapter (p593-629) is considerably wider: several
// layers, zones that confine a layer, a cloud material editor, spectral
// clouds, SmartClouds. Those are roadmap P5. They are declared here as
// planned nodes so the Atmosphere workspace shows the module and its gaps,
// and so the AI catalogue can say "not yet" instead of inventing something.
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
    CloudZone, "Cloud",
    "[Planned] Confine a cloud layer to a region of the sky",
    [](Node &n) {
      planned(n,
              "A soft box or ellipse that limits where a cloud layer forms, for\n"
              "a storm over one ridge or a clear patch above the camera.",
              "P5 Cloud layers (Vue p597 cloud layer zones)");
    },
    [](Node &) {})

REGISTER_NODE(
    CloudDensityField, "Cloud",
    "[Planned] Drive a cloud layer's density from a field graph",
    [](Node &n) {
      n.add_field_in("density", FieldType::Number, true);
      planned(n,
              "The Terragen 'Pattern' input: a field graph replaces the built-in\n"
              "shape noise, so coverage and detail can follow any function -\n"
              "including terrain altitude below the layer.",
              "P5 Cloud layers (TG p12 density shader)");
    },
    [](Node &) {})

REGISTER_NODE(
    CloudMaterial, "Cloud",
    "[Planned] Shade clouds through the material system",
    [](Node &n) {
      planned(n,
              "Advanced cloud material: colour, opacity, sharpness and lighting\n"
              "response per layer, edited like any other material.",
              "P5 Cloud layers (Vue p598-611)");
    },
    [](Node &) {})

REGISTER_NODE(
    SpectralClouds, "Cloud",
    "[Planned] Spectral / high-altitude cloud model and morphing clouds",
    [](Node &n) {
      planned(n,
              "Spectral clouds (cirrus, thin altostratus) rendered as a separate\n"
              "thin layer, plus simple animation and morphing between states.",
              "P5 Cloud layers (Vue p593-596, p1181)");
    },
    [](Node &) {})

} // namespace gpx
