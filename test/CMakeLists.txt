add_executable(airac airac.c)
add_executable(dsfdump dsfdump.c)
add_executable(rwmutex rwmutex.c)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(xcb REQUIRED IMPORTED_TARGET xcb)

foreach(tgt airac dsfdump rwmutex)
  target_compile_definitions(${tgt} PRIVATE _LACF_WITHOUT_XPLM=1 DEBUG
                                            _GNU_SOURCE IBM=0 LIN=1 APL=0)
  target_link_libraries(${tgt} PRIVATE Threads::Threads PkgConfig::xcb acfutils)
endforeach()
