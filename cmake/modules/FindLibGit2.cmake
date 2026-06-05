# SPDX-License-Identifier: BSD-3-Clause

find_package(PkgConfig QUIET)
pkg_check_modules(PC_LibGit2 QUIET libgit2)

find_path(LibGit2_INCLUDE_DIR
    NAMES git2.h
    HINTS ${PC_LibGit2_INCLUDE_DIRS}
)

find_library(LibGit2_LIBRARY
    NAMES git2 libgit2
    HINTS ${PC_LibGit2_LIBRARY_DIRS}
)

if(LibGit2_INCLUDE_DIR)
    file(STRINGS "${LibGit2_INCLUDE_DIR}/git2/version.h" _libgit2_version_line
        REGEX "#define LIBGIT2_VERSION \"[^\"]+\""
    )
    string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" LibGit2_VERSION "${_libgit2_version_line}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibGit2
    REQUIRED_VARS LibGit2_LIBRARY LibGit2_INCLUDE_DIR
    VERSION_VAR LibGit2_VERSION
)

if(LibGit2_FOUND AND NOT TARGET LibGit2::LibGit2)
    add_library(LibGit2::LibGit2 UNKNOWN IMPORTED)
    set_target_properties(LibGit2::LibGit2 PROPERTIES
        IMPORTED_LOCATION "${LibGit2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibGit2_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(LibGit2_INCLUDE_DIR LibGit2_LIBRARY)
