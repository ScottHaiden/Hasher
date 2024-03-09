cmake_minimum_required(VERSION 3.28.3)

project(Hasher)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
pkg_search_module(CRYPTO libcrypto REQUIRED)

add_executable(hasher hasher.cc)
target_link_libraries(hasher ${CRYPTO_LIBRARIES})

add_library(common common.cc)
target_link_libraries(hasher common)

add_library(file file.cc)
target_link_libraries(hasher file)

add_library(platform platform.cc)
target_link_libraries(hasher platform)

add_library(utils utils.cc)
target_link_libraries(hasher utils)
