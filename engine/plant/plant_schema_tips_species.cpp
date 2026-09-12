// Geekatplay TerraForge - tooltips for Kind::Species, Kind::Bias and
// Kind::Material (plant_schema_species.cpp).
#include "plant/plant_schema.hpp"

namespace gpx {
namespace plant {
namespace {

const Tip TABLE[] = {
    // ---- Species: General
    {Kind::Species, "name", "The species' name, shown in the library and the Objects list."},
    {Kind::Species, "seed", "The random seed the whole individual is grown from; a new value grows a different plant of the same species."},
    {Kind::Species, "max_age", "The age, in years, at which this species reaches full maturity."},
    {Kind::Species, "age", "The age of this individual, in years; drives its size and how far its seasonal and growth curves have progressed."},
    {Kind::Species, "health", "How healthy this individual is, from wilted to thriving; drives the health-based presence and tint curves on its parts."},
    {Kind::Species, "season", "Where in the yearly cycle this individual sits, 0 to 1; drives the season-based presence, tint and material look curves."},
    {Kind::Species, "time_from_scene", "Reads age and season from the scene's own time of year instead of the values set here."},
    {Kind::Species, "gravity", "How strongly gravity pulls on the growth simulation and its tropisms, as a multiple of normal."},
    {Kind::Species, "scale", "Overall size multiplier applied to the whole plant on top of its natural dimensions."},
    {Kind::Species, "true_size", "Builds the plant at its real, physically meaningful dimensions rather than a stylised size."},
    // ---- Placement
    {Kind::Species, "object", "The name the driven scene object is given, so other nodes and the Objects list can find it."},
    {Kind::Species, "x_m", "The plant's position along X, in metres."},
    {Kind::Species, "y_m", "The plant's position along Y, in metres."},
    {Kind::Species, "z_m", "The plant's position along Z, in metres."},
    {Kind::Species, "heading", "The plant's rotation around its vertical axis, in degrees."},
    {Kind::Species, "visible", "Whether the plant is drawn in the viewport and render."},
    {Kind::Species, "grounded", "Drops the plant onto the ground surface at its X/Z position instead of using Y directly."},
    // ---- Wind
    {Kind::Species, "receive_wind", "Whether this plant responds to the scene's wind at all."},
    {Kind::Species, "wind_follow",
     "Take the direction the wind blows, how strong it is and how it gusts\n"
     "from the scene's own weather, so this plant leans the way the clouds\n"
     "go and the waves run. Its strength below is then how hard it answers.\n"
     "Turn it off to blow this one plant by its own settings alone."},
    {Kind::Species, "wind_strength", "How hard the steady, constant wind bends the plant."},
    {Kind::Species, "wind_direction", "The compass direction the constant wind blows from, in degrees."},
    {Kind::Species, "wind_breeze_influence", "How much the constant wind adds to the ambient breeze motion, on top of its own steady bend."},
    {Kind::Species, "wind_breeze_speed", "How much the constant wind speeds up the breeze animation."},
    {Kind::Species, "gust_amplitude", "How strongly sudden gusts push the plant beyond the steady wind."},
    {Kind::Species, "gust_frequency", "How often gusts occur, in cycles per second."},
    {Kind::Species, "seg_breeze_influence", "How much the ambient breeze sways trunks, branches and stems."},
    {Kind::Species, "seg_breeze_speed", "How fast the ambient breeze animation runs on segments."},
    {Kind::Species, "seg_breeze_randomness", "How much each segment's breeze motion is offset from its neighbours, so they do not sway in lockstep."},
    {Kind::Species, "seg_flutter_influence", "How much fast, small fluttering motion is added to segments on top of the breeze sway."},
    {Kind::Species, "seg_flutter_speed", "How fast the fluttering motion on segments runs."},
    {Kind::Species, "seg_wind_influence", "How much the steady wind, as opposed to the breeze, bends segments."},
    {Kind::Species, "boost_thin", "Makes thinner segments flex more in the wind than thicker ones, on top of their natural stiffness."},
    {Kind::Species, "boost_long", "Makes longer segments flex more in the wind than shorter ones, on top of their natural stiffness."},
    {Kind::Species, "blade_breeze_influence", "How much the ambient breeze sways blades (grass-like flat sections)."},
    {Kind::Species, "blade_wind_influence", "How much the steady wind bends blades."},
    {Kind::Species, "boost_large_blade", "Makes larger blades flex more in the wind than smaller ones."},
    {Kind::Species, "leaf_breeze_influence", "How much the ambient breeze sways leaves."},
    {Kind::Species, "leaf_breeze_speed", "How fast the ambient breeze animation runs on leaves."},
    {Kind::Species, "leaf_breeze_randomness", "How much each leaf's breeze motion is offset from its neighbours."},
    {Kind::Species, "leaf_flutter_influence", "How much fast, small fluttering motion is added to leaves on top of the breeze sway."},
    {Kind::Species, "leaf_flutter_speed", "How fast the fluttering motion on leaves runs."},
    {Kind::Species, "boost_top", "Makes parts near the top of the plant flex more in the wind than parts lower down."},
    {Kind::Species, "boost_outer", "Makes parts near the outside of the plant's envelope flex more in the wind than parts closer to the trunk."},
    // ---- Meshing
    {Kind::Species, "geometry_target", "The overall meshing strategy: Offline for the densest quality mesh, Realtime for a lighter mesh built for game engines, Stylized for a simplified look."},
    {Kind::Species, "meshing_type", "How segments are tessellated: triangles, quads, an adaptive or uniform automatic choice, or a manual setting per part."},
    {Kind::Species, "optimize_for", "Whether the mesh is built to look its best still, or to deform cleanly when the plant is animated."},
    {Kind::Species, "mesh_boost", "Raises or lowers the mesh resolution across the whole plant, on top of each part's own setting."},
    {Kind::Species, "bone_boost", "Raises or lowers how many skinning bones the wind animation uses across the plant."},
    {Kind::Species, "bones_limit", "The maximum number of skinning bones the whole plant's animation rig may use."},
    {Kind::Species, "facing", "How camera-facing leaves are baked when their orientation is fixed: to a set direction, to the current camera, outward from the plant, or along the X axis."},
    // ---- Level of detail
    {Kind::Species, "lod_levels", "How many progressively simplified versions of the plant are built for distant viewing."},
    {Kind::Species, "lod_boost", "How aggressively each simplified level reduces the mesh, on top of the normal falloff."},
    {Kind::Species, "lod_trigger", "The on-screen size, in pixels, below which the plant switches to its first simplified level."},
    {Kind::Species, "impostor_trigger", "The on-screen size, in pixels, below which the plant is replaced by a flat impostor image."},
    {Kind::Species, "use_lod_render", "Lets the render use the same simplified levels as the viewport, instead of always rendering full detail."},
    {Kind::Species, "lod_method", "How distance to the camera decides which level of detail to show: by the plant's bounding sphere, or by a reference length."},
    {Kind::Species, "ecosystem_quality", "The detail level used when this species is scattered in bulk as part of an ecosystem, separate from its close-up quality."},
    // ---- Envelope
    {Kind::Species, "envelope_resolution", "How finely the plant's outer envelope (used for collision and fast previews) is sampled."},
    {Kind::Species, "envelope_smoothing", "Smooths the envelope so it does not follow every small bump of the growth."},
    {Kind::Species, "envelope_convexity", "Forces the envelope to stay convex (bulging outward everywhere) rather than following concave dips."},
    {Kind::Species, "residual_size", "Parts smaller than this, in metres, are dropped from the final geometry as negligible."},
    // ---- Post processing
    {Kind::Species, "ao_min", "The darkest value the baked ambient occlusion is allowed to reach."},
    {Kind::Species, "ao_max", "The brightest value the baked ambient occlusion is allowed to reach."},
    {Kind::Species, "ao_brightness", "Shifts the baked ambient occlusion brighter or darker overall."},
    {Kind::Species, "ao_falloff", "How quickly the ambient occlusion darkens with proximity; higher concentrates the shadowing into tighter creases."},
    {Kind::Species, "ao_ground_strength", "How strongly the plant is darkened near the ground, as if shadowed by it."},
    {Kind::Species, "ao_ground_height", "How high above the ground the ground-contact darkening reaches, as a percentage of the plant's height."},
    {Kind::Species, "ao_ground_transition", "How gradually the ground-contact darkening fades out with height."},

    // ---- Bias
    {Kind::Bias, "name", "The bias' name, shown where it is listed among the species' global biases."},
    {Kind::Bias, "bias_type", "The kind of pull this bias applies: a fixed direction, a cone, an attractor point, an axis repeller, a swirl, a curl, or a twist."},
    {Kind::Bias, "dir_x", "The X component of the direction this bias pulls growth toward."},
    {Kind::Bias, "dir_y", "The Y component of the direction this bias pulls growth toward."},
    {Kind::Bias, "dir_z", "The Z component of the direction this bias pulls growth toward."},
    {Kind::Bias, "strength", "How strongly this bias pulls growth toward its direction or point. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Bias, "local", "Measures the bias direction in the plant's local frame instead of the world, so it turns with the plant."},
    {Kind::Bias, "relative", "Scales the bias' pull by each segment's own length, so it affects short and long segments proportionally."},
    {Kind::Bias, "apply", "Which parts feel this bias: only free-floating ones, the entire segment, or only parts growing on an object."},
    {Kind::Bias, "cone_angle", "For a conic bias, the half-angle of the cone growth is pulled toward, in degrees."},
    {Kind::Bias, "repeller", "Reverses a conic bias into a repeller that pushes growth away instead of pulling it in."},
    {Kind::Bias, "base_length", "For a conic or attractor bias, the reference length, in metres, its falloff is measured against."},
    {Kind::Bias, "origin_x", "The X position of this bias' origin point, in metres."},
    {Kind::Bias, "origin_y", "The Y position of this bias' origin point, in metres."},
    {Kind::Bias, "origin_z", "The Z position of this bias' origin point, in metres."},

    // ---- Material
    {Kind::Material, "name", "The material's name, shown wherever it is picked for a part."},
    {Kind::Material, "color", "The base colour of the material where no picture overrides it."},
    {Kind::Material, "roughness", "How matte or glossy the surface is; 0 is a mirror finish, 1 is fully matte."},
    {Kind::Material, "metallic", "How metallic the surface reads; 0 is a dielectric like bark or leaf, 1 is a bare metal look."},
    {Kind::Material, "translucency", "How much light passes through the material, as if it were thin and backlit."},
    {Kind::Material, "backlight", "How much brighter the material looks when lit from behind, on top of its translucency."},
    {Kind::Material, "two_sided", "Shades both faces of the surface instead of only the one the normal points from."},
    {Kind::Material, "alpha_cutout", "Uses the alpha channel to cut holes in the surface instead of blending it."},
    {Kind::Material, "color_map", "The picture used for the base colour, replacing the flat Colour where it has content."},
    {Kind::Material, "alpha_map", "The picture whose alpha channel controls transparency or cut-out shape."},
    {Kind::Material, "normal_map", "The picture that perturbs the surface normal for fine bump detail."},
    {Kind::Material, "roughness_map", "The picture that varies roughness across the surface."},
    {Kind::Material, "u_tile", "How many times the pictures repeat across the surface along U."},
    {Kind::Material, "v_tile", "How many times the pictures repeat across the surface along V."},
    {Kind::Material, "cutout", "A hand-authored cut-out outline, as a list of u,v points, used instead of tracing one from the alpha picture."},
    {Kind::Material, "auto_cutout", "Traces the cut-out outline automatically from the alpha picture instead of using a hand-authored one."},
    {Kind::Material, "source", "Builds the colour and alpha pictures from procedural rules instead of loaded files: none, a leaf, bark, or a petal."},
    {Kind::Material, "leaf_shape", "The outline family the procedural leaf picture is drawn from."},
    {Kind::Material, "leaf_serration", "How jagged the procedural leaf's edge is."},
    {Kind::Material, "leaf_lobes", "How deeply the procedural leaf's lobes cut in."},
    {Kind::Material, "leaf_lobe_count", "How many lobes the procedural leaf has."},
    {Kind::Material, "leaf_aspect", "The procedural leaf's width relative to its length."},
    {Kind::Material, "leaf_vein", "How strongly the procedural leaf's veins show."},
    {Kind::Material, "vein_color", "The colour of the procedural leaf's veins."},
    {Kind::Material, "leaf_mottle", "How much random mottling is added to the procedural leaf's colour."},
    {Kind::Material, "bark_kind", "The pattern family the procedural bark picture is drawn from: fissured, plated, smooth and so on."},
    {Kind::Material, "crack_color", "The colour of the cracks or seams in the procedural bark."},
    {Kind::Material, "bark_scale", "How large the procedural bark's pattern reads relative to the surface."},
    {Kind::Material, "tex_size", "The pixel resolution the procedural picture is generated at."},
    {Kind::Material, "tex_seed", "The random seed the procedural picture is generated from."},
    {Kind::Material, "season_count", "How many of the four seasonal looks below are actually used."},
    {Kind::Material, "season_curve", "Which seasonal look is shown at each point of the year, read off the horizontal axis as season 0..1 and the vertical axis as the look index."},
    {Kind::Material, "shift_mask", "A picture whose value scales how far the season's hue/luminosity/saturation shift is applied across the surface."},

    // ---- Material: the four seasonal looks (s1_.. s4_)
    {Kind::Material, "s1_name", "The name of this seasonal look, shown in the season curve's picker."},
    {Kind::Material, "s1_color_map", "The colour picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s1_alpha_map", "The alpha picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s1_normal_map", "The normal picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s1_hue", "How far the base colour's hue is shifted while this look is active, in turns of the colour wheel."},
    {Kind::Material, "s1_lum", "How far the base colour's luminosity is shifted while this look is active."},
    {Kind::Material, "s1_sat", "How far the base colour's saturation is shifted while this look is active."},
    {Kind::Material, "s1_presence", "How strongly this look is blended in where the season curve selects it."},
    {Kind::Material, "s2_name", "The name of this seasonal look, shown in the season curve's picker."},
    {Kind::Material, "s2_color_map", "The colour picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s2_alpha_map", "The alpha picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s2_normal_map", "The normal picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s2_hue", "How far the base colour's hue is shifted while this look is active, in turns of the colour wheel."},
    {Kind::Material, "s2_lum", "How far the base colour's luminosity is shifted while this look is active."},
    {Kind::Material, "s2_sat", "How far the base colour's saturation is shifted while this look is active."},
    {Kind::Material, "s2_presence", "How strongly this look is blended in where the season curve selects it."},
    {Kind::Material, "s3_name", "The name of this seasonal look, shown in the season curve's picker."},
    {Kind::Material, "s3_color_map", "The colour picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s3_alpha_map", "The alpha picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s3_normal_map", "The normal picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s3_hue", "How far the base colour's hue is shifted while this look is active, in turns of the colour wheel."},
    {Kind::Material, "s3_lum", "How far the base colour's luminosity is shifted while this look is active."},
    {Kind::Material, "s3_sat", "How far the base colour's saturation is shifted while this look is active."},
    {Kind::Material, "s3_presence", "How strongly this look is blended in where the season curve selects it; defaults to off for the bare look."},
    {Kind::Material, "s4_name", "The name of this seasonal look, shown in the season curve's picker."},
    {Kind::Material, "s4_color_map", "The colour picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s4_alpha_map", "The alpha picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s4_normal_map", "The normal picture used while this seasonal look is active, replacing the base one."},
    {Kind::Material, "s4_hue", "How far the base colour's hue is shifted while this look is active, in turns of the colour wheel."},
    {Kind::Material, "s4_lum", "How far the base colour's luminosity is shifted while this look is active."},
    {Kind::Material, "s4_sat", "How far the base colour's saturation is shifted while this look is active."},
    {Kind::Material, "s4_presence", "How strongly this look is blended in where the season curve selects it."},
    {Kind::Species, "flagged",
     "Seeds of individuals worth keeping, separated by commas.\n"
     "Flag this plant adds the current seed; picking one in\n"
     "the Plant Editor grows that individual again."},
    {Kind::Species, "presets",
     "Named presets of this species as JSON: each keeps an age,\n"
     "a health, a season and the values of the published\n"
     "parameters, so one species gives a sapling, a veteran\n"
     "and a winter form without copying its graph."},
};

struct Reg {
  Reg() { tips_register(TABLE, sizeof TABLE / sizeof TABLE[0]); }
} reg_;

} // namespace
} // namespace plant
} // namespace gpx
