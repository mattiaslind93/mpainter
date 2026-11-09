# SPDX-License-Identifier: GPL-3.0-or-later
# Apply build-graph workarounds after Blender/mPainter have declared targets.
# This does not modify the snapshotted application source.
function(mpainter_cross_fix_targets)
  if(TARGET bts-file-browser-native-check)
    get_target_property(libs bts-file-browser-native-check LINK_LIBRARIES)
    string(REPLACE "$<TARGET_PROPERTY:bts-host,LINK_LIBRARIES>"
      "$<TARGET_GENEX_EVAL:bts-host,$<TARGET_PROPERTY:bts-host,LINK_LIBRARIES>>" libs "${libs}")
    set_property(TARGET bts-file-browser-native-check PROPERTY LINK_LIBRARIES "${libs}")
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL mpainter_cross_fix_targets)
