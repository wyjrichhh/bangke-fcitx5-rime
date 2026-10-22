# Included by project(rime); defer until librime and its merged plugins exist.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "The isolated Bangke runtime requires Linux ELF linking")
endif()
add_compile_definitions(rime_get_api=bangke_rime_get_api RIME_SHARED_GLOG)
set(BANGKE_RUNTIME_EXPORTS "${CMAKE_CURRENT_LIST_DIR}/runtime-exports.map")

function(bangke_isolate_runtime)
  cmake_policy(SET CMP0079 NEW)
  if(NOT BUILD_SHARED_LIBS OR NOT BUILD_MERGED_PLUGINS OR
     BUILD_SEPARATE_LIBS OR ENABLE_EXTERNAL_PLUGINS)
    message(FATAL_ERROR "Bangke requires one shared runtime with merged plugins and no external plugin loading")
  endif()
  if(NOT CTRANSLATE2_LIBRARY MATCHES "[.]a$")
    message(FATAL_ERROR "Bangke requires static CTranslate2 inside its private runtime")
  endif()
  # librime supplies these glog macros at directory scope. Do not redefine
  # them differently on plugin objects in the merged build.
  get_target_property(predict_definitions rime-ai-predict-objs COMPILE_DEFINITIONS)
  if(predict_definitions)
    list(FILTER predict_definitions EXCLUDE REGEX "^GLOG_(EXPORT|NO_EXPORT)=")
    set_property(TARGET rime-ai-predict-objs PROPERTY COMPILE_DEFINITIONS ${predict_definitions})
  endif()
  # CT2's archive does not absorb its static Ruy/cpuinfo dependencies.
  file(GLOB_RECURSE ct2_archives "${BANGKE_CT2_BUILD_DIR}/third_party/*.a")
  if(NOT ct2_archives)
    message(FATAL_ERROR "Missing CT2 static backend archives")
  endif()
  target_link_libraries(rime "-Wl,--start-group" ${ct2_archives} "-Wl,--end-group")
  target_link_options(rime PRIVATE
    "LINKER:--version-script=${BANGKE_RUNTIME_EXPORTS}"
    "LINKER:-Bsymbolic" "LINKER:--no-undefined")
  set_property(TARGET rime APPEND PROPERTY LINK_DEPENDS "${BANGKE_RUNTIME_EXPORTS}")
  set_target_properties(rime PROPERTIES OUTPUT_NAME bangke-rime)
endfunction()
cmake_language(DEFER CALL bangke_isolate_runtime)
