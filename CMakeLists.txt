cmake_minimum_required(VERSION 3.20)

project(isaac_seed_seeker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(isaac_seed_core
    native/generated/builtin_profile_j460.cpp
    native/src/core.cpp
    native/src/daily.cpp
    native/src/profile_tables.cpp
)
target_include_directories(isaac_seed_core PUBLIC native/include PRIVATE native/generated)
target_link_libraries(isaac_seed_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(isaac_seed_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(isaac_seed_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(isaac-seed-seeker-native
    native/src/main.cpp
    native/src/local_web_app_win.cpp
)
set_target_properties(isaac-seed-seeker-native PROPERTIES OUTPUT_NAME "IsaacSeedSeeker")
target_link_libraries(isaac-seed-seeker-native PRIVATE isaac_seed_core)
if(WIN32)
    enable_language(RC)
    target_sources(isaac-seed-seeker-native PRIVATE native/resources/web.rc)
    target_link_libraries(isaac-seed-seeker-native PRIVATE ws2_32 shell32 advapi32)
endif()
if(MSVC)
    set_property(
        TARGET isaac-seed-seeker-native
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(isaac-seed-seeker-native PRIVATE /W4 /permissive-)
elseif(MINGW)
    target_compile_options(isaac-seed-seeker-native PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(isaac-seed-seeker-native PRIVATE -static -static-libgcc -static-libstdc++)
else()
    target_compile_options(isaac-seed-seeker-native PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(isaac-seed-core-tests native/tests/core_tests.cpp)
    target_link_libraries(isaac-seed-core-tests PRIVATE isaac_seed_core)
    if(MSVC)
        target_compile_options(isaac-seed-core-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(isaac-seed-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME isaac-seed-core-tests COMMAND isaac-seed-core-tests)
endif()
