cmake_minimum_required(VERSION 3.25..3.27)

# Force CMake to build universal binaries for Mac.
set(CMAKE_OSX_ARCHITECTURES
    "x86_64;arm64"
    CACHE INTERNAL "" FORCE)

project(
  libacfutils
  DESCRIPTION "General purpose utility library for X-Plane plugins."
  LANGUAGES C)

# Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
  message(
    FATAL_ERROR
      "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles."
  )
endif()

option(MINIMAL "Build libacfutils in minimal mode.")
option(BUILD_SHARED_LIBS "Build shared (dynamic) libraries.")

# Do stuff if building this standalone. Would suggest one is specifically
# developing libacfutils.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

if(APPLE)
  enable_language(OBJC)
endif()

find_package(Git)
if(GIT_EXECUTABLE)
  # Generate a git-describe version string from Git repository tags
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --match "v*"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
    RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT GIT_DESCRIBE_ERROR_CODE)
    set(ACFUTILS_VERSION ${GIT_DESCRIBE_VERSION})
  endif()
endif()

if(NOT DEFINED ACFUTILS_VERSION)
  set(ACFUTILS_VERSION v0.0.0-unknown)
  message(
    WARNING
      "Failed to determine ACFUTILS_VERSION from Git tags. Using default version \"${ACFUTILS_VERSION}\"."
  )
endif()

add_subdirectory(src)

include(GNUInstallDirs)

install(
  TARGETS acfutils
  EXPORT acfutilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  EXPORT acfutilsTargets
  FILE libacfutils-targets.cmake
  NAMESPACE libacfutils::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libacfutils)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libacfutils-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/libacfutils-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libacfutils)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/libacfutils-config-version.cmake"
  VERSION "${ACFUTILS_VERSION}"
  COMPATIBILITY AnyNewerVersion
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libacfutils-config.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/libacfutils-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libacfutils)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR MODERN_CMAKE_BUILD_TESTING)
  # add_subdirectory(test) Unlinkable -- undefined symbols.
endif()
