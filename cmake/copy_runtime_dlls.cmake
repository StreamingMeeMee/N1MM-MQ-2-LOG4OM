# Copies every non-system DLL dependency of TARGET_FILE into OUTPUT_DIR, so
# a build produces a folder that runs standalone (e.g. from a plain cmd.exe
# with no toolchain directories on PATH) without needing `cmake --install`.
# Invoked as a POST_BUILD step from CMakeLists.txt on Windows/MinGW builds.
#
# Expects -DTARGET_FILE=... -DOUTPUT_DIR=... on the cmake -P command line.

if(NOT TARGET_FILE OR NOT OUTPUT_DIR)
    message(FATAL_ERROR "copy_runtime_dlls.cmake requires -DTARGET_FILE and -DOUTPUT_DIR")
endif()

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW) # normalize paths before matching; just quiets an author warning
endif()

set(extra_dirs_arg "")
if(TOOLCHAIN_BIN_DIR)
    set(extra_dirs_arg DIRECTORIES "${TOOLCHAIN_BIN_DIR}")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${TARGET_FILE}"
    ${extra_dirs_arg}
    RESOLVED_DEPENDENCIES_VAR resolved_deps
    UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
    CONFLICTING_DEPENDENCIES_PREFIX conflicting_deps
)

foreach(dep IN LISTS resolved_deps)
    string(TOLOWER "${dep}" dep_lower)
    # Skip DLLs that live under a Windows system directory (always present,
    # and not ours to redistribute) -- only bundle toolchain/library DLLs.
    if(dep_lower MATCHES "/windows/system32/" OR dep_lower MATCHES "/windows/syswow64/")
        continue()
    endif()
    get_filename_component(dep_name "${dep}" NAME)
    file(COPY_FILE "${dep}" "${OUTPUT_DIR}/${dep_name}" ONLY_IF_DIFFERENT RESULT copy_result)
    if(copy_result)
        message(WARNING "Failed to copy runtime dependency ${dep}: ${copy_result}")
    endif()
endforeach()

# Not reporting unresolved_deps: on Windows this is normally dozens to
# hundreds of virtual "api-ms-win-*"/"ext-ms-*" API Set DLLs, which are
# resolved specially by the OS loader and never exist as real files on
# disk -- expected for any Windows binary, not a sign of a missing
# dependency.
