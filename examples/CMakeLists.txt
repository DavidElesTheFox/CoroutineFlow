
add_executable(example.stdthread)

target_sources(example.stdthread 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/stdthread/main.cpp
)

target_link_libraries(example.stdthread 
    PUBLIC
        coroutine_flow::coroutine_flow
)