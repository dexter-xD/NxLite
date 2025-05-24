cmake_minimum_required(VERSION 3.10)
project(NxLite)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# compiler flags
add_compile_options(-Wall -Wextra -Werror)
add_definitions(-D_GNU_SOURCE)  # GNU extensions for timerfd, epoll, etc.

# flags for high concurrency
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native)
    add_definitions(-DNDEBUG)
endif()

# Find zlib for compression
find_package(ZLIB REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})

# include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# source files
set(SOURCES
    src/main.c
    src/master.c
    src/worker.c
    src/http.c
    src/config.c
    src/log.c
    src/server.c
    src/mempool.c
    src/shutdown.c
)

# executable
add_executable(NxLite ${SOURCES})

target_link_libraries(NxLite pthread rt ${ZLIB_LIBRARIES})  # rt for timerfd, zlib for compression

# installation paths
install(TARGETS NxLite DESTINATION bin)
install(FILES ${HEADERS} DESTINATION include/NxLite)
install(DIRECTORY config/ DESTINATION etc/NxLite)
install(DIRECTORY static/ DESTINATION share/NxLite)

# custom target for running the server
add_custom_target(run
    COMMAND ${CMAKE_BINARY_DIR}/NxLite
    DEPENDS NxLite
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
) 
