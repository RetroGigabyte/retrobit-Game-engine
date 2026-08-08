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

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
if(APPLE)
    file(GLOB_RECURSE ENGINE_OBJCXX_SOURCES CONFIGURE_DEPENDS src/*.mm)
    list(APPEND ENGINE_SOURCES ${ENGINE_OBJCXX_SOURCES})
endif()

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
