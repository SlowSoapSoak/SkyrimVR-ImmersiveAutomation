cmake_minimum_required(VERSION 3.21)

# Immersive Winds and Immersive Heat for Fallout 4: their own project, CommonLibF4 and CommonLibSSE-NG can not share one
project(
	ImmersiveWindsF4
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

# one DLL for Fallout 4 (original and next gen update) and Fallout 4 VR, addresses come from the Address Library
set(ENABLE_FALLOUT_F4 ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT_DIR}/extern/CommonLibF4/CommonLibF4" CommonLibF4)

find_package(nlohmann_json CONFIG REQUIRED)

# the plug communication and the wind simulation are shared with the Skyrim plugins
set(shared_sources
	${ROOT_DIR}/src/DeviceInfo.cpp
	${ROOT_DIR}/src/Enumerations.cpp
	${ROOT_DIR}/src/HttpSwitch.cpp
	${ROOT_DIR}/src/NetworkInterfaceInfo.cpp
	${ROOT_DIR}/src/PlugFinder.cpp
	${ROOT_DIR}/src/SPScanner.cpp
	${ROOT_DIR}/src/TPLinkSPHelper.cpp
)

# settings both plugins share
function(fallout4_plugin_settings TARGET)
	# fallout4/src first: its PCH.h, not the one of the Skyrim plugins
	target_include_directories(${TARGET} PRIVATE src "${ROOT_DIR}/src")
	target_precompile_headers(${TARGET} PRIVATE src/PCH.h)
	target_link_libraries(${TARGET} PRIVATE CommonLibF4::CommonLibF4 ws2_32 iphlpapi winhttp nlohmann_json::nlohmann_json)
	target_compile_definitions(${TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endfunction()

# Immersive Winds: fan control
add_library(
	ImmersiveWinds
	SHARED
	src/main.cpp
	src/WindsF4.cpp
	${shared_sources}
	${ROOT_DIR}/src/WindSimulation.cpp
)
fallout4_plugin_settings(ImmersiveWinds)

# Immersive Heat: heat lamp control (sunlight, fire)
add_library(
	ImmersiveHeat
	SHARED
	heat/main.cpp
	heat/HeatF4.cpp
	${shared_sources}
)
fallout4_plugin_settings(ImmersiveHeat)

# Deploy the built DLL straight into the game: cmake -DFALLOUT4_DATA_DIR=".../Data"
set(FALLOUT4_DATA_DIR "" CACHE PATH "Fallout 4 (VR) Data folder to copy the plugins into after each build")
if(FALLOUT4_DATA_DIR)
	foreach(plugin ImmersiveWinds ImmersiveHeat)
		add_custom_command(
			TARGET ${plugin} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${FALLOUT4_DATA_DIR}/F4SE/Plugins"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${plugin}> $<TARGET_PDB_FILE:${plugin}> "${FALLOUT4_DATA_DIR}/F4SE/Plugins"
		)
	endforeach()
endif()
