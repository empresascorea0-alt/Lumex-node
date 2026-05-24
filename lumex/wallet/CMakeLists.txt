add_library(wallet wallet_value.hpp wallet_value.cpp wallets_backend.hpp
                   lmdb/wallets_backend_lmdb.hpp lmdb/wallets_backend_lmdb.cpp)

target_link_libraries(wallet lumex_lib lumex_store)
