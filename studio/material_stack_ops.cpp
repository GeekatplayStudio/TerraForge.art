// Geekatplay TerraForge — the layer-stack surgery behind the Material Editor.
// See material_stack_ops.hpp for why this is not inside the panel.
#include "material_stack_ops.hpp"
#include <algorithm>
#include <cstdio>

namespace studio {

namespace {

// The ports a layer owns, as opposed to the ones carrying the stack along.
// Reordering moves these; it never touches the "below *" wiring.
const char *kOwnPorts[] = {"albedo", "normal", "roughness", "mask", "terrain"};

// (layer output, material input) for the three channels a layer composites.
const char *kChannelPairs[3][2] = {{"albedo", "base color"},
                                   {"normal", "normal"},
                                   {"roughness", "roughness"}};

} // namespace

gpx::Link *layer_incoming(gpx::Graph &g, uint64_t node, const char *port) {
  for (gpx::Link &l : g.links)
    if (l.to_node == node && l.to_port == port) return &l;
  return nullptr;
}

std::vector<gpx::Node *> collect_layers(gpx::Graph &g, gpx::Node *mat) {
  std::vector<gpx::Node *> out;
  if (!mat) return out;
  gpx::Node *n = g.upstream_node(*mat, "base color");
  // the guard is against a malformed file, not against a cycle: add_link
  // already refuses those
  int guard = 0;
  while (n && n->type == "MaterialLayer" && guard++ < 64) {
    out.push_back(n);
    n = g.upstream_node(*n, "below albedo");
  }
  return out;
}

void wire_layer_to_material(gpx::Graph &g, gpx::Node *layer, gpx::Node *mat) {
  if (!layer || !mat) return;
  for (auto &p : kChannelPairs) {
    if (gpx::Link *l = layer_incoming(g, mat->id, p[1])) g.remove_link(l->id);
    g.add_link(layer->id, p[0], mat->id, p[1]);
  }
}

gpx::Node *add_material_layer(gpx::Graph &g, gpx::Node *mat,
                              const std::vector<gpx::Node *> &layers) {
  if (!mat) return nullptr;
  float x = mat->pos_x - 300, y = mat->pos_y;
  for (gpx::Node *l : layers) {
    x = std::min(x, l->pos_x);
    y = std::max(y, l->pos_y + 40);
  }
  gpx::Node *nl = g.add_node("MaterialLayer", x, y);
  if (!nl) return nullptr;
  if (gpx::Attribute *na = nl->attrs.find("name"))
    na->s = "Layer " + std::to_string(layers.size() + 1);

  if (layers.empty()) {
    // adopt whatever was driving the material, so the first Add turns a flat
    // material into the bottom of a stack instead of discarding it
    for (auto &p : kChannelPairs)
      if (gpx::Link *l = layer_incoming(g, mat->id, p[1])) {
        uint64_t fn = l->from_node;
        std::string fp = l->from_port;
        g.remove_link(l->id);
        g.add_link(fn, fp, nl->id, p[0]);
      }
  } else {
    gpx::Node *below = layers.front();
    g.add_link(below->id, "albedo", nl->id, "below albedo");
    g.add_link(below->id, "normal", nl->id, "below normal");
    g.add_link(below->id, "roughness", nl->id, "below rough");
    // inherit the terrain the layer below is keyed on, so a new layer's
    // altitude and slope controls work the moment they are switched on
    if (gpx::Link *t = layer_incoming(g, below->id, "terrain"))
      g.add_link(t->from_node, t->from_port, nl->id, "terrain");
  }
  wire_layer_to_material(g, nl, mat);
  return nl;
}

void delete_material_layer(gpx::Graph &g, gpx::Node *victim, gpx::Node *mat,
                           const std::vector<gpx::Node *> &layers) {
  if (!victim || !mat) return;
  size_t idx = 0;
  while (idx < layers.size() && layers[idx] != victim) ++idx;
  if (idx >= layers.size()) return;

  gpx::Link *bl = layer_incoming(g, victim->id, "below albedo");
  const uint64_t below_id = bl ? bl->from_node : 0;

  if (idx == 0) {
    // it was the top: whatever it sat on now feeds the material directly
    if (gpx::Node *below = below_id ? g.find_node(below_id) : nullptr)
      wire_layer_to_material(g, below, mat);
  } else {
    gpx::Node *above = layers[idx - 1];
    for (const char *p : {"below albedo", "below normal", "below rough"})
      if (gpx::Link *l = layer_incoming(g, above->id, p)) g.remove_link(l->id);
    if (below_id) {
      g.add_link(below_id, "albedo", above->id, "below albedo");
      g.add_link(below_id, "normal", above->id, "below normal");
      g.add_link(below_id, "roughness", above->id, "below rough");
    }
  }
  g.remove_node(victim->id);
}

void swap_material_layers(gpx::Graph &g, gpx::Node *x, gpx::Node *y) {
  if (!x || !y || x == y) return;
  std::swap(x->attrs, y->attrs);
  for (const char *p : kOwnPorts) {
    // read both ends first: removing a link can move the link vector, so the
    // pointers must not outlive the reads
    gpx::Link *lx = layer_incoming(g, x->id, p);
    uint64_t fx = lx ? lx->from_node : 0;
    std::string px = lx ? lx->from_port : std::string();
    gpx::Link *ly = layer_incoming(g, y->id, p);
    uint64_t fy = ly ? ly->from_node : 0;
    std::string py = ly ? ly->from_port : std::string();
    if (lx) g.remove_link(lx->id);
    if ((ly = layer_incoming(g, y->id, p))) g.remove_link(ly->id);
    if (fy) g.add_link(fy, py, x->id, p);
    if (fx) g.add_link(fx, px, y->id, p);
  }
  x->dirty = y->dirty = true;
}

std::string layer_display_name(const gpx::Node *n, size_t index) {
  std::string s = n ? n->attrs.get_s("name") : std::string();
  if (s.empty()) s = "Layer " + std::to_string(index + 1);
  return s;
}

std::string layer_presence_summary(const gpx::Node *n) {
  if (!n) return "";
  std::vector<std::string> bits;
  char b[80];
  if (n->attrs.get_b("use_altitude", false)) {
    float lo, hi;
    n->attrs.get_range("altitude", lo, hi);
    std::snprintf(b, sizeof b, "alt %.2f-%.2f", lo, hi);
    bits.push_back(b);
  }
  if (n->attrs.get_b("use_slope", false)) {
    float lo, hi;
    n->attrs.get_range("slope", lo, hi);
    std::snprintf(b, sizeof b, "slope %.0f-%.0f deg", lo, hi);
    bits.push_back(b);
  }
  if (n->attrs.get_b("use_orientation", false)) {
    std::snprintf(b, sizeof b, "faces %.0f deg",
                  n->attrs.get_f("orientation", 0.f));
    bits.push_back(b);
  }
  if (bits.empty()) return "everywhere";
  std::string s = bits[0];
  for (size_t i = 1; i < bits.size(); ++i) s += ", " + bits[i];
  return s;
}

} // namespace studio
