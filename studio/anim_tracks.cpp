// Geekatplay TerraForge - track enumeration and resolution. See anim_tracks.hpp.
#include "anim_tracks.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "theme_colors.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <imgui.h>

namespace studio {

namespace {

void push_object_tracks(App &a, std::vector<TrackRef> &out, int i, bool animated_only) {
  SceneObject &o = scene().objects[(size_t)i];
  for (const AnimProp *p : anim_props_for(o)) {
    for (int c = 0; c < p->comps; ++c) {
      std::string key = anim_key(*p, c);
      auto it = o.anim.find(key);
      bool has = it != o.anim.end() && (it->second.animated() || !it->second.modifiers.empty());
      if (animated_only && !has) continue;
      TrackRef r;
      r.kind = TrackRef::Object;
      r.id = "o:" + std::to_string(i) + ":" + key;
      r.owner = o.name;
      r.group = p->group;
      r.label = p->label;
      r.comp = p->comps > 1 ? c : -1;
      r.color = p->color;
      r.object = i;
      r.prop = p;
      r.track = has ? &it->second : nullptr;
      out.push_back(r);
    }
  }
  (void)a;
}

void push_world_tracks(std::vector<TrackRef> &out, bool animated_only) {
  SceneState &sc = scene();
  for (const AnimProp &p : anim_world_props()) {
    for (int c = 0; c < p.comps; ++c) {
      std::string key = anim_key(p, c);
      auto it = sc.world_anim.find(key);
      bool has = it != sc.world_anim.end() && (it->second.animated() || !it->second.modifiers.empty());
      if (animated_only && !has) continue;
      TrackRef r;
      r.kind = TrackRef::World;
      r.id = "w:" + key;
      r.owner = "World";
      r.group = p.group;
      r.label = p.label;
      r.comp = p.comps > 1 ? c : -1;
      r.color = p.color;
      r.prop = &p;
      r.track = has ? &it->second : nullptr;
      out.push_back(r);
    }
  }
}

bool attr_keyable(const gpx::Attribute &at) {
  switch (at.type) {
    case gpx::AttrType::Float: case gpx::AttrType::Int: case gpx::AttrType::Bool:
    case gpx::AttrType::Choice: case gpx::AttrType::Seed: case gpx::AttrType::Vec2:
    case gpx::AttrType::Range: case gpx::AttrType::Color: return true;
    default: return false;
  }
}
int attr_comps(const gpx::Attribute &at) {
  if (at.type == gpx::AttrType::Vec2 || at.type == gpx::AttrType::Range) return 2;
  if (at.type == gpx::AttrType::Color) return 3;
  return 1;
}

void push_node_tracks(App &a, std::vector<TrackRef> &out, gpx::Node &n, bool animated_only) {
  (void)a;
  for (gpx::Attribute &at : n.attrs.items) {
    if (!attr_keyable(at)) continue;
    int nc = attr_comps(at);
    for (int c = 0; c < nc; ++c) {
      gpx::Track *t = nc == 1 ? &at.anim : ((int)at.anim_v.size() > c ? &at.anim_v[(size_t)c] : nullptr);
      bool has = t && (t->animated() || !t->modifiers.empty());
      if (animated_only && !has) continue;
      TrackRef r;
      r.kind = TrackRef::Node;
      r.id = "n:" + std::to_string(n.id) + ":" + at.key + (nc > 1 ? ":" + std::to_string(c) : "");
      std::string nm = n.attrs.get_s("name");
      r.owner = nm.empty() ? n.type + " " + std::to_string(n.id) : nm;
      r.group = at.group.empty() ? n.type : at.group;
      r.label = at.label;
      r.comp = nc > 1 ? c : -1;
      r.color = at.type == gpx::AttrType::Color;
      r.node = n.id;
      r.attr = at.key;
      r.track = has ? t : nullptr;
      out.push_back(r);
    }
  }
}

} // namespace

std::vector<TrackRef> anim_collect(App &a, bool animated_only, int object) {
  std::vector<TrackRef> out;
  SceneState &sc = scene();
  if (object >= 0 && object < (int)sc.objects.size() && !animated_only) {
    push_object_tracks(a, out, object, false);
    return out;
  }
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    bool selected = i == sc.selected;
    push_object_tracks(a, out, i, animated_only || !selected);
  }
  push_world_tracks(out, animated_only);
  for (auto &n : a.graph.nodes)
    push_node_tracks(a, out, *n, animated_only || n->id != a.selected_node);
  return out;
}

gpx::Track *anim_resolve(App &a, const std::string &id, TrackRef *out) {
  if (id.size() < 3) return nullptr;
  SceneState &sc = scene();
  if (id[0] == 'o') {
    size_t p = id.find(':', 2);
    if (p == std::string::npos) return nullptr;
    int i = std::atoi(id.substr(2, p - 2).c_str());
    if (i < 0 || i >= (int)sc.objects.size()) return nullptr;
    std::string key = id.substr(p + 1);
    auto it = sc.objects[(size_t)i].anim.find(key);
    if (it == sc.objects[(size_t)i].anim.end()) return nullptr;
    if (out) {
      out->kind = TrackRef::Object; out->id = id; out->object = i; out->owner = sc.objects[(size_t)i].name;
      std::string base = key; int comp = -1;
      if (key.size() > 2 && key[key.size() - 2] == '.') { base = key.substr(0, key.size() - 2); const char *xyz = "xyz", *rgb = "rgb"; char s = key.back(); comp = (int)(std::strchr(xyz, s) ? std::strchr(xyz, s) - xyz : std::strchr(rgb, s) ? std::strchr(rgb, s) - rgb : 0); }
      out->prop = anim_find_prop(sc.objects[(size_t)i], base);
      if (out->prop) { out->group = out->prop->group; out->label = out->prop->label; out->color = out->prop->color; }
      out->comp = comp;
      out->track = &it->second;
    }
    return &it->second;
  }
  if (id[0] == 'w') {
    std::string key = id.substr(2);
    auto it = sc.world_anim.find(key);
    if (it == sc.world_anim.end()) return nullptr;
    if (out) {
      out->kind = TrackRef::World; out->id = id; out->owner = "World";
      std::string base = key; int comp = -1;
      if (key.size() > 2 && key[key.size() - 2] == '.') { base = key.substr(0, key.size() - 2); comp = key.back() == 'x' || key.back() == 'r' ? 0 : key.back() == 'y' || key.back() == 'g' ? 1 : 2; }
      out->prop = anim_find_world_prop(base);
      if (out->prop) { out->group = out->prop->group; out->label = out->prop->label; out->color = out->prop->color; }
      out->comp = comp;
      out->track = &it->second;
    }
    return &it->second;
  }
  if (id[0] == 'n') {
    size_t p = id.find(':', 2);
    if (p == std::string::npos) return nullptr;
    uint64_t nid = std::strtoull(id.substr(2, p - 2).c_str(), nullptr, 10);
    std::string rest = id.substr(p + 1);
    int comp = -1;
    size_t q = rest.rfind(':');
    std::string key = rest;
    if (q != std::string::npos) { comp = std::atoi(rest.substr(q + 1).c_str()); key = rest.substr(0, q); }
    gpx::Node *n = a.graph.find_node(nid);
    if (!n) return nullptr;
    gpx::Attribute *at = n->attrs.find(key);
    if (!at) return nullptr;
    gpx::Track *t = comp < 0 ? &at->anim : &at->anim_comp(comp);
    if (out) {
      out->kind = TrackRef::Node; out->id = id; out->node = nid; out->attr = key; out->comp = comp;
      std::string nm = n->attrs.get_s("name");
      out->owner = nm.empty() ? n->type + " " + std::to_string(n->id) : nm;
      out->group = at->group.empty() ? n->type : at->group; out->label = at->label;
      out->color = at->type == gpx::AttrType::Color; out->track = t;
    }
    return t;
  }
  return nullptr;
}

bool anim_current_value(App &a, const TrackRef &r, float &v) {
  SceneState &sc = scene();
  if (r.kind == TrackRef::Object) {
    if (r.object < 0 || r.object >= (int)sc.objects.size() || !r.prop) return false;
    SceneObject &o = sc.objects[(size_t)r.object];
    if (r.prop->boolean) { bool *b = anim_bool_ptr(o, *r.prop); if (!b) return false; v = *b ? 1.f : 0.f; return true; }
    float *f = anim_ptr(o, *r.prop, std::max(r.comp, 0));
    if (!f) return false;
    v = *f;
    return true;
  }
  if (r.kind == TrackRef::World) {
    if (!r.prop) return false;
    float *f = anim_world_ptr(render_settings(), *r.prop, std::max(r.comp, 0));
    if (!f) return false;
    v = *f;
    return true;
  }
  gpx::Node *n = a.graph.find_node(r.node);
  gpx::Attribute *at = n ? n->attrs.find(r.attr) : nullptr;
  if (!at) return false;
  switch (at->type) {
    case gpx::AttrType::Float: v = at->f; return true;
    case gpx::AttrType::Int: case gpx::AttrType::Choice: v = (float)at->i; return true;
    case gpx::AttrType::Bool: v = at->b ? 1.f : 0.f; return true;
    case gpx::AttrType::Seed: v = (float)at->seed; return true;
    case gpx::AttrType::Vec2: case gpx::AttrType::Range: v = at->v2[std::clamp(r.comp, 0, 1)]; return true;
    case gpx::AttrType::Color: v = at->col[std::clamp(r.comp, 0, 3)]; return true;
    default: return false;
  }
}

void anim_touched(App &a, const TrackRef &r) {
  if (r.kind == TrackRef::Node) {
    if (gpx::Node *n = a.graph.find_node(r.node)) n->dirty = true;
    a.request_eval();
  } else {
    a.scene_selection_serial++;
  }
}

unsigned anim_comp_color(int comp, bool color) {
  (void)color;
  switch (comp) {
    case 0: return IM_COL32(0xd8, 0x5a, 0x4a, 0xff);
    case 1: return IM_COL32(0x7a, 0xb8, 0x5a, 0xff);
    case 2: return IM_COL32(0x5a, 0x8c, 0xd8, 0xff);
    default: return theme::accent();
  }
}

} // namespace studio
