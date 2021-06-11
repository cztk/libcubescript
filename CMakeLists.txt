cmake_minimum_required(VERSION 3.19)
project(libcubescript)

set(CMAKE_CXX_STANDARD 20)
option(RELEASE_BUILD "Enable release build" OFF) #OFF by default

if (RELEASE_BUILD)
    SET(GCC_COVERAGE_COMPILE_FLAGS "-D_REENTRANT -fno-rtti -fno-exceptions -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -fomit-frame-pointer -Wall -O2 -ftree-loop-vectorize -flto")
    SET(GCC_COVERAGE_LINK_FLAGS "-pthread -flto -lSDL2")
else ()
    SET(GCC_COVERAGE_COMPILE_FLAGS "-D_REENTRANT -fno-rtti -fexceptions -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wall -O0 -g3 -D_FORTIFY_SOURCE=1 -fasynchronous-unwind-tables -Werror=format-security -DDEBUG=1 -D_DEBUG=1 ")
    SET(GCC_COVERAGE_LINK_FLAGS "-pthread -lSDL2 ")
endif (RELEASE_BUILD)

file(GLOB SRC_FILES src/*.cc)
add_library(libcubescript ${SRC_FILES})
target_include_directories(libcubescript PUBLIC "include")