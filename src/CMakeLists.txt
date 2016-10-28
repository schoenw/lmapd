# So CMake can tell whether or not it can process this file
cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

configure_file(
  "${PROJECT_SOURCE_DIR}/src/lmap.h.in"
  "${PROJECT_BINARY_DIR}/src/lmap.h"
)
configure_file(
  "${PROJECT_SOURCE_DIR}/src/lmapd.h.in"
  "${PROJECT_BINARY_DIR}/src/lmapd.h"
)

include_directories(${PROJECT_BINARY_DIR}/src
	${LIBEVENT_INCLUDE_DIRS}
	${LIBXML2_INCLUDE_DIRS}
	${LIBJSONC_INCLUDE_DIRS})
	
link_directories(${LIBEVENT_LIBRARY_DIRS}
	${LIBXML2_LIBRARY_DIRS}
	${LIBJSONC_LIBRARY_DIRS})

add_library(lmap data.c pidfile.c utils.c parser.c workspace.c runner.c signals.c)

add_executable(lmapd lmapd.c)
target_link_libraries(lmapd
	lmap
	${LIBEVENT_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBJSONC_LIBRARIES})

add_executable(lmapctl lmapctl.c)
target_link_libraries(lmapctl
	lmap
	${LIBEVENT_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBJSONC_LIBRARIES})

install(TARGETS lmapd DESTINATION bin)
install(TARGETS lmapctl DESTINATION bin)
