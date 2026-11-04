# Apply build-graph workarounds after Blender/mPainter have declared targets.
# This does not modify the snapshotted application source.
function(mpainter_cross_fix_targets)
  # Current application code uses shared_ptr::unique(), removed from C++20.
  # MSVC STL offers an explicit compatibility switch; preserve the input source.
  get_property(targets DIRECTORY "${CMAKE_SOURCE_DIR}/source/creator" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type "${target}" TYPE)
    if(target MATCHES "^bts-" AND type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY)$")
      target_compile_definitions("${target}" PRIVATE _HAS_DEPRECATED_SHARED_PTR_UNIQUE=1)
    endif()
  endforeach()
  if(TARGET bts-file-browser-native-check)
    get_target_property(libs bts-file-browser-native-check LINK_LIBRARIES)
    string(REPLACE "$<TARGET_PROPERTY:bts-host,LINK_LIBRARIES>"
      "$<TARGET_GENEX_EVAL:bts-host,$<TARGET_PROPERTY:bts-host,LINK_LIBRARIES>>" libs "${libs}")
    set_property(TARGET bts-file-browser-native-check PROPERTY LINK_LIBRARIES "${libs}")
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL mpainter_cross_fix_targets)
