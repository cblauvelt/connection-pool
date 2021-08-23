include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

set(CLIENT_TEST "client_test")
add_executable(${CLIENT_TEST}
        "src/client_test.cpp"
        "src/echo_server.cpp"
)
target_include_directories(${CLIENT_TEST} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${CLIENT_TEST} ${CONAN_LIBS})
add_test(NAME ${CLIENT_TEST} COMMAND $<TARGET_FILE:${CLIENT_TEST}>)