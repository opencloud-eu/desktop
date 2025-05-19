include(CheckFunctionExists)
include(CheckLibraryExists)

# FUNCTIONS
if (NOT LINUX)
    # librt
    check_library_exists(rt nanosleep "" HAVE_LIBRT)
endif (NOT LINUX)

check_function_exists(utimes HAVE_UTIMES)
check_function_exists(lstat HAVE_LSTAT)
