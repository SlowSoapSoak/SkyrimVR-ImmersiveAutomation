cmake_minimum_required(VERSION 3.21)

project(
	ImmersiveWindsPluginScript
	VERSION 1.3.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# one DLL for SE, AE and VR (addresses come from the Address Library at runtime)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake) # add_commonlibsse_plugin(), only auto-included for prebuilt CommonLib

file(GLOB sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
find_package(nlohmann_json CONFIG REQUIRED)

# settings both plugins share
function(immersive_plugin_settings TARGET)
	target_precompile_headers(${TARGET} PRIVATE src/PCH.h)
	target_include_directories(${TARGET} PRIVATE src)
	target_link_libraries(${TARGET} PRIVATE ws2_32 iphlpapi winhttp nlohmann_json::nlohmann_json)
	target_compile_definitions(${TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endfunction()

# Immersive Winds: fan control
add_commonlibsse_plugin(
	${PROJECT_NAME}
	AUTHOR "shizof"
	SOURCES ${sources}
)
immersive_plugin_settings(${PROJECT_NAME})

# Immersive Heat: heat lamp control (sunlight, fire), shares the plug communication with Immersive Winds
file(GLOB heat_sources CONFIGURE_DEPENDS heat/*.cpp heat/*.h)
set(shared_sources
	src/DeviceInfo.cpp
	src/Enumerations.cpp
	src/HttpSwitch.cpp
	src/NetworkInterfaceInfo.cpp
	src/PlugFinder.cpp
	src/SPScanner.cpp
	src/TPLinkSPHelper.cpp
)
add_commonlibsse_plugin(
	ImmersiveHeat
	AUTHOR "shizof"
	VERSION 1.0.0
	SOURCES ${heat_sources} ${shared_sources}
)
immersive_plugin_settings(ImmersiveHeat)

# Deploy the built DLL straight into the game: cmake -DSKYRIM_DATA_DIR=".../Data"
set(SKYRIM_DATA_DIR "" CACHE PATH "Skyrim Data folder to copy the plugin into after each build")
if(SKYRIM_DATA_DIR)
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${SKYRIM_DATA_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_PDB_FILE:${PROJECT_NAME}> "${SKYRIM_DATA_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ImmersiveHeat> $<TARGET_PDB_FILE:ImmersiveHeat> "${SKYRIM_DATA_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Winds/ImmersiveWindsVR.esp" "${SKYRIM_DATA_DIR}"
	)
endif()
