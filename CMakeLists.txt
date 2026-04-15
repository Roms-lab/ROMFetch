cmake_minimum_required(VERSION 3.18)
project(ROMFetch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Easier Windows builds: avoid Meson/libpsl when building bundled curl
set(CPR_CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

add_subdirectory(include/cpr)

add_executable(ROMFetch src/ROMFetch-TERMINAL/ROMFetch.cpp)
target_link_libraries(ROMFetch PRIVATE cpr::cpr)

# MSVC: delay-load cpr/libcurl so we can SetDllDirectory(exe\DLL) before first use — DLLs live only in DLL/.
if(MSVC)
	# ROMFetch imports cpr only; libcurl is loaded when cpr.dll loads (still uses SetDllDirectory for DLL/).
	target_link_options(ROMFetch PRIVATE "/DELAYLOAD:cpr.dll")
	target_link_libraries(ROMFetch PRIVATE delayimp)
endif()

# Copy runtime DLLs into DLL/ next to the executable (not beside the .exe).
if(WIN32)
	if(TARGET cpr AND TARGET libcurl_shared)
		add_custom_command(
			TARGET ROMFetch
			POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ROMFetch>/DLL"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cpr>" "$<TARGET_FILE_DIR:ROMFetch>/DLL/"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libcurl_shared>" "$<TARGET_FILE_DIR:ROMFetch>/DLL/"
			COMMENT "Copy cpr.dll and libcurl.dll into DLL/")
		# Non-MSVC Windows (e.g. MinGW): implicit load still requires DLLs next to the exe.
		if(NOT MSVC)
			add_custom_command(
				TARGET ROMFetch
				POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cpr>" "$<TARGET_FILE_DIR:ROMFetch>/"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libcurl_shared>" "$<TARGET_FILE_DIR:ROMFetch>/"
				COMMENT "Copy cpr.dll and libcurl.dll next to ROMFetch.exe (non-MSVC)")
		endif()
	endif()
endif()
