cmake_minimum_required(VERSION 3.15)

project(Sidereal
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal build by default so the plugin loads in both native Apple Silicon
# and Intel builds of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64
# for a faster single-arch dev build.
#
# This must sit immediately after project() and before any target is defined.
# CMake latches the architecture list when the first target is created, and
# setting it later is silently ignored -- you get an arm64-only binary that the
# build log calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and Resolume's plugin interface
# is still OpenGL, so every SDK call warns. Silencing it is not ignoring a
# problem -- there is no alternative API to move to here.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
endif()

option(SIDEREAL_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule under external/.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything both plugins are, as an OBJECT library so both bundles and the
# offline harness are built from exactly the same code.
#
# OBJECT and not STATIC on purpose. CFFGLPluginInfo registers itself from a
# file-scope constructor and nothing ever references that object by name, so
# in an archive the linker is entitled to drop the whole translation unit --
# and the result is a bundle that loads, exports plugMain, and reports that it
# contains no plugins.
# ---------------------------------------------------------------------------
add_library(sidereal_core OBJECT
    source/Diag.h
    source/Diag.cpp
    source/Physics.h
    source/Physics.cpp
    source/Scene.h
    source/Scene.cpp
    source/Controls.h
    source/Controls.cpp
    source/Events.h
    source/Events.cpp
    source/PassBuffer.h
    source/PassBuffer.cpp
    source/GLState.h
    source/Shaders.h
    source/Shaders.cpp
    source/Sidereal.h
    source/Sidereal.cpp
)

target_include_directories(sidereal_core PUBLIC source)
target_link_libraries(sidereal_core PUBLIC ffgl::sdk)
target_compile_definitions(sidereal_core PUBLIC SIDEREAL_VERSION="${PROJECT_VERSION}")
set_target_properties(sidereal_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# On Windows/Linux the SDK headers we include pull in <GL/glew.h>. The SDK links
# GLEW privately, so consumers need it too for the include path (and for the
# GLEW_STATIC define the static vcpkg triplet propagates). macOS uses the system
# OpenGL framework and needs none of this.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(sidereal_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The two plugins. FFGL resolves one plugMain per binary, so a source and an
# effect are two bundles -- not one bundle with two entries. SourcePlugin.cpp
# and EffectPlugin.cpp each carry their own CFFGLPluginInfo and are listed in
# their own MODULE target: in the shared library they would register both
# plugins into both bundles.
# ---------------------------------------------------------------------------
function(sidereal_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE sidereal_core)

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            OUTPUT_NAME "${bundle_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
        # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
        target_link_libraries(${target} PRIVATE
            "-framework OpenGL"
            "-framework Carbon"
            "-framework AppKit"
        )
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
    endif()
endfunction()

sidereal_add_plugin(SiderealSource source/SourcePlugin.cpp
    "Sidereal" "com.stoatworks.ffgl.sidereal")
sidereal_add_plugin(SiderealEffect source/EffectPlugin.cpp
    "Sidereal Over" "com.stoatworks.ffgl.sidereal.over")

# ---------------------------------------------------------------------------
# Offline harness. Whether every ionising photon is accounted for, whether
# the ionisation zones stand in the order the physics puts them, whether a
# diffraction core grows with wavelength and a lens conserves surface
# brightness: measurements, not matters of taste -- so this is where the
# plugin is verified rather than merely previewed.
# ---------------------------------------------------------------------------
if(SIDEREAL_BUILD_TOOLS AND APPLE)
    add_executable(sdtest tools/sdtest/main.cpp)
    # zlib ships with the OS, which is why the PNG writer is fifty lines here
    # rather than a vendored dependency.
    target_link_libraries(sdtest PRIVATE sidereal_core z
        "-framework OpenGL"
        "-framework AppKit"
    )
endif()

# ---------------------------------------------------------------------------
# `cmake --install` drops the bundle straight into the user's Resolume plugin
# folder. Override with -DCMAKE_INSTALL_PREFIX=...
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # honour an explicit prefix
elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS SiderealSource SiderealEffect
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)
