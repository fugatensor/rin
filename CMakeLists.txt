cmake_minimum_required(VERSION 3.16)
project(suka VERSION 0.2.0 LANGUAGES CXX)
include(GNUInstallDirs)

add_library(suka_shared SHARED src/suka.cpp)
set_target_properties(suka_shared PROPERTIES OUTPUT_NAME suka)
target_compile_features(suka_shared PUBLIC cxx_std_17)
target_compile_definitions(suka_shared PRIVATE SUKA_BUILDING_LIBRARY)
target_include_directories(suka_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_executable(suka main.cpp)
target_link_libraries(suka PRIVATE suka_shared)
set_target_properties(suka PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(suka_shared PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(suka PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS suka suka_shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/suka.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME prover COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prover.py $<TARGET_FILE:suka> -v)
    endif()
endif()
