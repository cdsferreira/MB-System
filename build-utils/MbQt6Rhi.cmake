#------------------------------------------------------------------------------
#    The MB-system:  build-utils/MbQt6Rhi.cmake   28 November 2026
#
#    Copyright (c) 2026 by
#    Christian dos Santos Ferreira
#      MARUM
#      University of Bremen
#      Bremen Germany
#    David W. Caress (caress@mbari.org)
#      Monterey Bay Aquarium Research Institute
#      Moss Landing, California, USA
#    Dale N. Chayes
#      Center for Coastal and Ocean Mapping
#      University of New Hampshire
#      Durham, New Hampshire, USA
#
#    MB-System was created by Caress and Chayes in 1992 at the
#      Lamont-Doherty Earth Observatory
#      Columbia University
#      Palisades, NY 10964
#
#    See README.md file for copying and redistribution conditions.
#------------------------------------------------------------------------------

# mb_qt6_rhi_check(<rhi_var> <needs_var>)
#
# Whether a Qt6 tool can draw with QRhi. Sets <rhi_var> to ON where
# -DbuildQt6Rhi=ON, Qt 6.8 or later, Qt's private Gui module (Qt6::GuiPrivate)
# and Qt Shader Tools (qt6_add_shaders) are found and the include directories
# of Qt6::GuiPrivate and Qt6::CorePrivate exist (each a plain path or one path
# in Qt's condition on private headers; entries in other generator expressions
# are not checked), else to OFF; sets <needs_var> to the sentence that says
# what QRhi needs, for the tool's configure line where QRhi is not built. Call
# it after find_package(Qt6 QUIET COMPONENTS GuiPrivate ShaderTools) in the
# tool's own directory. Include this file by its full path: on macOS the Qt6
# tools take build-utils off CMAKE_MODULE_PATH.
include_guard(GLOBAL)

function(mb_qt6_rhi_check rhi_var needs_var)
  set(mbqr_rhi OFF)
  set(mbqr_headers ON)
  if(buildQt6Rhi AND Qt6_VERSION VERSION_GREATER_EQUAL 6.8 AND TARGET Qt6::GuiPrivate AND COMMAND qt6_add_shaders)
    # the private headers themselves: Debian and Ubuntu install the CMake files
    # of Qt6::GuiPrivate and Qt6::CorePrivate with qt6-base-dev but their
    # headers with qt6-base-private-dev, and CMake stops at generate time when
    # an include directory of an imported target is missing; so look at each
    # one (Qt wraps them in a condition on private headers) and draw with
    # OpenGL where one is missing
    foreach(mbqr_target Qt6::GuiPrivate Qt6::CorePrivate)
      if(TARGET ${mbqr_target})
        get_target_property(mbqr_dirs ${mbqr_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT mbqr_dirs)
          set(mbqr_dirs "")
        endif()
        foreach(mbqr_dir IN LISTS mbqr_dirs)
          string(REGEX REPLACE "^\\$<\\$<BOOL:.*>>:(.*)>$" "\\1" mbqr_dir "${mbqr_dir}")
          if(mbqr_dir AND NOT mbqr_dir MATCHES "\\$<" AND NOT EXISTS "${mbqr_dir}")
            set(mbqr_headers OFF)
          endif()
        endforeach()
      endif()
    endforeach()
    if(mbqr_headers)
      set(mbqr_rhi ON)
    endif()
  endif()
  # what QRhi needs, for the tool's line where it is not built
  set(mbqr_needs "QRhi needs -DbuildQt6Rhi=ON, Qt 6.8 or later, Qt Shader Tools and Qt's private Gui headers")
  if(NOT mbqr_headers)
    set(mbqr_needs "QRhi needs Qt's private headers, on Debian and Ubuntu in the package qt6-base-private-dev")
  endif()
  set(${rhi_var} ${mbqr_rhi} PARENT_SCOPE)
  set(${needs_var} "${mbqr_needs}" PARENT_SCOPE)
endfunction()
