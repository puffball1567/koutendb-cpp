cmake_minimum_required(VERSION 3.16)
project(koutendb_cpp VERSION 0.1.3 LANGUAGES CXX)

add_library(koutendb_cpp INTERFACE)
add_library(KoutenDB::cpp ALIAS koutendb_cpp)

target_compile_features(koutendb_cpp INTERFACE cxx_std_17)
target_include_directories(
  koutendb_cpp
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(KOUTENDB_CPP_BUILD_EXAMPLES "Build KoutenDB C++ examples" ON)
option(KOUTENDB_CPP_BUILD_TCP "Build the native TCP client (no KoutenDB shared library)" OFF)
option(KOUTENDB_CPP_FETCH_DEPS "Download pinned Asio/JSON dependencies if missing" OFF)
option(KOUTENDB_CPP_TCP_TESTS "Build native TCP conformance adapter and tests" OFF)

if(KOUTENDB_CPP_BUILD_TCP)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  find_path(ASIO_INCLUDE_DIR asio.hpp)
  find_package(nlohmann_json 3.10 QUIET)
  if(KOUTENDB_CPP_FETCH_DEPS)
    include(FetchContent)
    if(NOT ASIO_INCLUDE_DIR)
      FetchContent_Declare(asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG 42a93679dc4c8c5caf3d3082542f1bfa2438271d)
      FetchContent_MakeAvailable(asio)
      set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include")
    endif()
    if(NOT nlohmann_json_FOUND)
      FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b)
      FetchContent_MakeAvailable(json)
    endif()
  endif()
  if(NOT ASIO_INCLUDE_DIR OR NOT TARGET nlohmann_json::nlohmann_json)
    message(FATAL_ERROR "Install Asio and nlohmann_json, or enable KOUTENDB_CPP_FETCH_DEPS")
  endif()
  add_library(koutendb_tcp STATIC src/tcp.cpp)
  add_library(KoutenDB::tcp ALIAS koutendb_tcp)
  target_compile_features(koutendb_tcp PUBLIC cxx_std_17)
  target_compile_definitions(koutendb_tcp PRIVATE ASIO_STANDALONE)
  target_include_directories(koutendb_tcp PUBLIC include PRIVATE "${ASIO_INCLUDE_DIR}")
  target_link_libraries(koutendb_tcp PUBLIC nlohmann_json::nlohmann_json
    PRIVATE OpenSSL::SSL OpenSSL::Crypto PkgConfig::SODIUM Threads::Threads)
  if(KOUTENDB_CPP_TCP_TESTS)
    enable_testing()
    add_executable(koutendb_tcp_adapter tests/tcp_adapter.cpp)
    target_link_libraries(koutendb_tcp_adapter PRIVATE KoutenDB::tcp PkgConfig::SODIUM)
    add_executable(koutendb_tcp_unit tests/tcp_unit.cpp)
    target_link_libraries(koutendb_tcp_unit PRIVATE KoutenDB::tcp)
    add_test(NAME tcp_unit COMMAND koutendb_tcp_unit)
  endif()
endif()

set(KOUTENDB_CORE_DIR "" CACHE PATH "Path to a KoutenDB core checkout containing lib/libkoutendb.so")
set(KOUTENDB_LIBRARY "" CACHE FILEPATH "Path to libkoutendb.so")

if(KOUTENDB_LIBRARY)
  set(KOUTENDB_C_LIBRARY "${KOUTENDB_LIBRARY}")
elseif(KOUTENDB_CORE_DIR)
  set(KOUTENDB_C_LIBRARY "${KOUTENDB_CORE_DIR}/lib/libkoutendb.so")
elseif(DEFINED ENV{KOUTENDB_LIBRARY})
  set(KOUTENDB_C_LIBRARY "$ENV{KOUTENDB_LIBRARY}")
elseif(DEFINED ENV{KOUTENDB_CORE_DIR})
  set(KOUTENDB_C_LIBRARY "$ENV{KOUTENDB_CORE_DIR}/lib/libkoutendb.so")
else()
  find_library(KOUTENDB_C_LIBRARY NAMES koutendb libkoutendb)
endif()

if(KOUTENDB_CPP_BUILD_EXAMPLES)
  if(NOT KOUTENDB_C_LIBRARY)
    message(FATAL_ERROR "libkoutendb.so was not found. Set KOUTENDB_CORE_DIR or KOUTENDB_LIBRARY.")
  endif()

  add_executable(koutendb_cpp_contract_smoke examples/contract_smoke.cpp)
  target_link_libraries(koutendb_cpp_contract_smoke PRIVATE koutendb_cpp "${KOUTENDB_C_LIBRARY}")

  add_executable(koutendb_cpp_cluster_tls examples/cluster_tls.cpp)
  target_link_libraries(koutendb_cpp_cluster_tls PRIVATE koutendb_cpp "${KOUTENDB_C_LIBRARY}")

  add_executable(koutendb_cpp_sec_probe examples/sec_probe.cpp)
  target_link_libraries(koutendb_cpp_sec_probe PRIVATE koutendb_cpp "${KOUTENDB_C_LIBRARY}")

  get_filename_component(KOUTENDB_C_LIBRARY_DIR "${KOUTENDB_C_LIBRARY}" DIRECTORY)
  set_target_properties(
    koutendb_cpp_contract_smoke
    koutendb_cpp_cluster_tls
    koutendb_cpp_sec_probe
    PROPERTIES
      BUILD_RPATH "${KOUTENDB_C_LIBRARY_DIR}"
  )
endif()
