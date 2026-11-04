# Invoked by CTest after building bts-file-browser-native-check. Linux native
# thumbnails use XDG_CACHE_HOME; every writable application path is private.
foreach(required BROWSER_EXECUTABLE BROWSER_BLEND BROWSER_OCIO BROWSER_OUTPUT_PARENT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing ${required}")
  endif()
endforeach()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(output "${BROWSER_OUTPUT_PARENT}/run-${run_id}")
foreach(folder config data cache runtime recovery logs resources fixtures)
  file(MAKE_DIRECTORY "${output}/${folder}")
  file(CHMOD "${output}/${folder}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
  "QT_QPA_PLATFORM=offscreen"
  "XDG_CONFIG_HOME=${output}/config" "XDG_DATA_HOME=${output}/data"
  "XDG_CACHE_HOME=${output}/cache" "XDG_RUNTIME_DIR=${output}/runtime"
  "BTS_RECOVERY_DIR=${output}/recovery" "BTS_SESSION_LOG_DIR=${output}/logs"
  "BTS_RESOURCE_CACHE=${output}/resources" "OCIO=${BROWSER_OCIO}"
  "${BROWSER_EXECUTABLE}" "${output}/fixtures" "${BROWSER_BLEND}"
  RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
file(WRITE "${output}/stdout.log" "${stdout}")
file(WRITE "${output}/stderr.log" "${stderr}")
message(STATUS "Native browser test evidence: ${output}")
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "Native browser test exited ${status}\n${stdout}\n${stderr}")
endif()
