# TerraForge Project Reference Map

**Comprehensive project architecture and implementation status**

---

## Project Overview

**TerraForge** is a native node-based 3D terrain and environment studio built in C++20 with OpenGL 4.3. It provides photorealistic landscape generation through procedural and simulated terrain, layered PBR materials, volumetric sky, water, and offline path-traced rendering in a single native application with real-time viewport.

**Version**: 2.0.0  
**Language**: C++20, Python 3.9+  
**Graphics**: OpenGL 4.3  
**Platform**: Windows 10/11 (portable codebase)

---

## Architecture Overview

```
TerraForge
├── Engine Layer (C++)
│   ├── Node Graph System
│   ├── Two-Domain Evaluation (Raster + Field)
│   ├── Erosion Simulation
│   ├── Material System
│   └── GLSL Transpilation
├── Studio Layer (C++)
│   ├── UI Framework (ImGui + imgui-node-editor)
│   ├── Viewport Renderer
│   ├── Panel System
│   ├── Undo/Redo System
│   └── AI Integration
├── Orchestrator Layer (Python)
│   ├── Render Backends
│   ├── AI Agents
│   └── MCP Integration
└── Test Layer
    ├── C++ Test Suites
    ├── Python Tests
    └── Regression Lock
```

---

## Directory Structure

```
NodeTerrain/
├── engine/                    # Core terrain engine
│   ├── gpx/                   # Public headers
│   │   ├── node_graph.hpp     # Graph evaluation & topology
│   │   ├── field.hpp          # Field domain types
│   │   ├── field_glsl.hpp     # GLSL transpiler
│   │   ├── attribute.hpp      # Parameter system
│   │   ├── metanode.hpp       # Node grouping
│   │   ├── heightmap.hpp      # Heightmap buffers
│   │   ├── animation.hpp      # Keyframe tracks
│   │   ├── serialization.hpp # JSON I/O
│   │   ├── noise_core.hpp     # Shared noise functions
│   │   ├── planet_math.hpp    # Planet rendering math
│   │   └── camera_math.hpp    # Camera utilities
│   ├── nodes/                 # Node implementations (21 files)
│   │   ├── nodes_primitives.cpp    # Noise, Fractal, Shape, Constant, Strata
│   │   ├── nodes_filters.cpp      # Blend, Math, MixLayers, Clamp, Levels
│   │   ├── nodes_erosion.cpp      # Hydraulic, StreamPower, Thermal, Aeolian
│   │   ├── nodes_operators.cpp    # Blend, Math, MixLayers
│   │   ├── nodes_masks.cpp        # Altitude, Slope, Cavity, Selection
│   │   ├── nodes_warp.cpp         # WarpNoise, WarpDirectional, Transform
│   │   ├── nodes_texture.cpp      # ColorizeGradient, TerrainTexture, NormalMap
│   │   ├── nodes_materials.cpp    # MaterialBlend, Roughness, Metallic
│   │   ├── nodes_pbr_library.cpp  # PBRMaterial (ambientCG integration)
│   │   ├── nodes_modeling.cpp     # Stamp, Resample, Tile
│   │   ├── nodes_surface.cpp      # PowerFractal, FakeStones, Stratify, Shear, Craggy, Crater, Dunes, Snow
│   │   ├── nodes_terrain_fx.cpp   # Grit, Gravel, Peaks, Sharpen, Cracks, Glaciation, Dissolve, TerrainClip, TerrainSculpt
│   │   ├── nodes_atmosphere.cpp   # CloudShape, CloudNoise, Atmosphere
│   │   ├── nodes_material_graph.cpp # Distribution, ColorBlend, SurfaceColor
│   │   ├── nodes_hydro.cpp        # FlowAccumulation, WetnessIndex
│   │   ├── nodes_logic.cpp        # Switch, Router, Gate
│   │   ├── nodes_field.cpp       # Field math, Redirect, Displace, ComputeNormal, Zones, Rasterize, Sample
│   │   ├── nodes_displace.cpp     # Displacement nodes
│   │   ├── nodes_field_material.cpp # Field material nodes
│   │   ├── nodes_analysis.cpp     # Analysis nodes
│   │   └── nodes_export.cpp       # Export nodes
│   ├── node_graph.cpp         # Graph evaluation engine
│   ├── field_glsl.cpp         # GLSL transpiler (776 lines)
│   ├── metanode.cpp           # MetaNode grouping
│   ├── attribute.cpp          # Attribute system
│   ├── animation.cpp          # Animation tracks
│   ├── serialization.cpp      # JSON serialization
│   ├── heightmap.cpp          # Heightmap buffers
│   └── parallel.cpp           # Parallel execution helpers
├── studio/                    # Desktop application (42 files)
│   ├── main.cpp               # Entry point
│   ├── app.cpp/app.hpp        # Main application state
│   ├── renderer.cpp           # OpenGL renderer (2095 lines)
│   ├── shaders_terrain.cpp    # Terrain shaders
│   ├── shaders_scene.cpp      # Scene shaders
│   ├── planet_renderer.cpp    # Planet rendering
│   ├── terrain_cull.cpp       # Per-patch visibility culling
│   ├── gpu_timer.cpp          # GPU performance timing
│   ├── blue_noise.cpp         # Blue noise sampling
│   ├── cloud_noise.cpp        # Cloud noise generation
│   ├── field_gpu_check.cpp    # CPU/GPU verification
│   ├── panel_graph.cpp        # Node graph UI (imgui-node-editor)
│   ├── panel_properties.cpp    # Properties editor (auto-generated)
│   ├── panel_library.cpp      # Node library browser
│   ├── panel_nodelist.cpp     # Tree view of graph
│   ├── panel_viewport.cpp     # Viewport panel
│   ├── panel_environment.cpp  # Environment settings
│   ├── panel_materials.cpp    # Material editor
│   ├── panel_scene.cpp        # Scene object editor
│   ├── panel_render.cpp       # Render settings
│   ├── panel_camera.cpp       # Camera controls
│   ├── panel_ai.cpp           # AI assistant panel
│   ├── toolbar.cpp            # Main toolbar
│   ├── toolbar_bars.cpp       # Toolbar components
│   ├── theme.cpp              # UI theming
│   ├── theme_colors.cpp       # Color definitions
│   ├── undo.cpp/undo.hpp      # Undo/Redo system
│   ├── sculpt.cpp             # Terrain sculpting
│   ├── preview.cpp            # Node previews
│   ├── project_io.cpp         # Project save/load
│   ├── scene.cpp/scene.hpp    # Scene graph
│   ├── scene_nodes.cpp        # Scene object management
│   ├── material_library.cpp   # Material library
│   ├── node_library.cpp       # Node library
│   ├── ai_assist.cpp          # AI integration
│   ├── ai_ops_graph.cpp       # AI graph operations
│   ├── ai_ops_view.cpp        # AI view operations
│   ├── studio_api.cpp         # Python API bridge
│   ├── i18n.cpp               # Internationalization
│   ├── file_dialogs.cpp       # File dialogs
│   ├── prefs.cpp/prefs.hpp    # User preferences
│   ├── ollama.cpp             # Ollama integration
│   └── resources/             # UI resources
├── orchestrator/              # Python automation layer
│   ├── cli.py                 # Command-line interface
│   ├── graph.py               # Graph operations
│   ├── state.py               # State management
│   ├── render_engines.py      # Render backend orchestration
│   ├── render_mitsuba.py      # Mitsuba 3 integration
│   ├── render_cycles.py       # Blender Cycles integration
│   ├── texture_ai.py          # AI texture generation
│   ├── tonemap.py             # Tone mapping
│   └── agents/                # AI agent definitions (15 agents)
│       ├── art_director.py
│       ├── backend_engineer.py
│       ├── build_engineer.py
│       ├── copywriter.py
│       ├── cpp_systems_coder.py
│       ├── geologist.py
│       ├── gpu_master.py
│       ├── materials_master.py
│       ├── project_manager.py
│       ├── scientist.py
│       ├── system_architect.py
│       ├── tester.py
│       ├── three_engineer.py
│       └── ui_designer.py
├── mcp_server/                # MCP tool server
│   ├── server.py              # MCP server implementation
│   ├── studio_api.py          # Studio API for automation
│   └── studio_graph_tools.py  # Graph manipulation tools
├── src/                       # Original solver library (C++)
│   ├── noise.cpp              # Noise algorithms
│   ├── erosion.cpp            # Erosion solvers
│   ├── geology.cpp            # Geological processes
│   ├── materials.cpp          # Material functions
│   ├── ecosystem.cpp          # Ecosystem simulation
│   ├── atmosphere.cpp         # Atmosphere models
│   ├── dag.cpp                # Directed acyclic graph
│   ├── persistence.cpp        # Data persistence
│   ├── mcp_bridge.cpp         # MCP bridge
│   └── main.cpp               # CLI entry point
├── include/geekatplay/        # Public C++ headers
│   ├── noise.hpp
│   ├── erosion.hpp
│   ├── geology.hpp
│   ├── materials.hpp
│   ├── ecosystem.hpp
│   ├── atmosphere.hpp
│   ├── dag.hpp
│   └── persistence.hpp
├── tests/                     # Test suites
│   ├── cpp/                   # C++ tests
│   │   ├── test_main.cpp      # Original solver tests
│   │   ├── test_engine.cpp    # Engine tests (94KB)
│   │   ├── test_nodes.cpp     # Universal node contract
│   │   ├── test_regression.cpp # Regression lock
│   │   ├── test_undo.cpp      # Undo/Redo tests
│   │   └── test_render.cpp    # Renderer tests
│   ├── manifest/              # Regression records
│   │   ├── nodes.json         # Node census
│   │   ├── attributes.json    # Attribute census
│   │   ├── golden.json        # Golden project hashes
│   │   └── features.json      # Feature manifest
│   ├── projects/              # Golden test projects (14 projects)
│   ├── test_api_coverage.py   # API coverage tests
│   ├── test_core_solvers.py   # Core solver tests
│   ├── test_mcp_server.py     # MCP server tests
│   └── test_orchestrator.py   # Orchestrator tests
├── core/                      # Python core modules
│   ├── noise.py
│   ├── erosion.py
│   ├── geology.py
│   ├── materials.py
│   ├── ecosystem.py
│   ├── atmosphere.py
│   ├── dag.py
│   └── persistence.py
├── external/                  # Third-party dependencies (fetched at build)
│   ├── glfw/                  # Window/input
│   ├── imgui/                 # Immediate mode GUI
│   ├── imgui-node-editor/     # Node graph UI
│   ├── glm/                   # Math library
│   ├── glad/                  # OpenGL loader
│   └── miniz/                 # Compression
├── _temp_hesiod/              # Legacy documentation (reference only)
│   └── docs/                  # Hesiod project docs
├── CMakeLists.txt             # Build configuration
├── build.ps1                  # PowerShell build script
├── start.ps1                  # Application launcher
├── test.ps1                   # Test runner
├── requirements.txt           # Python dependencies
├── AGENTS.md                  # Engineering rules (CRITICAL)
└── README.md                  # Project documentation
```

---

## Implementation Status by Feature Area

### ✅ FULLY IMPLEMENTED

#### 1. Node Graph System
- **Two-domain architecture**: Raster (buffer-based) + Field (resolution-independent)
- **Dirty-tracking evaluation**: Only recomputes what changed
- **Multithreaded solvers**: Parallel execution where applicable
- **Per-node previews**: Thumbnail previews for each node
- **Per-node timings**: Performance metrics displayed per node
- **Resolution support**: 64 to 8192 pixels
- **Cycle detection**: Rejects cyclic graphs
- **Bypass system**: Ctrl+E to bypass nodes (resolves at link level)
- **MetaNodes**: Ctrl+G to group subgraphs into reusable nodes
- **Universal blend**: Automatic mask input on terrain→terrain nodes
- **Node List**: Tree view of graph from result backwards
- **Serialization**: JSON-based project save/load (.gpxt format)

#### 2. Field Domain (GPU Compilation)
- **GLSL transpilation**: Field graphs compile to shaders
- **Shared noise implementation**: CPU and GPU use same `gpx::planet::pl_*` functions
- **CPU/GPU verification**: Automated test ensures agreement (threshold: 2e-4, measured: 1.3e-5)
- **Live GPU evaluation**: Field graphs drive viewport in real-time
- **Adaptive subdivision**: 64×64 patches, 512-2048 effective resolution
- **Quality boost**: Displacement can resolve finer than geometry
- **Redirect node**: Domain distortion (warp, flow, swirl)
- **Displace node**: Value to relief conversion
- **Compute Normal**: Surface direction after displacement
- **Zones**: Sphere/box confinement with fade
- **Rasterize/Sample**: Bridges between domains

#### 3. Erosion Simulation
- **Particle droplet hydraulic erosion**: Fast, detailed carving
- **Shallow-water pipe model**: More physical simulation (Mei et al. 2007)
- **Stream-power fluvial incision**: Explicit D8 solver
- **Implicit Braun-Willett solver**: With tectonic uplift and rock hardness
- **Thermal talus weathering**: Optional run-to-convergence
- **Aeolian dunes**: Wind-driven sediment transport
- **Sediment deposition**: Fill basins with transported material
- **Deterministic parallel execution**: Same result on any thread count
- **Relief preservation**: Clamps prevent spike normalization

#### 4. Terrain Generators
- **Coherent noise**: Perlin fBm, Ridged, Billow, Swiss, Value fBm
- **Cellular noise**: Worley F1, F2, edges, F1*F2
- **Fractal methods**: Diamond-square, fault lines
- **Geometric shapes**: Slope, bump, crater, cone, ridge line
- **Geological strata**: Layered rock from input heightmap
- **Craters**: Bowl, rim lip, ejecta blanket (single or field)
- **Dunes**: Asymmetric slip faces, crest chaos, ripples
- **Surface realism**: Multi-scale power-fractal displacement, procedural boulders
- **Tilted stratification**: Rock strata exposed on cliffs
- **Shear folding**: Directional rock shearing
- **Slope-targeted detail**: Craggy detail on steep ground
- **Snow with settle-thaw**: Snow cover with slip-off

#### 5. Terrain Effects
- **Grit**: Fine random bumps and holes
- **Gravel**: Slope-seeking debris
- **Peaks**: Lifts high ground, digs valleys deeper
- **Sharpen**: Makes steep ground steeper
- **Cracks**: Narrow fissures (post-quake)
- **Glaciation**: U-shaped valleys, ridges intact
- **Dissolve**: Stream carving with flow-map output
- **Terrain clip**: Flat tops above, holes below
- **Terrain sculpt**: Hand-sculpted layer with brush strokes

#### 6. Terrain Analysis
- **Flow accumulation (D8)**: Water passage through each point
- **Wetness index**: ln(a/tan b) standard measure
- **Resample**: Half, quarter, double, custom sampling

#### 7. Material System
- **PBR terrain shading**: Roughness, metallic, specular, reflections
- **Distribution**: Altitude, steepness, facing criteria
- **Color blending**: Mix, add, multiply, screen, overlay, darken, lighten
- **Live GPU evaluation**: Material fields drive viewport
- **Multi-channel**: Albedo, roughness, bump from same graph
- **Photoscanned materials**: ambientCG CC0 PBR sets (albedo, normal, roughness, AO)
- **Multilayer compositing**: Splat maps, layer blending, color grading
- **Terrain texture**: Physically-inspired layered albedo
- **Normal map generation**: Tangent-space from height

#### 8. Environment & Rendering
- **Volumetric clouds**: Raymarched with Perlin-Worley noise
- **Cloud types**: Stratus, cumulus, cumulonimbus
- **Coverage, wind, self-shadowing**
- **Sky and light**: Configurable atmosphere, height fog with absorption
- **Sun positioning**: Manual or real lat/long/date/time
- **Water**: Depth-graded color, waves, shoreline/crest foam
- **Viewport**: 1-6 dockable views (perspective/top/front/right)
- **Shading modes**: Multiple render modes
- **Shadow mapping**: Real-time shadows
- **Scale bar**: Metric or imperial units
- **Planet renderer**: Unlimited procedural planets
- **Infinite terrains**: Endless procedural layers
- **Continuous zoom**: From planetary neighbourhood to individual ridges
- **Progressive quality**: LOD based on on-screen size
- **Adaptive tessellation**: Per-edge subdivision

#### 9. UI/Studio Features
- **Node graph editor**: imgui-node-editor based
- **Properties panel**: Auto-generated from attributes
- **Node library**: Categorized browser
- **Node list**: Tree view of graph
- **Viewport panel**: Real-time 3D preview
- **Environment panel**: Sky/cloud/water settings
- **Materials panel**: Material editor
- **Scene panel**: Object editor
- **Render panel**: Render settings
- **Camera panel**: Camera controls
- **AI panel**: AI assistant interface
- **Toolbar**: Main operations toolbar
- **Undo/Redo**: Full history with named steps
- **Project save/load**: JSON format (.gpxt)
- **Layout persistence**: Dockable layout saved between sessions
- **Internationalization**: tr("tag") system for all UI strings
- **Flat visual language**: Dark grey with orange accent
- **Numeric rows**: Slider-look with drag/type/wheel
- **Panel refresh without blanking**: Cached snapshot pattern
- **Interactive resolution**: Reduced res during drags

#### 10. AI Integration
- **Ollama integration**: Local LLM support
- **Text-to-terrain**: Describe landscape in plain language
- **Photo-to-terrain**: Drop in photograph to generate terrain
- **AI agents**: 15 specialized agents (art_director, geologist, gpu_master, etc.)
- **AI graph operations**: Automated graph construction
- **AI view operations**: Automated viewport manipulation

#### 11. Automation & APIs
- **MCP server**: Tool server for automation
- **Studio API**: Python API for graph manipulation
- **Graph tools**: High-level graph operations
- **Undo/redo from automation**: All changes undoable
- **CLI**: Command-line interface for batch operations

#### 12. Offline Rendering
- **Mitsuba 3 integration**: Path-traced rendering
- **Blender Cycles integration**: Production rendering
- **LuxCoreRender integration**: Alternative path tracer
- **HDR environment**: Reuses viewport sky/clouds
- **Material reuse**: Same materials as preview
- **Tone mapping**: Configurable output

#### 13. Testing Infrastructure
- **Six test suites**: All must pass before commit
- **nodeterrain_tests**: Original solver library and CLI
- **engine_tests**: Registry, evaluation, caching, determinism, erosion, materials, serialization, field domain, GLSL transpiler, bypass, MetaNodes, blend, animation
- **undo_tests**: Restore correctness, redo branching, history jumps
- **node_tests**: Universal node contract (6,056 assertions over 118 nodes)
- **regression_tests**: Regression lock (1,726 checks over 118 nodes, 540 attributes, 61 features)
- **pytest**: Render backends and AI helpers
- **Golden projects**: 14 committed projects must evaluate to same hash
- **Feature manifest**: Every feature must have a test

#### 14. Build System
- **CMake 3.20+**: Modern build configuration
- **C++20**: Latest language standard
- **Ninja**: Fast builds
- **MSVC/MinGW support**: Windows compilers
- **Dependency fetching**: External deps fetched at build time
- **Compile commands**: IDE integration support

### ⚠️ PARTIALLY IMPLEMENTED

#### Animation System
- **Implemented**:
  - Every parameter can carry a keyframe track (linear, smooth, constant)
  - Animation tracks live on Attribute itself
  - Graph::apply_animation() samples before topological walk
  - No node knows animation exists (reads attribute it always read)
  - Copy, serialize, undo, publish carry animation automatically
- **NOT Implemented**:
  - **Timeline UI**: No user interface for editing keyframes
  - Timeline controls (play/pause/scrub)
  - Keyframe visualization in graph
  - Animation curve editor

### ❌ NOT IMPLEMENTED

#### Timeline UI
- The README explicitly states: "The timeline UI is not built yet"
- This is the only major feature gap identified

---

## Node System Inventory

### Total Nodes: 113+ registered nodes across 21 categories

#### By Category:

1. **Primitive** (nodes_primitives.cpp, nodes_surface.cpp)
   - Noise (9 types: Perlin fBm, Ridged, Billow, Swiss, Value fBm, Worley F1/F2/edges/F1*F2)
   - Fractal (diamond-square, fault lines)
   - Shape (slope, bump, crater, cone, ridge line)
   - Constant
   - GeologicalStrata
   - FakeStones
   - Crater
   - Dunes

2. **Filter** (nodes_filters.cpp, nodes_surface.cpp)
   - Blend (many modes)
   - Math (per-pixel operations)
   - MixLayers (height-stack)
   - Clamp
   - Levels
   - PowerFractal
   - Stratify
   - Craggy
   - Snow

3. **Erosion** (nodes_erosion.cpp, nodes_terrain_fx.cpp)
   - Hydraulic (particle droplets)
   - Hydraulic (shallow-water pipe model)
   - StreamPower (explicit D8)
   - StreamPower (implicit Braun-Willett)
   - Thermal talus
   - Aeolian (wind)
   - Sediment fill
   - Glaciation
   - Dissolve

4. **Effect** (nodes_terrain_fx.cpp)
   - Grit
   - Gravel
   - Peaks
   - Sharpen
   - Cracks
   - TerrainClip
   - TerrainSculpt

5. **Transform** (nodes_warp.cpp, nodes_surface.cpp)
   - WarpNoise
   - WarpDirectional
   - Transform (translate/scale/rotate)
   - Shear

6. **Texture** (nodes_texture.cpp)
   - ColorizeGradient
   - TerrainTexture
   - NormalMap

7. **Material** (nodes_materials.cpp, nodes_pbr_library.cpp, nodes_material_graph.cpp)
   - MaterialBlend
   - Roughness
   - Metallic
   - Specular
   - PBRMaterial (ambientCG)
   - Distribution
   - ColorBlend
   - SurfaceColor

8. **Mask** (nodes_masks.cpp)
   - Altitude
   - Slope
   - Cavity
   - Selection

9. **Operator** (nodes_operators.cpp)
   - Blend
   - Math
   - MixLayers

10. **Modeling** (nodes_modeling.cpp)
    - Stamp
    - Resample
    - Tile

11. **Atmosphere** (nodes_atmosphere.cpp)
    - CloudShape
    - CloudNoise
    - Atmosphere

12. **Hydro** (nodes_hydro.cpp)
    - FlowAccumulation
    - WetnessIndex

13. **Logic** (nodes_logic.cpp)
    - Switch
    - Router
    - Gate

14. **Field** (nodes_field.cpp)
    - Field math operations
    - Redirect
    - Displace
    - ComputeNormal
    - Zones
    - Rasterize
    - Sample

15. **Field Material** (nodes_field_material.cpp)
    - Field material nodes

16. **Analysis** (nodes_analysis.cpp)
    - Analysis nodes

17. **Export** (nodes_export.cpp)
    - Export nodes

18. **Displace** (nodes_displace.cpp)
    - Displacement nodes

19. **Surface** (nodes_surface.cpp)
    - Surface nodes

20. **Terrain FX** (nodes_terrain_fx.cpp)
    - Terrain effects

21. **PBR Library** (nodes_pbr_library.cpp)
    - PBR material library

---

## Engineering Rules (AGENTS.md Compliance)

### UI Invariants
✅ **Panels must never blank out** - Implemented via `App::refresh_snapshot()` and cached data pattern  
✅ **No flicker on refresh** - Cache and diff pattern used  
✅ **Selection never steals active Properties tab** - Tab changes only on explicit action  
✅ **Every user-visible string through tr("tag")** - i18n system in place  
✅ **Flat visual language** - Dark grey with orange accent, custom Checkbox  
✅ **Numeric rows** - [-] [slider-look drag] [+] pattern implemented  

### Performance Rules
✅ **Never upload GPU texture per frame** - Versioned uploads in renderer  
✅ **Interactive edits at reduced resolution** - `prefs().interactive_res` implemented  
✅ **Node previews not regenerated during drags** - Implemented  
✅ **Measure before and after** - Node timings on every node  
✅ **Never measure with wall-clock** - `GpuTimer::Scope` used for GPU timing  
✅ **Performance numbers must be explained** - Patch count published beside timing  
✅ **Pair and interleave benchmarks** - A/B testing methodology  
✅ **Conservative bounds from real data** - Full-resolution heightmap used for bounds  

### Engine Rules
✅ **Every step deterministic** - Parallel solvers use per-worker buffers, fixed-order reduction  
✅ **Erosion must not destroy relief** - Per-round clamping, `test_erosion` enforces  
✅ **Port lookups direction-aware** - `Node::port(name, dir)` implemented  
✅ **Node parameters declarative** - `add_float`, `add_choice`, etc. with tooltips  

### Node Framework Rules
✅ **Two domains** - Raster (buffer) and Field (resolution-independent) implemented  
✅ **Every field node has GLSL emitter** - Enforced by `node_tests`  
✅ **CPU and GLSL paths agree** - Shared `gpx::planet::pl_*`, verified by `verify_field_gpu`  
✅ **Bypass resolved in link resolution** - `resolve_upstream` / `bypass_source` pattern  
✅ **Universal blend graph-provided** - `add_universal_blend` for terrain→terrain nodes  
✅ **MetaNode injects via buffer on port** - Port buffer precedence implemented  
✅ **Animation tracks on Attribute** - No side table, automatic copy/serialize/undo  
✅ **GLSL emitter resolves before streaming** - `EmitCtx::declare()` pattern prevents splicing  
✅ **Transpiler type conversions mirror FieldValue** - Number broadcast, vector length, color luminance  
✅ **Nodes have multiple field outputs** - Emission keyed by node+port+evaluation point  

### Generated Shaders Rules
✅ **Always substitute placeholder** - Stub or generated function  
✅ **Check GL_LINK_STATUS** - `link_checked` on all generated shaders  
✅ **Keep old program until new links** - Prevents black screen on error  
✅ **Remember request not just source** - Prevents infinite relink loop  
✅ **Displace normal wherever geometry** - Fragment stage central differences  
✅ **Vertex/fragment separate units** - Each can carry prelude copy  
✅ **Every pass sees same surface** - Shared `TERRAIN_VERT_COMMON`  
✅ **Tessellation needs floor and ceiling** - Minimum subdivision prevents coarsening  
✅ **Bind every sampler declared** - Refuses graph if insufficient texture units  
✅ **Build declaration with EmitCtx::declare()** - Prevents mid-statement splicing  

### Persisted UI State Rules
✅ **Validate saved layout before use** - `graph_view_is_sane` validation  

---

## Test Coverage Summary

### C++ Test Suites
- **nodeterrain_tests**: Original solver library and CLI
- **engine_tests**: Registry, evaluation, caching, cycle rejection, determinism, erosion, materials, serialization, field domain, GLSL transpiler, bypass, MetaNodes, blend, animation
- **undo_tests**: Restore correctness, redo branching, history jumps, node library round-trip
- **node_tests**: Universal node contract (6,056 assertions over 118 node types: metadata, ports, determinism, bypass, serialization, extremes, GLSL emitter)
- **regression_tests**: Regression lock (1,726 checks: node census, attribute census, golden hashes, feature manifest)
- **render_tests**: Renderer maths (per-patch visibility, packing helpers)

### Python Test Suites
- **test_api_coverage.py**: API coverage
- **test_core_solvers.py**: Core solver tests
- **test_mcp_server.py**: MCP server tests
- **test_orchestrator.py**: Orchestrator tests

### Golden Projects
- 14 committed .gpxt projects in `tests/projects/`
- Must evaluate to same hash on every run
- Covers major feature combinations

### Regression Lock
- **nodes.json**: Census of all 118 nodes (cannot remove or change category)
- **attributes.json**: Census of all 540 attributes (cannot remove or retype)
- **golden.json**: Hashes of 14 committed projects
- **features.json**: Manifest of 61 features (each must name existing test)

---

## Dependencies

### Build-time Dependencies (fetched by scripts/get_deps.ps1)
- **GLFW**: Window/input handling
- **GLAD**: OpenGL loader
- **Dear ImGui**: Immediate mode GUI
- **imgui-node-editor**: Node graph UI
- **GLM**: Math library
- **miniz**: Compression
- **nlohmann/json**: JSON parsing
- **stb**: Image write

### Runtime Dependencies (optional)
- **Python 3.9+**: For AI assistance and offline rendering
- **Ollama**: For local LLM (llama3.1, llava)
- **Mitsuba 3**: For path-traced rendering
- **Blender**: For Cycles rendering
- **LuxCoreRender**: Alternative path tracer

---

## Key Technical Decisions

### Two-Domain Architecture
- **Raster domain**: Buffer-based, neighbor-aware, where erosion lives
- **Field domain**: Resolution-independent, compiles to GLSL, GPU-evaluated
- **Bridges**: `Rasterize` (field→raster) and `Sample` (raster→field)
- **Rationale**: Erosion needs neighbor access; displacement needs resolution independence

### Determinism by Design
- Parallel solvers use per-worker buffers
- Fixed-order reduction prevents race conditions
- Same graph + seeds = bit-identical output on any thread count
- Enforced by `test_workflow_determinism`

### Declarative Node Parameters
- `add_float`, `add_choice`, `add_int`, etc.
- Auto-generates: Properties UI, serialization, AI catalog
- Single source of truth for node interface

### Bypass at Link Resolution
- Resolved in `resolve_upstream` / `bypass_source`
- Graph walks through bypassed nodes in both domains
- No per-node "enabled" check needed
- Works for every future node automatically

### Animation on Attribute
- Tracks live on `Attribute` itself, not side table
- Copy, serialize, undo, publish all carry animation automatically
- `Graph::apply_animation()` samples before topological walk
- Nodes never know animation exists

### CPU/GPU Agreement
- Field nodes share `gpx::planet::pl_*` noise implementation
- GLSL prelude is line-for-line mirror of CPU code
- `verify_field_gpu` measures agreement (threshold: 2e-4, measured: 1.3e-5)
- Prevents silent GPU failures

### Panel Refresh Without Blanking
- Evaluation holds `App::graph_mtx` for entire run
- Panels draw from cached `App::node_views` / `App::link_views`
- `App::refresh_snapshot()` updates cache when lock is free
- Properties panel uses `NodeMirror` pattern: edit mirror, flush on lock

### Performance Measurement
- `GpuTimer::Scope` for GPU timing (reads query from several frames ago)
- Never use wall-clock with vsync (GPU finishes early and waits)
- Publish patch count beside timing (cross-check)
- Pair and interleave benchmark arms (cancel drift)

---

## Known Limitations & Future Work

### Timeline UI
- **Status**: NOT IMPLEMENTED
- **Impact**: Users cannot visually edit animation keyframes
- **Workaround**: Animation system works internally, but no UI
- **Priority**: Medium (core system works, only UI missing)

### Documentation
- **Status**: Partial
- **Impact**: _temp_hesiod/ contains legacy Hesiod docs (not TerraForge-specific)
- **Need**: TerraForge-specific user manual and API documentation
- **Priority**: Low (README is comprehensive)

---

## Build & Development Workflow

### Build Commands
```powershell
# First-time setup
powershell -ExecutionPolicy Bypass -File scripts\get_deps.ps1

# Build
.\build.ps1

# Run
.\start.ps1

# Run tests
.\test.ps1
```

### Development Rules
1. All six test suites must pass before commit
2. Every user-visible string must go through `tr("tag")`
3. Every field node must have GLSL emitter
4. Never add per-node "enabled" check (use bypass)
5. Never measure renderer changes with wall-clock
6. Panels must never block on `graph_mtx`
7. Animation tracks live on Attribute, not side tables
8. CPU and GLSL paths must share noise implementation
9. Validate all persisted UI state before use
10. Use `EmitCtx::declare()` for GLSL declarations

---

## Summary

TerraForge is a **highly mature, production-ready** terrain generation system with:

- **113+ nodes** across 21 categories
- **Two-domain architecture** (raster + field) with GPU compilation
- **Comprehensive erosion simulation** (hydraulic, stream power, thermal, aeolian)
- **Real-time viewport** with adaptive tessellation and planet rendering
- **PBR material system** with photoscanned texture integration
- **AI assistance** via local LLM integration
- **Robust testing** (6,056 node contract assertions, 1,726 regression checks)
- **Strong engineering discipline** (comprehensive AGENTS.md rules)

**Only one major feature gap identified**: Timeline UI for animation editing (core animation system works, but lacks visual editor).

The codebase demonstrates exceptional attention to:
- Performance (GPU timing, adaptive quality, parallel execution)
- Correctness (determinism, regression lock, comprehensive tests)
- UX (panel refresh without blanking, undo/redo, internationalization)
- Maintainability (declarative parameters, shared CPU/GPU code, clear architecture)
