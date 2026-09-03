// Geekatplay TerraForge — environment / render settings shared between the
// renderer and the Environment panel.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace gpx {
struct Heightmap;
}

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
    // which camera a perspective view looks through: -2 the active camera
    // (or the free orbit when none is active), -1 the free orbit, >= 0 a
    // scene object index that is a camera
    int scene_camera = -2;
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
  // Multiple-scattering octaves. 1 is single scattering, which stops light at
  // its first hit and makes dense cloud read as plastic; each further octave
  // re-evaluates the shadow ray we already have with lower extinction, a more
  // isotropic phase and less energy, standing in for one more bounce.
  //
  // Two by default rather than the three the technique is usually written
  // with: measured on a dense cumulonimbus, 2 octaves at the depth below
  // lifts mean brightness by 9.5 levels while keeping 96 % of the image's
  // contrast, where 3 octaves at the textbook depth of 0.5 lifted it by 52
  // and left 55 % — a shaped storm turned into a flat pale sheet.
  int   cloud_scatter_octaves = 2;
  // How much deeper each of those bounces reaches: the extinction is
  // multiplied by this each time, so lower means light penetrates further and
  // the interior lifts more. The textbook 0.5 assumes an optical-depth scale
  // that is not ours — our `sum * 2.2` was tuned against single scattering.
  // Measured on the same scene: 0.9 keeps 94 % of contrast, 0.8 keeps 86 %,
  // 0.5 keeps 55 %.
  float cloud_scatter_depth = 0.9f;

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
  // graph-authored displacement: how strongly a TerrainDisplacement node's
  // field moves the surface, in world units
  float field_displacement = 0.05f;
  // adaptive subdivision: the surface is tessellated to whatever the camera
  // needs rather than to a fixed grid, so displacement keeps resolving as you
  // approach and distant ground costs almost nothing
  bool tessellation = true;
  float tess_pixels = 8.f;  // target pixels per triangle edge
  // 64 patches x 8 = 512 across, exactly the fixed grid this replaces, so the
  // adaptive path is never coarser than what it took over from
  float tess_min = 8.f;
  float tess_max = 32.f;    // Vue's "limit automatic subdivision"
  // Discard patches that cannot be on screen before the tessellator subdivides
  // them. Off is the old behaviour, kept so the two can be compared directly.
  bool frustum_cull = true;
  // Planetary curvature: 0 = flat, otherwise the horizon falls away. Radius is
  // in world units, and the terrain tile is 1 unit across — so at the default
  // 5 km tile, 1275 units is roughly Earth. A scene starts on a planet rather
  // than on an infinite plane, because that is what makes distance read
  // correctly (Terragen guide p5); set it to 0 for true infinite flatness.
  float planet_radius = 1275.f;

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
// where the cursor lands on the terrain surface, in normalized terrain
// coords — positions the sculpt brush
bool renderer_pick_terrain(int slot, const RenderSettings::ViewConfig &vc, float u,
                           float v, int w, int h, float &tx, float &tz);
// brush ring drawn on the terrain this frame (radius <= 0 hides it)
void renderer_set_brush_cursor(float tx, float tz, float radius, bool erasing);
// upload extra material maps (any may be null)
// version changes only when the map data changed; equal versions skip the
// (expensive) GPU upload entirely
void renderer_set_material_maps(const void *normal, const void *roughness,
                                const void *displacement,
                                unsigned long long version);
// Hand the renderer a field graph transpiled to GLSL, to displace the terrain
// on the GPU. Empty source removes the displacement. Version-guarded the same
// way as the material maps; the relink happens at the next draw.
void renderer_set_field_program(const std::string &glsl,
                                unsigned long long version);
// The surface channels a field graph can drive: colour, roughness and bump.
// One graph, several outputs (Vue p770). All live in one program, so setting
// any of them is a single relink; an empty string leaves that channel alone.
void renderer_set_surface_program(const std::string &color,
                                  const std::string &roughness,
                                  const std::string &bump,
                                  unsigned long long version);
void renderer_set_surface_bump(float strength, float scale);
// Buffers those programs sample, by the uniform name the transpiler declared.
// This is what lets an eroded heightfield drive displacement or colour.
void renderer_set_field_textures(
    const std::vector<std::pair<std::string, const gpx::Heightmap *>> &maps,
    unsigned long long version);
// Empty unless the last generated shader failed to link, in which case the
// previous program is still in use and this says why.
const char *renderer_field_error();
// Whether adaptive subdivision is actually running, and at what settings.
std::string renderer_tess_status();
// How many terrain patches survived the frustum test on the last camera pass.
std::string renderer_cull_status();
int renderer_patches_visible();
// camera navigation for the active camera (scene camera or free viewport)
void renderer_camera_input(float dx, float dy, float wheel, bool rotating,
                           bool panning, bool dolly);
void renderer_camera_look_at(const float target[3], float distance);
// Set for the duration of one view's draw (see ViewConfig::scene_camera).
int &renderer_camera_override();
// world-space right/up/forward of a view, for the corner orientation gizmo
void renderer_view_basis(const RenderSettings::ViewConfig &vc, float *right,
                         float *up, float *fwd);
// The view-projection matrix a slot last drew with (column-major, 16 floats),
// or null if it has not drawn yet. The transform gizmo projects with this so
// its handles land on the pixels the frame actually used.
const float *renderer_last_mvp(int slot);
// swing the free orbit camera to look down a world axis (0=X, 1=Y, 2=Z)
void renderer_camera_snap_axis(int axis, bool negative);
// photographic grading applied to every pass this frame
void renderer_set_film(const float tint[3], float saturation,
                       float exposure_mult);
// renders the current material onto a lit preview shape
// (0 sphere, 1 cube, 2 flat); spin = turntable angle in radians
unsigned renderer_material_preview(int size, int shape = 0, float spin = 0.f);

// computes sun direction from settings (handles geographic mode)
void compute_sun_dir(const RenderSettings &rs, float out_dir[3]);

} // namespace studio
