// Geekatplay TerraForge — environment / render settings shared between the
// renderer and the Environment panel.
#pragma once

namespace studio {

struct RenderSettings {
  // sun
  int sun_mode = 0;            // 0 = manual, 1 = geographic (lat/lon/date/time)
  float sun_azimuth = 135.f;   // manual, degrees
  float sun_altitude = 35.f;
  float latitude = 40.7f;      // geographic
  float longitude = -111.9f;
  float utc_offset = -7.f;
  int month = 6, day = 21;
  float hour = 14.f;           // local time, fractional
  float sun_color[3] = {1.f, 0.93f, 0.82f};
  float sun_intensity = 2.6f;

  // atmosphere
  float atmosphere_density = 1.f;   // sky scattering strength
  float sky_zenith[3] = {0.18f, 0.32f, 0.58f};
  float sky_horizon[3] = {0.62f, 0.65f, 0.70f};
  float ambient_intensity = 0.7f;

  // fog / haze / pollution
  int fog_type = 1;                 // 0 off, 1 haze, 2 fog, 3 pollution
  float fog_density = 0.9f;
  float fog_level = 0.25f;          // height (0..1 of terrain space) where fog sits
  float fog_falloff = 6.f;          // vertical falloff sharpness
  float fog_color[3] = {0.55f, 0.63f, 0.75f};
  float absorption_color[3] = {0.9f, 0.95f, 1.f}; // light absorbed through fog
  float fog_sun_scatter = 0.5f;     // forward scattering toward sun

  // water
  bool show_water = true;
  float water_level = 0.08f;
  float water_deep_color[3] = {0.02f, 0.08f, 0.12f};
  float water_shallow_color[3] = {0.10f, 0.26f, 0.36f};
  float water_wave_amp = 1.f;
  float water_wave_scale = 1.f;
  float water_wave_speed = 1.f;
  float water_clarity = 18.f;       // depth -> deep color rate
  float water_opacity = 0.92f;
  // foam
  bool water_foam = true;
  float foam_color[3] = {0.92f, 0.95f, 0.96f};
  float foam_amount = 0.6f;         // shoreline foam width/intensity
  float foam_scale = 3.f;           // foam noise pattern scale
  float foam_crests = 0.35f;        // wave-crest foam intensity

  // material assignment (Materials workspace)
  // terrain albedo source: 0 = auto (last composite texture in graph),
  // 1 = procedural slope/height shading, 2 = a specific node (by id)
  int terrain_material_mode = 0;
  unsigned long long terrain_material_node = 0;

  // viewport views (Blender/C4D-style)
  struct ViewConfig {
    int camera = 0;        // 0 perspective, 1 top, 2 front, 3 right
    int display = 2;       // 0 wireframe, 1 solid, 2 textured
    bool atmosphere = true;// fog + sky in this view
    bool show_water_view = true;
    bool grid = false;
    bool outlines = true;  // selection outline drawn in this view
    // ortho navigation state
    float ortho_zoom = 1.2f;
    float ortho_cx = 0.5f, ortho_cy = 0.5f;
  };
  int viewport_layout = 0; // 0 = single, 1 = quad (persp/top/front/right)
  int viewport_engine = 0; // 0 rasterized PBR, 1 cinematic raymarch
  ViewConfig views[6] = {
      {0, 2, true, true, false, true, 1.2f, 0.5f, 0.5f},
      {1, 2, false, true, true, true, 1.2f, 0.5f, 0.5f},
      {2, 1, false, true, true, true, 1.2f, 0.5f, 0.5f},
      {3, 1, false, true, true, true, 1.2f, 0.5f, 0.5f},
      {0, 1, true, true, false, true, 1.2f, 0.5f, 0.5f},
      {1, 0, false, false, true, true, 1.2f, 0.5f, 0.5f},
  };
  int background_mode = 0; // 0 sky, 1 gradient, 2 solid color
  float bg_color[3] = {0.16f, 0.17f, 0.19f};
  float bg_color2[3] = {0.05f, 0.05f, 0.06f}; // gradient bottom

  // units & world scale
  int units = 0;              // 0 metric, 1 imperial
  float terrain_size_m = 5000.f; // world width of the tile in meters

  // volumetric clouds
  bool clouds_on = true;
  float cloud_coverage = 0.55f;   // 0 clear .. 1 overcast
  float cloud_density = 1.0f;     // extinction multiplier
  float cloud_altitude = 1.4f;    // slab bottom (world units; terrain is ~0.2)
  float cloud_thickness = 0.8f;   // slab height
  int   cloud_type = 1;           // 0 stratus, 1 cumulus, 2 cumulonimbus
  float cloud_detail = 0.6f;      // erosion strength
  float cloud_wind_speed = 0.02f;
  float cloud_wind_dir = 45.f;
  float cloud_color[3] = {1.f, 1.f, 1.f};
  float cloud_ambient = 0.55f;    // sky light into clouds
  int   cloud_quality = 1;        // 0 draft, 1 normal, 2 high
  float cloud_anvil = 0.3f;       // cumulonimbus top spread

  // terrain surface material (PBR)
  float mat_roughness = 0.85f;
  float mat_metallic = 0.f;
  float mat_specular = 0.35f;     // reflectance at normal incidence scale
  float mat_reflection = 0.25f;   // sky reflection strength
  float mat_translucency = 0.f;   // back-scatter through thin material
  float mat_transparency = 0.f;   // surface alpha (glassy objects)
  float mat_displacement = 0.f;   // extra displacement from a map, world units
  float mat_normal_strength = 1.f;
  unsigned long long map_normal_node = 0;
  unsigned long long map_roughness_node = 0;
  unsigned long long map_displacement_node = 0;

  // fractal detail: keeps resolving as the camera closes in
  float fractal_detail = 0.0025f; // height of the procedural micro-relief
  float fractal_scale = 90.f;     // base frequency of that relief
  // planetary curvature: 0 = flat, otherwise the horizon falls away.
  // radius is in world units (the terrain tile is 1 unit across).
  float planet_radius = 0.f;

  // terrain / global
  float height_scale = 0.22f;
  float exposure = 1.1f;
  bool wireframe = false;
  bool use_albedo = true;
  bool shadows = true;
  float shadow_softness = 1.5f;
};

RenderSettings &render_settings();

// multi-view rendering (slot 0..3 = independent FBOs)
unsigned renderer_draw_view(int slot, RenderSettings::ViewConfig &vc, int w,
                            int h, float dt);
void renderer_view_input(RenderSettings::ViewConfig &vc, float dx, float dy,
                         float wheel, bool rotating, bool panning, int view_w);
float renderer_view_width_m(const RenderSettings::ViewConfig &vc);
void renderer_get_camera(float eye[3], float target[3], float *fovy_deg);
// pick a scene object under normalized view coords (0..1); -1 = nothing
int renderer_pick(int slot, const RenderSettings::ViewConfig &vc, float u, float v,
                  int w, int h);
// upload extra material maps (any may be null)
// version changes only when the map data changed; equal versions skip the
// (expensive) GPU upload entirely
void renderer_set_material_maps(const void *normal, const void *roughness,
                                const void *displacement,
                                unsigned long long version);
// camera navigation for the active camera (scene camera or free viewport)
void renderer_camera_input(float dx, float dy, float wheel, bool rotating,
                           bool panning, bool dolly);
void renderer_camera_look_at(const float target[3], float distance);
// photographic grading applied to every pass this frame
void renderer_set_film(const float tint[3], float saturation,
                       float exposure_mult);
// renders the current material onto a lit preview shape
// (0 sphere, 1 cube, 2 flat); spin = turntable angle in radians
unsigned renderer_material_preview(int size, int shape = 0, float spin = 0.f);

// computes sun direction from settings (handles geographic mode)
void compute_sun_dir(const RenderSettings &rs, float out_dir[3]);

} // namespace studio
