# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "triengine_interop_flutter")
project(${PROJECT_NAME} LANGUAGES CXX)

# Explicitly opt in to modern CMake behaviors to avoid warnings with recent
# versions of CMake.
cmake_policy(VERSION 3.14...3.25)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "triengine_interop_flutter_plugin")

# Any new source files that you add to the plugin should be added here.
list(APPEND PLUGIN_SOURCES
    "triengine_interop_flutter.cc"
    "triengine_interop_flutter.hh"
    "triengine_surface_manager.cc"
    "triengine_surface_manager.hh"
    "ipc_proto.hh"
    "ipc_service.cc"
    "ipc_service.hh"
    "utils/bit_cast.hh"
    "utils/spin_lock.hh"
    "utils/string_codecvt.hh"
    "utils/debug_utils.hh"
    "utils/win32_utils.hh"
)

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
    "include/triengine_interop_flutter/triengine_interop_flutter_plugin_c_api.h"
    "triengine_interop_flutter_plugin_c_api.cc"
    ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(${PLUGIN_NAME} PRIVATE 
    FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

###############################################################################
# Third-party dependencies
include(FetchContent)

# ===== Fmt Library =====
# Ref: https://fmt.dev/11.1/get-started/
message(STATUS "Fetching fmt library...")
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG        11.2.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_UNICODE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt)

# ===== Boost Libraries =====
# See: https://stackoverflow.com/q/72913306
message(STATUS "Fetching boost...")
set(BOOST_INCLUDE_LIBRARIES interprocess date_time program_options)
set(BOOST_ENABLE_CMAKE ON)
set(FETCHCONTENT_QUIET FALSE) # Needed to print downloading progress
FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.7z # downloading a zip release speeds up the download
    USES_TERMINAL_DOWNLOAD TRUE 
    GIT_PROGRESS TRUE   
    DOWNLOAD_NO_EXTRACT FALSE
)
FetchContent_MakeAvailable(Boost)

###############################################################################

target_link_libraries(${PLUGIN_NAME} PRIVATE 
    flutter
    flutter_wrapper_plugin
    # DirectX11 dependencies.
    d3d11
    dxgi
    d3dcompiler
    # Third-party dependencies.
    fmt::fmt
    Boost::interprocess
    Boost::date_time
    Boost::program_options
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(triengine_interop_flutter_bundled_libraries
    ""
    PARENT_SCOPE
)