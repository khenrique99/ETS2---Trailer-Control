cmake_minimum_required(VERSION 3.20)
project(ets2_trailer_control VERSION 0.3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime estatico: o usuario nao precisa instalar o VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Coloque o SCS SDK (https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry)
# em ./sdk  (precisa existir ./sdk/include/scssdk_telemetry.h e ./sdk/include/scssdk_input.h)
set(SCS_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "Pasta raiz do SCS SDK")

set(TC_DEFINES WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
set(TC_MSVC_OPTIONS /W4 /permissive- /utf-8)

# ---------------------------------------------------------------- plugin (DLL do jogo)
if(EXISTS "${SCS_SDK_DIR}/include/scssdk_telemetry.h")
    add_library(trailer_control SHARED
        src/plugin.cpp
        src/log.cpp
        src/config.cpp
        src/trailer_registry.cpp
        src/input_backend.cpp
        src/memory_backend.cpp
        src/hotkeys.cpp
        src/controller.cpp
        src/command_inbox.cpp
        src/app_launcher.cpp
        src/status_writer.cpp
        src/blinker_sound.cpp
        src/exports.def
    )
    target_include_directories(trailer_control PRIVATE "${SCS_SDK_DIR}/include" "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(trailer_control PRIVATE ${TC_DEFINES})
    if(MSVC)
        target_compile_options(trailer_control PRIVATE ${TC_MSVC_OPTIONS})
    endif()
    target_link_libraries(trailer_control PRIVATE user32 shell32 ole32 psapi version winmm)

    # Copia o .ini para junto da DLL
    add_custom_command(TARGET trailer_control POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/trailer_control.ini"
            "$<TARGET_FILE_DIR:trailer_control>/trailer_control.ini")
else()
    message(WARNING "SCS SDK nao encontrado em ${SCS_SDK_DIR}: so o app sera compilado. Veja o README.")
endif()

# ---------------------------------------------------------------- app externo (janela)
add_executable(trailer_control_app WIN32
    app/main.cpp
    app/model.cpp
    app/settings.cpp
    app/hotkey_text.cpp
    app/json.cpp
    src/shared/ini_edit.cpp
    src/blinker_sound.cpp
    app/app.rc
)
target_include_directories(trailer_control_app PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/app")
target_compile_definitions(trailer_control_app PRIVATE ${TC_DEFINES})
if(MSVC)
    target_compile_options(trailer_control_app PRIVATE ${TC_MSVC_OPTIONS})
endif()
target_link_libraries(trailer_control_app PRIVATE user32 gdi32 shell32 ole32 comctl32 dwmapi uxtheme winmm)
set_target_properties(trailer_control_app PROPERTIES OUTPUT_NAME "TrailerControl")
if(MSVC)
    # O manifesto vem do app.rc (DPI por monitor, controles v6, UTF-8)
    target_link_options(trailer_control_app PRIVATE /MANIFEST:NO /MAP)
endif()

# ---------------------------------------------------------------- testes (sem o jogo)
enable_testing()
add_executable(tc_tests tests/tests.cpp app/json.cpp app/model.cpp app/hotkey_text.cpp src/command_inbox.cpp src/shared/ini_edit.cpp src/blinker_sound.cpp)
target_include_directories(tc_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/app")
target_compile_definitions(tc_tests PRIVATE ${TC_DEFINES})
if(MSVC)
    target_compile_options(tc_tests PRIVATE ${TC_MSVC_OPTIONS})
endif()
target_link_libraries(tc_tests PRIVATE shell32 ole32 winmm)
add_test(NAME tc_tests COMMAND tc_tests)

# "Jogo falso" para testar plugin + app sem o ETS2 (precisa do SDK)
if(TARGET trailer_control)
    add_executable(fake_game tests/fake_game.cpp app/json.cpp src/shared/ini_edit.cpp)
    target_include_directories(fake_game PRIVATE "${SCS_SDK_DIR}/include" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/app")
    target_link_libraries(fake_game PRIVATE shell32 ole32)
    add_test(NAME plugin_selftest COMMAND fake_game "$<TARGET_FILE:trailer_control>" --selftest)
    target_compile_definitions(fake_game PRIVATE ${TC_DEFINES})
    if(MSVC)
        target_compile_options(fake_game PRIVATE ${TC_MSVC_OPTIONS})
    endif()
    add_dependencies(fake_game trailer_control)
    # DLL vazia "core_ets2mp.dll" (simula o TruckersMP no autoteste)
    add_library(fake_truckersmp SHARED tests/fake_truckersmp.cpp)
    set_target_properties(fake_truckersmp PROPERTIES OUTPUT_NAME "core_ets2mp")
    add_dependencies(fake_game fake_truckersmp)
endif()
