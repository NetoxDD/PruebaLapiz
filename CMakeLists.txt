cmake_minimum_required(VERSION 3.19)
project(PruebaLapiz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLZ_BUILD_APP   "Compilar la aplicación Qt"        ON)
option(PLZ_BUILD_TESTS "Compilar las pruebas del core"    ON)

# ---------- core: sin Qt ----------
add_library(plz_core STATIC
    core/documento.cpp
    core/historial.cpp
    core/comandos.cpp
    core/relleno.cpp
    core/seleccion.cpp
    core/transformar.cpp
    core/estampa.cpp
    core/documento.h core/historial.h core/comandos.h
    core/imagen.h core/mezcla.h core/trazo.h core/freehand.h core/relleno.h core/seleccion.h core/transformar.h core/estampa.h
)
target_include_directories(plz_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---------- pruebas ----------
if(PLZ_BUILD_TESTS)
    enable_testing()
    add_executable(plz_tests tests/test_main.cpp tests/test_documento.cpp tests/test_relleno.cpp tests/test_seleccion.cpp tests/test_transformar.cpp tests/test_poligono.cpp tests/test_fusion.cpp tests/test_estampa.cpp)
    target_link_libraries(plz_tests PRIVATE plz_core)
    add_test(NAME core COMMAND plz_tests)
endif()

# ---------- aplicación ----------
if(PLZ_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    qt_standard_project_setup()

    # Toda la interfaz va en una librería para que la compartan el ejecutable y la prueba de la ventana
    add_library(plz_app STATIC
        app/ventana.cpp app/ventana_menus.cpp app/ventana_barra.cpp app/ventana_paneles.cpp app/ventana_inicio.cpp
        app/ventana.h app/canvas.h app/puente.h app/archivo.h app/selector_color.h
    )
    target_include_directories(plz_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(plz_app PUBLIC plz_core Qt6::Widgets Qt6::OpenGLWidgets)

    qt_add_executable(PruebaLapiz app/main.cpp mainwindow.cpp mainwindow.h mainwindow.ui)
    target_link_libraries(PruebaLapiz PRIVATE plz_app)
    set_target_properties(PruebaLapiz PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
endif()

# Prueba de humo de la interfaz (necesita pantalla/OpenGL; en CI: xvfb-run)
option(PLZ_BUILD_SMOKE "Compilar la prueba de humo de la interfaz" OFF)
if(PLZ_BUILD_APP AND PLZ_BUILD_SMOKE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(smoke_canvas tests/smoke_canvas.cpp)
    target_include_directories(smoke_canvas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} app)
    target_link_libraries(smoke_canvas PRIVATE plz_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)

    # Construye la ventana completa y maneja sus botones, paneles y menús
    add_executable(smoke_ventana tests/smoke_ventana.cpp)
    target_link_libraries(smoke_ventana PRIVATE plz_app Qt6::Test)
endif()
