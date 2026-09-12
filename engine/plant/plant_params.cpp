// Geekatplay TerraForge - reading a plant node's parameters for an instance
// (plant_internal.hpp: ParamReader, random_range).
//
// Every number on a plant node is a Random attribute: a value, a spread,
// a rule for when a fresh draw is made, and two shaping curves. This is
// the one place that turns such an attribute into the number a builder
// uses, so the manual's semantics (spec H 2.3) hold on every node:
//
//   absolute  value + U(-1,1) * spread
//   relative  value * (1 + U(-1,1) * spread)
//   gaussian  value + N(0,1) * spread
//
// clamped to the attribute's range, then multiplied by the along-curve at
// the primal and by the hierarchy curve at the instance's position on its
// ancestor (cascading over the levels when asked). The draw stream is
// chosen by the attribute's scope, hashing (seed, instance or ancestor or
// plant, key, index): "each time" advances by the caller's draw index,
// "per primitive" ignores it, "per plant" hashes the node instead of the
// instance, "per ancestor" hashes the ancestor's id.
//
// A key whose field port is connected is driven: the field replaces value
// and spread (a field is already a function of where you are), and only
// the curves still multiply. The evaluation runs with the instance's
// PlantVars on the FieldContext, so PlantVariable / PlantVector nodes read
// the primitive being grown.
#include "plant/plant_internal.hpp"
#include "gpx/color_math.hpp"
#include "gpx/field.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

float random_range(float value, float spread, int spread_mode, float u, float g) {
  const float s = u * 2.f - 1.f;
  switch (spread_mode) {
    case 1: return value * (1.f + s * spread);
    case 2: return value + g * spread;
    default: return value + s * spread;
  }
}

namespace {

const Instance *ancestor_of(const Instance &i, int levels) {
  const Instance *a = &i;
  for (int k = 0; k < levels && a->parent; ++k) a = a->parent;
  return a;
}

} // namespace

const Attribute *ParamReader::attr(const char *key) const { return node.attrs.find(key); }

float ParamReader::random(const char *key, float primal, uint32_t draw_index, float def) const {
  const Attribute *a = node.attrs.find(key);
  if (!a) return def;
  if (a->type != AttrType::Random) return a->type == AttrType::Int ? (float)a->i : a->f;
  const uint32_t k = hash_str(key);
  float v;
  if (node.field_connected(key)) {
    v = field(key, primal, a->f);
  } else {
    float u = 0.5f, g = 0.f;
    if (a->spread != 0.f) {
      Draw d(ctx.seed, inst.id, k);
      switch (a->scope) {
        case 0: d.i = draw_index * 3u; break;
        case 2: d = Draw(ctx.seed, 0u, k ^ ctx.key_of(node)); break;
        case 3: d = Draw(ctx.seed, ancestor_of(inst, std::max(1, a->hier_level))->id, k); break;
        default: break;
      }
      u = d.unit();
      g = a->spread_mode == 2 ? d.gaussian() : 0.f;
    }
    v = random_range(a->f, a->spread, a->spread_mode, u, g);
    v = clampf(v, a->fmin, a->fmax);
  }
  if (!a->curve_along.empty()) v *= a->curve_along.eval(clampf(primal, 0.f, 1.f), ctx.seed, k);
  if (!a->curve_hier.empty()) {
    const int levels = std::max(1, a->hier_level);
    if (a->hier_cascade) {
      const Instance *p = &inst;
      for (int l = 0; l < levels && p->parent; ++l) {
        v *= a->curve_hier.eval(clampf(p->parent_primal, 0.f, 1.f), ctx.seed, k + (uint32_t)l);
        p = p->parent;
      }
    } else {
      const Instance *p = ancestor_of(inst, levels - 1);
      v *= a->curve_hier.eval(clampf(p->parent_primal, 0.f, 1.f), ctx.seed, k);
    }
  }
  return v;
}

float ParamReader::f(const char *key, float def) const {
  const Attribute *a = node.attrs.find(key);
  if (!a) return def;
  if (a->type == AttrType::Int) return (float)a->i;
  return a->f;
}

int ParamReader::i(const char *key, int def) const {
  const Attribute *a = node.attrs.find(key);
  if (!a) return def;
  if (a->type == AttrType::Float || a->type == AttrType::Random) return (int)std::lround(a->f);
  return a->i;
}

bool ParamReader::b(const char *key, bool def) const { return node.attrs.find(key) ? node.attrs.get_b(key, def) : def; }

int ParamReader::choice(const char *key, int def) const {
  return node.attrs.find(key) ? node.attrs.get_choice(key) : def;
}

std::string ParamReader::s(const char *key) const { return node.attrs.get_s(key); }

void ParamReader::color(const char *key, float out[4]) const {
  const Attribute *a = node.attrs.find(key);
  if (!a) {
    out[0] = out[1] = out[2] = out[3] = 1.f;
    return;
  }
  for (int c = 0; c < 4; ++c) out[c] = a->col[c];
}

const Curve &ParamReader::curve(const char *key) const {
  const Attribute *a = node.attrs.find(key);
  static const Curve one = Curve::constant(1.f);
  if (!a || a->curves.empty()) return one;
  return a->curves.pick(ctx.seed, hash_str(key));
}

float ParamReader::curve_at(const char *key, float x) const { return curve(key).eval(x); }

void ParamReader::gradient(const char *key, float t, float rgb[3]) const {
  const Attribute *a = node.attrs.find(key);
  rgb[0] = rgb[1] = rgb[2] = 1.f;
  if (!a || a->stops.empty()) return;
  float rgba[4];
  eval_gradient(a->stops, t, rgba);
  rgb[0] = rgba[0]; rgb[1] = rgba[1]; rgb[2] = rgba[2];
}

bool ParamReader::driven(const char *key) const { return node.field_connected(key); }

float ParamReader::field(const char *key, float primal, float def) const {
  if (!node.field_connected(key)) return def;
  const PlantVars v = vars(primal);
  return field_at(key, v, def);
}

float ParamReader::field_at(const char *key, const PlantVars &v, float def) const {
  if (!node.field_connected(key)) return def;
  FieldContext fc;
  fc.plant = &v;
  fc.pos[0] = v.pos[0]; fc.pos[1] = v.pos[1]; fc.pos[2] = v.pos[2];
  fc.altitude = v.pos[1];
  fc.time = ctx.time;
  return node.in_number(key, fc, def);
}

PlantVars ParamReader::vars(float primal) const { return vars(primal, 0.f, 0.f); }

PlantVars ParamReader::vars(float primal, float section_angle, float radial) const {
  PlantVars v;
  v.primal = primal;
  v.section_angle = section_angle;
  v.radial = radial;
  v.age = ctx.age; v.max_age = ctx.max_age; v.maturity = ctx.maturity;
  v.health = ctx.health; v.season = ctx.season; v.time = ctx.time;
  v.depth = inst.depth;
  v.dist_root = inst.dist_root + primal * inst.length;
  v.height_frac = inst.height_frac;
  v.iteration = inst.iterations > 1 ? (float)inst.iteration / (float)(inst.iterations - 1) : 0.f;
  v.parent_primal = inst.parent_primal;
  v.parent_radius = inst.parent_radius;
  v.parent_length = inst.parent_length;
  v.parent_remaining = inst.parent_remaining;
  v.length = inst.length;
  v.radius = inst.radius;
  v.azimuth = inst.azimuth;
  v.rnd_instance = inst.rnd;
  v.rnd_plant = hash_unit(hash_u32(ctx.seed, hash_str("plant")));
  v.lod = ctx.lod;
  v.lod_max = ctx.lod_max;
  v.pruned = inst.pruned;
  v.prune_ratio = inst.prune_ratio;
  // where on the primitive: along its axis when it has one, else its frame
  V3 p = inst.frame.o, d = inst.frame.z, n = inst.frame.x;
  if (inst.axis.size() >= 2) {
    const float t = clampf(primal, 0.f, 1.f) * (float)(inst.axis.size() - 1);
    const size_t i = std::min(inst.axis.size() - 2, (size_t)t);
    const float f = t - (float)i;
    p = lerp(inst.axis[i].p, inst.axis[i + 1].p, f);
    d = normalize(lerp(inst.axis[i].t, inst.axis[i + 1].t, f), inst.axis[i].t);
    n = normalize(lerp(inst.axis[i].n, inst.axis[i + 1].n, f), inst.axis[i].n);
  }
  p.to(v.pos);
  d.to(v.dir);
  inst.frame.z.to(v.axis_dir);
  rotate(n, d, section_angle * TAU).to(v.radial_dir);
  if (inst.parent) {
    inst.parent->frame.z.to(v.parent_dir);
    v.parent_tilt = std::acos(clampf(inst.parent->frame.z.y, -1.f, 1.f));
  }
  return v;
}

} // namespace plant
} // namespace gpx
