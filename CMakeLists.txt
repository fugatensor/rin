cmake_minimum_required(VERSION 3.16)
project(suka VERSION 0.3.0 LANGUAGES CXX)
include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_suka_top_level ON)
else()
    set(_suka_top_level OFF)
endif()
option(SUKA_BUILD_SHARED "Build the C ABI shared library" ${_suka_top_level})
option(SUKA_BUILD_CLI "Build the suka command line tool" ${_suka_top_level})
option(SUKA_BUILD_TESTS "Build and run Suka tests" ${_suka_top_level})
option(SUKA_INSTALL "Install Suka targets and headers" ${_suka_top_level})
if(SUKA_BUILD_CLI AND NOT SUKA_BUILD_SHARED)
    message(FATAL_ERROR "SUKA_BUILD_CLI requires SUKA_BUILD_SHARED")
endif()

add_library(suka_pass STATIC src/pass.cpp src/suka.cpp)
add_library(Suka::Pass ALIAS suka_pass)
set_target_properties(suka_pass PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Pass)
target_compile_features(suka_pass PUBLIC cxx_std_17)
target_compile_definitions(suka_pass PRIVATE SUKA_BUILDING_LIBRARY)
target_include_directories(suka_pass PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(SUKA_BUILD_SHARED)
    add_library(suka_shared SHARED src/suka.cpp)
    set_target_properties(suka_shared PROPERTIES OUTPUT_NAME suka EXPORT_NAME Core)
    target_compile_features(suka_shared PUBLIC cxx_std_17)
    target_compile_definitions(suka_shared PRIVATE SUKA_BUILDING_LIBRARY)
    target_include_directories(suka_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()
if(SUKA_BUILD_CLI)
    add_executable(suka main.cpp)
    target_link_libraries(suka PRIVATE suka_shared)
    set_target_properties(suka PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(suka_pass PRIVATE -Wall -Wextra -Wpedantic)
    if(SUKA_BUILD_SHARED)
        target_compile_options(suka_shared PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(SUKA_BUILD_CLI)
        target_compile_options(suka PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(SUKA_INSTALL)
    include(CMakePackageConfigHelpers)
    configure_package_config_file(cmake/SukaConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/SukaConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Suka")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SukaConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(TARGETS suka_pass EXPORT SukaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    if(SUKA_BUILD_SHARED)
        install(TARGETS suka_shared EXPORT SukaTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
    if(SUKA_BUILD_CLI)
        install(TARGETS suka RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(FILES include/suka.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES include/suka/pass.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/suka)
    install(EXPORT SukaTargets NAMESPACE Suka::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Suka)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SukaConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/SukaConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Suka)
endif()

if(SUKA_BUILD_TESTS)
    enable_testing()
    add_executable(suka_pass_test tests/cpp/pass.cpp)
    target_link_libraries(suka_pass_test PRIVATE Suka::Pass)
    add_test(NAME suka-pass COMMAND suka_pass_test)
    if(SUKA_BUILD_CLI)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME prover COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prover.py $<TARGET_FILE:suka> -v)
        endif()
    endif()
endif()
