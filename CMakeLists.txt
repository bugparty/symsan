cmake_minimum_required(VERSION 3.5)

project(symsan VERSION 1.2.2 LANGUAGES C CXX ASM)

find_package(LLVM 14 REQUIRED CONFIG)

option(SYMSAN_CTWM_ENABLE_INDEX "Enable CTWM index collection" ON)
option(SYMSAN_CTWM_ENABLE_BB_TRACE
       "Enable CTWM basic block trace instrumentation" OFF)

if (LLVM_FOUND)
    message(STATUS "LLVM_VERSION_MAJOR: ${LLVM_VERSION_MAJOR}")
    message(STATUS "LLVM_VERSION_MINOR: ${LLVM_VERSION_MINOR}")
    message(STATUS "LLVM_VERSION_PATCH: ${LLVM_VERSION_PATCH}")
else()
    message(FATAL_ERROR "You haven't install LLVM !")
endif()

if (NOT TARGET LLVMPassConfig)
    add_library(LLVMPassConfig INTERFACE IMPORTED)
    set_target_properties(LLVMPassConfig PROPERTIES
        INTERFACE_COMPILE_OPTIONS "-fno-rtti" #-fpic
        INTERFACE_INCLUDE_DIRECTORIES "${LLVM_INCLUDE_DIRS}"
        INTERFACE_LINK_DIRECTORIES "${LLVM_LIBRARY_DIRS}"
        INTERFACE_COMPILE_DEFINITIONS "LLVM_VERSION_MAJOR=${LLVM_VERSION_MAJOR};LLVM_VERSION_MINOR=${LLVM_VERSION_MINOR};"
        # INTERFACE_LINK_OPTIONS "-Wl,-znodelete"
    )
endif()

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if (SYMSAN_CTWM_ENABLE_INDEX)
    add_definitions(-DSYMSAN_CTWM_ENABLE_INDEX=1)
else()
    add_definitions(-DSYMSAN_CTWM_ENABLE_INDEX=0)
endif()

if (SYMSAN_CTWM_ENABLE_BB_TRACE)
    add_definitions(-DSYMSAN_CTWM_ENABLE_BB_TRACE=1)
else()
    add_definitions(-DSYMSAN_CTWM_ENABLE_BB_TRACE=0)
endif()

include_directories(include)

set(SYMSAN_BIN_DIR "bin")
set(SYMSAN_LIB_DIR "lib/symsan")

add_subdirectory(compiler)
add_subdirectory(instrumentation)
add_subdirectory(runtime)
add_subdirectory(wrappers)
add_subdirectory(parsers)
add_subdirectory(solvers)
add_subdirectory(backend)
add_subdirectory(driver)
add_subdirectory(tests)
add_subdirectory(libcxx)
add_subdirectory(python)
