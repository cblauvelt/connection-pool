include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)


# include source directories
include_directories(
        "${CMAKE_SOURCE_DIR}/"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set(CLIENT_EX "client_example")
add_executable(${CLIENT_EX}
        "client/client.cpp"
)
target_include_directories(${CLIENT_EX} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${CLIENT_EX} ${CONAN_LIBS})