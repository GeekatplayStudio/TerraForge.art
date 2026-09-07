// Geekatplay TerraForge - the machine-readable node inventory.
//
// `gen_node_docs` writes the human reference (docs/NODES.md). This writes the
// same walk of the registry as JSON, which is what everything that is not a
// person needs: the node audit ledger, the search index the studio and the
// assistant query, and any tooling that has to know what a node accepts
// without parsing markdown.
//
// One node of each type is really constructed, exactly as the port catalog
// and the docs generator do, so what is recorded is what is built - not what
// a header claims. Regenerate after adding or changing a node:
//
//     cmake --build build --target node_index_gen && build/node_index_gen
//
// The regression suite's node and attribute censuses will remind you when
// this and the registry drift apart.
#include "gpx/node_graph.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace gpx;

namespace {

// JSON string escaping. Tooltips carry newlines and the occasional quote, and
// a reference file that cannot be parsed is worse than no reference file.
std::string esc(const std::string &s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", (unsigned char)c);
          o += b;
        } else {
          o += c;
        }
    }
  }
  return o;
}

const char *port_type(const Port &p) {
  switch (p.type) {
    case DataType::Heightmap: return "heightmap";
    case DataType::Texture: return "texture";
    case DataType::Points: return "points";
    case DataType::Field:
      switch (p.field_type) {
        case FieldType::Color: return "field.color";
        case FieldType::Vector: return "field.vector";
        case FieldType::TexCoord: return "field.uv";
        default: return "field.number";
      }
  }
  return "unknown";
}

const char *attr_type(const Attribute &a) {
  switch (a.type) {
    case AttrType::Float: return "float";
    case AttrType::Int: return "int";
    case AttrType::Bool: return "bool";
    case AttrType::Seed: return "seed";
    case AttrType::Choice: return "choice";
    case AttrType::Color: return "color";
    case AttrType::Gradient: return "gradient";
    case AttrType::Range: return "range";
    case AttrType::Vec2: return "vec2";
    case AttrType::Filename: return "filename";
    case AttrType::Text: return "text";
    case AttrType::Field: return "painted";
  }
  return "value";
}

void write_attr(std::ofstream &f, const Attribute &a) {
  f << "      {\n";
  f << "        \"key\": \"" << esc(a.key) << "\",\n";
  f << "        \"label\": \"" << esc(a.label) << "\",\n";
  f << "        \"type\": \"" << attr_type(a) << "\",\n";
  if (!a.group.empty()) f << "        \"group\": \"" << esc(a.group) << "\",\n";
  switch (a.type) {
    case AttrType::Float:
      f << "        \"min\": " << a.fmin << ", \"max\": " << a.fmax
        << ", \"default\": " << a.fdefault << ",\n";
      break;
    case AttrType::Int:
      f << "        \"min\": " << a.imin << ", \"max\": " << a.imax
        << ", \"default\": " << a.idefault << ",\n";
      break;
    case AttrType::Bool:
      f << "        \"default\": " << (a.bdefault ? "true" : "false") << ",\n";
      break;
    case AttrType::Choice: {
      f << "        \"choices\": [";
      for (size_t i = 0; i < a.labels.size(); ++i)
        f << (i ? ", " : "") << "\"" << esc(a.labels[i]) << "\"";
      f << "],\n";
      break;
    }
    default: break;
  }
  f << "        \"tooltip\": \"" << esc(a.tooltip) << "\"\n";
  f << "      }";
}

} // namespace

int main() {
  Graph g;
  std::map<std::string, std::vector<Node *>> by_cat;
  for (const NodeDef *d : NodeRegistry::instance().all()) {
    Node *n = g.add_node(d->type, 0, 0);
    if (n) by_cat[n->category].push_back(n);
  }
  size_t total = 0;
  for (auto &[cat, nodes] : by_cat) total += nodes.size();

  // Binary, so the newline stays LF on every platform - the same reason
  // gen_node_docs does, and the same 8000-line phantom diff if it does not.
  std::ofstream f("docs/node_index.json", std::ios::binary);
  if (!f) {
    std::fprintf(stderr,
                 "cannot write docs/node_index.json (run from the repo root)\n");
    return 1;
  }

  f << "{\n";
  f << "  \"schema\": 1,\n";
  f << "  \"generated_by\": \"tools/gen_node_index.cpp\",\n";
  f << "  \"node_count\": " << total << ",\n";
  f << "  \"category_count\": " << by_cat.size() << ",\n";
  f << "  \"nodes\": [\n";

  bool first_node = true;
  for (auto &[cat, nodes] : by_cat) {
    for (Node *n : nodes) {
      const NodeDef *d = NodeRegistry::instance().find(n->type);
      if (!first_node) f << ",\n";
      first_node = false;
      f << "    {\n";
      f << "      \"type\": \"" << esc(n->type) << "\",\n";
      // The name a person reads, as against the identifier a file stores.
      // The search index ranks on this as much as on the type.
      f << "      \"display_name\": \"" << esc(node_display_name(n->type))
        << "\",\n";
      f << "      \"category\": \"" << esc(n->category) << "\",\n";
      f << "      \"description\": \""
        << esc(d ? d->description : std::string()) << "\",\n";
      f << "      \"ports\": [\n";
      for (size_t i = 0; i < n->ports.size(); ++i) {
        const Port &p = n->ports[i];
        f << "        {\"name\": \"" << esc(p.name) << "\", \"dir\": \""
          << (p.dir == PortDir::In ? "in" : "out") << "\", \"type\": \""
          << port_type(p) << "\", \"optional\": "
          << (p.optional ? "true" : "false") << "}";
        if (i + 1 < n->ports.size()) f << ",";
        f << "\n";
      }
      f << "      ],\n";
      f << "      \"attrs\": [\n";
      for (size_t i = 0; i < n->attrs.items.size(); ++i) {
        write_attr(f, n->attrs.items[i]);
        if (i + 1 < n->attrs.items.size()) f << ",";
        f << "\n";
      }
      f << "      ]\n";
      f << "    }";
    }
  }
  f << "\n  ]\n}\n";
  f.close();
  std::printf("wrote docs/node_index.json: %zu nodes in %zu categories\n", total,
              by_cat.size());
  return 0;
}
