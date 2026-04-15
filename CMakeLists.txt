cmake_minimum_required(VERSION 3.18)
project(ROMFetch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Easier Windows builds: avoid Meson/libpsl when building bundled curl
set(CPR_CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

add_subdirectory(include/cpr)

add_library(romfetch_core STATIC src/ROMFetch-Core/RomFetchCore.cpp)
target_include_directories(romfetch_core PUBLIC ${CMAKE_SOURCE_DIR}/src/ROMFetch-Core)
target_link_libraries(romfetch_core PUBLIC cpr::cpr)

add_executable(ROMFetch src/ROMFetch-TERMINAL/ROMFetch.cpp)
target_link_libraries(ROMFetch PRIVATE romfetch_core)

# MSVC: delay-load cpr so SetDllDirectory(exe\DLL) runs before load (see RomFetchCore::EnsureDllSearchPath).
if(MSVC)
	target_link_options(ROMFetch PRIVATE "/DELAYLOAD:cpr.dll")
	target_link_libraries(ROMFetch PRIVATE delayimp)
endif()

# [JamesBoer/ImFrame](https://github.com/JamesBoer/ImFrame) — dear imgui + GLFW (submodule: external/ImFrame)
# CPR turns on BUILD_SHARED_LIBS; ImFrame's Dear ImGui/Implot expect static libs (otherwise MSVC may omit .lib for DLLs).
set(_ROMFETCH_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
# GLFW's CMakeLists asks for <3.5; CMake 4.x needs this to keep configuring nested externals.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
add_subdirectory(external/ImFrame)
set(BUILD_SHARED_LIBS ${_ROMFETCH_BUILD_SHARED_LIBS})

add_executable(ROMFetchGUI WIN32 src/ROMFetch-GUI/ROMFetchGui.cpp)
target_include_directories(ROMFetchGUI PRIVATE ${CMAKE_SOURCE_DIR}/src/ROMFetch-GUI)
target_link_libraries(ROMFetchGUI PRIVATE ImFrame romfetch_core)
if(MSVC)
	target_link_options(ROMFetchGUI PRIVATE "/DELAYLOAD:cpr.dll")
	target_link_libraries(ROMFetchGUI PRIVATE delayimp)
	# ImFrame uses /WX; keep our translation unit from failing the build on level-4 noise.
	target_compile_options(ROMFetchGUI PRIVATE /W3 /WX-)
endif()

function(romfetch_copy_cpr_dlls _exe)
	if(WIN32 AND TARGET cpr AND TARGET libcurl_shared)
		add_custom_command(
			TARGET ${_exe}
			POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_exe}>/DLL"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cpr>" "$<TARGET_FILE_DIR:${_exe}>/DLL/"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libcurl_shared>"
			        "$<TARGET_FILE_DIR:${_exe}>/DLL/"
			COMMENT "Copy cpr.dll and libcurl.dll into DLL/ (${_exe})")
		if(NOT MSVC)
			add_custom_command(
				TARGET ${_exe}
				POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cpr>" "$<TARGET_FILE_DIR:${_exe}>/"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libcurl_shared>"
				        "$<TARGET_FILE_DIR:${_exe}>/"
				COMMENT "Copy DLLs next to ${_exe} (non-MSVC)")
		endif()
	endif()
endfunction()

romfetch_copy_cpr_dlls(ROMFetch)
romfetch_copy_cpr_dlls(ROMFetchGUI)
