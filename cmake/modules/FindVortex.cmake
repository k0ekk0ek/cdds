# TODO Preferably this file should be generated by cmake commands, i.e.:
# install(TARGETS ddsc99 DESTINATION lib/dcps EXPORT vortex-targets)
# install(EXPORT vortex-targets DESTINATION lib/dcps)
# This generates vortex-targets.cmake, which provides ddsc99 as imported target, with hard-coded
# locations (relative to install tree) for finding the ddsc99 lib, include-dir etc.
# More info: https://cmake.org/Wiki/CMake/Tutorials/Exporting_and_Importing_Targets
#
# The current implementation uses find_* to dynamically lookup the details of the
# vdds library. For the time being (until we have a separate c99 target) the target
# is named 'DDSC99'. Other targets should be added as the need arises.
message(STATUS "Loading Vortex exported targets")

# Allow a user-override, i.e. when Vortex is not installed in any of the system default locations
if(EXISTS "$ENV{VORTEX_HOME}")
    file(TO_CMAKE_PATH "$ENV{VORTEX_HOME}" VORTEX_HOME)
    set(VORTEX_HOME "${VORTEX_HOME}" CACHE PATH "Prefix of VortexDDS installation.")
endif()

find_path(VORTEX_INCLUDE_DIR
    NAMES dds.h
    HINTS ${VORTEX_HOME}/include)

find_library(VORTEX_DDSC99_LIBRARY
    NAMES vdds
    HINTS ${VORTEX_HOME}/lib)

# Convenience macro to handle the QUIETLY and REQUIRED arguments and set Vortex_FOUND to TRUE if
# all listed REQUIRED_VARS are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Vortex
    REQUIRED_VARS VORTEX_INCLUDE_DIR VORTEX_DDSC99_LIBRARY)

if (Vortex_FOUND)
    add_library(Vortex::DDSC99 UNKNOWN IMPORTED)
    set_target_properties(Vortex::DDSC99 PROPERTIES
      IMPORTED_LOCATION                 "${VORTEX_DDSC99_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES     "${VORTEX_INCLUDE_DIR}"
      IMPORTED_LINK_INTERFACE_LANGUAGES "C" )
endif()