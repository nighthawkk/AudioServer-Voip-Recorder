cmake_minimum_required(VERSION 3.22.1)
project("voip")

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O2 -fPIC -fno-rtti -fno-exceptions")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wall -O2 -fPIC")


include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(libdobby STATIC IMPORTED)
set_target_properties(libdobby PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}/libdobby.a"
)

add_library(audiohook SHARED
        audioserver_hook_dobby.cpp
        get_lib_address.c
)

target_link_libraries(audiohook
        libdobby
        log
        dl
)
