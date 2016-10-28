# So CMake can tell whether or not it can process this file
cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

pkg_check_modules(CHECK REQUIRED check)

include_directories(${PROJECT_BINARY_DIR}/src
        ${CMAKE_SOURCE_DIR}/src
	${LIBEVENT_INCLUDE_DIRS}
	${LIBXML2_INCLUDE_DIRS}
	${CHECK_INCLUDE_DIRS})
	
link_directories(${LIBEVENT_LIBRARY_DIRS}
	${LIBXML2_LIBRARY_DIRS}
	${CHECK_LIBRARY_DIRS}
	${LIBJSONC_LIBRARY_DIRS})

add_executable(check-lmap check-lmap.c)
add_executable(check-lmapd check-lmapd.c)

target_link_libraries(check-lmap
	lmap
	${LIBEVENT_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBJSONC_LIBRARIES}
 	${CHECK_LIBRARIES}
	${CMAKE_THREAD_LIBS_INIT})

target_link_libraries(check-lmapd
	lmap
	${LIBEVENT_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBJSONC_LIBRARIES}
 	${CHECK_LIBRARIES}
	${CMAKE_THREAD_LIBS_INIT})
