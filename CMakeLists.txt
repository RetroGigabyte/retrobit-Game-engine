cmake_minimum_required(VERSION 3.20)
if(APPLE)
    project(RETRObitEngine CXX OBJCXX)
else()
    project(RETRObitEngine CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Platform-agnostic sources (engine core) — deliberately excludes src/platform/,
# which holds one dedicated implementation per target platform (see below) so
# the engine can call things like the native menu bar without #ifdef guards at
# every call site: CMake picks which platform's file actually gets compiled.
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/platform/")

if(APPLE)
    file(GLOB_RECURSE ENGINE_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/macos/*.mm)
else()
    file(GLOB_RECURSE ENGINE_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/stub/*.cpp)
endif()
list(APPEND ENGINE_SOURCES ${ENGINE_PLATFORM_SOURCES})

add_executable(retrobit ${ENGINE_SOURCES})

target_include_directories(retrobit PRIVATE include)
target_link_libraries(retrobit PRIVATE glfw glm::glm)

if(APPLE)
    target_link_libraries(retrobit PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

# Copy shaders next to the built binary so relative paths work at runtime
add_custom_command(TARGET retrobit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:retrobit>/shaders
)
