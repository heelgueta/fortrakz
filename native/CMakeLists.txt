cmake_minimum_required(VERSION 3.22)
project(fortrakz CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(oboe REQUIRED CONFIG)

add_library(fortrakz SHARED
  core/fft.cpp core/dsp.cpp core/studio.cpp core/lab.cpp
  android/duplex.cpp android/engine.cpp android/jni.cpp)
target_include_directories(fortrakz PRIVATE core android)
target_link_libraries(fortrakz oboe::oboe log android)
