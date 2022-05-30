cmake_minimum_required(VERSION 3.12)
project(cpool)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 20)
include(CheckCXXCompilerFlag)
include(conan.cmake)

# include source directories
include_directories(
    "${CMAKE_SOURCE_DIR}/"
)

check_cxx_compiler_flag(-std=c++20 HAVE_FLAG_STD_CXX20)

if(HAVE_FLAG_STD_CXX20)

    set( THREAD_CXX_FLAGS "-pthread")
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines-ts")
        message(FATAL_ERROR "Clang coroutines is not supported in asio. Please add support")
    endif()

    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${THREAD_CXX_FLAGS}" )

    if(NOT CMAKE_INSTALL_INCLUDEDIR)
        set( CMAKE_INSTALL_INCLUDEDIR "/usr/include" )
    endif()

    if(NOT CMAKE_INSTALL_LIBDIR)
        set( CMAKE_INSTALL_LIBDIR "/usr/lib" )
    endif()

else()
    message(FATAL_ERROR "This library requires C++20")
endif()

# integrate conan build process
if(NOT EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    conan_cmake_run(CONANFILE conanfile.py
                    BASIC_SETUP CMAKE_TARGETS
                    BUILD missing)

    conan_cmake_autodetect(settings)

endif()

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    conan_basic_setup()
else()
    message(FATAL_ERROR "The file conanbuildinfo.cmake could not be generated")
endif()


# install headers
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cpool/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cpool"
)

# create and configure the example targets
option(BUILD_EXAMPLES "Build examples" OFF)
message("-- Examples is ${BUILD_EXAMPLES}")
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif(BUILD_EXAMPLES)


# create and configure the test targets
option(BUILD_TESTING "Build Unit Tests" ON)
message("-- Testing is ${BUILD_TESTING}")
if(BUILD_TESTING)
    if(CPOOL_TRACE_LOGGING)
        add_compile_definitions(CPOOL_TRACE_LOGGING)
    endif(CPOOL_TRACE_LOGGING)
    
	enable_testing()
    add_subdirectory(test)
endif(BUILD_TESTING)
