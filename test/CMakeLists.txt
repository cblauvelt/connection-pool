include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

set(ERROR_TEST "error_test")
add_executable(${ERROR_TEST}
        "src/error_test.cpp"
)
target_include_directories(${ERROR_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${ERROR_TEST} ${CONAN_LIBS})
add_test(NAME ${ERROR_TEST} COMMAND $<TARGET_FILE:${ERROR_TEST}>)

set(TIMER_TEST "timer_test")
add_executable(${TIMER_TEST}
        "src/timer_test.cpp"
)
target_include_directories(${TIMER_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${TIMER_TEST} ${CONAN_LIBS})
add_test(NAME ${TIMER_TEST} COMMAND $<TARGET_FILE:${TIMER_TEST}>)

set(CLIENT_TEST "client_test")
add_executable(${CLIENT_TEST}
        "src/client_test.cpp"
        "src/echo_server.cpp"
        "src/slow_echo_server.cpp"
)
target_include_directories(${CLIENT_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${CLIENT_TEST} ${CONAN_LIBS})
add_test(NAME ${CLIENT_TEST} COMMAND $<TARGET_FILE:${CLIENT_TEST}>)