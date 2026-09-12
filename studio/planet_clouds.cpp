// Geekatplay TerraForge - a planet's weather seen from space.
//
// The home world's clouds are the cloud layers, marched as volumes from the
// ground to orbit (renderer_clouds.cpp). A planet in the sky is a disc a few
// hundred pixels across, and marching a volume over it would spend a sky's
// worth of work on a coin; what reads at that size is the deck's pattern, its
// shadow on the ground and its light. So a planet's clouds are a sheet by
// direction over its surface: a fine field pulled round by a coarse one into
// fronts and storms, drifting slowly, sheared by latitude the way a real deck
// is, spliced into FS_PLANET through PL_CLOUDS_PLACEHOLDER. Needs PL_FN (the
// noise) before it and u_atmo declared.
namespace studio {

const char *PL_CLOUDS_FN = R"GLSL(
uniform float u_pl_clouds;  // cover, 0 none .. 1 overcast (PlanetData::clouds)
uniform float u_pl_cl_time; // the scene's cloud clock, seconds
uniform uint u_pl_cl_seed;
vec3 pl_turn_y(vec3 d, float a){
  float c = cos(a), s = sin(a);
  return vec3(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
}
// The deck's opacity along a direction in the planet's own frame, 0..1.
float pl_cloud_deck(vec3 rd, float octf){
  if (u_pl_clouds <= 0.001 || u_atmo <= 0.0) return 0.0;
  float lat = rd.y;
  // the deck runs ahead of the ground, faster toward the equator: a still
  // frame of it is already sheared, and it never turns as one rigid shell
  float drift = u_pl_cl_time * 0.002 * (1.0 + 0.5 * (1.0 - lat * lat));
  vec3 q = pl_turn_y(rd, drift) * 2.6;
  q.y *= 1.6; // weather is wider than it is tall
  // fronts and storms: the fine field pulled round by a coarse one
  vec3 w = vec3(pl_fbm(q * 0.8 + vec3(3.1, 7.7, 1.3), u_pl_cl_seed + 11u, 4.0, 0),
                pl_fbm(q * 0.8 + vec3(9.4, 2.2, 5.8), u_pl_cl_seed + 23u, 4.0, 0),
                pl_fbm(q * 0.8 + vec3(4.6, 8.1, 0.2), u_pl_cl_seed + 37u, 4.0, 0));
  q += w * 1.2;
  float n = pl_fbm(q, u_pl_cl_seed + 51u, clamp(octf, 1.0, 9.0), 0) * 2.5 + 0.5;
  // the fine structure a deck breaks into - cells and streets of cumulus
  // at the edges, not a smooth smear - where the planet is large enough on
  // screen to carry it
  if (octf > 3.0)
    n += pl_fbm(q * 3.7 + vec3(7.1, 2.9, 5.3), u_pl_cl_seed + 83u, clamp(octf - 2.0, 1.0, 7.0), 2) * 0.55;
  // storm tracks at the middle latitudes, drier subtropics, cloudier poles
  float a = abs(lat);
  n += 0.12 * cos(a * 11.0) + 0.1 * smoothstep(0.7, 0.95, a);
  float edge = 1.0 - u_pl_clouds;
  return smoothstep(edge - 0.1, edge + 0.3, n);
}
)GLSL";

} // namespace studio
