include(FetchContent)
FetchContent_Declare(
    Catch2Test
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
)

FetchContent_MakeAvailable(Catch2Test)
include(Catch)

function(add_testcase)
set(options "")
set(oneValueArgs TEST_NAME)
set(multiValueArgs SOURCES)

cmake_parse_arguments(PARSE_ARGV 0 arg
    "${options}" "${oneValueArgs}" "${multiValueArgs}"
)

add_executable(${arg_TEST_NAME})

target_sources(${arg_TEST_NAME} 
    PUBLIC
        "${arg_SOURCES}"
)

target_link_libraries(${arg_TEST_NAME}
    PUBLIC
        coroutine_flow::coroutine_flow
        Catch2::Catch2WithMain
        stdc++_libbacktrace
)
target_include_directories(${arg_TEST_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_options(${arg_TEST_NAME}
    PUBLIC
       ${CF_LINK_OPTIONS}
)
target_compile_options(${arg_TEST_NAME}
    PUBLIC
        ${CF_COMPILE_OPTIONS}
)
catch_discover_tests(${arg_TEST_NAME}
    PROPERTIES
        ENVIRONMENT "LLVM_PROFILE_FILE=coverage-%m-%p.profraw"
)

add_test(
    NAME ${arg_TEST_NAME} 
    COMMAND ${arg_TEST_NAME}
)
endfunction()


add_testcase(
    TEST_NAME unit.tag_invoke
    SOURCES unit/tag_invoke.cpp
)
add_testcase(
    TEST_NAME unit.execution
    SOURCES unit/execution.cpp
)
add_testcase(
    TEST_NAME unit.exception
    SOURCES unit/exception.cpp
)
add_testcase(
    TEST_NAME unit.memory
    SOURCES unit/memory.cpp
)
add_testcase(
    TEST_NAME unit.return_type
    SOURCES unit/return_type.cpp
)
add_testcase(
    TEST_NAME functional.tag_invoke
    SOURCES functional/tag_invoke.cpp
)
add_testcase(
    TEST_NAME functional.task_with_result
    SOURCES functional/task_with_result.cpp
)
add_testcase(
    TEST_NAME functional.execution_flow_controller
    SOURCES functional/execution_flow_controller.cpp
)