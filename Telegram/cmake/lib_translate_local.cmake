# This file is part of freshGram.
#
# CTranslate2 (CPU only, int8 on the bundled Ruy) and sentencepiece for the
# local translation with the Opus-MT models.

option(FRESHGRAM_DISABLE_TRANSLATION "Build without the local translation" OFF)

add_library(lib_translate_local INTERFACE)
add_library(tdesktop::lib_translate_local ALIAS lib_translate_local)

if (NOT FRESHGRAM_DISABLE_RECOGNITION AND NOT FRESHGRAM_DISABLE_TRANSLATION)
    function(freshgram_add_translate)
        set(ct2_dir ${third_party_loc}/CTranslate2)
        set(spm_dir ${third_party_loc}/sentencepiece)

        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(CMAKE_SKIP_INSTALL_RULES ON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        # The old minimum versions of the nested projects are rejected.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

        set(WITH_MKL OFF CACHE BOOL "" FORCE)
        set(WITH_DNNL OFF CACHE BOOL "" FORCE)
        set(WITH_OPENBLAS OFF CACHE BOOL "" FORCE)
        set(WITH_ACCELERATE OFF CACHE BOOL "" FORCE)
        set(WITH_CUDA OFF CACHE BOOL "" FORCE)
        set(WITH_CUDNN OFF CACHE BOOL "" FORCE)
        set(WITH_HIP OFF CACHE BOOL "" FORCE)
        set(WITH_RUY ON CACHE BOOL "" FORCE)
        set(OPENMP_RUNTIME NONE CACHE STRING "" FORCE)
        set(ENABLE_CPU_DISPATCH ON CACHE BOOL "" FORCE)
        set(ENABLE_PROFILING OFF CACHE BOOL "" FORCE)
        set(BUILD_CLI OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

        set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
        set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
        set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
        set(SPM_PROTOBUF_PROVIDER internal CACHE STRING "" FORCE)
        set(SPM_ABSL_PROVIDER internal CACHE STRING "" FORCE)

        if (COMMAND cmake_diagnostic)
            cmake_diagnostic(PUSH)
            foreach (category AUTHOR DEPRECATED POLICY UNINITIALIZED)
                cmake_diagnostic(DEMOTE CMD_${category} WARN)
            endforeach()
        else()
            set(was_suppressed "$CACHE{CMAKE_SUPPRESS_DEVELOPER_ERRORS}")
            set(CMAKE_SUPPRESS_DEVELOPER_ERRORS ON CACHE BOOL "" FORCE)
        endif()
        add_subdirectory(
            ${ct2_dir}
            ${CMAKE_CURRENT_BINARY_DIR}/CTranslate2
            EXCLUDE_FROM_ALL)
        add_subdirectory(
            ${spm_dir}
            ${CMAKE_CURRENT_BINARY_DIR}/sentencepiece
            EXCLUDE_FROM_ALL)
        if (COMMAND cmake_diagnostic)
            cmake_diagnostic(POP)
        elseif (was_suppressed STREQUAL "")
            unset(CMAKE_SUPPRESS_DEVELOPER_ERRORS CACHE)
        else()
            set(CMAKE_SUPPRESS_DEVELOPER_ERRORS ${was_suppressed} CACHE BOOL "" FORCE)
        endif()
    endfunction()
    freshgram_add_translate()

    # The headers of sentencepiece bring their own small "absl", so they
    # must be visible only to the one source file that includes them.
    set_source_files_properties(
        ${src_loc}/ayu/features/recognition/text_translation.cpp
        PROPERTIES INCLUDE_DIRECTORIES
        "${third_party_loc}/sentencepiece/src;${third_party_loc}/sentencepiece/third_party")
    target_link_libraries(lib_translate_local INTERFACE ctranslate2 sentencepiece-static)
    target_compile_definitions(lib_translate_local INTERFACE FRESHGRAM_LOCAL_TRANSLATE)
endif()
