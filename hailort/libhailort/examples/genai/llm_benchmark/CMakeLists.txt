cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai-llm-benchmark)

find_package(HailoRT 5 REQUIRED)

add_executable(llm_benchmark llm_benchmark.cpp)
target_link_libraries(llm_benchmark PRIVATE HailoRT::libhailort)
set_target_properties(llm_benchmark PROPERTIES CXX_STANDARD 17)
