cmake_minimum_required(VERSION 3.31)
project(CHIP8_Emulator)

set(CMAKE_CXX_STANDARD 20)

add_executable(CHIP8_Emulator main.cpp)

# Link with SDL3 ( Might be better to follow their way of linking https://github.com/libsdl-org/SDL/blob/main/docs/INTRO-cmake.md )
include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.0
)
FetchContent_MakeAvailable(SDL3)
target_link_libraries(CHIP8_Emulator PRIVATE
    SDL3::SDL3
)
add_custom_command(TARGET CHIP8_Emulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:CHIP8_Emulator>
)