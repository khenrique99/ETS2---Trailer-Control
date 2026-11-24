cmake_minimum_required(VERSION 3.20)
project(ets2_trailer_control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Coloque o SCS SDK (https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry)
# em ./sdk  (precisa existir ./sdk/include/scssdk_telemetry.h e ./sdk/include/scssdk_input.h)
set(SCS_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "Pasta raiz do SCS SDK")

if(NOT EXISTS "${SCS_SDK_DIR}/include/scssdk_telemetry.h")
    message(FATAL_ERROR "SCS SDK nao encontrado em ${SCS_SDK_DIR}. Veja o README.")
endif()

add_library(trailer_control SHARED
    src/plugin.cpp
    src/log.cpp
    src/config.cpp
    src/trailer_registry.cpp
    src/input_backend.cpp
    src/memory_backend.cpp
    src/hotkeys.cpp
    src/controller.cpp
    src/status_writer.cpp
    src/exports.def
)

target_include_directories(trailer_control PRIVATE
    "${SCS_SDK_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(trailer_control PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(trailer_control PRIVATE /W4 /permissive- /utf-8)
    # Runtime estatico: o usuario nao precisa instalar o VC++ Redistributable
    set_property(TARGET trailer_control PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(trailer_control PRIVATE user32 shell32 ole32 psapi)

# Copia o .ini para junto da DLL
add_custom_command(TARGET trailer_control POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/trailer_control.ini"
        "$<TARGET_FILE_DIR:trailer_control>/trailer_control.ini")
