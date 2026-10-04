#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include "ventana.h"

// 1 = pedir la GPU dedicada en portátiles con dos GPU (Windows).
// 0 = dejar que Windows decida (recomendado por batería y latencia).
#define PREFERIR_GPU_DEDICADA 0

#if defined(_WIN32) && PREFERIR_GPU_DEDICADA
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char *argv[]) {
    // "--software" fuerza el renderizador por software (equipos sin driver de GPU)
    for (int i = 1; i < argc; ++i)
        if (QString(argv[i]) == "--software")
            QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);

    // Formato de OpenGL: nada exigente, solo MSAA y stencil (se ajusta solo si no hay)
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    fmt.setSwapInterval(1);          // 0 = menos latencia, pero puede haber "tearing"
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("PruebaLapiz");
    QCoreApplication::setApplicationName("PruebaLapiz");

    VentanaPrincipal ventana;
    ventana.show();
    return app.exec();
}
