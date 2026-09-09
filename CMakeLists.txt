cmake_minimum_required(VERSION 3.25)

project(pmxer VERSION 0.1.0 LANGUAGES CXX)

# The bundled physics dependency still declares a pre-3.5 minimum. CMake 4
# requires an explicit compatibility floor when that subproject is added.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Compatibility floor for bundled dependencies")

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

include(CTest)
include(FetchContent)

find_package(argparse CONFIG QUIET)
if(NOT TARGET argparse::argparse)
  set(ARGPARSE_INSTALL OFF CACHE BOOL "" FORCE)
  set(ARGPARSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ARGPARSE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    argparse
    URL https://github.com/p-ranav/argparse/archive/refs/tags/v3.2.tar.gz
    URL_HASH SHA256=9dcb3d8ce0a41b2a48ac8baa54b51a9f1b6a2c52dd374e28cc713bab0568ec98
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(argparse)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PMXER_BUILD_GUI "Build the SDL3 desktop application" ON)
option(PMXER_FETCH_GUI_DEPENDENCIES "Fetch GUI source dependencies when missing" OFF)
option(PMXER_ENABLE_PHYSICS "Build the optional physics preview" ON)
option(PMXER_FETCH_LIBMMD_DEPENDENCIES "Allow the PMX library to fetch physics dependencies" OFF)
option(PMXER_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(PMXER_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

set(LIBMMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBMMD_INSTALL OFF CACHE BOOL "" FORCE)
set(LIBMMD_ENABLE_PHYSICS ${PMXER_ENABLE_PHYSICS} CACHE BOOL "" FORCE)
set(LIBMMD_ENABLE_BULLET ${PMXER_ENABLE_PHYSICS} CACHE BOOL "" FORCE)
set(LIBMMD_FETCH_DEPENDENCIES ${PMXER_FETCH_LIBMMD_DEPENDENCIES} CACHE BOOL "" FORCE)
add_subdirectory(external/libmmd)

if(PMXER_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(pmxer_platform
  src/platform/AtomicFile.cpp
  src/platform/Paths.cpp
  src/platform/ResourceLocator.cpp
)
target_include_directories(pmxer_platform PUBLIC src)
target_link_libraries(pmxer_platform PRIVATE mmd::core)

add_library(pmxer_editor
  src/editor/DeformController.cpp
  src/editor/CommandStack.cpp
  src/editor/Selection.cpp
  src/editor/SaveController.cpp
  src/editor/ValidationController.cpp
  src/editor/DiffController.cpp
  src/editor/ReferenceInspector.cpp
  src/editor/UiStatus.cpp
  src/editor/EditorDiagnostics.cpp
  src/editor/RecoveryController.cpp
  src/editor/EditorOperations.cpp
  src/editor/EditorSelectionController.cpp
  src/editor/EditorSelectionQueries.cpp
  src/editor/WorkspacePolicy.cpp
  src/editor/ViewportCapabilities.cpp
  src/editor/PreviewPoseQueries.cpp
  src/editor/TransformMath.cpp
  src/editor/morph/MorphCapture.cpp
  src/editor/morph/MorphMask.cpp
  src/editor/morph/MorphMixer.cpp
  src/editor/morph/MorphOps.cpp
  src/ui/WorkspaceLayout.cpp
  src/editor/tools/SdefTool.cpp
  src/editor/tools/StandardBoneTool.cpp
  src/editor/tools/WeightTool.cpp
  src/editor/tools/MaterialTool.cpp
  src/editor/tools/MorphTool.cpp
  src/editor/tools/PhysicsTool.cpp
  src/editor/tools/DisplayFrameTool.cpp
  src/editor/tools/BoneTool.cpp
  src/editor/tools/VertexTool.cpp
  src/editor/tools/ModelMergeTool.cpp
  src/editor/tools/TextureTool.cpp
)
target_include_directories(pmxer_editor PUBLIC src)
target_link_libraries(pmxer_editor PUBLIC mmd::core pmxer_platform)

add_library(pmxer_command_line
  src/app/CommandLine.cpp
)
target_include_directories(pmxer_command_line PUBLIC src)
target_link_libraries(pmxer_command_line PUBLIC mmd::core PRIVATE argparse::argparse)

add_library(pmxer_commands
  src/commands/CommandOutput.cpp
)
target_include_directories(pmxer_commands PUBLIC src)
target_link_libraries(pmxer_commands PUBLIC pmxer_editor mmd::core)

add_library(pmxer_preview
  src/preview/PreviewController.cpp
)
target_include_directories(pmxer_preview PUBLIC src)
target_link_libraries(pmxer_preview PUBLIC mmd::animation)
if(TARGET mmd::physics)
  target_link_libraries(pmxer_preview PUBLIC mmd::physics)
endif()

add_library(pmxer_render
  src/render/ModelRenderer.cpp
  src/render/Picking.cpp
  src/render/PreviewTextureFallbacks.cpp
  src/render/TextureCache.cpp
)
target_include_directories(pmxer_render PUBLIC src)
target_link_libraries(pmxer_render PUBLIC pmxer_editor pmxer_preview)

find_program(PMXER_OFFLINE_SHADER_COMPILER shadercross)
find_program(PMXER_GLSLANG_VALIDATOR glslangValidator)
set(PMXER_PRECOMPILED_SHADER_DIR "" CACHE PATH "Directory containing verified precompiled shaders")
set(PMXER_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${PMXER_SHADER_OUTPUT_DIR}")
set(PMXER_SHADER_OUTPUTS
  "${PMXER_SHADER_OUTPUT_DIR}/model.vert.spv"
  "${PMXER_SHADER_OUTPUT_DIR}/model.frag.spv"
)
set(PMXER_SHADER_OUTPUTS_CROSS
  "${PMXER_SHADER_OUTPUT_DIR}/model.vert.dxil"
  "${PMXER_SHADER_OUTPUT_DIR}/model.frag.dxil"
  "${PMXER_SHADER_OUTPUT_DIR}/model.vert.msl"
  "${PMXER_SHADER_OUTPUT_DIR}/model.frag.msl"
)
set(PMXER_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/model.hlsl")
file(READ "${PMXER_SHADER_SOURCE}" PMXER_SHADER_TEXT)
string(REPLACE "\r\n" "\n" PMXER_SHADER_TEXT "${PMXER_SHADER_TEXT}")
string(REPLACE "\r" "\n" PMXER_SHADER_TEXT "${PMXER_SHADER_TEXT}")
string(SHA256 PMXER_SHADER_SOURCE_HASH "${PMXER_SHADER_TEXT}")
if(PMXER_PRECOMPILED_SHADER_DIR)
  list(APPEND PMXER_SHADER_OUTPUTS ${PMXER_SHADER_OUTPUTS_CROSS})
  set(PMXER_PRECOMPILED_SHADER_HASH
      "${PMXER_PRECOMPILED_SHADER_DIR}/model.hlsl.sha256")
  if(NOT EXISTS "${PMXER_PRECOMPILED_SHADER_HASH}")
    message(FATAL_ERROR
      "Missing precompiled shader source hash: ${PMXER_PRECOMPILED_SHADER_HASH}")
  endif()
  file(READ "${PMXER_PRECOMPILED_SHADER_HASH}"
       PMXER_PRECOMPILED_SHADER_SOURCE_HASH)
  string(STRIP "${PMXER_PRECOMPILED_SHADER_SOURCE_HASH}"
         PMXER_PRECOMPILED_SHADER_SOURCE_HASH)
  if(NOT PMXER_SHADER_SOURCE_HASH STREQUAL
         PMXER_PRECOMPILED_SHADER_SOURCE_HASH)
    message(FATAL_ERROR
      "Precompiled shaders do not match assets/shaders/model.hlsl")
  endif()
  foreach(shader IN ITEMS model.vert.spv model.frag.spv model.vert.dxil model.frag.dxil model.vert.msl model.frag.msl)
    if(NOT EXISTS "${PMXER_PRECOMPILED_SHADER_DIR}/${shader}")
      message(FATAL_ERROR "Missing precompiled shader: ${PMXER_PRECOMPILED_SHADER_DIR}/${shader}")
    endif()
    configure_file("${PMXER_PRECOMPILED_SHADER_DIR}/${shader}" "${PMXER_SHADER_OUTPUT_DIR}/${shader}" COPYONLY)
  endforeach()
elseif(PMXER_OFFLINE_SHADER_COMPILER)
  list(APPEND PMXER_SHADER_OUTPUTS ${PMXER_SHADER_OUTPUTS_CROSS})
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.vert.spv"
    COMMAND "${PMXER_OFFLINE_SHADER_COMPILER}" "${PMXER_SHADER_SOURCE}"
            -s HLSL -d SPIRV -t vertex -e mainVS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.vert.spv"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.frag.spv"
    COMMAND "${PMXER_OFFLINE_SHADER_COMPILER}" "${PMXER_SHADER_SOURCE}"
            -s HLSL -d SPIRV -t fragment -e mainPS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.frag.spv"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.vert.dxil"
    COMMAND "${PMXER_OFFLINE_SHADER_COMPILER}" "${PMXER_SHADER_SOURCE}"
            -s HLSL -d DXIL -t vertex -e mainVS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.vert.dxil"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.frag.dxil"
    COMMAND "${PMXER_OFFLINE_SHADER_COMPILER}" "${PMXER_SHADER_SOURCE}"
            -s HLSL -d DXIL -t fragment -e mainPS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.frag.dxil"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.vert.msl"
    COMMAND "${PMXER_OFFLINE_SHADER_COMPILER}" "${PMXER_SHADER_SOURCE}"
            -s HLSL -d MSL -t vertex -e mainVS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.vert.msl"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.frag.msl"
    COMMAND "${PMXER_OFFLINE_SHADER_COMPILER}" "${PMXER_SHADER_SOURCE}"
            -s HLSL -d MSL -t fragment -e mainPS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.frag.msl"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
elseif(PMXER_GLSLANG_VALIDATOR)
  if(WIN32 OR APPLE)
    message(FATAL_ERROR "This platform requires precompiled shaders or shadercross")
  endif()
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.vert.spv"
    COMMAND "${PMXER_GLSLANG_VALIDATOR}" -D -V -S vert -e mainVS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.vert.spv" "${PMXER_SHADER_SOURCE}"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.frag.spv"
    COMMAND "${PMXER_GLSLANG_VALIDATOR}" -D -V -S frag -e mainPS
            -o "${PMXER_SHADER_OUTPUT_DIR}/model.frag.spv" "${PMXER_SHADER_SOURCE}"
    DEPENDS "${PMXER_SHADER_SOURCE}"
    VERBATIM
  )
else()
  set(PMXER_SHADER_OUTPUTS)
  if(PMXER_BUILD_GUI)
    message(FATAL_ERROR "PMXER_BUILD_GUI requires an offline shader compiler or glslangValidator")
  endif()
  message(STATUS "No offline shader compiler found; GUI shader generation is disabled")
endif()
add_custom_target(pmxer_shaders DEPENDS ${PMXER_SHADER_OUTPUTS})
add_dependencies(pmxer_render pmxer_shaders)

if(PMXER_BUILD_GUI)
  FetchContent_Declare(
    bcdec
    URL https://github.com/iOrange/bcdec/archive/80859ed3b7afb1c527a2a99d70c61457bea72d0c.tar.gz
    URL_HASH SHA256=c54b589fd1db768994c8bb4bbe9dbd658fb9401ce56cb7a1c544cb1ca2c0a033
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(bcdec)
  if(NOT bcdec_POPULATED)
    FetchContent_Populate(bcdec)
  endif()

  FetchContent_Declare(
    stb_image
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(stb_image)
  if(NOT stb_image_POPULATED)
    FetchContent_Populate(stb_image)
  endif()

  find_package(SDL3 CONFIG QUIET)
  if(NOT TARGET SDL3::SDL3)
    if(NOT PMXER_FETCH_GUI_DEPENDENCIES)
      message(FATAL_ERROR "PMXER_BUILD_GUI requires SDL3 development files")
    endif()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      SDL3
      URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.8.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)
  endif()

  if(PMXER_FETCH_GUI_DEPENDENCIES)
    FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG 7e1b65d26d52e9dd199d889c148c72184de647b4
    )
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
      FetchContent_Populate(imgui)
    endif()
    add_library(pmxer_imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
    )
    target_include_directories(pmxer_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(pmxer_imgui PUBLIC SDL3::SDL3)

    FetchContent_Declare(
      viewport_gizmo
      GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
      GIT_TAG b796ac3b861afc6e91ca74e4611effd9c9527367
      GIT_SHALLOW FALSE
    )
    FetchContent_GetProperties(viewport_gizmo)
    if(NOT viewport_gizmo_POPULATED)
      FetchContent_Populate(viewport_gizmo)
    endif()
    add_library(pmxer_gizmo STATIC ${viewport_gizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
    target_include_directories(pmxer_gizmo PUBLIC ${viewport_gizmo_SOURCE_DIR}/src)
    target_link_libraries(pmxer_gizmo PUBLIC pmxer_imgui)
  else()
    message(FATAL_ERROR "PMXER_BUILD_GUI requires PMXER_FETCH_GUI_DEPENDENCIES=ON in this source tree")
  endif()

  add_library(pmxer_ui
    src/ui/TransformViewPanel.cpp
    src/ui/MainWindow.cpp
    src/ui/EditorPanels.cpp
    src/ui/OutlinerPanel.cpp
    src/ui/InspectorPanel.cpp
    src/ui/MorphOffsetBrowser.cpp
    src/ui/ViewportPanel.cpp
    src/ui/ViewportGizmo.cpp
    src/ui/ViewportPicking.cpp
    src/ui/UiSemantics.cpp
    src/render/GpuModelRenderer.cpp
    src/render/ImageDecoder.cpp
    src/platform/FileDialog.cpp
  )
  target_include_directories(pmxer_ui PUBLIC src)
  target_include_directories(pmxer_ui SYSTEM PRIVATE "${stb_image_SOURCE_DIR}" "${bcdec_SOURCE_DIR}")
  target_link_libraries(pmxer_ui PUBLIC pmxer_render pmxer_imgui pmxer_gizmo SDL3::SDL3)
  target_compile_definitions(pmxer_ui PRIVATE PMXER_HAS_GUI=1 PMXER_SHADER_DIRECTORY="${PMXER_SHADER_OUTPUT_DIR}")
endif()

add_executable(pmxer
  src/app/main.cpp
  src/app/Dispatch.cpp
)
target_link_libraries(pmxer PRIVATE pmxer_commands pmxer_command_line)
if(PMXER_BUILD_GUI)
  target_link_libraries(pmxer PRIVATE pmxer_ui)
  target_compile_definitions(pmxer PRIVATE PMXER_HAS_GUI=1)
else()
  target_compile_definitions(pmxer PRIVATE PMXER_HAS_GUI=0)
endif()

if(PMXER_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(PMXER_WARNING_TARGETS pmxer_platform pmxer_editor pmxer_command_line pmxer_commands pmxer_preview pmxer_render pmxer)
  if(TARGET pmxer_ui)
    list(APPEND PMXER_WARNING_TARGETS pmxer_ui)
  endif()
  foreach(target IN LISTS PMXER_WARNING_TARGETS)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endforeach()
endif()

if(BUILD_TESTING)
  add_executable(pmxer_editor_tests tests/unit/EditorTests.cpp)
  target_link_libraries(pmxer_editor_tests PRIVATE pmxer_editor pmxer_preview pmxer_render)
  add_test(NAME pmxer_editor COMMAND pmxer_editor_tests)

  add_executable(pmxer_command_tests tests/integration/CliTests.cpp)
  target_link_libraries(pmxer_command_tests PRIVATE pmxer_editor)
  add_dependencies(pmxer_command_tests pmxer)
  target_compile_definitions(pmxer_command_tests PRIVATE PMXER_BINARY_PATH="$<TARGET_FILE:pmxer>")
  add_test(NAME pmxer_command COMMAND pmxer_command_tests)

  add_executable(pmxer_command_line_tests tests/unit/CommandLineTests.cpp)
  target_include_directories(pmxer_command_line_tests PRIVATE src)
  target_link_libraries(pmxer_command_line_tests PRIVATE pmxer_command_line)
  add_test(NAME pmxer_command_line COMMAND pmxer_command_line_tests)
  if(PMXER_BUILD_GUI)
    add_executable(pmxer_image_decoder_tests
      tests/unit/ImageDecoderTests.cpp
      src/render/ImageDecoder.cpp
    )
    target_include_directories(pmxer_image_decoder_tests PRIVATE src)
    target_include_directories(pmxer_image_decoder_tests SYSTEM PRIVATE "${stb_image_SOURCE_DIR}" "${bcdec_SOURCE_DIR}")
    add_test(NAME pmxer_image_decoder COMMAND pmxer_image_decoder_tests)
  endif()
endif()

install(TARGETS pmxer RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/pmxer/assets)
if(PMXER_SHADER_OUTPUTS)
  install(FILES ${PMXER_SHADER_OUTPUTS} DESTINATION share/pmxer/shaders)
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
