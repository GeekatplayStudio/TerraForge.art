// Geekatplay TerraForge - the table of animatable properties and the
// per-frame apply. See anim_targets.hpp.
#include "anim_targets.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace studio {

namespace {

// ---- object properties, by group; the table order is the panel order
const AnimProp OBJ_PROPS[] = {
    {"pos", "Position", "Transform", 3, false, false},
    {"rot", "Rotation", "Transform", 3, false, false},
    {"scale", "Size", "Transform", 1, false, false},
    {"scl", "Squeeze", "Transform", 3, false, false},
    {"color", "Colour", "Display", 3, true, false},
    {"visible", "Visible", "Display", 1, false, true},
    {"deform.twist", "Twist", "Deform", 3, false, false},
    {"deform.bend", "Bend", "Deform", 1, false, false},
    {"deform.shear", "Skew", "Deform", 3, false, false},
    {"deform.taper", "Taper", "Deform", 1, false, false},
    {"scatter.scale", "Scatter size", "Scatter", 1, false, false},
    {"scatter.jitter", "Scatter jitter", "Scatter", 1, false, false},
    {"scatter.sway", "Scatter sway", "Scatter", 1, false, false},
    {"light.intensity", "Intensity", "Light", 1, false, false},
    {"light.radius", "Reach", "Light", 1, false, false},
    {"light.cone", "Cone", "Light", 1, false, false},
    {"cam.eye", "Eye", "Camera", 3, false, false},
    {"cam.target", "Target", "Camera", 3, false, false},
    {"cam.focal_mm", "Focal length", "Camera", 1, false, false},
    {"cam.aperture", "Aperture", "Camera", 1, false, false},
    {"cam.shutter", "Shutter", "Camera", 1, false, false},
    {"cam.iso", "ISO", "Camera", 1, false, false},
    {"cam.distortion", "Distortion", "Camera", 1, false, false},
    {"cam.vignette", "Vignette", "Camera", 1, false, false},
    {"cam.chromatic", "Chromatic", "Camera", 1, false, false},
    {"cam.flare_strength", "Flare", "Camera", 1, false, false},
    {"cam.motion_blur", "Motion blur", "Camera", 1, false, false},
    {"planet.radius", "Radius", "Planet", 1, false, false},
    {"planet.relief", "Relief", "Planet", 1, false, false},
    {"planet.sea_level", "Sea level", "Planet", 1, false, false},
    {"planet.snow_line", "Snow line", "Planet", 1, false, false},
    {"planet.atmo_density", "Atmosphere", "Planet", 1, false, false},
    {"planet.spin", "Spin", "Planet", 1, false, false},
    {"planet.water_color", "Water colour", "Planet", 3, true, false},
    {"planet.atmo_color", "Atmosphere colour", "Planet", 3, true, false},
    {"surf.frequency", "Frequency", "Surface", 1, false, false},
    {"surf.amplitude", "Amplitude", "Surface", 1, false, false},
    {"surf.coverage", "Coverage", "Surface", 1, false, false},
    {"surf.height_scale", "Height scale", "Surface", 1, false, false},
};

const AnimProp WORLD_PROPS[] = {
    {"sun_azimuth", "Azimuth", "Sun", 1, false, false},
    {"sun_altitude", "Altitude", "Sun", 1, false, false},
    {"sun_intensity", "Intensity", "Sun", 1, false, false},
    {"sun_color", "Colour", "Sun", 3, true, false},
    {"hour", "Hour", "Sun", 1, false, false},
    {"exposure", "Exposure", "Sky", 1, false, false},
    {"ambient_intensity", "Ambient", "Sky", 1, false, false},
    {"atmosphere_density", "Density", "Sky", 1, false, false},
    {"sky_zenith", "Zenith colour", "Sky", 3, true, false},
    {"sky_horizon", "Horizon colour", "Sky", 3, true, false},
    {"fog_density", "Density", "Fog", 1, false, false},
    {"fog_level", "Level", "Fog", 1, false, false},
    {"fog_falloff", "Falloff", "Fog", 1, false, false},
    {"fog_color", "Colour", "Fog", 3, true, false},
    {"fog_sun_scatter", "Sun scatter", "Fog", 1, false, false},
    {"water_level", "Level", "Water", 1, false, false},
    {"water_wave_amp", "Wave height", "Water", 1, false, false},
    {"water_wave_scale", "Wave scale", "Water", 1, false, false},
    {"water_wave_speed", "Wave speed", "Water", 1, false, false},
    {"water_deep_color", "Deep colour", "Water", 3, true, false},
    {"water_shallow_color", "Shallow colour", "Water", 3, true, false},
    {"cloud_coverage", "Coverage", "Clouds", 1, false, false},
    {"cloud_density", "Density", "Clouds", 1, false, false},
    {"cloud_altitude", "Altitude", "Clouds", 1, false, false},
    {"cloud_thickness", "Thickness", "Clouds", 1, false, false},
    {"cloud_detail", "Detail", "Clouds", 1, false, false},
    {"cloud_wind_speed", "Wind speed", "Clouds", 1, false, false},
    {"cloud_wind_dir", "Wind direction", "Clouds", 1, false, false},
    {"cloud_color", "Colour", "Clouds", 3, true, false},
    {"height_scale", "Height scale", "Terrain", 1, false, false},
};

bool applies(const SceneObject &o, const AnimProp &p) {
  const char *g = p.group;
  if (!std::strcmp(g, "Transform") || !std::strcmp(g, "Display")) return true;
  if (!std::strcmp(g, "Deform") || !std::strcmp(g, "Scatter")) return o.type == SceneObject::Mesh;
  if (!std::strcmp(g, "Light")) return o.type == SceneObject::Light;
  if (!std::strcmp(g, "Camera")) return o.type == SceneObject::Camera;
  if (!std::strcmp(g, "Planet")) return o.type == SceneObject::Planet;
  if (!std::strcmp(g, "Surface")) return o.type == SceneObject::InfiniteSurface;
  return false;
}

float *obj_ptr(SceneObject &o, const char *path, int c) {
  auto is = [&](const char *s) { return !std::strcmp(path, s); };
  if (is("pos")) return &o.pos[c];
  if (is("rot")) return c == 0 ? &o.yaw : c == 1 ? &o.pitch : &o.roll;
  if (is("scale")) return &o.scale;
  if (is("scl")) return &o.scl[c];
  if (is("color")) return &o.color[c];
  if (is("deform.twist")) return &o.deform.twist[c];
  if (is("deform.bend")) return &o.deform.bend;
  if (is("deform.shear")) return &o.deform.shear[c];
  if (is("deform.taper")) return &o.deform.taper;
  if (is("scatter.scale")) return &o.scatter_scale;
  if (is("scatter.jitter")) return &o.scatter_jitter;
  if (is("scatter.sway")) return &o.scatter_sway;
  if (is("light.intensity")) return &o.light_intensity;
  if (is("light.radius")) return &o.light_radius;
  if (is("light.cone")) return &o.light_cone;
  if (is("cam.eye")) return &o.cam.eye[c];
  if (is("cam.target")) return &o.cam.target[c];
  if (is("cam.focal_mm")) return &o.cam.focal_mm;
  if (is("cam.aperture")) return &o.cam.aperture;
  if (is("cam.shutter")) return &o.cam.shutter;
  if (is("cam.iso")) return &o.cam.iso;
  if (is("cam.distortion")) return &o.cam.distortion;
  if (is("cam.vignette")) return &o.cam.vignette;
  if (is("cam.chromatic")) return &o.cam.chromatic;
  if (is("cam.flare_strength")) return &o.cam.flare_strength;
  if (is("cam.motion_blur")) return &o.cam.motion_blur;
  if (is("planet.radius")) return &o.planet.radius;
  if (is("planet.relief")) return &o.planet.relief;
  if (is("planet.sea_level")) return &o.planet.sea_level;
  if (is("planet.snow_line")) return &o.planet.snow_line;
  if (is("planet.atmo_density")) return &o.planet.atmo_density;
  if (is("planet.spin")) return &o.planet.spin_deg;
  if (is("planet.water_color")) return &o.planet.water_color[c];
  if (is("planet.atmo_color")) return &o.planet.atmo_color[c];
  if (is("surf.frequency")) return &o.surf.layer.frequency;
  if (is("surf.amplitude")) return &o.surf.layer.amplitude;
  if (is("surf.coverage")) return &o.surf.layer.coverage;
  if (is("surf.height_scale")) return &o.surf.height_scale;
  return nullptr;
}

float *world_ptr(RenderSettings &r, const char *path, int c) {
  auto is = [&](const char *s) { return !std::strcmp(path, s); };
  if (is("sun_azimuth")) return &r.sun_azimuth;
  if (is("sun_altitude")) return &r.sun_altitude;
  if (is("sun_intensity")) return &r.sun_intensity;
  if (is("sun_color")) return &r.sun_color[c];
  if (is("hour")) return &r.hour;
  if (is("exposure")) return &r.exposure;
  if (is("ambient_intensity")) return &r.ambient_intensity;
  if (is("atmosphere_density")) return &r.atmosphere_density;
  if (is("sky_zenith")) return &r.sky_zenith[c];
  if (is("sky_horizon")) return &r.sky_horizon[c];
  if (is("fog_density")) return &r.fog_density;
  if (is("fog_level")) return &r.fog_level;
  if (is("fog_falloff")) return &r.fog_falloff;
  if (is("fog_color")) return &r.fog_color[c];
  if (is("fog_sun_scatter")) return &r.fog_sun_scatter;
  if (is("water_level")) return &r.water_level;
  if (is("water_wave_amp")) return &r.water_wave_amp;
  if (is("water_wave_scale")) return &r.water_wave_scale;
  if (is("water_wave_speed")) return &r.water_wave_speed;
  if (is("water_deep_color")) return &r.water_deep_color[c];
  if (is("water_shallow_color")) return &r.water_shallow_color[c];
  if (is("cloud_coverage")) return &r.cloud_coverage;
  if (is("cloud_density")) return &r.cloud_density;
  if (is("cloud_altitude")) return &r.cloud_altitude;
  if (is("cloud_thickness")) return &r.cloud_thickness;
  if (is("cloud_detail")) return &r.cloud_detail;
  if (is("cloud_wind_speed")) return &r.cloud_wind_speed;
  if (is("cloud_wind_dir")) return &r.cloud_wind_dir;
  if (is("cloud_color")) return &r.cloud_color[c];
  if (is("height_scale")) return &r.height_scale;
  return nullptr;
}

const char *SUFFIX[2][3] = {{".x", ".y", ".z"}, {".r", ".g", ".b"}};

} // namespace

std::vector<const AnimProp *> anim_props_for(const SceneObject &o) {
  std::vector<const AnimProp *> out;
  for (const AnimProp &p : OBJ_PROPS)
    if (applies(o, p)) out.push_back(&p);
  return out;
}

const std::vector<AnimProp> &anim_world_props() {
  static const std::vector<AnimProp> v(std::begin(WORLD_PROPS), std::end(WORLD_PROPS));
  return v;
}

const AnimProp *anim_find_prop(const SceneObject &o, const std::string &path) {
  for (const AnimProp &p : OBJ_PROPS)
    if (path == p.path && applies(o, p)) return &p;
  return nullptr;
}

const AnimProp *anim_find_world_prop(const std::string &path) {
  for (const AnimProp &p : WORLD_PROPS)
    if (path == p.path) return &p;
  return nullptr;
}

std::string anim_key(const AnimProp &p, int comp) {
  if (p.comps == 1) return p.path;
  return std::string(p.path) + SUFFIX[p.color ? 1 : 0][std::clamp(comp, 0, 2)];
}

float *anim_ptr(SceneObject &o, const AnimProp &p, int comp) {
  if (p.boolean || !applies(o, p)) return nullptr;
  return obj_ptr(o, p.path, std::clamp(comp, 0, p.comps - 1));
}
bool *anim_bool_ptr(SceneObject &o, const AnimProp &p) {
  if (!p.boolean) return nullptr;
  if (!std::strcmp(p.path, "visible")) return &o.visible;
  return nullptr;
}
float *anim_world_ptr(RenderSettings &rs, const AnimProp &p, int comp) {
  if (p.boolean) return nullptr;
  return world_ptr(rs, p.path, std::clamp(comp, 0, p.comps - 1));
}
bool *anim_world_bool_ptr(RenderSettings &, const AnimProp &) { return nullptr; }

gpx::Track *anim_find(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp) {
  auto it = m.find(anim_key(p, comp));
  return it == m.end() ? nullptr : &it->second;
}
gpx::Track &anim_get(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp) {
  std::string k = anim_key(p, comp);
  auto it = m.find(k);
  if (it != m.end()) return it->second;
  gpx::Track &t = m[k];
  t.interp = p.boolean ? gpx::Interp::Constant : gpx::Interp::Bezier;
  return t;
}
bool anim_prop_animated(const std::map<std::string, gpx::Track> &m, const AnimProp &p) {
  for (int c = 0; c < p.comps; ++c) {
    auto it = m.find(anim_key(p, c));
    if (it != m.end() && it->second.animated()) return true;
  }
  return false;
}
bool anim_prop_keyed_at(const std::map<std::string, gpx::Track> &m, const AnimProp &p, float t) {
  for (int c = 0; c < p.comps; ++c) {
    auto it = m.find(anim_key(p, c));
    if (it != m.end() && it->second.has_key_at(t)) return true;
  }
  return false;
}

static bool record(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp, float t,
                   const std::function<float(int)> &value) {
  bool added = false;
  int c0 = comp < 0 ? 0 : comp, c1 = comp < 0 ? p.comps : comp + 1;
  for (int c = c0; c < c1; ++c) added = anim_get(m, p, c).set_key(t, value(c)) || added;
  return added;
}

bool anim_record(SceneObject &o, const AnimProp &p, int comp, float t) {
  if (p.boolean) {
    bool *b = anim_bool_ptr(o, p);
    if (!b) return false;
    return record(o.anim, p, 0, t, [&](int) { return *b ? 1.f : 0.f; });
  }
  return record(o.anim, p, comp, t, [&](int c) { float *f = anim_ptr(o, p, c); return f ? *f : 0.f; });
}
bool anim_record_world(RenderSettings &rs, const AnimProp &p, int comp, float t) {
  return record(scene().world_anim, p, comp, t,
                [&](int c) { float *f = anim_world_ptr(rs, p, c); return f ? *f : 0.f; });
}

bool anim_unkey(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp, float t) {
  bool removed = false;
  int c0 = comp < 0 ? 0 : comp, c1 = comp < 0 ? p.comps : comp + 1;
  for (int c = c0; c < c1; ++c) {
    std::string k = anim_key(p, c);
    auto it = m.find(k);
    if (it == m.end()) continue;
    removed = it->second.remove_key(t) || removed;
    if (it->second.empty() && it->second.modifiers.empty()) m.erase(it);
  }
  return removed;
}
void anim_remove_track(std::map<std::string, gpx::Track> &m, const AnimProp &p) {
  for (int c = 0; c < p.comps; ++c) m.erase(anim_key(p, c));
}

bool anim_object_animated(const SceneObject &o) {
  for (const auto &kv : o.anim) if (kv.second.animated()) return true;
  return false;
}

void anim_apply(SceneState &sc, RenderSettings &rs, float t, bool &scene_changed, bool &world_changed) {
  scene_changed = world_changed = false;
  gpx::ExprContext ctx = anim_expr_context(t);
  for (SceneObject &o : sc.objects) {
    if (o.anim.empty()) continue;
    for (const AnimProp &p : OBJ_PROPS) {
      if (!applies(o, p)) continue;
      if (p.boolean) {
        auto it = o.anim.find(p.path);
        if (it == o.anim.end() || !it->second.animated()) continue;
        bool *b = anim_bool_ptr(o, p);
        bool v = it->second.sample(t, ctx) >= 0.5f;
        if (b && *b != v) { *b = v; scene_changed = true; }
        continue;
      }
      for (int c = 0; c < p.comps; ++c) {
        auto it = o.anim.find(anim_key(p, c));
        if (it == o.anim.end() || !it->second.animated()) continue;
        float *f = anim_ptr(o, p, c);
        float v = it->second.sample(t, ctx);
        if (f && *f != v) { *f = v; scene_changed = true; }
      }
    }
  }
  if (sc.world_anim.empty()) return;
  for (const AnimProp &p : WORLD_PROPS)
    for (int c = 0; c < p.comps; ++c) {
      auto it = sc.world_anim.find(anim_key(p, c));
      if (it == sc.world_anim.end() || !it->second.animated()) continue;
      float *f = anim_world_ptr(rs, p, c);
      float v = it->second.sample(t, ctx);
      if (f && *f != v) { *f = v; world_changed = true; }
    }
}

bool anim_lookup(const std::string &name, float &out) {
  // "<object name>.<path>[.x]" or "world.<path>[.x]"; names may use '_' for ' '
  size_t dot = name.find('.');
  if (dot == std::string::npos) return false;
  std::string owner = name.substr(0, dot), rest = name.substr(dot + 1);
  int comp = 0;
  std::string path = rest;
  size_t last = rest.find_last_of('.');
  if (last != std::string::npos && rest.size() - last == 2) {
    char s = rest.back();
    const char *xyz = "xyz", *rgb = "rgb";
    const char *fx = std::strchr(xyz, s), *fr = std::strchr(rgb, s);
    if (fx) { comp = (int)(fx - xyz); path = rest.substr(0, last); }
    else if (fr) { comp = (int)(fr - rgb); path = rest.substr(0, last); }
  }
  if (owner == "world") {
    const AnimProp *p = anim_find_world_prop(path);
    float *f = p ? anim_world_ptr(render_settings(), *p, comp) : nullptr;
    if (!f) return false;
    out = *f;
    return true;
  }
  for (SceneObject &o : scene().objects) {
    std::string n = o.name;
    for (char &ch : n) if (ch == ' ') ch = '_';
    if (o.name != owner && n != owner) continue;
    const AnimProp *p = anim_find_prop(o, path);
    if (!p) return false;
    if (p->boolean) { bool *b = anim_bool_ptr(o, *p); if (!b) return false; out = *b ? 1.f : 0.f; return true; }
    float *f = anim_ptr(o, *p, comp);
    if (!f) return false;
    out = *f;
    return true;
  }
  return false;
}

gpx::ExprContext anim_expr_context(float t) {
  gpx::ExprContext ctx;
  ctx.t = t;
  ctx.fps = scene().timeline.fps;
  ctx.lookup = [](const std::string &n, float &v) { return anim_lookup(n, v); };
  return ctx;
}

} // namespace studio
