cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_COMPILER g++)
project(page_loader VERSION 0.0.1 LANGUAGES CXX)

include_directories(
    src/
)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    anl
    rt
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)