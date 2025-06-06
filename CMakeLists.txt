cmake_minimum_required(VERSION 3.18.4)

project(Hasher)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-O3 -fpic -pie -flto ${CMAKE_CXX_FLAGS}")

find_package(PkgConfig REQUIRED)
pkg_search_module(CRYPTO libcrypto REQUIRED)
pkg_search_module(FTS musl-fts OPTIONAL)

add_executable(hasher hasher.cc)
target_link_libraries(hasher ${CRYPTO_LIBRARIES})
target_link_libraries(hasher ${FTS_LIBRARIES})
target_link_libraries(hasher pthread)

add_library(common OBJECT common.cc)
target_link_libraries(hasher common)

add_library(file OBJECT file.cc)
target_link_libraries(hasher file)

add_library(platform OBJECT platform.cc)
target_link_libraries(hasher platform)

add_library(utils OBJECT utils.cc)
target_link_libraries(hasher utils)

install(TARGETS hasher DESTINATION bin)
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink hasher ${CMAKE_INSTALL_PREFIX}/bin/checker)")
