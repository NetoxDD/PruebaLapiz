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

    // 12) Formas: se mide la caja de píxeles rojos (no depende del zoom de la vista)
    {
        c.setBorrador(false); c.setCubo(false); c.setColor(Qt::red); c.setGrosor(6);
        auto rojos = [&](const char *n) {
            c.exportarPNG(T(n)); const QImage im(T(n)); QRect r; int k = 0;
            for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x)
                if (im.pixelColor(x, y) == QColor(Qt::red)) { ++k; r = r.united(QRect(x, y, 1, 1)); }
            return std::make_pair(k, r);
        };
        auto arrastrar = [&](QPoint a, QPoint b) {
            QTest::mousePress(&c, Qt::LeftButton, {}, a);
            for (int i = 1; i <= 8; ++i) QTest::mouseMove(&c, a + (b - a) * i / 8);
            QTest::mouseRelease(&c, Qt::LeftButton, {}, b);
        };
        // rectángulo relleno
        c.nuevoLienzo(800, 600); c.ajustar(); c.forma = plz::FORMA_RECT; c.formaRelleno = true;
        arrastrar({300, 250}, {500, 400});
        auto [kr, rr] = rojos("f1.png");
        CHECK(rr.width() > 150 && rr.height() > 110 && kr > rr.width() * rr.height() * 0.95);   // sólido
        // elipse solo contorno: hueco en el centro, rojo en el borde
        c.nuevoLienzo(800, 600); c.ajustar(); c.forma = plz::FORMA_ELIPSE; c.formaRelleno = false;
        arrastrar({200, 200}, {500, 400});
        auto [ke, re] = rojos("f2.png");
        const QImage ie(T("f2.png"));
        CHECK(re.width() > 250 && re.height() > 170);
        CHECK(ie.pixelColor(re.center()) == QColor(Qt::white));
        CHECK(ie.pixelColor(re.right() - 2, re.center().y()) == QColor(Qt::red));
        CHECK(ke < re.width() * re.height() * 0.5);                                               // no es sólida
        // línea: fina y horizontal; con Shift quedaría a 15°
        c.nuevoLienzo(800, 600); c.ajustar(); c.forma = plz::FORMA_LINEA;
        arrastrar({200, 300}, {600, 300});
        auto [kl, rl] = rojos("f3.png");
        CHECK(rl.width() > 300 && rl.height() < 12);
        // deshacer y guardar/abrir
        c.forma = plz::FORMA_RECT; c.formaRelleno = true; arrastrar({300, 400}, {400, 450});
        const int antes = rojos("f4.png").first;
        c.deshacer();
        CHECK(rojos("f5.png").first == kl);
        c.rehacer();
        CHECK(rojos("f6.png").first == antes);
        CHECK(c.guardar(T("formas.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("formas.json")));
        const int tras = rojos("f7.png").first;
        std::printf("formas: %d rojos antes de guardar, %d tras abrir\n", antes, tras);
        CHECK(std::abs(tras - antes) <= antes / 200);          // el archivo redondea a 0.01 px: solo cambian píxeles del borde
        c.forma = plz::FORMA_LIBRE;
    }

    // 13) Selección: borrar, recorte de trazos, lazo, copiar/pegar y guardado
    {
        auto cuenta = [&](const char *n, QColor col) {
            c.exportarPNG(T(n)); const QImage im(T(n)); QRect r; int k = 0;
            for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x)
                if (im.pixelColor(x, y) == col) { ++k; r = r.united(QRect(x, y, 1, 1)); }
            return std::make_pair(k, r);
        };
        auto arrastrar = [&](std::initializer_list<QPoint> ps) {
            auto it = ps.begin(); QTest::mousePress(&c, Qt::LeftButton, {}, *it);
            for (++it; it != ps.end(); ++it) for (int i = 1; i <= 6; ++i) QTest::mouseMove(&c, *it);
            QTest::mouseRelease(&c, Qt::LeftButton, {}, *(ps.end() - 1));
        };
        c.nuevoLienzo(800, 600); c.ajustar(); c.setBorrador(false); c.setCubo(false); c.forma = plz::FORMA_LIBRE;
        c.setColor(Qt::red); c.setCubo(true); c.cubo.todasCapas = false; c.cubo.tolerancia = 30;
        QTest::mouseClick(&c, Qt::LeftButton, {}, QPoint(450, 350)); c.setCubo(false);
        c.setSeleccion(1); arrastrar({QPoint(300, 250), QPoint(500, 400)}); c.setSeleccion(0);
        CHECK(c.haySeleccion());
        c.borrarSel();
        auto [kb, rb] = cuenta("s1.png", Qt::white);                       // lo borrado queda blanco
        CHECK(kb > 20000 && kb < 45000);
        CHECK(QImage(T("s1.png")).pixelColor(5, 5) == QColor(Qt::red));
        c.deshacer();
        CHECK(cuenta("s2.png", Qt::white).first == 0);
        c.setColor(Qt::blue); c.setGrosor(30);                              // un trazo que cruza el borde de la selección
        arrastrar({QPoint(200, 325), QPoint(600, 325)});
        auto [kz, rz] = cuenta("s3.png", Qt::blue);
        CHECK(kz > 500);
        CHECK(rz.left() >= rb.left() - 1 && rz.right() <= rb.right() + 1);  // no se sale de la selección
        // lazo triangular
        c.deshacer(); c.deseleccionar();
        c.setSeleccion(2); arrastrar({QPoint(100, 100), QPoint(260, 100), QPoint(100, 260), QPoint(100, 102)}); c.setSeleccion(0);
        CHECK(c.haySeleccion());
        c.borrarSel();
        auto [kl, rl] = cuenta("s4.png", Qt::white);
        CHECK(kl > 8000 && kl < 22000 && kl < rl.width() * rl.height() * 0.65);   // triángulo, no cuadrado
        // copiar y pegar (portapapeles del sistema), con deshacer y guardado
        c.nuevoLienzo(800, 600); c.ajustar();
        c.setColor(Qt::red); c.forma = plz::FORMA_RECT; c.formaRelleno = true;
        arrastrar({QPoint(300, 250), QPoint(400, 330)}); c.forma = plz::FORMA_LIBRE;
        c.setSeleccion(1); arrastrar({QPoint(280, 230), QPoint(420, 350)}); c.setSeleccion(0);
        c.copiar(false);
        c.deseleccionar(); c.nuevaCapa();
        QTest::mouseMove(&c, QPoint(620, 470));
        const int antes = cuenta("s5.png", Qt::red).first;
        c.pegar();
        const int despues = cuenta("s6.png", Qt::red).first;
        std::printf("pegar: %d rojos antes, %d despues\n", antes, despues);
        CHECK(despues > antes * 18 / 10);
        c.deshacer();
        CHECK(cuenta("s7.png", Qt::red).first == antes);
        c.rehacer();
        CHECK(c.guardar(T("sel.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("sel.json")));
        const int tras = cuenta("s8.png", Qt::red).first;
        CHECK(std::abs(tras - despues) <= despues / 200);
    }

    std::printf(fallos ? "\n%d FALLOS\n" : "\nSMOKE OK\n", fallos);
    return fallos ? 1 : 0;
}
