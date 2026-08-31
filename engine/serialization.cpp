#include "gpx/serialization.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <vector>
#include <json.hpp>
#include <miniz/miniz.h>

using json = nlohmann::json;

namespace gpx {

// ---- painted fields -------------------------------------------------------
// A sculpt layer is far too big to write as a JSON array of floats, so it is
// quantized to 16 bits over its own range, deflated, and base64'd. Sparse
// strokes compress to almost nothing, which also keeps undo snapshots small.
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const std::vector<uint8_t> &in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  for (size_t i = 0; i < in.size(); i += 3) {
    uint32_t v = in[i] << 16;
    if (i + 1 < in.size()) v |= in[i + 1] << 8;
    if (i + 2 < in.size()) v |= in[i + 2];
    out += B64[(v >> 18) & 63];
    out += B64[(v >> 12) & 63];
    out += (i + 1 < in.size()) ? B64[(v >> 6) & 63] : '=';
    out += (i + 2 < in.size()) ? B64[v & 63] : '=';
  }
  return out;
}

static std::vector<uint8_t> b64_decode(const std::string &in) {
  int8_t rev[256];
  std::memset(rev, -1, sizeof rev);
  for (int i = 0; i < 64; ++i) rev[(uint8_t)B64[i]] = (int8_t)i;
  std::vector<uint8_t> out;
  out.reserve(in.size() / 4 * 3);
  uint32_t acc = 0;
  int bits = 0;
  for (char c : in) {
    int8_t d = rev[(uint8_t)c];
    if (d < 0) continue; // '=' and any whitespace
    acc = (acc << 6) | (uint32_t)d;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)((acc >> bits) & 0xff));
    }
  }
  return out;
}

static json field_to_json(const Attribute &a) {
  json j;
  j["fw"] = a.fw;
  j["fh"] = a.fh;
  if (a.field.empty()) return j;
  float mn = a.field[0], mx = a.field[0];
  for (float v : a.field) {
    mn = std::min(mn, v);
    mx = std::max(mx, v);
  }
  j["lo"] = mn;
  j["hi"] = mx;
  float d = (mx - mn) > 1e-20f ? (mx - mn) : 1.f;
  std::vector<uint8_t> raw(a.field.size() * 2);
  for (size_t i = 0; i < a.field.size(); ++i) {
    uint16_t q = (uint16_t)std::lround((a.field[i] - mn) / d * 65535.f);
    raw[i * 2] = (uint8_t)(q & 0xff);
    raw[i * 2 + 1] = (uint8_t)(q >> 8);
  }
  mz_ulong cap = mz_compressBound((mz_ulong)raw.size());
  std::vector<uint8_t> packed(cap);
  if (mz_compress2(packed.data(), &cap, raw.data(), (mz_ulong)raw.size(),
                   MZ_BEST_SPEED) == MZ_OK) {
    packed.resize(cap);
    j["z"] = b64_encode(packed);
  }
  return j;
}

static void field_from_json(Attribute &a, const json &j) {
  if (j.contains("fw")) a.fw = j["fw"].get<int>();
  if (j.contains("fh")) a.fh = j["fh"].get<int>();
  a.field.clear();
  if (!j.contains("z") || a.fw <= 0 || a.fh <= 0) return;
  std::vector<uint8_t> packed = b64_decode(j["z"].get<std::string>());
  size_t count = (size_t)a.fw * a.fh;
  std::vector<uint8_t> raw(count * 2);
  mz_ulong got = (mz_ulong)raw.size();
  if (mz_uncompress(raw.data(), &got, packed.data(),
                    (mz_ulong)packed.size()) != MZ_OK ||
      got != raw.size())
    return; // corrupt payload: leave the field empty rather than guess
  float mn = j.value("lo", 0.f), mx = j.value("hi", 0.f);
  float d = (mx - mn) > 1e-20f ? (mx - mn) : 1.f;
  a.field.resize(count);
  for (size_t i = 0; i < count; ++i) {
    uint16_t q = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
    a.field[i] = mn + (q / 65535.f) * d;
  }
}

static json attr_to_json(const Attribute &a) {
  json j;
  switch (a.type) {
    case AttrType::Float: j["f"] = a.f; break;
    case AttrType::Int:
    case AttrType::Choice: j["i"] = a.i; break;
    case AttrType::Bool: j["b"] = a.b; break;
    case AttrType::Seed: j["seed"] = a.seed; break;
    case AttrType::Range:
    case AttrType::Vec2: j["v2"] = {a.v2[0], a.v2[1]}; break;
    case AttrType::Color: j["col"] = {a.col[0], a.col[1], a.col[2], a.col[3]}; break;
    case AttrType::Gradient: {
      json stops = json::array();
      for (const auto &s : a.stops)
        stops.push_back({s.t, s.r, s.g, s.b, s.a});
      j["stops"] = stops;
    } break;
    case AttrType::Filename:
    case AttrType::Text: j["s"] = a.s; break;
    case AttrType::Field: j = field_to_json(a); break;
  }
  // animation rides along with the value it belongs to
  if (!a.anim.empty()) j["anim"] = track_to_string(a.anim);
  return j;
}

static void attr_from_json(Attribute &a, const json &j) {
  if (j.contains("anim")) track_from_string(a.anim, j["anim"].get<std::string>());
  if (a.type == AttrType::Field) {
    field_from_json(a, j);
    return;
  }
  if (j.contains("f")) a.f = j["f"].get<float>();
  if (j.contains("i")) a.i = j["i"].get<int>();
  if (j.contains("b")) a.b = j["b"].get<bool>();
  if (j.contains("seed")) a.seed = j["seed"].get<uint32_t>();
  if (j.contains("v2")) {
    a.v2[0] = j["v2"][0].get<float>();
    a.v2[1] = j["v2"][1].get<float>();
  }
  if (j.contains("col"))
    for (int k = 0; k < 4; ++k) a.col[k] = j["col"][k].get<float>();
  if (j.contains("stops")) {
    a.stops.clear();
    for (const auto &s : j["stops"])
      a.stops.push_back({s[0].get<float>(), s[1].get<float>(), s[2].get<float>(),
                         s[3].get<float>(), s[4].get<float>()});
  }
  if (j.contains("s")) a.s = j["s"].get<std::string>();
}

std::string graph_to_json(const Graph &g) {
  json j;
  j["app"] = "Geekatplay Studio";
  j["version"] = 2;
  j["resolution"] = g.resolution;
  json nodes = json::array();
  for (const auto &n : g.nodes) {
    json jn;
    jn["id"] = n->id;
    jn["type"] = n->type;
    jn["pos"] = {n->pos_x, n->pos_y};
    json attrs;
    for (const auto &a : n->attrs.items) attrs[a.key] = attr_to_json(a);
    jn["attrs"] = attrs;
    if (!n->enabled) jn["enabled"] = false;
    nodes.push_back(jn);
  }
  j["nodes"] = nodes;
  json links = json::array();
  for (const auto &l : g.links)
    links.push_back({{"from", l.from_node},
                     {"from_port", l.from_port},
                     {"to", l.to_node},
                     {"to_port", l.to_port}});
  j["links"] = links;
  return j.dump(2);
}

bool graph_from_json(Graph &g, const std::string &text, std::string &err) {
  json j;
  try {
    j = json::parse(text);
  } catch (const std::exception &e) {
    err = e.what();
    return false;
  }
  g.clear();
  if (j.contains("resolution")) g.resolution = j["resolution"].get<int>();
  std::map<uint64_t, uint64_t> idmap; // file id -> live id
  for (const auto &jn : j.value("nodes", json::array())) {
    Node *n = g.add_node(jn["type"].get<std::string>(),
                         jn["pos"][0].get<float>(), jn["pos"][1].get<float>());
    if (!n) continue; // unknown node type: skip, keep loading
    idmap[jn["id"].get<uint64_t>()] = n->id;
    n->enabled = jn.value("enabled", true);
    if (jn.contains("attrs"))
      for (auto &a : n->attrs.items)
        if (jn["attrs"].contains(a.key)) attr_from_json(a, jn["attrs"][a.key]);
  }
  for (const auto &jl : j.value("links", json::array())) {
    auto f = idmap.find(jl["from"].get<uint64_t>());
    auto t = idmap.find(jl["to"].get<uint64_t>());
    if (f == idmap.end() || t == idmap.end()) continue;
    g.add_link(f->second, jl["from_port"].get<std::string>(), t->second,
               jl["to_port"].get<std::string>());
  }
  g.mark_all_dirty();
  return true;
}

bool save_project(const Graph &g, const std::string &path) {
  std::ofstream f(path);
  if (!f) return false;
  f << graph_to_json(g);
  return true;
}

bool load_project(Graph &g, const std::string &path, std::string &err) {
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::string text((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  return graph_from_json(g, text, err);
}

// -------------------------------------------------- AI spec -> graph
static void ai_set_attr(Attribute &a, const json &v) {
  try {
    switch (a.type) {
      case AttrType::Float:
        if (v.is_number()) a.f = std::clamp(v.get<float>(), a.fmin, a.fmax);
        break;
      case AttrType::Int:
        if (v.is_number()) a.i = std::clamp(v.get<int>(), a.imin, a.imax);
        break;
      case AttrType::Choice:
        if (v.is_number()) {
          a.i = std::clamp(v.get<int>(), 0, (int)a.labels.size() - 1);
        } else if (v.is_string()) {
          std::string s = v.get<std::string>();
          for (auto &c : s) c = (char)tolower(c);
          for (size_t k = 0; k < a.labels.size(); ++k) {
            std::string l = a.labels[k];
            for (auto &c : l) c = (char)tolower(c);
            if (l.find(s) != std::string::npos || s.find(l) != std::string::npos) {
              a.i = (int)k;
              break;
            }
          }
        }
        break;
      case AttrType::Bool:
        if (v.is_boolean()) a.b = v.get<bool>();
        break;
      case AttrType::Seed:
        if (v.is_number()) a.seed = (uint32_t)std::max(v.get<long long>(), 0LL);
        break;
      case AttrType::Range:
      case AttrType::Vec2:
        if (v.is_array() && v.size() >= 2) {
          a.v2[0] = v[0].get<float>();
          a.v2[1] = v[1].get<float>();
        }
        break;
      case AttrType::Filename:
      case AttrType::Text:
        if (v.is_string()) a.s = v.get<std::string>();
        break;
      // Colour and gradient used to fall through to "do nothing", so the
      // assistant, the Python API and MCP could all ask for a colour and be
      // silently ignored — on a material node, which is where colour matters
      // most.
      case AttrType::Color:
        if (v.is_array() && v.size() >= 3) {
          for (size_t k = 0; k < 4 && k < v.size(); ++k)
            a.col[k] = std::clamp(v[k].get<float>(), 0.f, 1.f);
          if (v.size() < 4) a.col[3] = 1.f; // rgb given: opaque
        } else if (v.is_number()) {
          // a single number is a grey, which is what "set it to 0.2" means
          float g = std::clamp(v.get<float>(), 0.f, 1.f);
          a.col[0] = a.col[1] = a.col[2] = g;
          a.col[3] = 1.f;
        }
        break;
      case AttrType::Gradient:
        if (v.is_array() && !v.empty()) {
          std::vector<GradientStop> stops;
          for (const auto &js : v) {
            if (!js.is_array() || js.size() < 4) continue;
            GradientStop s;
            s.t = std::clamp(js[0].get<float>(), 0.f, 1.f);
            s.r = std::clamp(js[1].get<float>(), 0.f, 1.f);
            s.g = std::clamp(js[2].get<float>(), 0.f, 1.f);
            s.b = std::clamp(js[3].get<float>(), 0.f, 1.f);
            s.a = js.size() > 4 ? std::clamp(js[4].get<float>(), 0.f, 1.f) : 1.f;
            stops.push_back(s);
          }
          // a gradient whose stops are out of order draws nothing sensible
          std::sort(stops.begin(), stops.end(),
                    [](const GradientStop &x, const GradientStop &y) {
                      return x.t < y.t;
                    });
          if (!stops.empty()) a.stops = std::move(stops);
        }
        break;
      default:
        break;
    }
  } catch (...) {
  }
}

bool graph_from_ai_spec(Graph &g, const std::string &spec_json, std::string &err,
                        std::string *environment_json_out, bool merge) {
  json j;
  try {
    // tolerate markdown fences around the JSON
    std::string text = spec_json;
    size_t b = text.find('{');
    size_t e = text.rfind('}');
    if (b == std::string::npos || e == std::string::npos || e <= b) {
      err = "no JSON object in AI response";
      return false;
    }
    j = json::parse(text.substr(b, e - b + 1));
  } catch (const std::exception &ex) {
    err = std::string("AI JSON parse: ") + ex.what();
    return false;
  }
  if (!j.contains("nodes") || !j["nodes"].is_array() || j["nodes"].empty()) {
    err = "AI spec has no nodes";
    return false;
  }
  size_t before = 0;
  if (!merge) g.clear();
  else before = g.nodes.size();
  std::map<std::string, uint64_t> ids;
  float auto_x = 0;
  if (merge) // place merged nodes below the existing graph
    for (auto &n : g.nodes) auto_x = std::max(auto_x, n->pos_x);
  for (const auto &jn : j["nodes"]) {
    if (!jn.contains("type")) continue;
    std::string type = jn["type"].get<std::string>();
    float x = auto_x, y = 100;
    if (jn.contains("pos") && jn["pos"].is_array() && jn["pos"].size() >= 2) {
      x = jn["pos"][0].get<float>();
      y = jn["pos"][1].get<float>();
    }
    Node *n = g.add_node(type, x, y);
    if (!n) continue; // unknown type: skip
    auto_x += 260;
    if (jn.contains("id") && jn["id"].is_string())
      ids[jn["id"].get<std::string>()] = n->id;
    if (jn.contains("attrs") && jn["attrs"].is_object())
      for (auto &[key, val] : jn["attrs"].items())
        if (Attribute *a = n->attrs.find(key)) ai_set_attr(*a, val);
  }
  if (g.nodes.size() == before) {
    err = "AI spec contained no known node types";
    return false;
  }
  for (const auto &jl : j.value("links", json::array())) {
    if (!jl.is_array() || jl.size() < 4) continue;
    auto f = ids.find(jl[0].get<std::string>());
    auto t = ids.find(jl[2].get<std::string>());
    if (f == ids.end() || t == ids.end()) continue;
    g.add_link(f->second, jl[1].get<std::string>(), t->second,
               jl[3].get<std::string>());
  }
  if (environment_json_out && j.contains("environment"))
    *environment_json_out = j["environment"].dump();
  g.mark_all_dirty();
  return true;
}

// ---------------------------------------------------- material library
std::string material_to_json(const Graph &g, uint64_t material_node_id) {
  // upstream closure from the MaterialOutput node
  std::set<uint64_t> keep;
  std::vector<uint64_t> stack{material_node_id};
  while (!stack.empty()) {
    uint64_t id = stack.back();
    stack.pop_back();
    if (!keep.insert(id).second) continue;
    for (const Link &l : g.links)
      if (l.to_node == id) stack.push_back(l.from_node);
  }
  json j;
  j["format"] = "terraforge-material";
  j["version"] = 1;
  Node *mat = g.find_node(material_node_id);
  j["name"] = mat ? mat->attrs.get_s("name") : "Material";
  float min_x = 1e9f, min_y = 1e9f;
  for (uint64_t id : keep)
    if (Node *n = g.find_node(id)) {
      min_x = std::min(min_x, n->pos_x);
      min_y = std::min(min_y, n->pos_y);
    }
  json nodes = json::array();
  for (uint64_t id : keep) {
    Node *n = g.find_node(id);
    if (!n) continue;
    json jn;
    jn["id"] = n->id;
    jn["type"] = n->type;
    jn["pos"] = {n->pos_x - min_x, n->pos_y - min_y};
    jn["output"] = (n->id == material_node_id);
    json attrs;
    for (const auto &at : n->attrs.items) attrs[at.key] = attr_to_json(at);
    jn["attrs"] = attrs;
    if (!n->enabled) jn["enabled"] = false;
    nodes.push_back(jn);
  }
  j["nodes"] = nodes;
  json links = json::array();
  for (const Link &l : g.links)
    if (keep.count(l.from_node) && keep.count(l.to_node))
      links.push_back({{"from", l.from_node},
                       {"from_port", l.from_port},
                       {"to", l.to_node},
                       {"to_port", l.to_port}});
  j["links"] = links;
  return j.dump(2);
}

uint64_t material_from_json(Graph &g, const std::string &text, std::string &err,
                            float x, float y) {
  json j;
  try {
    j = json::parse(text);
  } catch (const std::exception &e) {
    err = e.what();
    return 0;
  }
  if (j.value("format", "") != "terraforge-material") {
    err = "not a TerraForge material file";
    return 0;
  }
  std::map<uint64_t, uint64_t> idmap;
  uint64_t out_id = 0;
  for (const auto &jn : j.value("nodes", json::array())) {
    Node *n = g.add_node(jn["type"].get<std::string>(),
                         x + jn["pos"][0].get<float>(),
                         y + jn["pos"][1].get<float>());
    if (!n) continue;
    idmap[jn["id"].get<uint64_t>()] = n->id;
    n->enabled = jn.value("enabled", true);
    if (jn.contains("attrs"))
      for (auto &at : n->attrs.items)
        if (jn["attrs"].contains(at.key)) attr_from_json(at, jn["attrs"][at.key]);
    if (jn.value("output", false)) out_id = n->id;
  }
  for (const auto &jl : j.value("links", json::array())) {
    auto f = idmap.find(jl["from"].get<uint64_t>());
    auto t = idmap.find(jl["to"].get<uint64_t>());
    if (f == idmap.end() || t == idmap.end()) continue;
    g.add_link(f->second, jl["from_port"].get<std::string>(), t->second,
               jl["to_port"].get<std::string>());
  }
  if (!out_id) err = "material file has no MaterialOutput node";
  g.mark_all_dirty();
  return out_id;
}

std::string registry_catalog_for_ai(const std::vector<std::string> &categories) {
  std::string out;
  Graph probe;
  probe.resolution = 8;
  for (const NodeDef *d : NodeRegistry::instance().all()) {
    if (!categories.empty() &&
        std::find(categories.begin(), categories.end(), d->category) ==
            categories.end())
      continue;
    Node *n = probe.add_node(d->type);
    if (!n) continue;
    out += d->type + " (" + d->category + "): " + d->description + "\n";
    out += "  in:[";
    bool first = true;
    for (auto &p : n->ports)
      if (p.dir == PortDir::In) {
        if (!first) out += ",";
        out += p.name + (p.optional ? "?" : "");
        first = false;
      }
    out += "] out:[";
    first = true;
    for (auto &p : n->ports)
      if (p.dir == PortDir::Out) {
        if (!first) out += ",";
        out += p.name;
        first = false;
      }
    out += "]\n  attrs: ";
    for (auto &a : n->attrs.items) {
      out += a.key;
      char buf[96];
      switch (a.type) {
        case AttrType::Float:
          snprintf(buf, sizeof buf, "(float %.3g..%.3g)", a.fmin, a.fmax);
          out += buf;
          break;
        case AttrType::Int:
          snprintf(buf, sizeof buf, "(int %d..%d)", a.imin, a.imax);
          out += buf;
          break;
        case AttrType::Bool: out += "(bool)"; break;
        case AttrType::Seed: out += "(seed)"; break;
        case AttrType::Choice: {
          out += "(choice:";
          for (size_t k = 0; k < a.labels.size(); ++k) {
            if (k) out += "|";
            out += a.labels[k];
          }
          out += ")";
        } break;
        case AttrType::Range:
        case AttrType::Vec2: out += "([a,b])"; break;
        default: out += "(text)"; break;
      }
      out += " ";
    }
    out += "\n";
  }
  return out;
}

} // namespace gpx


