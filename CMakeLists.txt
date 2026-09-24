cmake_minimum_required(VERSION 3.20)

project(SpinTheWheel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

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

if(WIN32 AND DEFINED SFML_RUNTIME_DIR)
    foreach(dll IN ITEMS sfml-graphics-3.dll sfml-window-3.dll sfml-system-3.dll)
        add_custom_command(TARGET SpinTheWheel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SFML_RUNTIME_DIR}/${dll}"
                "$<TARGET_FILE_DIR:SpinTheWheel>/${dll}"
        )
    endforeach()
endif()