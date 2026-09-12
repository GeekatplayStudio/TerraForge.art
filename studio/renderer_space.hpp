// Geekatplay TerraForge - deep space uniforms (shaders_space.cpp): the star
// field and the galaxy band from RenderSettings, the nebulas from the
// scene's Nebula objects. Any program that carries SPACE_FN_PLACEHOLDER
// calls this after glUseProgram: the sky pass and the panorama export.
#pragma once

namespace studio {

// `finished`: a capture or a render pass, which marches the nebulas a
// quality step finer than the working view and is never capped by the
// performance governor.
void upload_space_uniforms(unsigned prog, bool finished = false);

// How far the nebula march steps at each quality (SpaceSettings::quality):
// 0 draft, 1 normal, 2 fine, 3 exhaustive. Only pixels inside a nebula pay
// for it, and only when deep space is visible at all.
int space_march_steps(int quality);
// the steps a view marches at: its quality under the governor's cap, or a
// step finer for a finished picture
int space_view_steps(bool finished);

// A nebula's direction in the sky from its azimuth and elevation (degrees),
// the frame the sun uses: azimuth 0 along +z, 90 along +x.
void nebula_direction(float azimuth_deg, float elevation_deg, float out[3]);

} // namespace studio
