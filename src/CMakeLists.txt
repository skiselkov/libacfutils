set(SOURCE_LIST
    acf_file.c
    airportdb.c
    avl.c
    base64.c
    cmd.c
    compress_7z.c
    compress_zip.c
    compress_zlib.c
    conf.c
    core.c
    crc64.c
    dr.c
    dr_cmd_reg.c
    dsf.c
    except.c
    GeomagnetismLibrary.c
    geom.c
    helpers.c
    hexcode.c
    htbl.c
    icao2cc.c
    jsmn_path.h
    intl.c
    list.c
    log.c
    math.c
    osrand.c
    perf.c
    taskq.c
    time.c
    thread.c
    tumbler.c
    widget.c
    wmm.c
    worker.c
    junzip.c
    junzip.h)

set(HEADER_LIST
    acfutils/acf_file.h
    acfutils/airportdb.h
    acfutils/assert.h
    acfutils/avl.h
    acfutils/avl_impl.h
    acfutils/base64.h
    acfutils/cmd.h
    acfutils/compress.h
    acfutils/conf.h
    acfutils/core.h
    acfutils/crc64.h
    acfutils/delay_line.h
    acfutils/dr_cmd_reg.h
    acfutils/dr.h
    acfutils/dsf.h
    acfutils/except.h
    acfutils/geom.h
    acfutils/helpers.h
    acfutils/lacf_getline_impl.h
    acfutils/parser_funcs.h
    acfutils/hexcode.h
    acfutils/hp_filter.h
    acfutils/htbl.h
    acfutils/icao2cc.h
    acfutils/intl.h
    acfutils/joystick.h
    acfutils/libconfig.h
    acfutils/list.h
    acfutils/list_impl.h
    acfutils/log.h
    acfutils/math_core.h
    acfutils/math.h
    acfutils/osrand.h
    acfutils/perf.h
    acfutils/pid_ctl.h
    acfutils/pid_ctl_parsing.h
    acfutils/safe_alloc.h
    acfutils/sysmacros.h
    acfutils/taskq.h
    acfutils/thread.h
    acfutils/time.h
    acfutils/tls.h
    acfutils/tumbler.h
    acfutils/types.h
    acfutils/widget.h
    acfutils/wmm.h
    acfutils/worker.h
    acfutils/xpfail.h)

set(ACFUTILS_DEFINES
    LIBACFUTILS_VERSION=\"${ACFUTILS_VERSION}\"
    _GNU_SOURCE=1
    _FILE_OFFSET_BITS=64
    ACFUTILS_BUILD=1
    GL_GLEXT_PROTOTYPES=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM303=1
    HAVE_ZLIB=1
    GLEW_STATIC=1)

if(NOT MINIMAL)
  list(
    APPEND
    SOURCE_LIST
    apps.c
    chartdb.c
    chart_prov_autorouter.c
    chart_prov_common.c
    chart_prov_faa.c
    chart_prov_navigraph.c
    font_utils.c
    glctx.c
    glew.c
    glutils.c
    lacf_gl_pic.c
    minimp3.c
    cairo_utils.c
    mt_cairo_render.c
    odb.c
    paste.c
    png.c
    riff.c
    shader.c
    wav.c)

  list(
    APPEND
    HEADER_LIST
    acfutils/apps.h
    acfutils/chartdb.h
    acfutils/cursor.h
    acfutils/font_utils.h
    acfutils/glctx.h
    acfutils/glew.h
    acfutils/glutils.h
    acfutils/lacf_gl_pic.h
    acfutils/cairo_utils.h
    acfutils/mt_cairo_render.h
    acfutils/odb.h
    acfutils/paste.h
    acfutils/png.h
    acfutils/riff.h
    acfutils/shader.h
    acfutils/wav.h)
  list(APPEND ACFUTILS_DEFINES AL_ALEXT_PROTOTYPES=1)
  if(WIN32)
    list(APPEND SOURCE_LIST platform/cursor-win.c)
  elseif(APPLE)
    list(APPEND SOURCE_LIST platform/cursor-mac.m)
  elseif(LINUX)
    list(APPEND SOURCE_LIST platform/cursor-lin.c)
  endif()
endif()

if(BUILD_SHARED_LIBS)
  list(APPEND ACFUTILS_DEFINES ACFUTILS_DLL=1)
endif()

list(TRANSFORM SOURCE_LIST PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM HEADER_LIST PREPEND "${PROJECT_SOURCE_DIR}/include/")

add_library(acfutils ${SOURCE_LIST} ${HEADER_LIST})

find_package(PkgConfig REQUIRED)
pkg_check_modules(cairo REQUIRED IMPORTED_TARGET cairo)
find_package(cglm CONFIG REQUIRED)
find_package(unofficial-libclipboard CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(Freetype REQUIRED)
find_package(GeographicLib CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_path(JSMN_INCLUDE_DIRS "jsmn.h")
find_package(LibXml2 REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(OpusFile CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(unofficial-xplane-sdk CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(7zip CONFIG REQUIRED)

target_link_libraries(
  acfutils
  PUBLIC PkgConfig::cairo
         cglm::cglm
         unofficial::libclipboard::clipboard
         CURL::libcurl
         Freetype::Freetype
         ${GeographicLib_LIBRARIES}
         GLEW::GLEW
         glfw
         LibXml2::LibXml2
         Opus::opus
         OpusFile::opusfile
         PNG::PNG
         unofficial::xplane-sdk::xplm
         unofficial::xplane-sdk::xpwidgets
         ZLIB::ZLIB
         7zip::7zip)

target_include_directories(
  acfutils PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                  $<INSTALL_INTERFACE:include>)
target_include_directories(acfutils PRIVATE ${JSMN_INCLUDE_DIRS})
target_compile_features(acfutils PUBLIC c_std_17)

if(WIN32)
  if(NOT MINGW)
    message(FATAL_ERROR "libacfutils requires a build with MinGW on Windows.")
  endif()
  list(APPEND ACFUTILS_DEFINES MSDOS _WIN32_WINNT=0x0600)
elseif(APPLE)
  list(APPEND ACFUTILS_DEFINES GL_SILENCE_DEPRECATION)
elseif(LINUX)

else()
  message(
    FATAL_ERROR "libacfutils can only be built for Windows, Mac, or Linux.")
endif()

target_compile_definitions(acfutils PUBLIC ${ACFUTILS_DEFINES}
                                           $<$<CONFIG:Debug>:DEBUG>)
# Make IDEs put headers in a nice place
source_group(
  TREE "${PROJECT_SOURCE_DIR}/include"
  PREFIX "Header Files"
  FILES ${HEADER_LIST})
