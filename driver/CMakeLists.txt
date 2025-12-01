set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 14)

include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

## launcher lib
add_subdirectory(launcher)

## simple driver for testing out-of-process solving
add_executable(FGTest fgtest.cpp)
set_target_properties(FGTest PROPERTIES OUTPUT_NAME "fgtest")
target_include_directories(FGTest PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
)
target_link_libraries(FGTest PRIVATE
  launcher
  z3parser
  z3
  rt
  nlohmann_json::nlohmann_json
)
install (TARGETS FGTest DESTINATION ${SYMSAN_BIN_DIR})

if (DEFINED AFLPP_PATH)
    add_subdirectory(aflpp)
endif()

install (CODE "MESSAGE(STATUS \"Build & Install: libSymsanProxy.o\")")
install (CODE "execute_process(COMMAND \
    ${CMAKE_INSTALL_PREFIX}/${SYMSAN_BIN_DIR}/ko-clang \
    -c ${CMAKE_CURRENT_SOURCE_DIR}/harness-proxy.c \
    -o ${CMAKE_INSTALL_PREFIX}/${SYMSAN_LIB_DIR}/libSymsanProxy.o)")
