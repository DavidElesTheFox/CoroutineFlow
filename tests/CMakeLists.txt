include(FetchContent)

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
)

add_test(
    NAME ${arg_TEST_NAME} 
    COMMAND ${arg_TEST_NAME}
)
endfunction()

FetchContent_Declare(
    Catch2Test
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
)

FetchContent_MakeAvailable(Catch2Test)

add_testcase(
    TEST_NAME unit.tag_invoke
    SOURCES unit/tag_invoke.cpp
)