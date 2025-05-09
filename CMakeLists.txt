cmake_minimum_required(VERSION 3.28)

set(CF_VERSION_MAJOR 1)
set(CF_VERSION_MINOR 0)
set(CF_VERSION_PATCH 0)
set(CF_PROJECT_VERSION ${CF_VERSION_MAJOR}.${CF_VERSION_MINOR}.${CF_VERSION_PATCH})

project(CoroutineFlow
    VERSION ${CF_PROJECT_VERSION}
    LANGUAGES CXX)

option(CF_BUILD_TESTS OFF)
option(CF_BUILD_EXAMPLES OFF)

set(CF_LIB_NAME coroutine_flow)

add_library(${CF_LIB_NAME} INTERFACE)
add_library(coroutine_flow::${CF_LIB_NAME} ALIAS ${CF_LIB_NAME})

target_include_directories(
    ${CF_LIB_NAME}
    INTERFACE 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(
    ${CF_LIB_NAME}
    INTERFACE
        cxx_std_23
)


if(CF_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

install(
    TARGETS ${CF_LIB_NAME} 
    EXPORT coroutineFlowTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/coroutine_flow
    DESTINATION include/coroutine_flow
)

install(
    EXPORT coroutineFlowTargets
    FILE coroutineFlowTargets.cmake
    NAMESPACE coroutine_flow::
    DESTINATION  ${CMAKE_INSTALL_LIBDIR}/cmake/coroutine_flow
)


include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "coroutineFlowConfigVersion.cmake"
    VERSION ${CF_PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/coroutineFlowConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/coroutineFlowConfig.cmake"
    INSTALL_DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/coroutine_flow
)

install(
    FILES 
        ${PROJECT_BINARY_DIR}/coroutineFlowConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/coroutineFlowConfigVersion.cmake 
    DESTINATION 
        ${CMAKE_INSTALL_LIBDIR}/cmake/coroutineFlow
)


export(TARGETS ${CF_LIB_NAME} NAMESPACE coroutine_flow:: FILE coroutineFlowTargets.cmake)