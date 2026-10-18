# mugen360: a MUGEN engine for the Xbox 360, ported from Ikemen-GO.
#   PC (debugging):  build_pc.bat                     -> build/pc/mugen360.exe
#   Xbox 360:        build_360.bat (WSL + xsdk)       -> build/x360/mugen360.xex
cmake_minimum_required(VERSION 3.20)
project(mugen360 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SOURCES
	src/main.cpp
	src/core/ai.cpp
	src/core/air.cpp
	src/core/bgdef.cpp
	src/core/camera.cpp
	src/core/character.cpp
	src/core/debug_text.cpp
	src/core/effects.cpp
	src/core/expr.cpp
	src/core/fight_screen.cpp
	src/core/file.cpp
	src/core/font.cpp
	src/core/game_flow.cpp
	src/core/game_config.cpp
	src/core/hitdef.cpp
	src/core/hud.cpp
	src/core/ini.cpp
	src/core/input.cpp
	src/core/layout.cpp
	src/core/log.cpp
	src/core/match.cpp
	src/core/motif.cpp
	src/core/music.cpp
	src/core/select.cpp
	src/core/select_screen.cpp
	src/core/projectile.cpp
	src/core/sff.cpp
	src/core/sound.cpp
	src/core/sprite_renderer.cpp
	src/core/stage.cpp
	src/core/states.cpp
	src/core/text.cpp
	src/core/title_screen.cpp
	src/core/versus_screen.cpp
	third_party/lodepng/lodepng.cpp
	third_party/audio_decoders.c
	${CMAKE_CURRENT_BINARY_DIR}/build_version.cpp
)

# The version text shown in the game, regenerated on every build (see cmake/build_version.cmake).
add_custom_target(build_version
	COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/build_version.cpp -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_version.cmake
	BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/build_version.cpp
	VERBATIM)

if(XSDK)
	# Set by xsdk's toolchain file (cmake/xsdk.cmake), which also gives XSDK_ROOT.
	add_executable(mugen360 ${CORE_SOURCES} src/platform/x360/platform_x360.cpp)
	# xsdk's tests/ holds the shader containers the platform draws with; src/ its gpu_private.h (xsdk_gpu_swap).
	target_include_directories(mugen360 PRIVATE src third_party third_party/lodepng "${XSDK_ROOT}/tests" "${XSDK_ROOT}/src")
	target_compile_options(mugen360 PRIVATE -O2 -g -Wall -Wextra)
	set_source_files_properties(third_party/lodepng/lodepng.cpp third_party/audio_decoders.c PROPERTIES COMPILE_OPTIONS "-w")
	add_dependencies(mugen360 build_version)
	xsdk_add_xex(mugen360)
else()
	set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sdl3/SDL3-3.4.18/cmake" CACHE PATH "SDL3's CMake package")
	find_package(SDL3 REQUIRED CONFIG)
	add_executable(mugen360 ${CORE_SOURCES} src/platform/pc/platform_pc.cpp)
	target_include_directories(mugen360 PRIVATE src third_party third_party/lodepng)
	target_link_libraries(mugen360 PRIVATE SDL3::SDL3)
	add_dependencies(mugen360 build_version)
	if(MSVC)
		target_compile_options(mugen360 PRIVATE /W4 /utf-8)
		set_source_files_properties(third_party/lodepng/lodepng.cpp third_party/audio_decoders.c PROPERTIES COMPILE_OPTIONS "/W0")
		target_compile_definitions(mugen360 PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
	add_custom_command(TARGET mugen360 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:mugen360>)

	# Unit tests for the engine's core, no platform: build\pc\mugen360_tests.exe, run from this folder.
	add_executable(mugen360_tests tests/expr_test.cpp tests/states_test.cpp tests/effects_sound_test.cpp tests/combat_test.cpp tests/game_flow_test.cpp
		tests/legacy_test.cpp tests/music_test.cpp tests/screenpack_test.cpp tests/stage_test.cpp
		src/core/expr.cpp src/core/air.cpp src/core/bgdef.cpp src/core/camera.cpp src/core/character.cpp src/core/fight_screen.cpp src/core/match.cpp src/core/motif.cpp src/core/music.cpp src/core/select_screen.cpp src/core/title_screen.cpp src/core/versus_screen.cpp src/core/effects.cpp src/core/file.cpp src/core/font.cpp src/core/game_flow.cpp src/core/game_config.cpp src/core/hitdef.cpp src/core/ini.cpp src/core/input.cpp src/core/layout.cpp src/core/log.cpp src/core/sff.cpp src/core/sound.cpp src/core/stage.cpp
		src/core/projectile.cpp src/core/sprite_renderer.cpp src/core/states.cpp src/core/text.cpp third_party/lodepng/lodepng.cpp
		third_party/audio_decoders.c)
	target_include_directories(mugen360_tests PRIVATE src third_party third_party/lodepng)
	if(MSVC)
		target_compile_options(mugen360_tests PRIVATE /W4 /utf-8)
		target_compile_definitions(mugen360_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
endif()
