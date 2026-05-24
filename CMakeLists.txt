cmake_minimum_required(VERSION 3.14)

if(CMAKE_VERSION VERSION_GREATER 3.12 OR CMAKE_VERSION VERSION_EQUAL 3.12)
  # find_package uses <PACKAGENAME>_ROOT variables
  cmake_policy(SET CMP0074 NEW)
endif()
if(CMAKE_VERSION VERSION_GREATER 3.13 OR CMAKE_VERSION VERSION_EQUAL 3.13)
  # option honors normal variables
  cmake_policy(SET CMP0077 NEW)
endif()
# De-duplicate libraries on link lines based on linker capabilities
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()

# OSX compatibility needs to be set before project is declared
set(CMAKE_OSX_DEPLOYMENT_TARGET
    13.3
    CACHE STRING "")

project(lumex-node)

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

option(CI_VERSION_PRE_RELEASE false)

set(COVERAGE
    OFF
    CACHE BOOL "Code Coverage Targets")
if(COVERAGE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")
  include(CoverageTest)
endif()

if(MSVC)
  add_definitions(/MP)
  add_definitions(
    -D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING) # Suppress iterator warning
endif()

set(CPACK_PACKAGE_VENDOR "Lumex Currency")
set(CPACK_PACKAGE_VERSION_MAJOR "29")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")

if(CI_VERSION_PRE_RELEASE)
  set(CPACK_PACKAGE_VERSION_PRE_RELEASE "${CI_VERSION_PRE_RELEASE}")
else()
  set(CPACK_PACKAGE_VERSION_PRE_RELEASE "99")
endif()

if(CI_TAG)
  set(TAG_VERSION_STRING "${CI_TAG}")
else()
  set(TAG_VERSION_STRING
      "V${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}-DEV")
endif()

if(APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
else()
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

# Create all libraries and executables in the root binary dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

set(LUMEX_GUI
    OFF
    CACHE BOOL "")
set(LUMEX_TEST
    OFF
    CACHE BOOL "")
set(LUMEX_SECURE_RPC
    OFF
    CACHE BOOL "")
set(LUMEX_WARN_TO_ERR
    OFF
    CACHE BOOL "")
set(LUMEX_TIMED_LOCKS
    0
    CACHE STRING "")
set(LUMEX_TIMED_LOCKS_IGNORE_BLOCKED
    OFF
    CACHE BOOL "")
set(LUMEX_TIMED_LOCKS_FILTER
    ""
    CACHE
      STRING
      "Selects which mutex should be the only one to have stacktraces generated, empty string means report on all"
)
set_property(
  CACHE LUMEX_TIMED_LOCKS_FILTER
  PROPERTY STRINGS
           active
           block_arrival
           block_processor
           block_uniquer
           dropped_elections,
           election_winner_details
           gap_cache
           network_filter
           observer_set
           request_aggregator
           state_block_signature_verification
           telemetry
           vote_generator
           vote_processor
           vote_uniquer
           votes_cache
           work_pool)
set(LUMEX_FUZZER_TEST
    OFF
    CACHE BOOL "")
set(LUMEX_ASIO_HANDLER_TRACKING
    OFF
    CACHE BOOL "")
set(LUMEX_ROCKSDB_TOOLS
    OFF
    CACHE BOOL "")

# Enable LUMEX_TRACING by default in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  option(LUMEX_TRACING "Enable trace logging" ON)
else()
  option(LUMEX_TRACING "Enable trace logging" OFF)
endif()
if(LUMEX_TRACING)
  message(STATUS "Using trace logging")
  add_compile_definitions(LUMEX_TRACING)
endif()

if(${LUMEX_TIMED_LOCKS} GREATER 0)
  add_definitions(-DLUMEX_TIMED_LOCKS=${LUMEX_TIMED_LOCKS})
  add_definitions(-DLUMEX_TIMED_LOCKS_FILTER=${LUMEX_TIMED_LOCKS_FILTER})
  if(LUMEX_TIMED_LOCKS_IGNORE_BLOCKED)
    add_definitions(-DLUMEX_TIMED_LOCKS_IGNORE_BLOCKED)
  endif()
endif()

if(LUMEX_ASIO_HANDLER_TRACKING)
  add_definitions(-DBOOST_ASIO_ENABLE_HANDLER_TRACKING)
endif()

option(LUMEX_SIMD_OPTIMIZATIONS
       "Enable CPU-specific SIMD optimizations (SSE/AVX or NEON, e.g.)" OFF)
option(
  ENABLE_AES
  "Enable AES optimizations (enabled by default with LUMEX_SIMD_OPTIMIZATIONS, set OFF to disable"
  ON)
option(ENABLE_AVX2 "Enable AVX2 optimizations" OFF)

set(ACTIVE_NETWORK
    lumex_live_network
    CACHE STRING "Selects which network parameters are used")
set_property(
  CACHE ACTIVE_NETWORK PROPERTY STRINGS lumex_dev_network lumex_beta_network
                                lumex_live_network lumex_test_network)
set(CPACK_PACKAGE_NAME
    "lumex-node"
    CACHE STRING "" FORCE)
set(CPACK_NSIS_PACKAGE_NAME
    "Lumex"
    CACHE STRING "" FORCE)
set(CPACK_PACKAGE_INSTALL_DIRECTORY
    "lumexcurrency"
    CACHE STRING "" FORCE)
set(LUMEX_SERVICE "lumexcurrency.service")
set(LUMEX_PREFIX "")
if("${ACTIVE_NETWORK}" MATCHES "lumex_beta_network")
  project("lumex-node-beta")
  set(CPACK_PACKAGE_NAME
      "lumex-node-beta"
      CACHE STRING "" FORCE)
  set(CPACK_NSIS_PACKAGE_NAME
      "Lumex-Beta"
      CACHE STRING "" FORCE)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY
      "lumexcurrency-beta"
      CACHE STRING "" FORCE)
  set(LUMEX_SERVICE "lumexcurrency-beta.service")
  set(LUMEX_PREFIX "Beta")
elseif("${ACTIVE_NETWORK}" MATCHES "lumex_test_network")
  project("lumex-node-test")
  set(CPACK_PACKAGE_NAME
      "lumex-node-test"
      CACHE STRING "" FORCE)
  set(CPACK_NSIS_PACKAGE_NAME
      "Lumex-Test"
      CACHE STRING "" FORCE)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY
      "lumexcurrency-test"
      CACHE STRING "" FORCE)
  set(LUMEX_SERVICE "lumexcurrency-test.service")
  set(LUMEX_PREFIX "Test")
endif()
set(LUMEX_OSX_PACKAGE_NAME
    ${CPACK_NSIS_PACKAGE_NAME}
    CACHE STRING "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE
        STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads)

# Sanitizers
option(LUMEX_ASAN "Enable ASan" OFF)
if(LUMEX_ASAN)
  if(MSVC)
    message(FATAL_ERROR "ASan is not supported on MSVC")
  endif()

  message(STATUS "Using Asan")
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
endif()

option(LUMEX_ASAN_INT "Enable ASan+Integer overflow" OFF)
if(LUMEX_ASAN_INT)
  if(MSVC)
    message(FATAL_ERROR "ASan+Integer overflow is not supported on MSVC")
  endif()

  message(STATUS "Using ASan+Integer overflow")
  add_compile_options(-fsanitize=address,integer)
  add_link_options(-fsanitize=address,integer)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
endif()

option(LUMEX_UBSAN "Enable UBSan" OFF)
if(LUMEX_UBSAN)
  if(MSVC)
    message(FATAL_ERROR "UBSan is not supported on MSVC")
  endif()

  message(STATUS "Using UBSan")
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_ubsan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_ubsan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
  add_definitions(-DROCKSDB_UBSAN_RUN)
endif()

option(LUMEX_TSAN "Enable TSan" OFF)
if(LUMEX_TSAN)
  if(MSVC)
    message(FATAL_ERROR "TSan is not supported on MSVC")
  endif()

  message(STATUS "Using TSan")
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_tsan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_tsan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
endif()

if(LUMEX_WARN_TO_ERR)
  add_compile_options(-Werror -Wno-deprecated-declarations)
endif()

if(WIN32)
  find_library(PSAPI Psapi)
  add_definitions(-D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -DMINIUPNP_STATICLIB
                  -D_CRT_SECURE_NO_WARNINGS -DNOGDI /EHsc)
else()
  if(LUMEX_FUZZER_TEST)
    add_compile_options(-fsanitize=fuzzer-no-link -fno-omit-frame-pointer)
    add_definitions(-DLUMEX_FUZZER_TEST)
  endif()

  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
    if(LUMEX_SIMD_OPTIMIZATIONS
       OR RAIBLOCKS_SIMD_OPTIMIZATIONS
       OR ENABLE_AVX2)
      add_compile_options(-msse4)
      if(ENABLE_AES)
        add_compile_options(-maes)
      else()
        add_definitions(-DCRYPTOPP_DISABLE_AESNI)
      endif()
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
      add_compile_options(-msse2)
      add_definitions(-DCRYPTOPP_DISABLE_SSSE3 -DCRYPTOPP_DISABLE_AESNI)
    endif()

  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    if(LUMEX_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS)
      add_compile_options(-march=armv8-a+crc+crypto)
    else()
      add_compile_options(-march=armv8-a)
      add_definitions(-DCRYPTOPP_DISABLE_ASM)
    endif()
  endif()
endif()

if(ENABLE_AVX2)
  if(WIN32)
    add_definitions(/arch:AVX2)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
    add_compile_options(-mavx2 -mbmi -mbmi2 -maes)
  endif()
  if(PERMUTE_WITH_GATHER)
    add_definitions(-DPERMUTE_WITH_GATHER)
  elseif(PERMUTE_WITH_SHUFFLES)
    add_definitions(-DPERMUTE_WITH_SHUFFLES)
  endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEX_PRECOMPILED_HEADERS
       "Enable precompiled headers for first-party C++ targets" OFF)

if(LUMEX_PRECOMPILED_HEADERS AND NOT COMMAND target_precompile_headers)
  message(STATUS "Precompiled headers require CMake 3.16 or newer; skipping")
endif()

function(lumex_target_precompile_headers target)
  if(NOT LUMEX_PRECOMPILED_HEADERS OR NOT COMMAND target_precompile_headers)
    return()
  endif()

  if(ARGC GREATER 1)
    set(header "${ARGV1}")
  else()
    set(header "lumex/pch.hpp")
  endif()

  if(NOT TARGET ${target})
    message(
      FATAL_ERROR "Unknown lumex target for precompiled headers: ${target}")
  endif()

  target_precompile_headers(
    ${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${PROJECT_SOURCE_DIR}/${header}>")
endfunction()

if(WIN32)
  set(PLATFORM_LINK_FLAGS "")
  add_definitions(/bigobj)
else()
  if(APPLE)
    set(PLATFORM_LINK_FLAGS "-framework Foundation -framework OpenCL")
  else()
    set(PLATFORM_LINK_FLAGS "-static-libgcc -static-libstdc++")
  endif()

  if(LUMEX_FUZZER_TEST)
    set(PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=fuzzer-no-link")
  endif()
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${PLATFORM_LINK_FLAGS}")

if(LUMEX_SECURE_RPC OR RAIBLOCKS_SECURE_RPC)
  find_package(OpenSSL 1.0 REQUIRED)
  include_directories(${OPENSSL_INCLUDE_DIR})
  add_definitions(-DLUMEX_SECURE_RPC)
  message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
  message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
  message("OpenSSL lib: ${OPENSSL_SSL_LIBRARY}")
  message("Crypto lib: ${OPENSSL_CRYPTO_LIBRARY}")
else()
  set(OPENSSL_LIBRARIES "")
endif()

include_directories(${CMAKE_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

# Find libbacktrace first (Unix/Linux only)
if(NOT WIN32)
  find_path(
    BACKTRACE_INCLUDE_DIR
    NAMES backtrace.h
    PATHS /usr/local/include /usr/include)

  if(BACKTRACE_INCLUDE_DIR)
    set(BACKTRACE_FOUND TRUE)
    # Most systems just need -lbacktrace
    set(BACKTRACE_LIBRARIES backtrace)
    message(STATUS "Found backtrace.h at ${BACKTRACE_INCLUDE_DIR}")
  endif()
endif()

# Then set up Boost modules. Only list libraries directly used by lumex-node;
# BoostRoot resolves transitive dependencies automatically. BoostRoot resolves
# transitive dependencies automatically.
set(BOOST_INCLUDE_LIBRARIES
    algorithm
    asio
    beast
    circular_buffer
    dll
    endian
    format
    iostreams
    lexical_cast
    multi_index
    multiprecision
    outcome
    pool
    process
    program_options
    property_tree
    stacktrace
    system
    thread
    unordered
    variant)

add_definitions(-DBOOST_ALL_NO_LIB) # Disable automatic boost linking
add_subdirectory(submodules/boost EXCLUDE_FROM_ALL)
include_directories(${BOOST_LIBRARY_INCLUDES})

# Configure stacktrace with appropriate backend
if(WIN32)
  message(STATUS "Windows platform - Using WinDbg stacktrace")
  add_definitions(-DBOOST_STACKTRACE_USE_WINDBG)
  add_library(Boost::stacktrace ALIAS boost_stacktrace_windbg)
  target_link_libraries(boost_stacktrace_windbg PRIVATE dbghelp)
elseif(BOOST_STACKTRACE_HAS_BACKTRACE)
  message(
    STATUS "Found libbacktrace - enabling Boost stacktrace backtrace support")
  add_definitions(-DBOOST_STACKTRACE_USE_BACKTRACE)
  add_library(Boost::stacktrace ALIAS boost_stacktrace_backtrace)
  target_link_libraries(boost_stacktrace_backtrace
                        PRIVATE ${BACKTRACE_LIBRARIES})
else()
  message(STATUS "Using basic stacktrace backend")
  add_definitions(-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)
  add_library(Boost::stacktrace ALIAS boost_stacktrace_basic)
endif()

# RocksDB
include_directories(submodules/rocksdb/include)
set(FAIL_ON_WARNINGS
    OFF
    CACHE BOOL "")
set(USE_RTTI
    ON
    CACHE BOOL "")
set(WITH_GFLAGS
    OFF
    CACHE BOOL "")
set(WITH_TESTS
    OFF
    CACHE BOOL "")
set(WITH_BENCHMARK_TOOLS
    OFF
    CACHE BOOL "")
set(ROCKSDB_BUILD_SHARED
    OFF
    CACHE BOOL "")
set(WITH_CORE_TOOLS
    ${LUMEX_ROCKSDB_TOOLS}
    CACHE BOOL "" FORCE)
set(WITH_TOOLS
    ${LUMEX_ROCKSDB_TOOLS}
    CACHE BOOL "" FORCE)
if(ENABLE_AVX2)
  set(PORTABLE
      0
      CACHE BOOL "" FORCE)
else()
  set(PORTABLE
      1
      CACHE BOOL "" FORCE)
endif()
add_subdirectory(submodules/rocksdb EXCLUDE_FROM_ALL)

# cpptoml
include_directories(submodules/cpptoml/include)

# magic_enum
include_directories(submodules/magic_enum/include/magic_enum)

add_subdirectory(crypto/ed25519-donna)

add_subdirectory(lumex/ipc_flatbuffers_lib)
add_subdirectory(lumex/ipc_flatbuffers_test)

# fmt
add_subdirectory(submodules/fmt EXCLUDE_FROM_ALL)
include_directories(submodules/fmt/include)

# spdlog
add_definitions(-DSPDLOG_FMT_EXTERNAL)
add_subdirectory(submodules/spdlog EXCLUDE_FROM_ALL)
include_directories(submodules/spdlog/include)

# miniupnp
set(UPNPC_BUILD_SHARED
    OFF
    CACHE BOOL "")
add_subdirectory(submodules/miniupnp/miniupnpc EXCLUDE_FROM_ALL)

set(BUILD_SHARED
    OFF
    CACHE BOOL "")
set(BUILD_TESTING
    OFF
    CACHE BOOL "")
set(USE_INTERMEDIATE_OBJECTS_TARGET
    OFF
    CACHE BOOL "")
set(CRYPTOPP_EXTRA "")
if(WIN32)
  add_definitions(-DCRYPTOPP_DISABLE_ASM -DCRYPTOPP_DISABLE_SSSE3
                  -DCRYPTOPP_DISABLE_AESNI)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"
       AND (LUMEX_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS))
  set(CRYPTOPP_EXTRA
      submodules/cryptopp/crc_simd.cpp submodules/cryptopp/gcm_simd.cpp
      submodules/cryptopp/gf2n_simd.cpp submodules/cryptopp/neon_simd.cpp)
  add_definitions(-DCRYPTOPP_NO_CPU_FEATURE_PROBES)
endif()
# Some Clang cannot handle mixed asm with positional arguments, where the body
# is Intel style with no prefix and the templates are AT&T style. See:
# submodules/cryptopp/config.h Also see
# https://bugs.llvm.org/show_bug.cgi?id=39895
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_definitions(-DCRYPTOPP_DISABLE_MIXED_ASM -DCRYPTOPP_DISABLE_ASM)
  message(
    STATUS
      "CryptoPP with disabled ASM for ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  )
endif()
add_definitions(-DCRYPTOPP_DISABLE_SHANI)
# Fix failing builds after commit
# https://github.com/weidai11/cryptopp/commit/df9fa62205f2d341e2b1b26595a3a1b6377c60c5
add_definitions(-DCRYPTOPP_DISABLE_CLMUL)

set(CRYPTOPP_LIBRARY cryptopp)
add_library(
  cryptopp
  submodules/cryptopp/aes.h
  submodules/cryptopp/algparam.cpp
  submodules/cryptopp/allocate.cpp
  submodules/cryptopp/asn.cpp
  submodules/cryptopp/basecode.cpp
  submodules/cryptopp/cpu.cpp
  submodules/cryptopp/cryptlib.cpp
  submodules/cryptopp/default.cpp
  submodules/cryptopp/des.cpp
  submodules/cryptopp/dessp.cpp
  submodules/cryptopp/dll.cpp
  submodules/cryptopp/ec2n.cpp
  submodules/cryptopp/ecp.cpp
  submodules/cryptopp/filters.cpp
  submodules/cryptopp/fips140.cpp
  submodules/cryptopp/gcm.cpp
  submodules/cryptopp/gf2n.cpp
  submodules/cryptopp/gfpcrypt.cpp
  submodules/cryptopp/hex.cpp
  submodules/cryptopp/hmac.cpp
  submodules/cryptopp/hrtimer.cpp
  submodules/cryptopp/integer.cpp
  submodules/cryptopp/iterhash.cpp
  submodules/cryptopp/misc.h
  submodules/cryptopp/misc.cpp
  submodules/cryptopp/modes.h
  submodules/cryptopp/modes.cpp
  submodules/cryptopp/mqueue.cpp
  submodules/cryptopp/nbtheory.cpp
  submodules/cryptopp/oaep.cpp
  submodules/cryptopp/osrng.h
  submodules/cryptopp/osrng.cpp
  submodules/cryptopp/pubkey.cpp
  submodules/cryptopp/queue.cpp
  submodules/cryptopp/randpool.cpp
  submodules/cryptopp/rdtables.cpp
  submodules/cryptopp/rijndael.cpp
  submodules/cryptopp/rijndael_simd.cpp
  submodules/cryptopp/rng.cpp
  submodules/cryptopp/sha.cpp
  submodules/cryptopp/sha_simd.cpp
  submodules/cryptopp/simple.cpp
  submodules/cryptopp/sse_simd.cpp
  submodules/cryptopp/seckey.h
  submodules/cryptopp/siphash.h
  submodules/cryptopp/words.h
  ${CRYPTOPP_EXTRA})

target_include_directories(cryptopp PUBLIC submodules)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
  set(ARGON_CORE submodules/phc-winner-argon2/src/opt.c)
else()
  set(ARGON_CORE submodules/phc-winner-argon2/src/ref.c)
endif()

add_library(
  argon2
  submodules/phc-winner-argon2/src/argon2.c
  submodules/phc-winner-argon2/include/argon2.h
  submodules/phc-winner-argon2/src/core.c
  submodules/phc-winner-argon2/src/thread.c
  submodules/phc-winner-argon2/src/encoding.c
  ${ARGON_CORE})

target_include_directories(argon2 PUBLIC submodules/phc-winner-argon2/include)
target_include_directories(argon2 PUBLIC submodules/phc-winner-argon2/src)
target_include_directories(argon2 PUBLIC crypto/blake2)

# LMDB
add_library(
  lmdb
  submodules/lmdb/libraries/liblmdb/lmdb.h
  submodules/lmdb/libraries/liblmdb/mdb.c
  submodules/lmdb/libraries/liblmdb/midl.c)

if(WIN32)
  target_link_libraries(lmdb ntdll)
endif()

# LMDB defaults to SysV semaphores on macOS, which are blocked by Seatbelt
# sandbox Use POSIX semaphores instead to avoid EPERM from mdb_env_open()
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(lmdb PRIVATE MDB_USE_POSIX_SEM=1)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
  set(BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b.c")
else()
  set(BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b-ref.c")
endif()

add_library(blake2 crypto/blake2/blake2-config.h crypto/blake2/blake2-impl.h
                   crypto/blake2/blake2.h ${BLAKE2_IMPLEMENTATION})

target_compile_definitions(blake2 PRIVATE -D__SSE2__)

add_subdirectory(lumex/crypto_lib)
add_subdirectory(lumex/secure)
add_subdirectory(lumex/store)
target_include_directories(lumex_store PUBLIC ${BOOST_LIBRARY_INCLUDES})
target_include_directories(lumex_store PUBLIC submodules)
add_subdirectory(lumex/lib)
add_subdirectory(lumex/weights)
add_subdirectory(lumex/messages)
add_subdirectory(lumex/wallet)
add_subdirectory(lumex/node)
add_subdirectory(lumex/lumex_node)
add_subdirectory(lumex/rpc)
add_subdirectory(lumex/lumex_rpc)

foreach(
  lumex_pch_target
  lumex_lib
  secure
  lumex_store
  lumex_messages
  rpc
  node
  lumex_node)
  lumex_target_precompile_headers(${lumex_pch_target})
endforeach()

add_custom_target(
  executables
  COMMAND echo "BATCH BUILDING node + rpc"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS lumex_node lumex_rpc)

if(LUMEX_FUZZER_TEST)
  if(NOT WIN32)
    add_subdirectory(lumex/fuzzer_test)
  else()
    message(WARNING "Fuzzing is not supported on Windows")
  endif()
endif()

if(LUMEX_TEST OR RAIBLOCKS_TEST)
  if(WIN32)
    if(MSVC_VERSION)
      if(MSVC_VERSION GREATER_EQUAL 1910)
        add_definitions(-DGTEST_LANG_CXX11=1)
        add_definitions(-DGTEST_HAS_TR1_TUPLE=0)
      endif()
    endif()
    set(gtest_force_shared_crt ON)
  else()
    set(gtest_force_shared_crt OFF)
  endif()

  add_subdirectory(lumex/load_test)

  # FIXME: This fixes googletest GOOGLETEST_VERSION requirement
  set(GOOGLETEST_VERSION 1.11.0)
  add_subdirectory(submodules/gtest/googletest)
  # FIXME: This fixes gtest include directories without modifying gtest's
  # CMakeLists.txt. Ideally we should use GTest::GTest and GTest::Main as
  # dependencies but it requires building gtest differently
  set_target_properties(
    gtest PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                     "${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include")

  set(BENCHMARK_ENABLE_TESTING OFF)
  add_subdirectory(submodules/benchmark)

  add_subdirectory(lumex/test_common)
  add_subdirectory(lumex/core_test)
  add_subdirectory(lumex/rpc_test)
  add_subdirectory(lumex/slow_test)
  add_subdirectory(lumex/benchmarks)

  foreach(lumex_test_pch_target test_common core_test rpc_test slow_test
                               benchmarks)
    lumex_target_precompile_headers(${lumex_test_pch_target}
                                   lumex/test_common/pch.hpp)
  endforeach()

  add_custom_target(
    all_tests
    COMMAND echo "BATCH BUILDING TESTS"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS core_test load_test rpc_test slow_test lumex_node lumex_rpc)
endif()

if(LUMEX_TEST OR RAIBLOCKS_TEST)
  if(LUMEX_GUI OR RAIBLOCKS_GUI)
    add_custom_target(
      quick_tests
      COMMAND echo "BATCH QUICK TESTS"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS core_test rpc_test qt_test)
  else()
    add_custom_target(
      quick_tests
      COMMAND echo "BATCH QUICK TESTS -- qt_test is disabled"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS core_test rpc_test)
  endif()
endif()

if(LUMEX_GUI OR RAIBLOCKS_GUI)
  install(FILES ${PROJECT_BINARY_DIR}/config-node.toml.sample DESTINATION .)
  install(FILES ${PROJECT_BINARY_DIR}/config-rpc.toml.sample DESTINATION .)
  install(FILES ${PROJECT_BINARY_DIR}/config-log.toml.sample DESTINATION .)
  if(WIN32)
    set(PLATFORM_QT_PACKAGES WinExtras)
  else()
    set(PLATFORM_QT_PACKAGES)
  endif()

  find_package(Qt5 COMPONENTS Core Gui Widgets Test ${PLATFORM_QT_PACKAGES})

  add_library(qt lumex/qt/qt.cpp lumex/qt/qt.hpp)
  include_directories(${CMAKE_SOURCE_DIR}/submodules)

  target_link_libraries(
    qt
    node
    secure
    lumex_lib
    libminiupnpc-static
    Qt5::Gui
    Qt5::Widgets)

  target_compile_definitions(
    qt PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
               -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

  if(WIN32)
    set(PLATFORM_GUI_TARGET_PROPERTIES WIN32)
  else()
    set(PLATFORM_GUI_TARGET_PROPERTIES "")
  endif()

  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(PLATFORM_WALLET_SOURCE lumex/lumex_wallet/plat/default/icon.cpp)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(PLATFORM_WALLET_SOURCE lumex/lumex_wallet/plat/windows/icon.cpp Lumex.rc)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(PLATFORM_WALLET_SOURCE lumex/lumex_wallet/plat/default/icon.cpp)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    set(PLATFORM_WALLET_SOURCE lumex/lumex_wallet/plat/default/icon.cpp)
  else()
    error("Unknown platform: ${CMAKE_SYSTEM_NAME}")
  endif()

  qt5_add_resources(RES resources.qrc)

  add_executable(
    lumex_wallet ${PLATFORM_GUI_TARGET_PROPERTIES} ${PLATFORM_WALLET_SOURCE}
                lumex/lumex_wallet/entry.cpp lumex/lumex_wallet/icon.hpp ${RES})

  target_link_libraries(lumex_wallet Boost::process rpc node qt)

  if(WIN32)
    target_link_libraries(lumex_wallet Qt5::WinExtras)
  endif()

  set_target_properties(
    lumex_wallet PROPERTIES OUTPUT_NAME lumex_wallet
                           COMPILE_FLAGS
                           "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

  if(LUMEX_TEST OR RAIBLOCKS_TEST)
    add_executable(qt_test lumex/qt_test/entry.cpp lumex/qt_test/qt.cpp)

    target_link_libraries(
      qt_test
      node
      secure
      test_common
      gtest
      gtest_main
      qt
      Qt5::Test)

    set_target_properties(
      qt_test PROPERTIES COMPILE_FLAGS
                         "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")
    add_dependencies(all_tests qt_test)
  endif()

  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    configure_file(${CMAKE_SOURCE_DIR}/Info.plist.in
                   ${CMAKE_SOURCE_DIR}/Info.plist @ONLY)
    install(TARGETS lumex_wallet
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/MacOS)
    install(TARGETS lumex_node
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/MacOS)
    install(TARGETS lumex_rpc
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/MacOS)
    install(FILES Info.plist DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents)
    install(FILES qt.conf
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Resources)
    install(DIRECTORY ${Qt5_DIR}/../../QtCore.framework
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtDBus.framework
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtGui.framework
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtPrintSupport.framework
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtTest.framework
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtWidgets.framework
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(FILES "${Qt5_DIR}/../../../plugins/platforms/libqcocoa.dylib"
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/PlugIns/platforms)
    install(FILES Lumex.icns
            DESTINATION ${LUMEX_OSX_PACKAGE_NAME}.app/Contents/Resources)
  elseif(WIN32)
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
      set(WIN_REDIST vc_redist.x64.exe)
    else()
      set(WIN_REDIST vc_redist.x86.exe)
    endif()
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "ExecWait '\\\"$INSTDIR\\\\${WIN_REDIST}\\\" /quiet /norestart'
												WriteRegDWORD HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Lumex\\\\Lumex${LUMEX_PREFIX}' 'TypesSupported' '0x7'
												WriteRegStr HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Lumex\\\\Lumex${LUMEX_PREFIX}' 'EventMessageFile' '$INSTDIR\\\\lumex_wallet.exe'"
    )
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegKey HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Lumex${LUMEX_PREFIX}'"
    )
    set(CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/Lumex.ico)
    set(CPACK_NSIS_DISPLAY_NAME "Lumex${LUMEX_PREFIX}")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://lumex.org")
    set(CPACK_NSIS_CONTACT "info@lumex.org")
    set(CPACK_NSIS_MENU_LINKS "lumex_wallet.exe" "Lumex Wallet"
                              "https://lumex.org" "Lumex website")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    get_target_property(Qt5WindowsPlugin Qt5::QWindowsIntegrationPlugin
                        LOCATION)
    get_filename_component(Qt5_bin_DIR ${Qt5_DIR}/../../../bin ABSOLUTE)
    install(TARGETS lumex_wallet DESTINATION .)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${WIN_REDIST} DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/libGLESv2.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5Core.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5Gui.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5Widgets.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5WinExtras.dll DESTINATION .)
    install(FILES ${Qt5WindowsPlugin} DESTINATION platforms)
  else()
    set(CPACK_GENERATOR "TBZ2;DEB")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qt5-default | qtbase5-dev, qtchooser, qt5-qmake, qtbase5-dev-tools")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "russel@lumex.org")
    install(TARGETS lumex_wallet RUNTIME DESTINATION ./bin)
    set(DEBIAN_POSTINST postinst.in)
    set(DEBIAN_POSTRM postrm.in)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/debian-control/${DEBIAN_POSTINST}
                   ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postinst)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/debian-control/${DEBIAN_POSTRM}
                   ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postrm)
    file(
      COPY ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postinst
           ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postrm
      DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/debian-control/.
      FILE_PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE)
    install(FILES etc/systemd/${LUMEX_SERVICE} DESTINATION ./extras/systemd/.)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_BINARY_DIR}/debian-control/postinst;${CMAKE_CURRENT_BINARY_DIR}/debian-control/postrm"
    )
  endif()
endif()

if(COVERAGE)
  add_subdirectory(coverage)
endif()

set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
include(CPack)
