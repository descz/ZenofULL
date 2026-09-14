cmake_minimum_required(VERSION 3.20)

project(ZenoC VERSION 1.2.0 LANGUAGES C)

option(ZENO_BUILD_TESTS "Build the deterministic C test suite" ON)
option(ZENO_ENABLE_CURL "Enable libcurl-backed HTTP and LLM adapters when available" ON)
option(ZENO_WERROR "Treat compiler warnings as errors (recommended for CI)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(zenoc STATIC
    src/zeno_json.c
    src/zeno_core.c
    src/zeno_tools.c
    src/zeno_llm.c
    src/zeno_agent.c
    src/zeno_parallel.c
    src/zeno_studio.c
)
target_compile_features(zenoc PUBLIC c_std_11)

target_include_directories(zenoc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

function(zenoc_enable_warnings target)
    if(MSVC)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ZENO_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(ZENO_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
zenoc_enable_warnings(zenoc)

set(ZENO_CURL_ENABLED 0)
if(ZENO_ENABLE_CURL)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        set(ZENO_CURL_ENABLED 1)
        target_compile_definitions(zenoc PRIVATE ZENO_HAVE_CURL=1)
        target_link_libraries(zenoc PUBLIC CURL::libcurl)
    endif()
endif()

add_executable(zenoc_cli src/zenoc_cli.c)
target_link_libraries(zenoc_cli PRIVATE zenoc)
zenoc_enable_warnings(zenoc_cli)

if(WIN32)
    # winhttp backs the libcurl-free HTTP transport on Windows.
    target_link_libraries(zenoc PRIVATE userenv ws2_32 winhttp)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(zenoc PRIVATE Threads::Threads)
endif()

if(ZENO_BUILD_TESTS)
    enable_testing()
    add_executable(zenoc_tests tests/test_zeno.c)
    target_include_directories(zenoc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(zenoc_tests PRIVATE zenoc)
    zenoc_enable_warnings(zenoc_tests)
    add_test(NAME zenoc_tests COMMAND zenoc_tests)
    set_tests_properties(zenoc_tests PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(zenoc_eval tests/test_eval.c)
    target_include_directories(zenoc_eval PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(zenoc_eval PRIVATE zenoc)
    zenoc_enable_warnings(zenoc_eval)
    add_test(NAME zenoc_eval COMMAND zenoc_eval)
    set_tests_properties(zenoc_eval PROPERTIES TIMEOUT 120 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(zenoc_fuzz tests/fuzz_zeno.c)
    target_include_directories(zenoc_fuzz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(zenoc_fuzz PRIVATE zenoc)
    zenoc_enable_warnings(zenoc_fuzz)
    add_test(NAME zenoc_fuzz COMMAND zenoc_fuzz 20000)
    set_tests_properties(zenoc_fuzz PROPERTIES TIMEOUT 120 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ZenoCConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ZenoCConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ZenoCConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ZenoC"
)

install(TARGETS zenoc zenoc_cli
    EXPORT ZenoCTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES include/zeno.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ZenoCTargets
    FILE ZenoCTargets.cmake
    NAMESPACE ZenoC::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ZenoC"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ZenoCConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ZenoCConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ZenoC"
)
