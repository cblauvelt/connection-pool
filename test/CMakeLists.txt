include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

set(ERROR_TEST "error_test")
add_executable(${ERROR_TEST}
        "src/error_test.cpp"
)
target_include_directories(${ERROR_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${ERROR_TEST} ${CONAN_LIBS})
add_test(NAME ${ERROR_TEST} COMMAND $<TARGET_FILE:${ERROR_TEST}>)

set(BACK_OFF_TEST "back_off_test")
add_executable(${BACK_OFF_TEST}
        "src/back_off_test.cpp"
)
target_include_directories(${BACK_OFF_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${BACK_OFF_TEST} ${CONAN_LIBS})
add_test(NAME ${BACK_OFF_TEST} COMMAND $<TARGET_FILE:${BACK_OFF_TEST}>)

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

set(SSL_CLIENT_TEST "ssl_client_test")
add_executable(${SSL_CLIENT_TEST}
        "src/ssl_client_test.cpp"
        "src/ssl_echo_server.cpp"
        "src/slow_echo_server.cpp"
)
target_include_directories(${SSL_CLIENT_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${SSL_CLIENT_TEST} ${CONAN_LIBS})
add_test(NAME ${SSL_CLIENT_TEST} COMMAND $<TARGET_FILE:${SSL_CLIENT_TEST}>)

set(CONNECTION_POOL_TEST "connection_pool_test")
add_executable(${CONNECTION_POOL_TEST}
        "src/connection_pool_test.cpp"
        "src/test_connection.cpp"
        "src/echo_server.cpp"
)
target_include_directories(${CONNECTION_POOL_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${CONNECTION_POOL_TEST} ${CONAN_LIBS})
add_test(NAME ${CONNECTION_POOL_TEST} COMMAND $<TARGET_FILE:${CONNECTION_POOL_TEST}>)