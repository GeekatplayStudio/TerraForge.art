// Geekatplay TerraForge — the node reference generator.
//
// Every node self-registers with its category, description, ports and
// attributes, tooltips included — which means the reference manual already
// exists in the binary and only needs writing down. This tool walks the
// registry the same way the port catalog does (one real node per type, so
// what is documented is what is constructed) and emits docs/NODES.md.
//
// Regenerate after adding or changing a node:
//     cmake --build build --target node_docs_gen && build/node_docs_gen
// The regression suite's node census will remind you when the two drift.
#include "gpx/node_graph.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace gpx;

static std::string one_line(std::string s) {
  for (char &c : s)
    if (c == '\n') c = ' ';
  return s;
}

static std::string type_name(const Port &p) {
  switch (p.type) {
    case DataType::Heightmap: return "heightmap";
    case DataType::Texture: return "texture";
    case DataType::Field:
      switch (p.field_type) {
        case FieldType::Color: return "field (color)";
        case FieldType::Vector: return "field (vector)";
        case FieldType::TexCoord: return "field (uv)";
        default: return "field (number)";
      }
  }
  return "?";
}

static std::string attr_kind(const Attribute &a) {
  switch (a.type) {
    case AttrType::Float: {
      char b[64];
      std::snprintf(b, sizeof b, "float, %g to %g, default %g", a.fmin, a.fmax,
                    a.fdefault);
      return b;
    }
    case AttrType::Int: {
      char b[64];
      std::snprintf(b, sizeof b, "int, %d to %d, default %d", a.imin, a.imax,
                    a.idefault);
      return b;
    }
    case AttrType::Bool:
      return std::string("toggle, default ") + (a.bdefault ? "on" : "off");
    case AttrType::Seed: return "seed";
    case AttrType::Choice: {
      std::string s = "choice: ";
      for (size_t i = 0; i < a.labels.size(); ++i)
        s += (i ? " / " : "") + a.labels[i];
      return s;
    }
    case AttrType::Color: return "color";
    case AttrType::Gradient: return "gradient";
    case AttrType::Range: return "range";
    case AttrType::Vec2: return "x/y pair";
    case AttrType::Filename: return "file path";
    case AttrType::Text: return "text";
    case AttrType::Field: return "painted buffer";
    default: return "value";
  }
}

int main() {
  Graph g;
  // category -> [node]; the registry is already sorted by category then name
  std::map<std::string, std::vector<Node *>> by_cat;
  for (const NodeDef *d : NodeRegistry::instance().all()) {
    Node *n = g.add_node(d->type, 0, 0);
    if (n) by_cat[n->category].push_back(n);
  }

  std::ofstream f("docs/NODES.md");
  if (!f) {
    std::fprintf(stderr, "cannot write docs/NODES.md (run from the repo root)\n");
    return 1;
  }

  size_t total = 0;
  for (auto &[cat, nodes] : by_cat) total += nodes.size();

  f << "# Node reference\n\n";
  f << "Every node in Geekatplay TerraForge — " << total << " across "
    << by_cat.size() << " categories. Generated from the registry itself by "
    << "`tools/gen_node_docs.cpp`, so what is written here is what is "
    << "constructed; regenerate with the `node_docs_gen` target after adding "
    << "a node.\n\n";

  f << "| Category | Nodes |\n| :--- | :--- |\n";
  for (auto &[cat, nodes] : by_cat) {
    f << "| [" << cat << "](#"
      << ([](std::string s) {
           for (char &c : s) {
             if (c == ' ') c = '-';
             else c = (char)tolower((unsigned char)c);
           }
           return s;
         })(cat)
      << ") | " << nodes.size() << " |\n";
  }
  f << "\n";

  for (auto &[cat, nodes] : by_cat) {
    f << "## " << cat << "\n\n";
    for (Node *n : nodes) {
      const NodeDef *d = NodeRegistry::instance().find(n->type);
      f << "### " << n->type << "\n\n";
      if (d && !d->description.empty()) f << d->description << "\n\n";
      bool any_in = false, any_out = false;
      for (const Port &p : n->ports)
        (p.dir == PortDir::In ? any_in : any_out) = true;
      if (any_in || any_out) {
        f << "| Port | Direction | Type |\n| :--- | :--- | :--- |\n";
        for (const Port &p : n->ports)
          f << "| " << p.name << " | "
            << (p.dir == PortDir::In ? (p.optional ? "in (optional)" : "in")
                                     : "out")
            << " | " << type_name(p) << " |\n";
        f << "\n";
      }
      if (!n->attrs.items.empty()) {
        f << "| Parameter | Kind | Notes |\n| :--- | :--- | :--- |\n";
        for (const Attribute &a : n->attrs.items)
          f << "| " << (a.label.empty() ? a.key : a.label) << " | "
            << attr_kind(a) << " | " << one_line(a.tooltip) << " |\n";
        f << "\n";
      }
    }
  }
  std::printf("wrote docs/NODES.md: %zu nodes in %zu categories\n", total,
              by_cat.size());
  return 0;
}
