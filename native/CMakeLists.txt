cmake_minimum_required(VERSION 3.15)
project(taskscheduler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pthread -fPIC")

find_package(JNI REQUIRED)

include_directories(${JNI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SOURCES
    src/scheduler_shm.c
    src/process_mgmt.c
    src/ipc_sync.c
    src/jni_bridge.c
    src/scheduler_core.c
    src/scheduler_loop.c
    src/deadlock_detector.c
)

add_library(taskscheduler SHARED ${SOURCES})

target_link_libraries(taskscheduler PRIVATE rt pthread)

set_target_properties(taskscheduler PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)