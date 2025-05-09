
function(add_example)
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
    )
endfunction()

add_example(
    TEST_NAME example.stdthread
    SOURCES stdthread/main.cpp
)

add_example(
    TEST_NAME example.tag_invoke
    SOURCES tag_invoke/main.cpp
)