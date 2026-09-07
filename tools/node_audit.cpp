// Geekatplay TerraForge - the node audit report.
//
// The node audit programme asks, of every node: does it explain itself, are
// its connectors right, is its name something a person would choose? With 245
// nodes those are not questions to answer one at a time by reading - they are
// questions to ask the registry, so the answer is a list of what actually
// needs a hand rather than a list of everything.
//
// This reports; it does not fail. The checks that survive a clean sweep get
// promoted into the contract battery (tests/cpp/test_nodes.cpp), where they
// become things that cannot regress. Reporting first is deliberate: a gate
// switched on over 245 pre-existing gaps is a gate someone turns off.
//
//     cmake --build build --target node_audit && build/node_audit
//     build/node_audit --json     (machine-readable, for the ledger)
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace gpx;

namespace {

struct Finding {
  std::string code, detail;
};

// The same classification the contract battery uses (tests/cpp/test_nodes.cpp).
// A node with no output is a defect only if it was supposed to compute
// something: a light, a camera, a scene object, a cloud or a render setting
// configures the scene instead, and a sink consumes without producing. Getting
// this wrong made the first run of this tool report 36 findings of which every
// single one was correct behaviour - which is how an audit tool teaches people
// to ignore it.
bool is_sink(const std::string &t) {
  return t == "ExportMesh" || t == "ExportTexture" ||
         t == "TerrainDisplacement" || t == "TerrainSurface" ||
         t == "SurfaceDisplacement";
}
bool is_container(const std::string &t) { return t == "MetaNode"; }
bool is_config_node(const std::string &t) {
  if (t == "SunLight" || t == "AtmosphereSettings" || t == "CloudLayer" ||
      t == "WaterLayer" || t == "RenderCamera" || t == "RenderQuality")
    return true;
  const NodeDef *d = NodeRegistry::instance().find(t);
  if (!d) return false;
  const std::string &c = d->category;
  if (d->description.rfind("[Planned]", 0) == 0) return true;
  return c == "Light" || c == "Camera" || c == "Scene" || c == "Cloud" ||
         c == "Render" || (c == "Animation" && t == "AnimationSequence");
}

// A trailing single capital is a channel or an operand label - "input A",
// "mask R", "splat B" - and is exactly right. Only a capital inside a word is
// out of step with the rest of the port names.
bool port_name_miscased(const std::string &n) {
  for (size_t i = 0; i < n.size(); ++i) {
    if (!std::isupper((unsigned char)n[i])) continue;
    const bool lone = (i + 1 == n.size() || n[i + 1] == ' ') &&
                      (i == 0 || n[i - 1] == ' ');
    if (!lone) return true;
  }
  return false;
}

// A type name a person would not have chosen. Two signals, both weak on their
// own and reported rather than enforced: a digit suffix (Fractal2 is a
// version, not a name), and a bare noun+noun compound with no display name to
// put in front of it. The display name is the fix for all of them; this only
// says which ones are worst without one.
bool name_is_jargon(const std::string &t) {
  if (!t.empty() && std::isdigit((unsigned char)t.back())) return true;
  int caps = 0;
  for (char c : t)
    if (std::isupper((unsigned char)c)) ++caps;
  return caps >= 3; // three or more words jammed together
}

std::string words_of(const std::string &t) {
  std::string o;
  for (size_t i = 0; i < t.size(); ++i) {
    if (i && std::isupper((unsigned char)t[i]) &&
        !std::isupper((unsigned char)t[i - 1]))
      o += ' ';
    o += t[i];
  }
  return o;
}

// A [Planned] node is a placeholder: a name, a category and a sentence saying
// what it will become, with no ports and a `plan` attribute standing in for
// the real ones. Auditing one for tooltips measures nothing. They are counted
// and listed apart, because they are the honest record of what the catalogue
// does not have yet - which is a different question from whether what it does
// have explains itself.
bool is_planned(const NodeDef *d) {
  return d->description.rfind("[Planned]", 0) == 0;
}

void audit(const NodeDef *d, Node *n, std::vector<Finding> &out) {
  // ---- does it explain itself (step 8) -------------------------------
  if (d->description.size() < 25)
    out.push_back({"THIN_DESC", "description is " +
                                    std::to_string(d->description.size()) +
                                    " chars: \"" + d->description + "\""});
  int no_tip = 0;
  std::string first_missing;
  for (const Attribute &a : n->attrs.items) {
    // A seed explains itself; so does a plain name field.
    if (a.type == AttrType::Seed) continue;
    if (a.tooltip.empty()) {
      if (first_missing.empty()) first_missing = a.key;
      ++no_tip;
    }
  }
  if (no_tip > 0)
    out.push_back({"NO_TOOLTIP",
                   std::to_string(no_tip) + " of " +
                       std::to_string(n->attrs.items.size()) +
                       " parameters have no tooltip (first: " + first_missing +
                       ")"});

  // ---- are the labels doing any work ---------------------------------
  for (const Attribute &a : n->attrs.items)
    if (!a.label.empty() && a.label == a.key)
      out.push_back({"LABEL_IS_KEY",
                     "parameter '" + a.key + "' is labelled with its own key"});

  // ---- connectors (step 4) -------------------------------------------
  //
  // Declaration order is deliberately NOT checked. The node editor keeps a
  // separate row counter per direction and pins inputs to the left edge and
  // outputs to the right (studio/panel_graph_draw.cpp), so a port's position
  // among its own kind is all that shows - and `add_universal_blend` appends
  // its optional mask input after the outputs on 32 nodes, every one of which
  // draws correctly. Checking it reported 32 findings and 32 non-problems.
  int outs = 0;
  for (const Port &p : n->ports)
    if (p.dir == PortDir::Out) ++outs;
  if (outs == 0 && !is_sink(n->type) && !is_container(n->type) &&
      !is_config_node(n->type))
    out.push_back({"NO_OUTPUT", "computes nothing and is not a sink, a "
                                "container or a configuration node"});
  for (const Port &p : n->ports)
    if (port_name_miscased(p.name))
      out.push_back({"PORT_CASE", "port '" + p.name +
                                      "' is not lower case like the rest"});

  // ---- name (step 9) -------------------------------------------------
  if (name_is_jargon(n->type))
    out.push_back({"NAME", "type '" + n->type + "' reads as \"" +
                               words_of(n->type) +
                               "\" - needs a display name"});
}

} // namespace

int main(int argc, char **argv) {
  const bool as_json = argc > 1 && std::strcmp(argv[1], "--json") == 0;

  Graph g;
  std::map<std::string, std::vector<Node *>> by_cat;
  for (const NodeDef *d : NodeRegistry::instance().all()) {
    Node *n = g.add_node(d->type, 0, 0);
    if (n) by_cat[n->category].push_back(n);
  }

  std::map<std::string, std::vector<Finding>> findings;
  std::map<std::string, int> by_code;
  std::vector<std::string> planned;
  size_t total = 0, clean = 0;
  for (auto &[cat, nodes] : by_cat)
    for (Node *n : nodes) {
      const NodeDef *d = NodeRegistry::instance().find(n->type);
      if (!d) continue;
      if (is_planned(d)) {
        planned.push_back(n->type);
        continue;
      }
      ++total;
      std::vector<Finding> f;
      audit(d, n, f);
      if (f.empty()) {
        ++clean;
      } else {
        for (const Finding &x : f) ++by_code[x.code];
        findings[n->type] = std::move(f);
      }
    }

  if (as_json) {
    std::printf("{\n  \"total\": %zu,\n  \"clean\": %zu,\n  \"nodes\": {\n",
                total, clean);
    bool first = true;
    for (auto &[type, fs] : findings) {
      if (!first) std::printf(",\n");
      first = false;
      std::printf("    \"%s\": [", type.c_str());
      for (size_t i = 0; i < fs.size(); ++i)
        std::printf("%s\"%s\"", i ? ", " : "", fs[i].code.c_str());
      std::printf("]");
    }
    std::printf("\n  }\n}\n");
    return 0;
  }

  std::printf("Node audit - %zu implemented nodes, %zu with nothing to answer "
              "for\n%zu [Planned] placeholders, not audited\n\n",
              total, clean, planned.size());
  std::printf("By finding:\n");
  for (auto &[code, count] : by_code)
    std::printf("  %-14s %4d\n", code.c_str(), count);
  std::printf("\n");

  for (auto &[cat, nodes] : by_cat) {
    bool header = false;
    for (Node *n : nodes) {
      auto it = findings.find(n->type);
      if (it == findings.end()) continue;
      if (!header) {
        std::printf("== %s ==\n", cat.c_str());
        header = true;
      }
      std::printf("  %s\n", n->type.c_str());
      for (const Finding &f : it->second)
        std::printf("      [%s] %s\n", f.code.c_str(), f.detail.c_str());
    }
    if (header) std::printf("\n");
  }
  if (!planned.empty()) {
    std::printf("== [Planned] placeholders (%zu) ==\n  ", planned.size());
    for (size_t i = 0; i < planned.size(); ++i)
      std::printf("%s%s", i ? ", " : "", planned[i].c_str());
    std::printf("\n");
  }
  return 0;
}
