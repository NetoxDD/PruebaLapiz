#include <QApplication>
#include <QTest>
#include <QImage>
#include <cstdio>
#include <QDir>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include "app/canvas.h"

static int fallos = 0;
static QTemporaryDir *tmp = nullptr;
static QString T(const char *n) { return QDir(tmp->path()).filePath(n); }
#define CHECK(c) do { if (!(c)) { std::printf("FALLO linea %d: %s\n", __LINE__, #c); ++fallos; } } while (0)

static int pixelesNoBlancos(const QString &ruta) {
    QImage im(ruta); int n = 0;
    for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x)
        if (im.pixel(x, y) != 0xFFFFFFFF) ++n;
    return n;
}

// Prueba de humo de la aplicación: necesita pantalla y OpenGL
// (en Linux sin monitor: QT_QPA_PLATFORM=xcb xvfb-run -a ./smoke_canvas)
int main(int argc, char **argv) {
    QSurfaceFormat fmt; fmt.setDepthBufferSize(24); fmt.setStencilBufferSize(8); fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir; tmp = &dir;
    Canvas c;
    c.resize(900, 700);
    c.show();
    c.nuevoLienzo(800, 600);
    c.ajustar();
    QTest::qWait(50);

    auto trazo = [&](QPoint a, QPoint b) {
        QTest::mousePress(&c, Qt::LeftButton, {}, a);
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(&c, a + (b - a) * i / 10);
        QTest::mouseRelease(&c, Qt::LeftButton, {}, b);
    };
    int cambiosCapas = 0;
    c.alCambiarCapas = [&] { ++cambiosCapas; };

    // 1) Dibujar y exportar
    trazo({200, 200}, {600, 400});
    c.exportarPNG(T("a.png"));
    const int px1 = pixelesNoBlancos(T("a.png"));
    CHECK(px1 > 500);
    CHECK(c.modificado());

    // 2) Deshacer / rehacer trazo
    c.deshacer();
    c.exportarPNG(T("b.png"));
    CHECK(pixelesNoBlancos(T("b.png")) == 0);
    c.rehacer();
    c.exportarPNG(T("c.png"));
    CHECK(pixelesNoBlancos(T("c.png")) == px1);

    // 3) Capas: crear, dibujar, borrar, deshacer el borrado
    c.nuevaCapa();
    CHECK(c.numCapas() == 2 && c.indiceActivo() == 1);
    trazo({200, 500}, {600, 500});
    c.exportarPNG(T("d.png"));
    const int px2 = pixelesNoBlancos(T("d.png"));
    CHECK(px2 > px1);
    c.borrarCapa(1);
    CHECK(c.numCapas() == 1);
    c.exportarPNG(T("e.png"));
    CHECK(pixelesNoBlancos(T("e.png")) == px1);
    c.deshacer();                       // vuelve la capa con su contenido
    CHECK(c.numCapas() == 2 && c.indiceActivo() == 1);
    c.exportarPNG(T("f.png"));
    CHECK(pixelesNoBlancos(T("f.png")) == px2);
    CHECK(cambiosCapas >= 3);

    // 4) Mover capa + deshacer
    c.moverCapa(1, -1);
    CHECK(c.indiceActivo() == 0);
    c.deshacer();
    CHECK(c.indiceActivo() == 1);

    // 5) Opacidad: varios cambios = un paso
    for (double o : {0.9, 0.5, 0.2}) c.setOpacidad(1, o);
    CHECK(std::abs(c.capa(1).opacidad - 0.2) < 1e-9);
    c.deshacer();
    CHECK(std::abs(c.capa(1).opacidad - 1.0) < 1e-9);

    // 6) Guardar y abrir
    CHECK(c.guardar(T("dibujo.json")));
    CHECK(!c.modificado());
    c.nuevoLienzo(300, 300);
    CHECK(c.numCapas() == 1 && c.ancho() == 300);
    CHECK(c.abrir(T("dibujo.json")));
    CHECK(c.numCapas() == 2 && c.ancho() == 800);
    c.exportarPNG(T("g.png"));
    CHECK(pixelesNoBlancos(T("g.png")) == px2);
    CHECK(!c.modificado());

    // 7) Borrador: borra lo dibujado
    c.setBorrador(true);
    c.setGrosor(60);
    trazo({150, 495}, {650, 505});
    c.setBorrador(false);
    c.exportarPNG(T("h.png"));
    CHECK(pixelesNoBlancos(T("h.png")) < px2);
    c.deshacer();
    c.exportarPNG(T("i.png"));
    CHECK(pixelesNoBlancos(T("i.png")) == px2);

    // 8) Renderizar el widget (paintGL) sin caerse
    QImage frame = c.grabFramebuffer();
    std::printf("grabFramebuffer: %dx%d\n", frame.width(), frame.height());


    // 9) La PANTALLA se refresca (caché de texturas de Qt) tras pintar, deshacer y rehacer
    {
        auto gris = [&]() { QEvent fuera(QEvent::Leave); QApplication::sendEvent(&c, &fuera); c.update(); QTest::qWait(30); QImage f = c.grabFramebuffer(); int n = 0;
            for (int y = 0; y < f.height(); y += 2) for (int x = 0; x < f.width(); x += 2)
                if (QColor(f.pixel(x, y)).red() < 60) ++n;   // tinta negra
            return n; };
        c.nuevoLienzo(800, 600); c.ajustar(); c.setColor(Qt::black); c.setGrosor(30);
        const int base = gris();
        trazo({200, 300}, {600, 300});
        const int conTrazo = gris();
        CHECK(conTrazo > base + 200);
        c.deshacer();
        CHECK(gris() <= base + 5);
        c.rehacer();
        { int v = gris(); std::printf("rehacer: %d vs %d\n", v, conTrazo); CHECK(v == conTrazo); }
        c.nuevaCapa(); trazo({200, 400}, {600, 400});
        const int dos = gris();
        CHECK(dos > conTrazo + 200);
        c.borrarCapa(1);
        { int v = gris(); std::printf("borrar capa: %d vs %d\n", v, conTrazo); CHECK(v == conTrazo); }
        c.deshacer();
        { int v = gris(); std::printf("undo borrar capa: %d vs %d\n", v, dos); CHECK(v == dos); }
        c.setBorrador(true); c.setGrosor(80); trazo({150, 400}, {650, 400}); c.setBorrador(false);
        CHECK(gris() < dos - 100);
        c.deshacer();
        { int v = gris(); std::printf("undo borrador: %d vs %d\n", v, dos); CHECK(v == dos); }
        std::printf("pantalla: base=%d trazo=%d dosCapas=%d\n", base, conTrazo, dos);
    }

    // 10) Cubo de relleno
    {
        c.nuevoLienzo(800, 600); c.ajustar(); c.setBorrador(false);
        c.setColor(Qt::black); c.setGrosor(8);
        QTest::mousePress(&c, Qt::LeftButton, {}, QPoint(250, 200));
        for (QPoint q : {QPoint(650, 200), QPoint(650, 500), QPoint(250, 500), QPoint(250, 200)})
            for (int i = 1; i <= 10; ++i) QTest::mouseMove(&c, c.mapFromGlobal(c.mapToGlobal(q)) );
        QTest::mouseRelease(&c, Qt::LeftButton, {}, QPoint(250, 200));
        // el cuadrado hecho con mouse: ahora el cubo
        c.setColor(Qt::red); c.setCubo(true); c.cubo.tolerancia = 30; c.cubo.todasCapas = false;
        QTest::mouseClick(&c, Qt::LeftButton, {}, QPoint(450, 350));
        c.exportarPNG(T("cubo1.png"));
        QImage im(T("cubo1.png"));
        CHECK(im.pixelColor(400, 300) == QColor(Qt::red));
        CHECK(im.pixelColor(5, 5) == QColor(Qt::white));
        const int rojos = [&] { int n = 0; for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x) if (im.pixelColor(x, y) == QColor(Qt::red)) ++n; return n; }();
        CHECK(rojos > 1000);

        c.deshacer();
        c.exportarPNG(T("cubo2.png"));
        CHECK(QImage(T("cubo2.png")).pixelColor(400, 300) == QColor(Qt::white));
        c.rehacer();
        c.exportarPNG(T("cubo3.png"));
        CHECK(QImage(T("cubo3.png")).pixelColor(400, 300) == QColor(Qt::red));

        // guardar y abrir conserva el relleno (formato con operaciones en orden)
        CHECK(c.guardar(T("cubo.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("cubo.json")));
        c.exportarPNG(T("cubo4.png"));
        CHECK(QImage(T("cubo4.png")).pixelColor(400, 300) == QColor(Qt::red));
        CHECK(QImage(T("cubo4.png")).pixelColor(5, 5) == QColor(Qt::white));

        // "todas las capas": la capa nueva se rellena solo dentro del cuadrado de la capa de abajo
        c.nuevaCapa();
        c.setColor(Qt::blue); c.cubo.todasCapas = false;
        c.setCubo(true);
        c.cubo.todasCapas = true;
        c.setColor(Qt::blue);
        QTest::mouseClick(&c, Qt::LeftButton, {}, QPoint(450, 350));
        c.exportarPNG(T("cubo5.png"));
        CHECK(QImage(T("cubo5.png")).pixelColor(400, 300) == QColor(Qt::blue));
        CHECK(QImage(T("cubo5.png")).pixelColor(5, 5) == QColor(Qt::white));   // no se salió del cuadrado
        c.setCubo(false);
    }

    // 11) Teselas: con zoom fraccionario una capa de un solo color no debe mostrar rendijas
    {
        c.nuevoLienzo(2000, 1500); c.ajustar();
        c.setColor(Qt::red); c.setCubo(true); c.cubo.todasCapas = false;
        QTest::mouseClick(&c, Qt::LeftButton, {}, QPoint(450, 350));      // rellena toda la capa
        c.setCubo(false);
        QEvent fuera(QEvent::Leave); QApplication::sendEvent(&c, &fuera);
        c.update(); QTest::qWait(40);
        const QImage f = c.grabFramebuffer();
        int mal = 0, total = 0;
        for (int y = int(f.height() * 0.2); y < int(f.height() * 0.8); ++y)
            for (int x = int(f.width() * 0.2); x < int(f.width() * 0.8); ++x) {
                ++total;
                if (QColor(f.pixel(x, y)) != QColor(Qt::red)) ++mal;
            }
        std::printf("teselas: %d de %d pixeles distintos del rojo esperado\n", mal, total);
        CHECK(mal == 0);
    }

    std::printf(fallos ? "\n%d FALLOS\n" : "\nSMOKE OK\n", fallos);
    return fallos ? 1 : 0;
}
