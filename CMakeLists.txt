cmake_minimum_required(VERSION 3.10)
project(SpringMapConvNG)

# Default to an optimized build: an -O0 build of the compressor is ~4x slower.
# Only applies to single-config generators (Make/Ninja); multi-config generators
# (e.g. Visual Studio) pick the build type at build time and ignore this.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	message(STATUS "No build type specified; defaulting to Release")
endif ()

set (CMAKE_CXX_STANDARD 11)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(DevIL REQUIRED)

set(MAPCONV_FILES
	src/CRC.cpp
	src/Dxt1.cpp
	src/Image.cpp
	src/Raster.cpp
	src/SMFMap.cpp
	src/TileStorage.cpp
	src/third_party/fpng.cpp
	src/third_party/lodepng.cpp
)


add_executable(mapcompile src/mapcompile.cpp ${MAPCONV_FILES})
add_executable(mapdecompile src/mapdecompile.cpp ${MAPCONV_FILES})

foreach(target mapcompile mapdecompile)
	target_link_libraries(${target} PRIVATE DevIL::IL DevIL::ILU)
	# MSVC predefines _WIN32 but not bare WIN32, which the source guards on.
	if (WIN32)
		target_compile_definitions(${target} PRIVATE WIN32)
	endif ()
endforeach()


if (DEFINED BINDIR)
	install(TARGETS mapcompile mapdecompile RUNTIME DESTINATION ${BINDIR})
else ()
	install(TARGETS mapcompile mapdecompile RUNTIME DESTINATION bin)
endif ()

