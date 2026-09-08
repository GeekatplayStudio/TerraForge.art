// Geekatplay TerraForge — MaterialSource: another object's material, as an
// input to this one.
//
// A material in this application is a MaterialOutput node with the channels
// wired into it, and an object points at one. Until now the only way to reuse
// a material was to build it again, or to assign the same one and lose the
// ability to change it for the second object - there was no way to say "the
// terrain's material, but rougher".
//
// This node says it. Name a scene object and its material's channels come out
// of this node, ready to be filtered, tinted, blended and sent into a
// MaterialOutput of your own. The textures arrive exactly as the source built
// them, so whatever mapping, tiling and projection went into it comes with
// it - a copy that has been modified, not a re-creation that has to be
// matched by eye.
//
// The scene is not something the engine knows about, so the studio resolves
// the name (studio/scene_nodes_material.cpp) and writes the material node's
// id into `source_node`. This node reads only that. The same split as every
// other node in the object family: the graph declares the intent, the studio
// answers the question only it can answer.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <cstdlib>
#include <string>

namespace gpx {

namespace {

// The channels a MaterialOutput takes in, which are the ones this hands out.
// One list, used to declare the ports and to copy them, so the two cannot
// come to disagree about what a material is made of.
struct Channel {
  const char *name;
  bool texture; // false = the displacement heightmap
};
const Channel CHANNELS[] = {
    {"base color", true},        {"normal", true},
    {"roughness", true},         {"metallic", true},
    {"height", true},            {"ambient occlusion", true},
    {"alpha", true},             {"displacement", false},
};

uint64_t source_id(const Node &n) {
  // Stored as text: an id outgrows an int attribute's range, and the node
  // has to survive being saved and loaded with the id it was given.
  const std::string s = n.attrs.get_s("source_node");
  return s.empty() ? 0ull : std::strtoull(s.c_str(), nullptr, 10);
}

} // namespace

REGISTER_NODE(
    MaterialSource, "Material",
    "Brings another object's material into this graph, channel by channel, so "
    "it can be reused and altered instead of rebuilt",
    [](Node &n) {
      for (const Channel &c : CHANNELS) {
        if (c.texture) n.add_out(c.name, DataType::Texture);
        else n.add_out(c.name, DataType::Heightmap);
      }
      add_text(n.attrs, "object", "From object", "", "Source")
          .object_ref = true;
      n.attrs.find("object")->tooltip =
          "The scene object whose material to bring in - the terrain, a "
          "rock,\nanything that has one. Its channels come out of this node "
          "as they\nare, so the mapping and tiling it was built with come "
          "with them.\n\nLeave it empty and the node passes nothing, which "
          "is what an\nunconnected input looks like anywhere else.";
      // Written by the studio after it resolves the name above. Not for
      // typing into, but saved, so a project that is loaded before its scene
      // has settled still knows which node it meant.
      Attribute &src = add_text(n.attrs, "source_node", "Material node", "",
                                "Source");
      src.node_ref = true; // renumbered with the graph when a project loads
      src.tooltip = "The material node the name above resolved to. The studio "
                    "fills\nthis in; it is here so it can be saved and so a "
                    "script can set it\ndirectly.";
    },
    [](Node &n) {
      const uint64_t id = source_id(n);
      Node *src = id && n.graph ? n.graph->find_node(id) : nullptr;
      if (!src) {
        // Empty outputs rather than an error: a material graph half-built is
        // the normal state of one being built, and an unresolved source
        // reads the same as an unconnected input everywhere else.
        for (const Channel &c : CHANNELS) {
          if (c.texture) n.out_tex(c.name) = TextureRGBA();
          else n.out_hmap(c.name) = Heightmap();
        }
        if (!n.attrs.get_s("object").empty())
          n.error = "no material found on '" + n.attrs.get_s("object") + "'";
        return;
      }
      if (src->type != "MaterialOutput") {
        n.error = "'" + src->type + "' is not a material";
        return;
      }
      // What the source material *receives* is what the material is; its own
      // single output is only a preview thumbnail. So this reads the source's
      // resolved inputs - which follow links, bypasses and conversions
      // already, so a channel fed through a bypassed node arrives here the
      // same way it arrives at the material.
      for (const Channel &c : CHANNELS) {
        if (c.texture) {
          const TextureRGBA *t = src->in_tex(c.name);
          n.out_tex(c.name) = t ? *t : TextureRGBA();
        } else {
          const Heightmap *h = src->in_hmap(c.name);
          n.out_hmap(c.name) = h ? *h : Heightmap();
        }
      }
    },
    // The source has to be computed before this node reads it, and nothing
    // links them, so the edge is declared. Without it the importer reads
    // whatever the source held on the previous evaluation.
    [](const Node &n, std::vector<uint64_t> &out) {
      if (uint64_t id = source_id(n)) out.push_back(id);
    })

} // namespace gpx
