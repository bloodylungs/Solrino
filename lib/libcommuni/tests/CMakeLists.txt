project(libcommuni-test LANGUAGES CXX)

if(MAJOR_QT_VERSION LESS "6")
    message(SEND_ERROR "Building tests requires at least Qt6.")
endif()

find_package(Qt${MAJOR_QT_VERSION} REQUIRED COMPONENTS Test)

set(CMAKE_AUTOMOC ON)

set(LIBCOMMUNI_TEST_COMMON_LIBS
    Qt${MAJOR_QT_VERSION}::Core
    Qt${MAJOR_QT_VERSION}::Network
    Qt${MAJOR_QT_VERSION}::Test
    LibCommuni::LibCommuni
)

function(libcommuni_add_test arg_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES")

    set(test_name "tst_${arg_NAME}")
    qt_add_executable("${test_name}" ${arg_SOURCES})
    add_test(NAME "${test_name}" COMMAND "${test_name}")
    target_link_libraries("${test_name}" PRIVATE ${LIBCOMMUNI_TEST_COMMON_LIBS})
    set_target_properties("${test_name}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
    return(PROPAGATE test_name)
endfunction()

if(LIBCOMMUNI_BUILD_TESTS)
    add_subdirectory(auto)
endif()
if(LIBCOMMUNI_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
