cmake_minimum_required(VERSION 3.10)
project(SpringMapConvNG)

set (CMAKE_CXX_STANDARD 11)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(DevIL REQUIRED)

set(MAPCONV_FILES
	src/CRC.cpp
	src/Image.cpp
	src/SMFMap.cpp
	src/TileStorage.cpp
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

