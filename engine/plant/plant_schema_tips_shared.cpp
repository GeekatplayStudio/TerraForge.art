// Geekatplay TerraForge - tooltips for the shared parameter blocks
// (transform_params, lod_params, breeze_params, season_params,
// attachment_params in plant_schema.cpp). Registered under Kind::COUNT, so
// one entry here applies to every kind that includes that block.
#include "plant/plant_schema.hpp"

namespace gpx {
namespace plant {
namespace {

const Tip TABLE[] = {
    // ---- transform_params
    {Kind::COUNT, "wind_strength",
     "How hard the constant wind pushes this part, as a\n"
     "multiple of the species wind strength. The value may\n"
     "be given a spread and shaped along the part / by the\n"
     "hierarchy curve."},
    {Kind::COUNT, "breeze_override",
     "Ignores the ambient-motion settings inherited from the\n"
     "species and uses this part's own breeze strength\n"
     "instead."},
    {Kind::COUNT, "scale",
     "Overall size multiplier for this part, applied before\n"
     "the per-axis scales below. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "scale_x", "Extra scale along the part's local X axis, on top of Scale."},
    {Kind::COUNT, "scale_y", "Extra scale along the part's local Y axis, on top of Scale."},
    {Kind::COUNT, "scale_z", "Extra scale along the part's local Z axis, on top of Scale."},
    {Kind::COUNT, "scale_inherit",
     "Lets this part's size follow its parent's scale, so\n"
     "scaling the plant scales every child with it."},
    {Kind::COUNT, "offset_x", "Sideways shift from the attachment point, in metres."},
    {Kind::COUNT, "offset_y", "Shift along the attachment axis, in metres."},
    {Kind::COUNT, "offset_z", "Shift out of the attachment plane, in metres."},
    {Kind::COUNT, "rot_x", "Extra rotation around the local X axis, in degrees, on top of the attachment orientation."},
    {Kind::COUNT, "rot_y", "Extra rotation around the local Y axis, in degrees, on top of the attachment orientation."},
    {Kind::COUNT, "rot_z", "Extra rotation around the local Z axis, in degrees, on top of the attachment orientation."},
    {Kind::COUNT, "orient_vertical",
     "How strongly the part turns to face up or down instead\n"
     "of following its parent's orientation. The value may\n"
     "be given a spread and shaped along the part / by the\n"
     "hierarchy curve."},
    {Kind::COUNT, "orient_horizontal",
     "How strongly the part turns to lie flat instead of\n"
     "following its parent's orientation. The value may be\n"
     "given a spread and shaped along the part / by the\n"
     "hierarchy curve."},

    // ---- lod_params
    {Kind::COUNT, "lod_min",
     "The coarsest level of detail this part still appears\n"
     "at; it is dropped from levels rougher than this."},
    {Kind::COUNT, "lod_max",
     "The finest level of detail this part is drawn at; it\n"
     "is dropped once the view is closer than this level\n"
     "needs."},
    {Kind::COUNT, "lod_inherit",
     "Clamps this part's own LOD range to fall inside its\n"
     "parent's, so it never outlives the branch that carries\n"
     "it."},

    // ---- breeze_params
    {Kind::COUNT, "breeze_strength",
     "How far this part sways in the constant ambient motion\n"
     "that runs even without wind. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "breeze_flexibility",
     "How loosely this part follows the ambient motion\n"
     "relative to its neighbours; higher lags and overshoots\n"
     "more. The value may be given a spread and shaped along\n"
     "the part / by the hierarchy curve."},

    // ---- season_params
    {Kind::COUNT, "presence_season",
     "How much of this part exists over the course of a\n"
     "year, read off the horizontal axis as the season 0..1;\n"
     "0 sheds the part, 1 keeps it in full."},
    {Kind::COUNT, "tint_season",
     "The colour this part is tinted at each point of the\n"
     "year, read along the season 0..1."},
    {Kind::COUNT, "presence_health",
     "How much of this part survives as health drops, read\n"
     "off the horizontal axis as health 0..1; 0 sheds the\n"
     "part, 1 keeps it in full."},
    {Kind::COUNT, "tint_health",
     "The colour this part is tinted toward as its health\n"
     "changes, read along health 0..1."},
    {Kind::COUNT, "presence_age",
     "How much of this part exists as the plant matures,\n"
     "read off the horizontal axis as maturity 0..1; young\n"
     "or old plants can carry less of it."},
    {Kind::COUNT, "droop_health",
     "How far the part droops downward, in degrees, when the\n"
     "plant is dry. The value may be given a spread and\n"
     "shaped along the part / by the hierarchy curve."},
    {Kind::COUNT, "shrink_health",
     "How much smaller the part gets when the plant is dry,\n"
     "as a fraction of its full size. The value may be given\n"
     "a spread and shaped along the part / by the hierarchy\n"
     "curve."},

    // ---- attachment_params
    {Kind::COUNT, "presence",
     "The fraction of the instances at this position that\n"
     "actually grow; below 1 thins the distribution at\n"
     "random. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "count",
     "How many of this child grow on the parent, before\n"
     "presence and pruning thin them. The value may be given\n"
     "a spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "count_mode",
     "How Number is read: a fixed count, a count per metre\n"
     "of the parent's length, or the legacy per-length\n"
     "convention."},
    {Kind::COUNT, "soft_insert",
     "What happens to the fractional part of Number when it\n"
     "is not a whole number: rounded off, turned into an\n"
     "offset, scaled down, or resolved at random."},
    {Kind::COUNT, "start_mode",
     "How Start is measured: relative to the parent's\n"
     "length, or an absolute distance from one end."},
    {Kind::COUNT, "start",
     "Where the distribution of children begins along the\n"
     "parent. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "end_mode",
     "How End is measured: relative to the parent's length,\n"
     "or an absolute distance from one end."},
    {Kind::COUNT, "end",
     "Where the distribution of children ends along the\n"
     "parent. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "pair_offset_mode",
     "How Pair offset is measured: relative to the spacing\n"
     "between children, or an absolute distance."},
    {Kind::COUNT, "pair_offset",
     "Shifts alternating children apart along the parent,\n"
     "for a staggered rather than aligned pair. The value\n"
     "may be given a spread and shaped along the part / by\n"
     "the hierarchy curve."},
    {Kind::COUNT, "margin_before_cut",
     "Keeps children off the very tip of the parent, in\n"
     "metres, so pruning cuts do not leave a stub with\n"
     "nothing on it."},
    {Kind::COUNT, "density",
     "How thickly children cluster along the parent, read\n"
     "off the horizontal axis as position 0..1; high stretches\n"
     "of the curve gather more children."},
    {Kind::COUNT, "pruning",
     "The fraction of children removed after placement, as\n"
     "if cut away by hand. The value may be given a spread\n"
     "and shaped along the part / by the hierarchy curve."},
    {Kind::COUNT, "randomness",
     "How much each child's position is nudged off its\n"
     "regular slot, from perfectly even to scattered. The\n"
     "value may be given a spread and shaped along the part\n"
     "/ by the hierarchy curve."},
    {Kind::COUNT, "arrangement",
     "The pattern children are laid out in around the\n"
     "parent: spiral, alternating sides, opposite pairs,\n"
     "decussate pairs, or one per slot."},
    {Kind::COUNT, "positioning",
     "How a child is anchored to the parent's surface: at\n"
     "the tip, on the axis, on the skin, on a dummy point,\n"
     "orthogonal to the surface, blended automatically, or\n"
     "at the bottom."},
    {Kind::COUNT, "move_out",
     "Pushes the child away from the parent's axis, along\n"
     "its own outward direction. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "roll",
     "Rotates the child around the direction it points in,\n"
     "in degrees. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "coil",
     "The angle added around the parent's axis from one\n"
     "child to the next, in degrees; 137.5 is the golden-angle\n"
     "spiral seen in real phyllotaxis. The value may be given\n"
     "a spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "avoid_mesh",
     "Nudges children so they do not grow through an\n"
     "obstacle mesh in the scene."},
    {Kind::COUNT, "influenced_by_twist",
     "Lets the segment's own twist carry its children around\n"
     "with it, instead of holding their placement fixed."},
    {Kind::COUNT, "blend_subdiv",
     "Builds the join between this part and its parent with\n"
     "a subdivision surface, for a smoother blend than the\n"
     "regular mesh gives."},
    {Kind::COUNT, "blend_upper",
     "How far up the child the blended join reaches. The\n"
     "value may be given a spread and shaped along the part\n"
     "/ by the hierarchy curve."},
    {Kind::COUNT, "blend_lower",
     "How far down the parent the blended join reaches. The\n"
     "value may be given a spread and shaped along the part\n"
     "/ by the hierarchy curve."},
    {Kind::COUNT, "blend_child",
     "How far the blend reaches up the child, counted in the\n"
     "parent's radii, so one setting suits a trunk and a twig\n"
     "alike. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "blend_move_away",
     "Pushes the blended join outward from the parent, to\n"
     "avoid it looking sunken. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "blend_materials",
     "How far the parent's material bleeds into the child\n"
     "across the blended join. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "blend_normal",
     "How the surface normal is blended across the join,\n"
     "read off the horizontal axis as position along the\n"
     "blend 0..1."},
    {Kind::COUNT, "blend_post_offset",
     "Shifts the blended surface after it is built, to fix\n"
     "any remaining gap or overlap. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "blend_bending",
     "How strongly the parent's surface bends to meet the\n"
     "child at the join. The value may be given a spread and\n"
     "shaped along the part / by the hierarchy curve."},
    {Kind::COUNT, "blend_ignore_disp",
     "Leaves surface displacement out of the blended area,\n"
     "so bark detail does not fight with the join. The value\n"
     "may be given a spread and shaped along the part / by\n"
     "the hierarchy curve."},
    {Kind::COUNT, "shrink_radius",
     "How much the parent's radius is pinched in around each\n"
     "child, as if the child were pulling material with it.\n"
     "The value may be given a spread and shaped along the\n"
     "part / by the hierarchy curve."},
    {Kind::COUNT, "bending",
     "How sharply the parent kinks sideways at the child's\n"
     "attachment point, giving a zig-zag look. The value may\n"
     "be given a spread and shaped along the part / by the\n"
     "hierarchy curve."},
    {Kind::COUNT, "bending_smoothness",
     "How gradually the zig-zag kink eases in and out around\n"
     "the attachment point. The value may be given a spread\n"
     "and shaped along the part / by the hierarchy curve."},
    {Kind::COUNT, "angle",
     "How far the child tilts away from its parent's axis,\n"
     "in degrees. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "rotation",
     "Rotates the child's attachment angle around the\n"
     "parent's axis, in degrees. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "tropism_x", "The X component of the direction this child's tropism pulls it toward."},
    {Kind::COUNT, "tropism_y", "The Y component of the direction this child's tropism pulls it toward."},
    {Kind::COUNT, "tropism_z", "The Z component of the direction this child's tropism pulls it toward."},
    {Kind::COUNT, "tropism_cone",
     "How wide a cone around the tropism direction the child\n"
     "is allowed to settle within, in degrees. The value may\n"
     "be given a spread and shaped along the part / by the\n"
     "hierarchy curve."},
    {Kind::COUNT, "tropism_local",
     "Measures the tropism direction in the parent's local\n"
     "frame instead of the world, so it turns with the\n"
     "parent."},
    {Kind::COUNT, "tropism_angle",
     "How much of the child's tilt the tropism controls,\n"
     "versus its own natural attachment angle. The value may\n"
     "be given a spread and shaped along the part / by the\n"
     "hierarchy curve."},
    {Kind::COUNT, "tropism_rotation",
     "How much of the child's rotation around the parent the\n"
     "tropism controls. The value may be given a spread and\n"
     "shaped along the part / by the hierarchy curve."},
    {Kind::COUNT, "tropism_roll",
     "How much of the child's roll the tropism controls. The\n"
     "value may be given a spread and shaped along the part\n"
     "/ by the hierarchy curve."},
    {Kind::COUNT, "per_whorl",
     "How many children sit in one ring before the pattern\n"
     "steps to the next whorl. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "whorl_soft",
     "What happens to a fractional Per whorl count: rounded\n"
     "off, scaled down, or resolved at random."},
    {Kind::COUNT, "whorl_spread",
     "How much of the full circle one whorl's children are\n"
     "spread across, in degrees. The value may be given a\n"
     "spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "whorl_randomness",
     "How far each child within a whorl is nudged off its\n"
     "even slot. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "whorl_angle_randomness",
     "How much a whorl child's tilt angle is randomised, in\n"
     "degrees. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "whorl_position_randomness",
     "How much a whorl child's position along the parent is\n"
     "randomised. The value may be given a spread and shaped\n"
     "along the part / by the hierarchy curve."},
    {Kind::COUNT, "inherit_density",
     "How much of the parent's own density this child\n"
     "carries into its own distribution. The value may be\n"
     "given a spread and shaped along the part / by the\n"
     "hierarchy curve."},
    {Kind::COUNT, "inherit_scale",
     "How much of the parent's scale this child inherits on\n"
     "top of its own. The value may be given a spread and\n"
     "shaped along the part / by the hierarchy curve."},
    {Kind::COUNT, "inherit_sap",
     "How much of the parent's sap (vigour) this child\n"
     "inherits, which feeds growth and shadowing elsewhere.\n"
     "The value may be given a spread and shaped along the\n"
     "part / by the hierarchy curve."},
    {Kind::COUNT, "cut_probability",
     "The chance that this child is pruned away entirely.\n"
     "The value may be given a spread and shaped along the\n"
     "part / by the hierarchy curve."},
    {Kind::COUNT, "cut_length",
     "The length, in metres, that a pruned child is cut down\n"
     "to instead of removed outright. The value may be given\n"
     "a spread and shaped along the part / by the hierarchy\n"
     "curve."},
    {Kind::COUNT, "cut_radius_reduction",
     "How much the radius shrinks at a pruning cut. The\n"
     "value may be given a spread and shaped along the part\n"
     "/ by the hierarchy curve."},
};

struct Reg {
  Reg() { tips_register(TABLE, sizeof TABLE / sizeof TABLE[0]); }
} reg_;

} // namespace
} // namespace plant
} // namespace gpx
