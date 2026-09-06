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
)
target_include_directories(pmxer_platform PUBLIC src)
target_link_libraries(pmxer_platform PRIVATE mmd::core)

add_library(pmxer_editor
  src/editor/CommandStack.cpp
  src/editor/Selection.cpp
  src/editor/SaveController.cpp
  src/editor/ValidationController.cpp
  src/editor/DiffController.cpp
  src/editor/ReferenceInspector.cpp
  src/editor/EditorDiagnostics.cpp
  src/editor/RecoveryController.cpp
  src/editor/EditorOperations.cpp
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
  src/render/TextureCache.cpp
)
target_include_directories(pmxer_render PUBLIC src)
target_link_libraries(pmxer_render PUBLIC pmxer_editor pmxer_preview)

find_program(PMXER_SHADERCROSS shadercross)
set(PMXER_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${PMXER_SHADER_OUTPUT_DIR}")
if(PMXER_SHADERCROSS)
  add_custom_command(
    OUTPUT "${PMXER_SHADER_OUTPUT_DIR}/model.spv"
    COMMAND "${PMXER_SHADERCROSS}" -s assets/shaders/model.hlsl -d "${PMXER_SHADER_OUTPUT_DIR}/model.spv"
    DEPENDS assets/shaders/model.hlsl
    VERBATIM
  )
  add_custom_target(pmxer_shaders DEPENDS "${PMXER_SHADER_OUTPUT_DIR}/model.spv")
else()
  add_custom_target(pmxer_shaders SOURCES assets/shaders/model.hlsl)
endif()
add_dependencies(pmxer_render pmxer_shaders)

if(PMXER_BUILD_GUI)
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
  else()
    message(FATAL_ERROR "PMXER_BUILD_GUI requires PMXER_FETCH_GUI_DEPENDENCIES=ON in this source tree")
  endif()

  add_library(pmxer_ui
    src/ui/MainWindow.cpp
    src/ui/EditorPanels.cpp
    src/ui/ViewportPanel.cpp
    src/platform/FileDialog.cpp
  )
  target_include_directories(pmxer_ui PUBLIC src)
  target_link_libraries(pmxer_ui PUBLIC pmxer_render pmxer_imgui SDL3::SDL3)
  target_compile_definitions(pmxer_ui PRIVATE PMXER_HAS_GUI=1)
else()
  add_library(pmxer_ui src/ui/MainWindow.cpp)
  target_include_directories(pmxer_ui PUBLIC src)
  target_link_libraries(pmxer_ui PUBLIC pmxer_render)
  target_compile_definitions(pmxer_ui PRIVATE PMXER_HAS_GUI=0)
endif()

add_executable(pmxer src/app/main.cpp)
target_link_libraries(pmxer PRIVATE pmxer_ui)

add_executable(pmxer-cli src/cli/main.cpp)
target_link_libraries(pmxer-cli PRIVATE pmxer_editor pmxer_preview)

if(PMXER_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(target IN ITEMS pmxer_platform pmxer_editor pmxer_preview pmxer_render pmxer_ui pmxer pmxer-cli)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endforeach()
endif()

if(BUILD_TESTING)
  add_executable(pmxer_editor_tests tests/unit/EditorTests.cpp)
  target_link_libraries(pmxer_editor_tests PRIVATE pmxer_editor pmxer_preview pmxer_render)
  add_test(NAME pmxer_editor COMMAND pmxer_editor_tests)

  add_executable(pmxer_cli_tests tests/integration/CliTests.cpp)
  target_link_libraries(pmxer_cli_tests PRIVATE pmxer_editor)
  add_test(NAME pmxer_cli COMMAND pmxer_cli_tests)
endif()

install(TARGETS pmxer pmxer-cli RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/pmxer/assets)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
