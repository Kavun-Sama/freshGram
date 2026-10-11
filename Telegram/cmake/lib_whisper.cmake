# This file is part of freshGram.
#
# whisper.cpp (CPU only) for the local speech recognition.

option(FRESHGRAM_DISABLE_RECOGNITION "Build without the local recognition" OFF)

add_library(lib_whisper INTERFACE)
add_library(tdesktop::lib_whisper ALIAS lib_whisper)

if (NOT FRESHGRAM_DISABLE_RECOGNITION)
    function(freshgram_add_whisper)
        # The options are forced in the cache, because the old minimum
        # version of whisper.cpp ignores ordinary variables for them.
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(CMAKE_SKIP_INSTALL_RULES ON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
        set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
        set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
        set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
        set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
        set(GGML_METAL OFF CACHE BOOL "" FORCE)
        set(GGML_CUDA OFF CACHE BOOL "" FORCE)
        set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
        set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
        add_subdirectory(
            ${third_party_loc}/whisper.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/whisper.cpp
            EXCLUDE_FROM_ALL)
    endfunction()
    freshgram_add_whisper()

    target_link_libraries(lib_whisper INTERFACE whisper)
    target_compile_definitions(lib_whisper INTERFACE FRESHGRAM_LOCAL_RECOGNITION)
endif()
