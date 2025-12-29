add_library(nano_messages messages.hpp messages.cpp)

target_link_libraries(nano_messages nano_lib Boost::endian)
