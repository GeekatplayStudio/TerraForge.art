// Geekatplay TerraForge - tooltips for Kind::Leaf, CutoutLeaf, Warpboard,
// Object, Urchin, Hydra and Ball (plant_schema_leaves.cpp,
// plant_schema_shapes.cpp). Flower, Growth, Repeat, ChildSelect, Variable
// and Vector are in plant_schema_tips_parts2.cpp (500-line module rule).
#include "plant/plant_schema.hpp"

namespace gpx {
namespace plant {
namespace {

const Tip TABLE[] = {
    // ---- Leaf
    {Kind::Leaf, "orientation", "How the leaf plane faces: a fixed direction set here, facing the current camera, facing outward from the plant, along the X axis, or dynamically tracking the camera."},
    {Kind::Leaf, "mesh_kind", "The leaf's geometry: a single flat plane, two or three crossed planes, or a diamond-shaped plane."},
    {Kind::Leaf, "length", "The leaf's length, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "width", "The leaf's width, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "subdiv_w", "Extra mesh subdivisions across the leaf's width, on top of the minimum needed."},
    {Kind::Leaf, "subdiv_h", "Extra mesh subdivisions along the leaf's length, on top of the minimum needed."},
    {Kind::Leaf, "midrib_angle", "How far the leaf folds along its mid-rib, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "curvature_w", "How much the leaf curves across its width, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "curvature_h", "How much the leaf curves along its length, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "normals", "How the leaf's surface normal is computed: from its own geometry, from the leaf's own direction, or blended toward a sphere around the plant or the local part."},
    {Kind::Leaf, "normal_hazard", "How much randomness is mixed into the leaf's normal, for a less uniform look under lighting."},
    {Kind::Leaf, "rib_orientation", "Which way the leaf's basic mid-rib runs: horizontal or vertical."},
    {Kind::Leaf, "uv_mode", "How the leaf's texture coordinates are laid out: the normal mode, or a diamond mode for diamond-shaped planes."},
    {Kind::Leaf, "hook", "The point on the leaf's own picture (u,v) that is anchored to its attachment point."},
    {Kind::Leaf, "mat_distribution", "How materials are assigned across the leaf's planes: per plane, per whole billboard, or per material group."},
    {Kind::Leaf, "disable_baking", "Skips baking this leaf's texture detail into vertex data, keeping it live instead."},
    {Kind::Leaf, "shift_hue", "Randomly shifts this leaf's colour hue from the material's base, in turns of the colour wheel. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "shift_lum", "Randomly shifts this leaf's colour luminosity from the material's base. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "shift_sat", "Randomly shifts this leaf's colour saturation from the material's base. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "global_axial", "An extra offset applied to every leaf along the parent's axis direction, useful for nudging a whole batch at once. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Leaf, "global_radial", "An extra offset applied to every leaf along the parent's radial direction, useful for nudging a whole batch at once. The value may be given a spread and shaped along the part / by the hierarchy curve."},

    // ---- CutoutLeaf
    {Kind::CutoutLeaf, "planes", "How many crossed planes the cut-out leaf is built from: one, two, or three."},
    {Kind::CutoutLeaf, "length", "The leaf's length, used as its overall scale, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "width_tweak", "Stretches or narrows the leaf's width relative to what its picture's outline implies. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "axis_bend", "How far the leaf's axis bends from straight, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "axis_curve", "The leaf's axis sideways offset, read off the horizontal axis as position along the leaf 0..1."},
    {Kind::CutoutLeaf, "twist", "How many turns the leaf twists along its length. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "midrib_angle", "How far the leaf folds along its mid-rib, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "midrib_curve", "How much the mid-rib fold varies along the leaf's length, read off the horizontal axis as position 0..1."},
    {Kind::CutoutLeaf, "smooth_rib", "Smooths the normals across the mid-rib fold instead of leaving it faceted."},
    {Kind::CutoutLeaf, "lateral_profile", "The leaf's outline width from its centre line outward, read off the horizontal axis as position along the leaf 0..1."},
    {Kind::CutoutLeaf, "gravitropism", "How strongly the leaf droops toward gravity as it grows. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "grid_boost_x", "Extra mesh grid resolution across the leaf's width, on top of the minimum needed."},
    {Kind::CutoutLeaf, "grid_boost_y", "Extra mesh grid resolution along the leaf's length, on top of the minimum needed."},
    {Kind::CutoutLeaf, "lod_affects_grid", "Lets the level of detail reduce this leaf's grid resolution boost at a distance."},
    {Kind::CutoutLeaf, "catmull", "Extra Catmull-Clark smoothing resolution applied to the leaf's mesh."},
    {Kind::CutoutLeaf, "lod_affects_catmull", "Lets the level of detail reduce the Catmull-Clark smoothing boost at a distance."},
    {Kind::CutoutLeaf, "mixed_quads", "Builds the mesh from a mix of quads and triangles instead of quads alone, where that gives a cleaner cut-out."},
    {Kind::CutoutLeaf, "grid_x", "The mesh grid's cell size across the leaf's width."},
    {Kind::CutoutLeaf, "grid_y", "The mesh grid's cell size along the leaf's length."},
    {Kind::CutoutLeaf, "cutout_index", "Which cut-out outline in the material's library this leaf uses."},
    {Kind::CutoutLeaf, "mat_distribution", "How materials are assigned across the leaf's planes: per plane, per whole billboard, or per material group."},
    {Kind::CutoutLeaf, "disable_baking", "Skips baking this leaf's texture detail into vertex data, keeping it live instead."},
    {Kind::CutoutLeaf, "shift_hue", "Randomly shifts this leaf's colour hue from the material's base, in turns of the colour wheel. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "shift_lum", "Randomly shifts this leaf's colour luminosity from the material's base. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "shift_sat", "Randomly shifts this leaf's colour saturation from the material's base. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "breeze_strength", "How far this leaf sways in the ambient breeze. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::CutoutLeaf, "breeze_flexibility", "How loosely different points of the leaf follow the breeze relative to each other, read off the horizontal axis as position along the leaf 0..1."},
    {Kind::CutoutLeaf, "breeze_override", "Ignores the species' inherited ambient-motion settings and uses this leaf's own."},

    // ---- Warpboard
    {Kind::Warpboard, "curl", "How much the board curls along its length, like a drying petal. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Warpboard, "flexibility", "How much the board bends away from flat under its own shape rules. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Warpboard, "shape_randomness", "How much the board's curl and flexibility vary at random from the average. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Warpboard, "width", "The board's width, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Warpboard, "length", "The board's length, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Warpboard, "disable_baking", "Skips baking this board's texture detail into vertex data, keeping it live instead."},
    {Kind::Warpboard, "mesh_boost", "Raises or lowers the board's mesh resolution on top of its minimum."},
    {Kind::Warpboard, "min_subdiv", "The fewest subdivisions the board's mesh is ever allowed to have."},

    // ---- Object
    {Kind::Object, "file", "The imported mesh file this part instances."},
    {Kind::Object, "double_sided", "Shades both faces of the imported mesh's surface."},
    {Kind::Object, "size_m", "Scales the imported mesh to this size, in metres, overriding its own file scale. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Object, "disable_baking", "Skips baking this object's texture detail into vertex data, keeping it live instead."},

    // ---- Urchin
    {Kind::Urchin, "radius", "The urchin's own radius, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "skin", "Whether the urchin's own sphere surface is built at all, or omitted so only its children show."},
    {Kind::Urchin, "normal_mode", "How surface normals are computed: parametric from the profile, or geometric from the built mesh."},
    {Kind::Urchin, "double_sided", "Shades both faces of the urchin's surface."},
    {Kind::Urchin, "profile", "The urchin's radius from bottom to top, read off the horizontal axis as height 0..1."},
    {Kind::Urchin, "section", "The urchin's radius around its cross-section, read off the horizontal axis as angle 0..1."},
    {Kind::Urchin, "pivot_offset", "Shifts the urchin's pivot point along its axis, changing where it attaches and rotates from. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "mesh_boost", "Raises or lowers the urchin's mesh resolution on top of its minimum."},
    {Kind::Urchin, "subdiv_mode", "How the urchin's subdivision counts are decided: parametric, or driven by a unit length/radius."},
    {Kind::Urchin, "axial_subdiv", "The number of subdivisions from bottom to top of the urchin."},
    {Kind::Urchin, "axial_min", "The fewest axial subdivisions the urchin is ever allowed to have."},
    {Kind::Urchin, "axial_density", "Where axial subdivisions concentrate, read off the horizontal axis as height 0..1."},
    {Kind::Urchin, "angular_subdiv", "The number of subdivisions around the urchin's cross-section."},
    {Kind::Urchin, "angular_min", "The fewest angular subdivisions the urchin is ever allowed to have."},
    {Kind::Urchin, "adaptive", "Lets the urchin's subdivision counts adjust themselves to its size instead of staying fixed."},
    {Kind::Urchin, "uv_parametric", "Lays out the urchin's texture coordinates parametrically over its surface instead of projecting them."},
    {Kind::Urchin, "u_tile", "How many times the material repeats around the urchin."},
    {Kind::Urchin, "v_tile", "How many times the material repeats from bottom to top of the urchin."},
    {Kind::Urchin, "u_offset", "Shifts the material's texture coordinates around the urchin."},
    {Kind::Urchin, "v_offset", "Shifts the material's texture coordinates from bottom to top of the urchin."},
    {Kind::Urchin, "disp_source", "What drives the urchin's surface displacement: none, noise, bumps, or an input field."},
    {Kind::Urchin, "disp_relative", "Scales the displacement by the urchin's own radius, so small and large urchins displace proportionally."},
    {Kind::Urchin, "disp_amount", "How far the urchin's surface is pushed in and out. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "disp_offset", "Shifts the displacement pattern before it is applied."},
    {Kind::Urchin, "child_count", "How many children grow on the urchin's skin. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "child_start", "Where on the urchin, from bottom to top, children begin growing. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "child_end", "Where on the urchin, from bottom to top, children stop growing. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "child_density", "How thickly children cluster over the urchin's surface, read off the horizontal axis as height 0..1."},
    {Kind::Urchin, "child_soft", "What happens to a fractional Number of children: rounded down, rounded up, scaled down, or resolved at random."},
    {Kind::Urchin, "child_orthogonal", "Orients each child perpendicular to the urchin's surface at its growing point."},
    {Kind::Urchin, "child_angle", "Tilts each child away from perpendicular, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "child_scale", "The scale applied to each child growing on the urchin. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "child_scale_shift", "How much each child's scale is randomised from the average. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "child_phi", "The angle added around the urchin from one child to the next, in degrees; 137.5 gives the golden-angle spiral. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "wind_flexibility", "How easily the urchin bends under wind, independent of its size. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "breeze_strength", "How far the urchin sways in the ambient breeze. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "breeze_override", "Ignores the species' inherited ambient-motion settings and uses the urchin's own."},
    {Kind::Urchin, "gust_gravity", "How strongly gravity pulls the urchin back after a gust. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Urchin, "gust_wind", "How strongly gusts push the urchin, on top of the steady wind. The value may be given a spread and shaped along the part / by the hierarchy curve."},

    // ---- Hydra
    {Kind::Hydra, "child_count", "How many children are spread around the circle. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Hydra, "radius", "The radius of the circle the children are spread around, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Hydra, "child_scale", "The scale applied to each child in the spread. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Hydra, "distribution", "How thickly children cluster around the circle, read off the horizontal axis as angle 0..1."},
    {Kind::Hydra, "angle1", "Tilts each child away from the circle's plane, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Hydra, "angle2", "Tilts each child sideways, tangential to the circle, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Hydra, "angle3", "Rotates each child around its own outward direction, in degrees. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Hydra, "child_soft", "What happens to a fractional Number of children: rounded off, scaled down, or resolved at random."},

    // ---- Ball
    {Kind::Ball, "radius", "The ball's radius, in metres. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Ball, "pivot_offset", "Shifts the ball's pivot point along its axis, changing where it attaches and rotates from. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Ball, "squash", "The ball's height relative to its width; below 1 flattens it, above 1 stretches it. The value may be given a spread and shaped along the part / by the hierarchy curve."},
    {Kind::Ball, "disable_baking", "Skips baking this ball's texture detail into vertex data, keeping it live instead."},
    {Kind::Ball, "mesh_boost", "Raises or lowers the ball's mesh resolution on top of its minimum."},
    {Kind::Ball, "min_subdiv", "The fewest subdivisions the ball's mesh is ever allowed to have."},
};

struct Reg {
  Reg() { tips_register(TABLE, sizeof TABLE / sizeof TABLE[0]); }
} reg_;

} // namespace
} // namespace plant
} // namespace gpx
