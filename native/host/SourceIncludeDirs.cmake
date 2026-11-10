# SPDX-License-Identifier: GPL-3.0-or-later
# Test and demo code lives in native/harness, and host code is moving into domain
# folders under native/host. Files include each other by bare name across all of
# them, so every compiled bts target searches every one of these folders.
# native/host/CMakeLists.txt and the standalone projects in native/tests/*/ include
# this file, so the list exists once.
set(BTS_HOST_DOMAINS app python materials/openpbr baking gpu painting layers fill ui/panels ui/dialogs assets viewport materials/layering materials/native persistence io diagnostics platform ui/widgets)
if(DEFINED BTS_HOST_SOURCE_DIR)
  set(_bts_host_dir ${BTS_HOST_SOURCE_DIR})
else()
  set(_bts_host_dir ${CMAKE_CURRENT_LIST_DIR})
endif()
set(BTS_SOURCE_INCLUDE_DIRS ${_bts_host_dir} ${_bts_host_dir}/../harness)
foreach(_bts_domain IN LISTS BTS_HOST_DOMAINS)
  list(APPEND BTS_SOURCE_INCLUDE_DIRS ${_bts_host_dir}/${_bts_domain})
endforeach()
