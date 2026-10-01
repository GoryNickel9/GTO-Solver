cmake_minimum_required(VERSION 3.25)
project(gtosd_install_consumer LANGUAGES CXX)

find_package(gtosd 0.9 CONFIG REQUIRED)

add_executable(gtosd_install_consumer main.cpp)
target_link_libraries(gtosd_install_consumer PRIVATE gtosd::storage)
target_compile_features(gtosd_install_consumer PRIVATE cxx_std_20)
