cmake_minimum_required(VERSION 3.20)

# macOS: the oldest system we build for. This has to be set *before* project(),
# which is when the compiler is probed — set afterwards it would not reach the
# ABI checks. Left alone if the user or a packaging script already chose one.
#
# CMAKE_OSX_ARCHITECTURES is deliberately not set, so a Mac builds natively for
# its own architecture. A packaging script that wants a universal binary sets
# it on the command line; do not hard-code it here.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()

# RC is the Windows resource compiler. It exists nowhere else, and naming it
# in LANGUAGES makes configure fail on macOS and Linux before it starts — so
# it is enabled below, under if(WIN32), instead.
project(GeekatplayTerraForge VERSION 2.0.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Floating-point policy.
#
# We used to build with -ffast-math (/fp:fast). That implies -ffinite-math-only,
# which tells the compiler NaN and Inf cannot occur — so GCC folds every
# std::isfinite() to true. Measured with our exact flags: isfinite(NaN) returns
# 1. That deleted, silently:
#   - the NaN/Inf guard in gpx::FieldValue::finite() (engine/gpx/field.hpp)
#   - both "output has NaN/Inf" checks in the universal node contract battery,
#     which apply to every port of every node in the registry
#   - nine more finiteness assertions across the engine tests
# The whole Tier-1 finiteness requirement was passing without testing anything.
#
# It also permits reassociation, which makes the committed golden hashes valid
# only for the exact compiler that produced them.
#
# What it bought, measured on the solvers that dominate a real graph
# (medians of 3, same seeds; -O3 vs -O3 -ffast-math):
#   noise 1024          50.1 -> 49.9 ms
#   hydraulic 512      128.7 -> 125.2 ms
#   thermal 512        253.8 -> 251.7 ms
#   stream power 512   796.1 -> 784.8 ms
#   5-node chain 512   404.3 -> 403.5 ms
# About 1%, inside run-to-run noise. Not worth a safety net.
if(MSVC)
    add_compile_options(/O2 /permissive-)
else()
    # -g alongside -O3: no runtime cost, and a crash report's module+RVA
    # resolves to file:line with addr2line (scripts/resolve_crash.py).
    add_compile_options(-O3 -g)
endif()

# ---------------------------------------------------------------- dependencies
# System libraries, per platform.
#
# Windows keeps naming opengl32 directly, exactly as it always has — the point
# of this file is portability, not churn on the one platform that works. macOS
# and Linux go through FindOpenGL, which knows about the framework and about
# GLVND. On Linux OpenGL_GL_PREFERENCE has no default (CMP0072) and CMake warns
# unless it is told which one; GLVND is the modern answer.
if(NOT WIN32)
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL REQUIRED)
    # engine/parallel.cpp runs std::thread; on Linux that needs -pthread at
    # compile and at link, and every test binary links the engine.
    find_package(Threads REQUIRED)
endif()

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

set(IMNE_DIR ${CMAKE_SOURCE_DIR}/external/imgui-node-editor)
add_library(imgui_node_editor STATIC
    ${IMNE_DIR}/imgui_node_editor.cpp
    ${IMNE_DIR}/imgui_node_editor_api.cpp
    ${IMNE_DIR}/imgui_canvas.cpp
    ${IMNE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor PUBLIC ${IMNE_DIR})
target_link_libraries(imgui_node_editor PUBLIC imgui)

add_library(third_party_headers INTERFACE)
target_include_directories(third_party_headers INTERFACE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/glm
)

# ------------------------------------------------------------------ engine lib
set(ENGINE_SOURCES
    src/noise.cpp
    src/erosion.cpp
    src/geology.cpp
    src/materials.cpp
    src/ecosystem.cpp
    src/atmosphere.cpp
    src/dag.cpp
    src/persistence.cpp
    src/mcp_bridge.cpp
    engine/heightmap.cpp
    engine/attribute.cpp
    engine/material_params.cpp
    engine/animation.cpp
    engine/asset_index.cpp
    engine/mesh_analysis.cpp
    engine/mesh_engines.cpp
    engine/mesh_cleanup.cpp
    engine/mesh_io.cpp
    engine/mesh_io_gltf.cpp
    engine/mesh_reduce.cpp
    engine/mesh_repair.cpp
    engine/mesh_topology.cpp
    engine/node_graph.cpp
    engine/node_graph_eval.cpp
    engine/field_glsl_emitters_convert.cpp
    engine/node.cpp
    engine/port_catalog.cpp
    engine/hydrology.cpp
    engine/distance.cpp
    engine/serialization.cpp
    engine/serialization_material.cpp
    engine/parallel.cpp
    engine/field_glsl.cpp
    engine/field_glsl_emitters.cpp
    engine/metanode.cpp
    engine/nodes/nodes_field_convert.cpp
    engine/nodes/nodes_lighting.cpp
    engine/nodes/nodes_camera.cpp
    engine/nodes/nodes_animation.cpp
    engine/nodes/nodes_scene.cpp
    engine/nodes/nodes_clouds.cpp
    engine/nodes/nodes_render.cpp
    engine/nodes/nodes_field_color.cpp
    engine/nodes/nodes_channels.cpp
    engine/nodes/nodes_primitives.cpp
    engine/nodes/nodes_fractals.cpp
    engine/nodes/nodes_fractals2.cpp
    engine/nodes/nodes_filters.cpp
    engine/nodes/nodes_erosion.cpp
    engine/nodes/nodes_erosion_slope.cpp
    engine/nodes/nodes_erosion_wind.cpp
    engine/nodes/nodes_erosion_layers.cpp
    engine/nodes/nodes_material_layers.cpp
    engine/nodes/nodes_material_types.cpp
    engine/nodes/nodes_material_grain.cpp
    engine/nodes/nodes_operators.cpp
    engine/nodes/nodes_masks.cpp
    engine/nodes/nodes_points.cpp
    engine/nodes/nodes_morphology.cpp
    engine/nodes/nodes_filters_local.cpp
    engine/nodes/nodes_segment.cpp
    engine/nodes/nodes_quilt.cpp
    engine/nodes/nodes_noise_extra.cpp
    engine/nodes/nodes_warp.cpp
    engine/nodes/nodes_texture.cpp
    engine/nodes/nodes_materials.cpp
    engine/nodes/nodes_pbr_library.cpp
    engine/nodes/nodes_modeling.cpp
    engine/nodes/nodes_surface.cpp
    engine/nodes/nodes_surface_forms.cpp
    engine/nodes/nodes_atmosphere.cpp
    engine/nodes/nodes_material_graph.cpp
    engine/nodes/nodes_hydro.cpp
    engine/nodes/nodes_logic.cpp
    external/miniz/miniz.c
    engine/nodes/nodes_field.cpp
    engine/nodes/nodes_field_math.cpp
    engine/nodes/nodes_field_shapes.cpp
    engine/nodes/nodes_path.cpp
    engine/nodes/nodes_path_ops.cpp
    engine/nodes/nodes_displace.cpp
    engine/nodes/nodes_analysis.cpp
    engine/nodes/nodes_field_material.cpp
    engine/nodes/nodes_terrain_fx.cpp
    engine/nodes/nodes_sculpt_layer.cpp
    engine/nodes/nodes_export.cpp
)

add_library(geekatplay_nodeterrain STATIC ${ENGINE_SOURCES})
target_include_directories(geekatplay_nodeterrain PUBLIC include engine)
target_link_libraries(geekatplay_nodeterrain PUBLIC third_party_headers)

# ---------------------------------------------------------- optional engines
# Manifold (Apache-2.0) gives the mesh module guaranteed-manifold solid
# reconstruction - the one repair stage worth more than we can sensibly write
# ourselves. It has no required dependencies of its own; scripts/get_deps
# clones it at a pinned tag, and the build works with or without it (see
# engine/mesh_engines.cpp and docs/LICENSING.md).
set(MANIFOLD_TAG "v3.5.2")
option(TERRAFORGE_WITH_MANIFOLD "Build with the Manifold solid engine" ON)
if(TERRAFORGE_WITH_MANIFOLD AND EXISTS "${CMAKE_SOURCE_DIR}/external/manifold/CMakeLists.txt")
    # Only what we use: no 2D cross sections (which would pull in Clipper2),
    # no TBB, no tests, no bindings, and static so nothing ships a DLL.
    set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
    set(MANIFOLD_DEBUG OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/manifold EXCLUDE_FROM_ALL)
    target_link_libraries(geekatplay_nodeterrain PUBLIC manifold)
    target_compile_definitions(geekatplay_nodeterrain PUBLIC
        GPX_HAVE_MANIFOLD GPX_MANIFOLD_VERSION="${MANIFOLD_TAG}")
    message(STATUS "Mesh engines: Manifold ${MANIFOLD_TAG} (solid reconstruction)")
else()
    message(STATUS "Mesh engines: none - run scripts/get_deps to add Manifold")
endif()

# QuadriFlow (BSD-3) rebuilds a surface as clean, curvature-aligned quads -
# the one thing our quadric collapse cannot do. Its own CMake requires Boost
# and Eigen; scripts/patch_quadriflow.py removes the Boost dependency by
# pointing its two library-backed max-flow solvers at the one it ships itself
# (see the script for what that costs), so all that is left is Eigen, which is
# header-only. We compile its sources directly rather than use its CMake.
option(TERRAFORGE_WITH_QUADRIFLOW "Build with the QuadriFlow retopology engine" ON)
set(QF_DIR "${CMAKE_SOURCE_DIR}/external/quadriflow")
if(TERRAFORGE_WITH_QUADRIFLOW AND EXISTS "${QF_DIR}/src/parametrizer.cpp"
   AND EXISTS "${CMAKE_SOURCE_DIR}/external/eigen/Eigen")
    add_library(quadriflow STATIC
        ${QF_DIR}/src/adjacent-matrix.cpp
        ${QF_DIR}/src/dedge.cpp
        ${QF_DIR}/src/hierarchy.cpp
        ${QF_DIR}/src/loader.cpp
        ${QF_DIR}/src/localsat.cpp
        ${QF_DIR}/src/merge-vertex.cpp
        ${QF_DIR}/src/optimizer.cpp
        ${QF_DIR}/src/parametrizer.cpp
        ${QF_DIR}/src/parametrizer-flip.cpp
        ${QF_DIR}/src/parametrizer-int.cpp
        ${QF_DIR}/src/parametrizer-mesh.cpp
        ${QF_DIR}/src/parametrizer-scale.cpp
        ${QF_DIR}/src/parametrizer-sing.cpp
        ${QF_DIR}/src/subdivide.cpp)
    target_include_directories(quadriflow PUBLIC
        ${QF_DIR}/src ${QF_DIR}/3rd "${CMAKE_SOURCE_DIR}/external/eigen")
    # EIGEN_MPL2_ONLY keeps Eigen's LGPL corner (Sparse Cholesky) out of the
    # build - the same thing QuadriFlow's own BUILD_FREE_LICENSE flag does.
    # EIGEN_MPL2_ONLY keeps Eigen's LGPL corner (Sparse Cholesky) out of the
    # build; M_PI needs asking for by name in a strict-standard build.
    target_compile_definitions(quadriflow PUBLIC EIGEN_MPL2_ONLY _USE_MATH_DEFINES)
    # Upstream is from 2019 and warns loudly on a 2026 compiler; its warnings
    # are not ours to fix, and we do not want them drowning our own.
    target_compile_options(quadriflow PRIVATE -w)
    set_target_properties(quadriflow PROPERTIES CXX_STANDARD 14)
    target_link_libraries(geekatplay_nodeterrain PUBLIC quadriflow)
    target_compile_definitions(geekatplay_nodeterrain PUBLIC GPX_HAVE_QUADRIFLOW)
    message(STATUS "Mesh engines: QuadriFlow (quad retopology)")
endif()
if(WIN32)
    # urlmon: URLDownloadToFileA, the PBR library downloader
    # (engine/nodes/nodes_pbr_library.cpp, guarded by #ifdef _WIN32)
    target_link_libraries(geekatplay_nodeterrain PUBLIC urlmon)
else()
    # PUBLIC so every executable that links the engine gets -pthread too.
    target_link_libraries(geekatplay_nodeterrain PUBLIC Threads::Threads)
endif()
if(NOT MSVC)
    target_compile_options(geekatplay_nodeterrain PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------ studio app
set(STUDIO_SOURCES
    studio/main.cpp
    studio/app.cpp
    studio/app_eval.cpp
    studio/app_services.cpp
    studio/layout.cpp
    studio/mesh_object.cpp
    studio/mesh_ops.cpp
    studio/panel_mesh.cpp
    studio/layout_record.cpp
    studio/layout_store.cpp
    studio/layout_workspace.cpp
    studio/panel_material_studio.cpp
    studio/panel_material_browser.cpp
    studio/material_ui.cpp
    studio/material_ops.cpp
    studio/material_channel_ops.cpp
    studio/config.cpp
    studio/shortcuts.cpp
    studio/perf.cpp
    studio/panel_statusbar.cpp
    studio/http_client.cpp
    studio/ai_prompts.cpp
    studio/ai_text_providers.cpp
    studio/ai_image_providers.cpp
    studio/ai_comfy.cpp
    studio/ai_model_providers.cpp
    studio/ai_jobs.cpp
    studio/ai_ops_generate.cpp
    studio/ai_describe.cpp
    studio/panel_ai_generate.cpp
    studio/panel_settings.cpp
    studio/panel_settings_services.cpp
    studio/renderer_matparams.cpp
    studio/panel_material_tabs.cpp
    studio/panel_material_tabs_mix.cpp
    studio/panel_material_hierarchy.cpp
    studio/material_channel_ui.cpp
    studio/asset_store.cpp
    studio/asset_ops.cpp
    studio/panel_assets.cpp
    studio/theme.cpp
    studio/theme_colors.cpp
    studio/icons.cpp
    studio/panel_graph.cpp
    studio/panel_graph_edit_ops.cpp
    studio/panel_graph_draw.cpp
    studio/panel_properties.cpp
    studio/panel_properties_object.cpp
    studio/panel_properties_object_surface.cpp
    studio/panel_properties_scene.cpp
    studio/panel_properties_node.cpp
    studio/panel_attr_tips.cpp
    studio/panel_library.cpp
    studio/panel_nodelist.cpp
    studio/panel_viewport.cpp
    studio/toolbar.cpp
    studio/toolbar_layout.cpp
    studio/dialog_new.cpp
    studio/autosave.cpp
    studio/autosave_ui.cpp
    studio/gizmo.cpp
    studio/gizmo_tools.cpp
    studio/gizmo_draw.cpp
    studio/toolbar_bars.cpp
    studio/terrain_styles.cpp
    studio/undo.cpp
    studio/sculpt.cpp
    studio/renderer.cpp
    studio/renderer_camera.cpp
    studio/renderer_scene.cpp
    studio/renderer_passes.cpp
    studio/renderer_post.cpp
    studio/renderer_matprev.cpp
    studio/camera_optics.cpp
    studio/renderer_programs.cpp
    studio/renderer_init.cpp
    studio/renderer_export.cpp
    studio/renderer_backdrop.cpp
    studio/renderer_aov.cpp
    studio/hdr_image.cpp
    studio/shaders_terrain.cpp
    studio/shaders_terrain_frag.cpp
    studio/terrain_cull.cpp
    studio/planet_place.cpp
    studio/panel_properties_object_planet.cpp
    studio/planet_pick.cpp
    studio/planet_shaders_common.cpp
    studio/planet_gpu_check.cpp
    studio/gpu_timer.cpp
    studio/blue_noise.cpp
    studio/shaders_scene.cpp
    studio/shaders_sky.cpp
    studio/planet_renderer.cpp
    studio/planet_shaders.cpp
    studio/field_gpu_check.cpp
    studio/field_gpu_check_convert.cpp
    studio/preview.cpp
    studio/project_io.cpp
    studio/panel_environment.cpp
    studio/panel_ai.cpp
    studio/panel_materials.cpp
    studio/panel_material_editor.cpp
    studio/material_stack_ops.cpp
    studio/panel_scene.cpp
    studio/panel_console.cpp
    studio/panel_timeline.cpp
    studio/console.cpp
    studio/crash_log.cpp
    studio/panel_float.cpp
    studio/panel_preview.cpp
    studio/panel_render.cpp
    studio/panel_render_editor.cpp
    studio/render_scene_export.cpp
    studio/scene.cpp
    studio/scene_primitives.cpp
    studio/scene_io.cpp
    studio/scene_io_object.cpp
    studio/render_settings.cpp
    studio/cloud_noise.cpp
    studio/scene_nodes.cpp
    studio/scene_nodes_objects.cpp
    studio/material_library.cpp
    studio/node_library.cpp
    studio/panel_camera.cpp
    studio/panel_camera_optics.cpp
    studio/ai_assist.cpp
    studio/ai_actions.cpp
    studio/ai_actions_camera.cpp
    studio/ai_actions_scene.cpp
    studio/ai_schema.cpp
    studio/ai_ops_graph.cpp
    studio/ai_ops_scene.cpp
    studio/ai_ops_view.cpp
    studio/studio_api.cpp
    studio/i18n.cpp
    studio/file_dialogs.cpp
    studio/prefs.cpp
    studio/paths.cpp
    studio/ollama.cpp
)
# The icon and version resource; only the Windows resource compiler can read
# it, and RC is only enabled there.
if(WIN32)
    list(APPEND STUDIO_SOURCES studio/resources/app.rc)
endif()

# WIN32 means "no console subsystem" and is a Windows-only flag; MACOSX_BUNDLE
# means "build a .app". They occupy the same slot, so the platform picks one
# and there stays exactly one add_executable call. Linux gets neither.
set(STUDIO_EXE_KIND "")
if(WIN32)
    set(STUDIO_EXE_KIND WIN32)
elseif(APPLE)
    set(STUDIO_EXE_KIND MACOSX_BUNDLE)
endif()
add_executable(geekatplay_studio ${STUDIO_EXE_KIND} ${STUDIO_SOURCES})

# studio/resources has to sit next to the running binary. Inside a .app the
# binary is in Contents/MacOS, and resources belong in Contents/Resources.
if(APPLE AND STUDIO_EXE_KIND STREQUAL "MACOSX_BUNDLE")
    add_custom_command(TARGET geekatplay_studio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/studio/resources
                $<TARGET_BUNDLE_CONTENT_DIR:geekatplay_studio>/Resources)
else()
    add_custom_command(TARGET geekatplay_studio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/studio/resources
                $<TARGET_FILE_DIR:geekatplay_studio>/resources)
endif()

target_include_directories(geekatplay_studio PRIVATE studio)
target_link_libraries(geekatplay_studio PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>"
    imgui imgui_node_editor glad glfw third_party_headers)
if(WIN32)
    # dbghelp: symbolised stacks in crash reports (studio/crash_log.cpp)
    target_link_libraries(geekatplay_studio PRIVATE opengl32 winhttp dbghelp crypt32)
else()
    target_link_libraries(geekatplay_studio PRIVATE OpenGL::GL)
endif()
if(APPLE)
    # GLFW's own CMake normally propagates these; naming them again costs
    # nothing and covers a GLFW built without that propagation.
    target_link_libraries(geekatplay_studio PRIVATE
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()
if(UNIX AND NOT APPLE)
    # dlopen, used by GLFW's runtime loading of X11/Wayland
    target_link_libraries(geekatplay_studio PRIVATE ${CMAKE_DL_LIBS})
endif()
if(WIN32 AND NOT MSVC)
    # MinGW only.
    #
    # -mconsole: console visible in debug runs, undoing the WIN32 flag's
    # subsystem choice. Not an option any other linker knows.
    #
    # -static-*: a MinGW build otherwise needs libstdc++-6.dll,
    # libgcc_s_seh-1.dll and libwinpthread-1.dll beside it, which exist only on
    # a machine that has the toolchain. Linking them in is what makes the
    # installed copy and the portable ZIP run on someone else's computer; it
    # costs about 2 MB and removes a whole class of "it works here" bug.
    set_target_properties(geekatplay_studio PROPERTIES
        LINK_FLAGS "-mconsole -static-libgcc -static-libstdc++ -static")
endif()
if(APPLE)
    # Info.plist fields. The .icns is produced by the packaging script and is
    # referenced by name only, so a plain `cmake --build` works without it —
    # the bundle just shows the generic icon.
    # The bundle directory is named after the target (geekatplay_studio.app);
    # add OUTPUT_NAME "TerraForge" here if it should be TerraForge.app.
    set_target_properties(geekatplay_studio PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "TerraForge"
        MACOSX_BUNDLE_GUI_IDENTIFIER "art.terraforge.studio"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "terraforge.icns"
        MACOSX_BUNDLE_COPYRIGHT "Geekatplay Studio")
endif()

# ------------------------------------------------------------------- cli/tests
add_executable(nodeterrain_cli src/main.cpp)
target_link_libraries(nodeterrain_cli PRIVATE geekatplay_nodeterrain)

add_executable(nodeterrain_tests tests/cpp/test_main.cpp)
target_link_libraries(nodeterrain_tests PRIVATE geekatplay_nodeterrain)

add_executable(engine_tests tests/cpp/test_engine.cpp)
target_link_libraries(engine_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Undo works on studio state, but none of the state it touches needs a GL
# context, so it tests without the renderer.
add_executable(undo_tests
    tests/cpp/test_undo.cpp
    tests/cpp/test_undo_render.cpp
    tests/cpp/test_material_stack_ops.cpp
    tests/cpp/test_material_channel_ops.cpp
    studio/material_stack_ops.cpp
    studio/material_channel_ops.cpp
    studio/undo.cpp
    studio/project_io.cpp
    studio/node_library.cpp
    studio/scene.cpp
    studio/scene_primitives.cpp
    studio/scene_io.cpp
    studio/scene_io_object.cpp
    studio/render_settings.cpp
    studio/autosave.cpp
    studio/console.cpp
    studio/crash_log.cpp)
target_include_directories(undo_tests PRIVATE studio)
target_link_libraries(undo_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)
if(WIN32)
    target_link_libraries(undo_tests PRIVATE dbghelp)
endif()

# Regression lock: guards every shipped capability against silent removal.
add_executable(regression_tests tests/cpp/test_regression.cpp)
target_link_libraries(regression_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Universal node contract, applied to every node in the registry.
add_executable(node_tests tests/cpp/test_nodes.cpp tests/cpp/test_nodes_convert.cpp)
target_link_libraries(node_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Renderer maths that needs no GL context: per-patch visibility, and the
# packing helpers that follow it.
add_executable(render_tests tests/cpp/test_render.cpp tests/cpp/test_render_hdr.cpp
                            tests/cpp/test_planet_place.cpp
                            studio/terrain_cull.cpp studio/blue_noise.cpp
                            studio/hdr_image.cpp studio/planet_place.cpp)
target_include_directories(render_tests PRIVATE studio)
target_link_libraries(render_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# The asset index: scan, tf-idf search, tags, trash, and its JSON file,
# against a folder the test builds itself.
add_executable(asset_tests tests/cpp/test_assets.cpp engine/asset_index.cpp)
target_include_directories(asset_tests PRIVATE engine)
target_link_libraries(asset_tests PRIVATE third_party_headers)

# The mesh module: diagnosis, repair, reduction and the file formats, all of
# it free of GL and of the node graph, so it tests in a console.
# The material editor's engine side: Vue tab groups, NaturalGrain, mapped
# picture controls, layer presence modes, the two-material distribution.
add_executable(material_editor_tests tests/cpp/test_material_editor.cpp)
target_link_libraries(material_editor_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

add_executable(mesh_tests tests/cpp/test_mesh.cpp)
target_link_libraries(mesh_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Saved window layouts: the record round-trip and the name sanitiser, with
# no ImGui and no GL in the link (layout_record.cpp is the half without a UI).
add_executable(layout_tests tests/cpp/test_layout.cpp studio/layout_record.cpp
                            studio/paths.cpp)
target_include_directories(layout_tests PRIVATE studio)
target_link_libraries(layout_tests PRIVATE third_party_headers)

# The configuration manager and the shortcut table: JSON round trips with
# protected secrets, provider defaults, chord parse/format, table sanity.
add_executable(config_tests tests/cpp/test_config.cpp studio/config.cpp studio/shortcuts.cpp)
target_include_directories(config_tests PRIVATE studio)
target_link_libraries(config_tests PRIVATE imgui third_party_headers)
if(WIN32)
  target_link_libraries(config_tests PRIVATE crypt32)
endif()

# The AI services without a network: request builders and response parsers
# on the providers' documented JSON, ComfyUI injection and inference, prompts.
add_executable(ai_services_tests tests/cpp/test_ai_services.cpp
    studio/http_client.cpp studio/config.cpp studio/ai_prompts.cpp
    studio/ai_text_providers.cpp studio/ai_image_providers.cpp studio/ai_comfy.cpp
    studio/ai_model_providers.cpp studio/ollama.cpp studio/prefs.cpp studio/paths.cpp)
target_include_directories(ai_services_tests PRIVATE studio)
target_link_libraries(ai_services_tests PRIVATE third_party_headers)
if(WIN32)
  target_link_libraries(ai_services_tests PRIVATE winhttp crypt32)
endif()

# The node reference generator: docs/NODES.md straight from the registry.
add_executable(node_docs_gen tools/gen_node_docs.cpp)
target_link_libraries(node_docs_gen PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

add_executable(node_bench tools/bench_nodes.cpp)
target_link_libraries(node_bench PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# headless chain preview: evaluate "Type:k=v|Type:k=v" and write a hillshade
add_executable(chain_preview tools/chain_preview.cpp)
target_link_libraries(chain_preview PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

enable_testing()
add_test(NAME PerfGuard COMMAND node_bench)
add_test(NAME RegressionLock COMMAND regression_tests)
add_test(NAME NodeContract COMMAND node_tests)
add_test(NAME AllTests COMMAND nodeterrain_tests)
add_test(NAME EngineTests COMMAND engine_tests)
add_test(NAME UndoTests COMMAND undo_tests)
add_test(NAME RenderTests COMMAND render_tests)
add_test(NAME LayoutTests COMMAND layout_tests)
add_test(NAME MeshTests COMMAND mesh_tests)
add_test(NAME MaterialEditorTests COMMAND material_editor_tests)
add_test(NAME ConfigTests COMMAND config_tests)
add_test(NAME AiServiceTests COMMAND ai_services_tests)
add_test(NAME AssetTests COMMAND asset_tests)












