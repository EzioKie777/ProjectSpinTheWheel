cmake_minimum_required(VERSION 3.20)

project(SpinTheWheel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_SYSTEM_SFML "Use an installed SFML package instead of fetching it" ON)

if(USE_SYSTEM_SFML)
    find_package(SFML 3 CONFIG QUIET COMPONENTS Graphics Window System)
endif()

if(NOT SFML_FOUND)
    include(FetchContent)

    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.1.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SFML)
endif()

add_executable(SpinTheWheel main.cpp)
target_link_libraries(SpinTheWheel PRIVATE SFML::Graphics SFML::Window SFML::System)

set(RUNTIME_ASSETS
    Poppins-Bold.ttf
    Poppins-Regular.ttf
)

foreach(asset IN LISTS RUNTIME_ASSETS)
    add_custom_command(TARGET SpinTheWheel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/${asset}"
            "$<TARGET_FILE_DIR:SpinTheWheel>/${asset}"
    )
endforeach()

if(WIN32)
    foreach(sfml_target IN ITEMS SFML::Graphics SFML::Window SFML::System)
        add_custom_command(TARGET SpinTheWheel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${sfml_target}>"
                "$<TARGET_FILE_DIR:SpinTheWheel>"
        )
    endforeach()
endif()