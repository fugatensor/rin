cmake_minimum_required(VERSION 3.16)
project(rin VERSION 0.3.0 LANGUAGES CXX)
include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_rin_top_level ON)
else()
    set(_rin_top_level OFF)
endif()
option(RIN_BUILD_SHARED "Build the optional C ABI shared library" OFF)
option(RIN_BUILD_CLI "Build the rin command line tool" ${_rin_top_level})
option(RIN_BUILD_TESTS "Build and run Rin tests" ${_rin_top_level})
option(RIN_INSTALL "Install Rin targets and headers" ${_rin_top_level})

add_library(rin_pass INTERFACE)
add_library(Rin::Pass ALIAS rin_pass)
set_target_properties(rin_pass PROPERTIES EXPORT_NAME Pass)
target_compile_features(rin_pass INTERFACE cxx_std_17)
target_include_directories(rin_pass INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(RIN_BUILD_SHARED)
    add_library(rin_shared SHARED src/rin.cpp)
    set_target_properties(rin_shared PROPERTIES OUTPUT_NAME rin EXPORT_NAME Core)
    target_compile_features(rin_shared PUBLIC cxx_std_17)
    target_compile_definitions(rin_shared PRIVATE RIN_BUILDING_LIBRARY)
    target_include_directories(rin_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()
if(RIN_BUILD_CLI)
    add_executable(rin main.cpp)
    target_link_libraries(rin PRIVATE Rin::Pass)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(RIN_BUILD_SHARED)
        target_compile_options(rin_shared PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(RIN_BUILD_CLI)
        target_compile_options(rin PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(RIN_INSTALL)
    include(CMakePackageConfigHelpers)
    configure_package_config_file(cmake/RinConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/RinConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Rin")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/RinConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(TARGETS rin_pass EXPORT RinTargets)
    if(RIN_BUILD_SHARED)
        install(TARGETS rin_shared EXPORT RinTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
    if(RIN_BUILD_CLI)
        install(TARGETS rin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(FILES include/rin.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES include/rin/pass.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rin)
    install(EXPORT RinTargets NAMESPACE Rin::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Rin)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/RinConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/RinConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Rin)
endif()

if(RIN_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(rin_pass_test tests/cpp/pass.cpp tests/cpp/header_link.cpp)
    target_compile_definitions(rin_pass_test PRIVATE
        RIN_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pass")
    target_link_libraries(rin_pass_test PRIVATE Rin::Pass Threads::Threads)
    add_test(NAME rin-pass COMMAND rin_pass_test)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        if(RIN_BUILD_CLI)
            add_test(NAME prover COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prover.py $<TARGET_FILE:rin> -v)
            add_test(NAME cli COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:rin> -v)
        endif()
        if(RIN_BUILD_SHARED)
            add_test(NAME ffi COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ffi.py $<TARGET_FILE:rin_shared> -v)
        endif()
    endif()
endif()
