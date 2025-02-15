cmake_minimum_required(VERSION 3.16)

project(RateLimitServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -ggdb -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -Wno-unused")

include(FetchContent)

FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG  tags/v3.12.0
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(simdjson)

find_package(Protobuf REQUIRED)
file(GLOB_RECURSE PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/gen)
execute_process(
    COMMAND protoc --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}/gen --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto ${PROTO_FILES}
)

file(GLOB_RECURSE PROTO_SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/gen/*.cc")
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HDR_FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_executable(${PROJECT_NAME} main.cpp ${PROTO_SRC_FILES} ${SRC_FILES})
target_link_libraries(${PROJECT_NAME} brpc protobuf gflags pthread simdjson)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gen ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Example
add_executable(RateLimitClient example/ratelimit_client.cpp ${PROTO_SRC_FILES})
target_link_libraries(RateLimitClient brpc protobuf gflags pthread)
target_include_directories(RateLimitClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gen)

#
# Clang-Format
#
find_program(CLANG_FORMAT_EXECUTABLE
    NAME "clang-format-12"
    PATHS "/usr/bin" "/usr/local/bin"
)

if (CLANG_FORMAT_EXECUTABLE)
    message(STATUS "Successfully find program `clang-format-12`")
    message(STATUS "You can use the `make clang-format` command to automatically format the code style")
    add_custom_target(clang-format
    COMMAND
        ${CLANG_FORMAT_EXECUTABLE} --style=file -i ${HDR_FILES};${SRC_FILES};${PROJECT_SOURCE_DIR}/main.cpp
    COMMENT
        "Automatically format the code style"
    )
endif()