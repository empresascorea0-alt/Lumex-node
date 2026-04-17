add_library(wallet wallet_value.hpp wallet_value.cpp)

target_link_libraries(wallet nano_lib nano_store)
