// Geekatplay TerraForge - tooltips for Kind::Flower, Growth, Repeat,
// ChildSelect, Variable and Vector (plant_schema_shapes.cpp,
// plant_schema_growth.cpp). Split from plant_schema_tips_parts.cpp for the
// 500-line module rule.
#include "plant/plant_schema.hpp"

namespace gpx {
namespace plant {
namespace {

const Tip TABLE[] = {
    // ---- Flower
    {Kind::Flower, "length", "The flower's length along its axis, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "radius", "The flower's overall radius, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "plug_radius", "The radius of the hole left at the flower's centre for a plugged-in part like a Ball. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "plug_modify", "Lets the plugged-in part reshape the flower's normals and displacement around the hole."},
    {Kind::Flower, "plug_influence", "How far the plugged-in part's influence reaches into the flower's surface, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "axis_bend", "How far the flower's axis bends from straight, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "twist", "How many turns the flower's lathe twists along its axis. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "axis_influence", "How much the axis bend affects the shape of individual cross-sections along the flower."},
    {Kind::Flower, "profile_mode", "The lathe's overall shape family: lobed inner/outer profiles, a cylinder, a disc, a cup, a bell, a trumpet, or a fully custom profile."},
    {Kind::Flower, "inner_profile", "The lobed profile's inner radius from base to tip, read off the horizontal axis as height 0..1."},
    {Kind::Flower, "outer_profile", "The lobed profile's outer radius from base to tip, read off the horizontal axis as height 0..1."},
    {Kind::Flower, "inout_filter", "How much the lobed profile blends between its inner and outer radius around each lobe, read off the horizontal axis as angle across the lobe 0..1."},
    {Kind::Flower, "lobes", "How many lobes the flower's rim is divided into."},
    {Kind::Flower, "custom_profile", "The custom profile's radius from base to tip, read off the horizontal axis as height 0..1, used when Profile is set to Custom."},
    {Kind::Flower, "disable_baking", "Skips baking this flower's texture detail into vertex data, keeping it live instead."},
    {Kind::Flower, "disp_amount", "How far the flower's surface is displaced. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "gravitropism", "How strongly the flower droops toward gravity as it grows. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "mesh_mode", "How the flower's mesh resolution along its axis is decided: parametric, or driven by a unit length."},
    {Kind::Flower, "geometric_twist", "Computes the lathe's twist from the built geometry instead of the parametric angle, for a more even look at extreme twist."},
    {Kind::Flower, "normal_mode", "How surface normals are computed: parametric from the profile, or geometric from the built mesh."},
    {Kind::Flower, "radial_continuity", "Keeps the lathe's normals continuous around the ring instead of seaming at the wrap point."},
    {Kind::Flower, "invert_faces", "Flips the flower's surface to face inward instead of outward."},
    {Kind::Flower, "axial_subdiv", "The number of subdivisions along the flower's axis."},
    {Kind::Flower, "axial_min", "The fewest axial subdivisions the flower is ever allowed to have."},
    {Kind::Flower, "symmetry", "Repeats the flower's cross-section in this many identical wedges, for a faceted look."},
    {Kind::Flower, "angular_subdiv", "The number of subdivisions around the flower's ring."},
    {Kind::Flower, "angular_min", "The fewest angular subdivisions the flower is ever allowed to have."},
    {Kind::Flower, "uv_mode", "How the flower's texture coordinates are projected: disc or cylinder, each either native to the lathe or projected flat."},
    {Kind::Flower, "uv_geometric_twist", "Computes the texture coordinates' twist from the built geometry instead of the parametric angle."},
    {Kind::Flower, "uv_twist", "How many turns the texture coordinates twist along the flower's axis. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Flower, "angular_mapping", "How many times the texture repeats around the flower's ring."},
    {Kind::Flower, "axial_mapping", "How the texture's V coordinate is spaced along the axis: linear, or a custom mapping curve."},
    {Kind::Flower, "axial_map_curve", "The custom axial texture mapping, read off the horizontal axis as height along the flower 0..1."},

    // ---- Growth
    {Kind::Growth, "iterations", "How many bud-by-bud growth cycles are simulated. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "growth_input", "Where growth begins: from the root, or spread over the parent segment's length."},
    {Kind::Growth, "favor_up", "How strongly new buds are biased to form on the upward-facing side of a branch. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "favor_sides", "How strongly new buds are biased to form on the sides of a branch rather than its top or bottom. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "internode", "The length of one internode segment between buds, in metres."},
    {Kind::Growth, "bud_radius", "The radius a new bud starts at, in metres."},
    {Kind::Growth, "angle_with_parent", "The angle a new branch forms with the branch it grows from, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "phyllotaxis", "The angle added around the parent from one new bud to the next, in degrees; 137.5 gives the golden-angle spiral. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "angular_noise", "How much each bud's angle around the parent is randomised, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "growth_speed", "How quickly a branch elongates per iteration, relative to the others. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "apical", "How much the tip of a branch dominates and suppresses the buds behind it. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "decay", "How quickly a branch's vigour fades the further it grows from the trunk. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "shadow_strength", "How strongly nearby growth shades a bud and suppresses it."},
    {Kind::Growth, "shadow_size", "How far, in metres, one bud's shading reaches to affect its neighbours."},
    {Kind::Growth, "light_influence", "How strongly the amount of light a bud receives affects its growth. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "shedding", "The light level below which a shaded bud stops growing and is shed. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "gravitropism_influence", "How strongly branches bend to follow the gravitropism angle. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "gravitropism_angle", "The angle, in degrees, branches are pulled toward by gravitropism. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "phototropism", "How strongly branches bend to grow toward the light. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "vertical_trunk", "Keeps the main trunk growing straight up rather than following the other tropisms."},
    {Kind::Growth, "bottom_cut_mode", "How Bottom cut's height is measured: an absolute value, or relative to the plant's total height."},
    {Kind::Growth, "bottom_cut_rank", "Removes branch orders below this rank from the bottom of the plant, clearing the lower trunk."},
    {Kind::Growth, "bottom_cut_height", "The height below which growth is cut away entirely. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "profile_cut_enable", "Cuts the grown plant down to fit inside a profile silhouette instead of growing freely."},
    {Kind::Growth, "profile_cut_mode", "How the profile cut's height values are measured: absolute, or relative to the plant's total height."},
    {Kind::Growth, "profile_cut", "The silhouette growth is cut down to fit inside, read off the horizontal axis as height 0..1 and the vertical axis as radius."},
    {Kind::Growth, "profile_bottom", "The height the profile cut's silhouette starts at. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "profile_top", "The height the profile cut's silhouette ends at. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "profile_radius", "The overall radius the profile cut's silhouette is scaled to. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "axial_subdiv", "The number of subdivisions along the grown branches, per metre."},
    {Kind::Growth, "angular_subdiv", "The number of subdivisions around the grown branches, per metre."},
    {Kind::Growth, "mesh_boost", "Raises or lowers the grown mesh's resolution on top of its own setting."},
    {Kind::Growth, "flexibility", "How easily the grown branches bend under wind and gusts. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "gravity", "How strongly gravity pulls the grown branches back after a gust. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "wind", "How strongly gusts push the grown branches, on top of the steady wind. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "bone_boost", "Raises or lowers how many skinning bones the grown branches' wind animation uses."},
    {Kind::Growth, "breeze_strength", "How far the grown branches sway in the ambient breeze. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Growth, "breeze_override", "Ignores the species' inherited ambient-motion settings and uses the growth's own."},
    {Kind::Growth, "mat_distribution", "How the body material is assigned across the grown branches: at random, or always the first."},

    // ---- Repeat
    {Kind::Repeat, "iterations", "How many times the subtree on the body input is grown into itself. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Repeat, "tail_every", "Grows the tail input at every iteration instead of only after the last one."},

    // ---- ChildSelect
    {Kind::ChildSelect, "mode", "How children are chosen to grow: at random by presence, spread across a range, in a fixed sequence, between two children by a value, by level of detail, or picking among random inputs."},
    {Kind::ChildSelect, "sequence", "The letters naming which child input plays at each step, read left to right, for Sequence mode."},
    {Kind::ChildSelect, "level", "The value the LOD selector compares against each child's level range to choose which one grows. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::ChildSelect, "value", "The threshold value used by Select 2 children to choose between its two inputs. The value may be given a spread and shaped along the part / by the hierarchy curve."},

    // ---- Variable
    {Kind::Variable, "which", "Which number about the primitive being grown this field reads: its position along the parent, its age, depth, size, and so on."},
    {Kind::Variable, "scale", "Multiplies the chosen variable before it is output, since the GPU always sees this variable as zero and outputs Scale times 0 plus Offset."},
    {Kind::Variable, "offset", "Added to the chosen variable after scaling, since the GPU always sees this variable as zero and outputs Scale times 0 plus Offset."},

    // ---- Vector
    {Kind::Vector, "which", "Which direction or position about the primitive being grown this field reads: its position, its own direction, the axis direction, the radial direction, or its parent's direction."},
};

struct Reg {
  Reg() { tips_register(TABLE, sizeof TABLE / sizeof TABLE[0]); }
} reg_;

} // namespace
} // namespace plant
} // namespace gpx
