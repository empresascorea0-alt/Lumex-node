add_executable(lumex_rpc entry.cpp)

set_target_properties(lumex_rpc PROPERTIES OUTPUT_NAME lumex_rpc)

target_link_libraries(lumex_rpc node)

target_compile_definitions(
  lumex_rpc
  PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK}
  PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
          -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

if((LUMEX_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS lumex_rpc RUNTIME DESTINATION .)
  else()
    install(TARGETS lumex_rpc RUNTIME DESTINATION ./bin)
  endif()
endif()
