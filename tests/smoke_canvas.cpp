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

// Con PLZ_CAPTURAS=<carpeta> se guarda una captura de la pantalla en puntos clave (para revisarla a ojo)
static void captura(Canvas &c, const char *nombre) {
    const QString dir = qEnvironmentVariable("PLZ_CAPTURAS");
    if (dir.isEmpty()) return;
    QEvent fuera(QEvent::Leave); QApplication::sendEvent(&c, &fuera);
    c.update(); QTest::qWait(40);
    c.grabFramebuffer().save(QDir(dir).filePath(nombre));
}

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

    // 14) Transformar lo seleccionado: mover (exacto), deshacer/rehacer, cancelar, escalar y guardar
    {
        auto cuenta = [&](const char *n, QColor col) {
            c.exportarPNG(T(n)); const QImage im(T(n)); QRect r; int k = 0;
            for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x)
                if (im.pixelColor(x, y) == col) { ++k; r = r.united(QRect(x, y, 1, 1)); }
            return std::make_pair(k, r);
        };
        auto arrastrar = [&](QPoint a, QPoint b) {
            QTest::mousePress(&c, Qt::LeftButton, {}, a);
            for (int i = 1; i <= 6; ++i) QTest::mouseMove(&c, a + (b - a) * i / 6);
            QTest::mouseRelease(&c, Qt::LeftButton, {}, b);
        };
        auto pant = [&](double x, double y) { return c.pantallaDe(QPointF(x, y)).toPoint(); };
        c.nuevoLienzo(800, 600); c.zoom100(); c.setBorrador(false); c.setCubo(false);
        c.setColor(Qt::red); c.forma = plz::FORMA_RECT; c.formaRelleno = true;
        arrastrar({300, 250}, {400, 330}); c.forma = plz::FORMA_LIBRE;
        const auto [k0, r0] = cuenta("t0.png", Qt::red);
        CHECK(k0 > 5000);
        // seleccionar un poco más que el rectángulo
        c.setSeleccion(1); arrastrar(pant(r0.left() - 5, r0.top() - 5), pant(r0.right() + 6, r0.bottom() + 6)); c.setSeleccion(0);
        CHECK(c.haySeleccion());
        c.iniciarTransformacion();
        CHECK(c.transformandoAhora() && c.sesionActiva());
        const QPoint centroCaja = pant(r0.center().x(), r0.center().y());
        arrastrar(centroCaja, centroCaja + QPoint(60, 40));
        CHECK(c.transformandoAhora());                                  // sigue abierta hasta aplicar
        captura(c, "transformar_mover.png");
        c.cerrarSesiones();
        CHECK(!c.transformandoAhora());
        const auto [k1, r1] = cuenta("t1.png", Qt::red);
        std::printf("mover: %d rojos antes, %d despues; caja %d,%d -> %d,%d\n", k0, k1, r0.left(), r0.top(), r1.left(), r1.top());
        CHECK(k1 == k0);                                                // mover enteros = copia exacta
        CHECK(r1.left() == r0.left() + 60 && r1.top() == r0.top() + 40 && r1.size() == r0.size());
        c.deshacer();
        { const auto [k, r] = cuenta("t2.png", Qt::red); CHECK(k == k0 && r == r0); }
        c.rehacer();
        { const auto [k, r] = cuenta("t3.png", Qt::red); CHECK(k == k1 && r == r1); }
        CHECK(c.haySeleccion());                                        // la selección acompaña a lo movido

        // Esc cancela sin dejar paso en el historial
        c.iniciarTransformacion();
        arrastrar(pant(r1.center().x(), r1.center().y()), pant(r1.center().x(), r1.center().y()) + QPoint(-80, 10));
        QTest::keyClick(&c, Qt::Key_Escape);
        CHECK(!c.transformandoAhora());
        { const auto [k, r] = cuenta("t4.png", Qt::red); CHECK(k == k1 && r == r1); }
        c.deshacer();                                                   // deshace el movimiento anterior, no uno fantasma
        { const auto [k, r] = cuenta("t5.png", Qt::red); CHECK(k == k0 && r == r0); }
        c.rehacer();

        // Escalar: arrastrar la asa inferior derecha el ancho de la caja = el doble de ancho, misma altura
        c.iniciarTransformacion();
        const double bx1 = r1.right() + 1 + 6, by1 = r1.bottom() + 1 + 6;      // esquina de la selección movida
        const double w0 = r1.width() + 11;
        arrastrar(pant(bx1, by1), pant(bx1, by1) + QPoint(int(w0), 0));
        captura(c, "transformar_escalar.png");
        c.cerrarSesiones();
        const auto [k2, r2] = cuenta("t6.png", Qt::red);
        std::printf("escalar: ancho %d -> %d, alto %d -> %d\n", r1.width(), r2.width(), r1.height(), r2.height());
        CHECK(std::abs(r2.width() - 2 * r1.width()) <= 14 && std::abs(r2.height() - r1.height()) <= 2);
        CHECK(k2 > k1 * 17 / 10);
        // Rotar: arrastrar fuera de la caja 90° alrededor de su centro cambia ancho por alto
        c.iniciarTransformacion();
        const QPoint cen = pant(r2.center().x() + 0.5, r2.center().y() + 0.5);
        arrastrar(cen + QPoint(260, 0), cen + QPoint(0, 260));
        captura(c, "transformar_rotar.png");
        c.cerrarSesiones();
        const auto [k2b, r2b] = cuenta("t6b.png", Qt::red);
        std::printf("rotar: %dx%d -> %dx%d\n", r2.width(), r2.height(), r2b.width(), r2b.height());
        CHECK(std::abs(r2b.width() - r2.height()) <= 4 && std::abs(r2b.height() - r2.width()) <= 4);
        // guardar y abrir repite las tres operaciones
        CHECK(c.guardar(T("trans.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("trans.json")));
        const auto [k3, r3] = cuenta("t7.png", Qt::red);
        CHECK(std::abs(k3 - k2b) <= k2b / 25 && std::abs(r3.width() - r2b.width()) <= 3);
        // sin selección no se puede transformar
        c.deseleccionar();
        c.iniciarTransformacion();
        CHECK(!c.transformandoAhora());
    }

    // 15) Polígono: un clic por vértice; doble clic, Enter o clic en el primero lo cierran
    {
        auto rojos = [&](const char *n) {
            c.exportarPNG(T(n)); const QImage im(T(n)); QRect r; int k = 0;
            for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x)
                if (im.pixelColor(x, y) == QColor(Qt::red)) { ++k; r = r.united(QRect(x, y, 1, 1)); }
            return std::make_pair(k, r);
        };
        auto clic = [&](QPoint p) { QTest::mouseClick(&c, Qt::LeftButton, {}, p); };
        c.nuevoLienzo(800, 600); c.zoom100(); c.setBorrador(false); c.setCubo(false); c.setSeleccion(0);
        c.setColor(Qt::red); c.setGrosor(6); c.formaRelleno = true; c.setForma(plz::FORMA_POLIGONO);
        // cuadrado de 100x100 cerrado con doble clic en el último vértice
        clic({300, 250}); clic({400, 250}); clic({400, 350});
        CHECK(c.sesionActiva());
        CHECK(rojos("p0.png").first == 0);                               // mientras se construye no está pintado
        clic({300, 350});                                                // un doble clic real = pulsar, soltar y "doble clic"
        QTest::mouseDClick(&c, Qt::LeftButton, {}, QPoint(300, 350));    // (QTest solo manda este último evento)
        CHECK(!c.sesionActiva());
        const auto [k1, r1] = rojos("p1.png");
        std::printf("poligono: %d rojos, caja %dx%d\n", k1, r1.width(), r1.height());
        CHECK(k1 > 9500 && k1 < 10300 && r1.width() >= 99 && r1.width() <= 102 && r1.height() >= 99 && r1.height() <= 102);
        c.deshacer();
        CHECK(rojos("p2.png").first == 0);
        c.rehacer();
        CHECK(rojos("p3.png").first == k1);
        // Enter cierra un triángulo: la mitad del cuadrado
        clic({500, 250}); clic({600, 250}); clic({500, 350});
        QTest::keyClick(&c, Qt::Key_Return);
        CHECK(!c.sesionActiva());
        { const int k = rojos("p4.png").first - k1; CHECK(k > 4500 && k < 5300); }
        // Retroceso quita el último vértice; Esc cancela todo
        const int antes = rojos("p5.png").first;
        clic({100, 100}); clic({200, 100}); clic({350, 300});              // este último vértice sobra
        QTest::keyClick(&c, Qt::Key_Backspace);
        clic({200, 200}); clic({100, 200});                                // queda un cuadrado 100x100 en (100,100)
        QTest::keyClick(&c, Qt::Key_Return);
        CHECK(!c.sesionActiva());
        { const int k = rojos("p6.png").first - antes; CHECK(k > 9500 && k < 10300); }
        const int antesEsc = rojos("p7.png").first;
        clic({100, 400}); clic({200, 400}); clic({200, 500});
        captura(c, "poligono_en_curso.png");
        QTest::keyClick(&c, Qt::Key_Escape);
        CHECK(!c.sesionActiva() && rojos("p8.png").first == antesEsc);
        // clic sobre el primer vértice cierra
        clic({500, 400}); clic({600, 400}); clic({600, 500}); clic({500, 500});
        CHECK(c.sesionActiva());
        clic({502, 402});
        CHECK(!c.sesionActiva());
        { const int k = rojos("p9.png").first - antesEsc; CHECK(k > 9000); }
        // con solo dos vértices no se pinta nada
        const int antes2 = rojos("p10.png").first;
        clic({50, 50}); clic({150, 60});
        QTest::keyClick(&c, Qt::Key_Return);
        CHECK(!c.sesionActiva() && rojos("p11.png").first == antes2);
        // solo contorno (sin relleno): hueco en el centro
        c.nuevoLienzo(800, 600); c.zoom100(); c.formaRelleno = false;
        clic({300, 250}); clic({420, 250}); clic({420, 370}); clic({300, 370});
        QTest::keyClick(&c, Qt::Key_Return);
        {
            const auto [k, r] = rojos("p12.png");
            const QImage im(T("p12.png"));
            CHECK(k > 1500 && k < r.width() * r.height() * 0.3);
            CHECK(im.pixelColor(r.center()) == QColor(Qt::white));
        }
        // guardar y abrir
        c.formaRelleno = true; clic({100, 450}); clic({180, 450}); clic({140, 520}); QTest::keyClick(&c, Qt::Key_Return);
        const int antesGuardar = rojos("p13.png").first;
        CHECK(c.guardar(T("poli.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("poli.json")));
        CHECK(std::abs(rojos("p14.png").first - antesGuardar) <= antesGuardar / 100);
        c.setForma(plz::FORMA_LIBRE);
    }

    // 16) Modos de fusión: la pantalla (compuesta en CPU) debe coincidir con lo exportado
    {
        auto arrastrar = [&](QPoint a, QPoint b) {
            QTest::mousePress(&c, Qt::LeftButton, {}, a);
            for (int i = 1; i <= 6; ++i) QTest::mouseMove(&c, a + (b - a) * i / 6);
            QTest::mouseRelease(&c, Qt::LeftButton, {}, b);
        };
        c.nuevoLienzo(800, 600); c.zoom100(); c.setBorrador(false); c.setCubo(false); c.setSeleccion(0);
        c.forma = plz::FORMA_RECT; c.formaRelleno = true;
        c.setColor(Qt::red);   arrastrar({200, 150}, {500, 400});            // capa de abajo: rojo
        c.nuevaCapa();
        c.setColor(Qt::green); arrastrar({350, 250}, {650, 500});            // capa de arriba: verde, solapa en 350..500 x 250..400
        c.forma = plz::FORMA_LIBRE;
        const QPoint o = c.pantallaDe(QPointF(0, 0)).toPoint();
        auto enExport = [&](QPoint s) { c.exportarPNG(T("m.png")); return QImage(T("m.png")).pixelColor(s - o); };
        auto enPantalla = [&](QPoint s) {
            QEvent fuera(QEvent::Leave); QApplication::sendEvent(&c, &fuera);
            c.update(); QTest::qWait(40);
            return c.grabFramebuffer().pixelColor(s);
        };
        const QPoint solape(425, 325), soloVerde(600, 450), soloRojo(250, 200);
        CHECK(enPantalla(solape) == QColor(Qt::green) && enExport(solape) == QColor(Qt::green));   // normal: tapa

        c.setFusion(1, plz::Fusion::Multiplicar);
        CHECK(c.capa(1).fusion == plz::Fusion::Multiplicar);
        CHECK(enExport(solape) == QColor(0, 0, 0));                           // rojo × verde = negro
        CHECK(enPantalla(solape) == enExport(solape));
        CHECK(enPantalla(soloVerde) == QColor(Qt::green) && enExport(soloVerde) == QColor(Qt::green));   // sobre blanco no cambia
        CHECK(enPantalla(soloRojo) == QColor(Qt::red));

        c.setFusion(1, plz::Fusion::Diferencia);
        CHECK(enExport(solape) == QColor(255, 255, 0));                       // |rojo - verde| = amarillo
        CHECK(enPantalla(solape) == enExport(solape));

        c.setFusion(1, plz::Fusion::Trama);
        CHECK(enPantalla(solape) == enExport(solape));
        CHECK(enExport(solape) == QColor(255, 255, 0));                       // trama de rojo y verde = amarillo

        // dibujar con un modo activo: el trazo se ve en pantalla y en la exportación
        c.setFusion(1, plz::Fusion::Multiplicar);
        c.setColor(Qt::blue); c.setGrosor(40);
        arrastrar({100, 500}, {300, 500});
        CHECK(enPantalla({200, 500}) == QColor(Qt::blue) && enExport({200, 500}) == QColor(Qt::blue));
        c.deshacer();
        CHECK(enPantalla({200, 500}) == QColor(Qt::white));

        // deshacer el cambio de modo y rehacerlo
        c.deshacer();
        CHECK(c.capa(1).fusion == plz::Fusion::Normal);
        CHECK(enPantalla(solape) == QColor(Qt::green));
        c.rehacer();
        CHECK(c.capa(1).fusion == plz::Fusion::Multiplicar && enPantalla(solape) == QColor(0, 0, 0));
        captura(c, "fusion_multiplicar.png");

        // un modo en una capa OCULTA no afecta, y al guardar/abrir se conserva
        CHECK(c.guardar(T("fusion.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("fusion.json")));
        CHECK(c.numCapas() == 2 && c.capa(1).fusion == plz::Fusion::Multiplicar && c.capa(0).fusion == plz::Fusion::Normal);
        CHECK(enExport(solape) == QColor(0, 0, 0));
        c.setVisible(1, false);
        CHECK(enExport(solape) == QColor(Qt::red) && enPantalla(solape) == QColor(Qt::red));
    }

    // 17) Pinceles de estampado
    {
        auto rojos = [&](const char *n) {
            c.exportarPNG(T(n)); const QImage im(T(n)); QRect r; int k = 0;
            for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x)
                if (im.pixelColor(x, y) != QColor(Qt::white)) { ++k; r = r.united(QRect(x, y, 1, 1)); }
            return std::make_pair(k, r);
        };
        auto arrastrar = [&](QPoint a, QPoint b) {
            QTest::mousePress(&c, Qt::LeftButton, {}, a);
            for (int i = 1; i <= 12; ++i) QTest::mouseMove(&c, a + (b - a) * i / 12);
            QTest::mouseRelease(&c, Qt::LeftButton, {}, b);
        };
        int ind[4] = {-1, -1, -1, -1};
        const auto &pins = Canvas::pinceles();
        for (int i = 0; i < int(pins.size()); ++i) {
            if (pins[std::size_t(i)].nombre == "Estrellas") ind[0] = i;
            if (pins[std::size_t(i)].nombre == "Aerógrafo") ind[1] = i;
            if (pins[std::size_t(i)].nombre == "Hojas") ind[2] = i;
            if (pins[std::size_t(i)].nombre == "Tiza") ind[3] = i;
        }
        CHECK(ind[0] >= 0 && ind[1] >= 0 && ind[2] >= 0 && ind[3] >= 0);
        c.nuevoLienzo(800, 600); c.zoom100(); c.setBorrador(false); c.setCubo(false); c.setSeleccion(0);
        c.forma = plz::FORMA_LIBRE; c.setColor(Qt::red); c.setGrosor(30);
        c.setPincel(ind[0]);

        // en vivo: antes de soltar ya se ve en pantalla
        QTest::mousePress(&c, Qt::LeftButton, {}, QPoint(200, 200));
        for (int i = 1; i <= 12; ++i) QTest::mouseMove(&c, QPoint(200 + i * 30, 200));
        {
            QEvent fuera(QEvent::Leave); QApplication::sendEvent(&c, &fuera);
            c.update(); QTest::qWait(40);
            const QImage f = c.grabFramebuffer();
            int n = 0;
            for (int y = 150; y < 250; ++y) for (int x = 200; x < 560; ++x) if (QColor(f.pixel(x, y)).red() > 200 && QColor(f.pixel(x, y)).green() < 80) ++n;
            CHECK(n > 300);
        }
        QTest::mouseRelease(&c, Qt::LeftButton, {}, QPoint(560, 200));
        const auto [k1, r1] = rojos("e1.png");
        std::printf("estrellas: %d pixeles, caja %dx%d\n", k1, r1.width(), r1.height());
        CHECK(k1 > 600 && r1.width() > 330 && r1.height() < 90);
        captura(c, "estampado.png");
        c.deshacer();
        CHECK(rojos("e2.png").first == 0);
        c.rehacer();
        CHECK(rojos("e3.png").first == k1);                                // rehacer = exactamente lo mismo
        // guardar y abrir: mismo dibujo (el archivo redondea a 0.01 px)
        CHECK(c.guardar(T("estampado.json")));
        c.nuevoLienzo(300, 300);
        CHECK(c.abrir(T("estampado.json")));
        { const auto [k, r] = rojos("e4.png"); CHECK(std::abs(k - k1) <= k1 / 30 && std::abs(r.width() - r1.width()) <= 3); }

        // otros pinceles dejan marca
        c.nuevoLienzo(800, 600); c.zoom100(); c.setColor(Qt::blue);
        c.setPincel(ind[1]); arrastrar({100, 100}, {400, 100});
        const int aero = rojos("e5.png").first; CHECK(aero > 500);
        c.setPincel(ind[2]); arrastrar({100, 200}, {400, 260});
        const int hojas = rojos("e6.png").first; CHECK(hojas > aero + 300);
        c.setPincel(ind[3]); arrastrar({100, 350}, {400, 350});
        CHECK(rojos("e7.png").first > hojas + 300);
        c.setPincel(ind[0]); c.setColor(Qt::red); arrastrar({100, 450}, {400, 500});
        captura(c, "pinceles.png");
        // un simple toque deja un sello
        c.nuevoLienzo(800, 600); c.zoom100(); c.setPincel(ind[1]);
        QTest::mouseClick(&c, Qt::LeftButton, {}, QPoint(300, 300));
        { const auto [k, r] = rojos("e8.png"); CHECK(k > 100 && r.width() < 90); }

        // dentro de una selección: no se sale
        c.nuevoLienzo(800, 600); c.zoom100(); c.setPincel(ind[0]); c.setColor(Qt::red); c.setGrosor(40);
        c.setSeleccion(1); arrastrar({300, 100}, {500, 300}); c.setSeleccion(0);
        arrastrar({100, 200}, {700, 200});
        { const auto [k, r] = rojos("e9.png");
          const QPoint o2 = c.pantallaDe(QPointF(0, 0)).toPoint();
          CHECK(k > 200 && r.left() >= 300 - o2.x() - 1 && r.right() <= 500 - o2.x() + 1); }

        // el borrador sigue funcionando aunque el pincel sea de estampado
        c.deseleccionar(); c.nuevoLienzo(800, 600); c.zoom100(); c.setPincel(ind[1]); c.setGrosor(60);
        arrastrar({100, 300}, {500, 300});
        const int antes = rojos("e10.png").first;
        c.setBorrador(true); c.setGrosor(40); arrastrar({200, 300}, {400, 300}); c.setBorrador(false);
        CHECK(rojos("e11.png").first < antes);
        c.setPincel(0);
    }

    // 18) Exportar PNG, JPEG y WebP
    {
        c.nuevoLienzo(400, 300); c.zoom100(); c.setBorrador(false); c.setCubo(false); c.setSeleccion(0);
        c.setPincel(0); c.forma = plz::FORMA_RECT; c.formaRelleno = true; c.setColor(Qt::red);
        const QPoint org = c.pantallaDe(QPointF(0, 0)).toPoint();          // esquina del lienzo en pantalla
        QTest::mousePress(&c, Qt::LeftButton, {}, org + QPoint(100, 80));
        for (int i = 1; i <= 6; ++i) QTest::mouseMove(&c, org + QPoint(100 + i * 20, 80 + i * 15));
        QTest::mouseRelease(&c, Qt::LeftButton, {}, org + QPoint(220, 170));
        c.forma = plz::FORMA_LIBRE;
        const QPoint dentro(160, 125);
        const QPoint fuera = QPoint(5, 5);
        QString err;
        // PNG: exacto
        CHECK(c.exportar(T("x.png"), "png", 100, false, &err));
        { const QImage im(T("x.png")); CHECK(im.size() == QSize(400, 300) && im.pixelColor(dentro) == QColor(Qt::red) && im.pixelColor(fuera) == QColor(Qt::white)); }
        // JPEG: mismo tamaño, colores muy parecidos, menos calidad = archivo más pequeño
        CHECK(c.exportar(T("x95.jpg"), "jpg", 95, false, &err));
        CHECK(c.exportar(T("x10.jpg"), "jpeg", 10, false, &err));
        { const QImage im(T("x95.jpg"));
          CHECK(im.size() == QSize(400, 300) && !im.isNull());
          const QColor a = im.pixelColor(dentro), b = im.pixelColor(fuera);
          CHECK(a.red() > 235 && a.green() < 25 && a.blue() < 25 && b.red() > 240 && b.green() > 240 && b.blue() > 240); }
        CHECK(QFileInfo(T("x10.jpg")).size() < QFileInfo(T("x95.jpg")).size());
        // WebP: con pérdida y sin pérdida (100)
        if (QImageWriter::supportedImageFormats().contains("webp")) {
            CHECK(c.exportar(T("xl.webp"), "webp", 100, false, &err));
            CHECK(c.exportar(T("x50.webp"), "webp", 50, false, &err));
            { const QImage im(T("xl.webp"));
              CHECK(im.size() == QSize(400, 300) && im.pixelColor(dentro) == QColor(Qt::red) && im.pixelColor(fuera) == QColor(Qt::white)); }
            { const QImage im(T("x50.webp")); const QColor a = im.pixelColor(dentro);
              CHECK(!im.isNull() && a.red() > 230 && a.green() < 30 && a.blue() < 30); }
        } else {
            std::printf("(WebP no disponible en este Qt: se omiten sus pruebas)\n");
        }
        // errores claros: formato desconocido y ruta imposible
        err.clear();
        CHECK(!c.exportar(T("x.xyz"), "xyz", 90, false, &err) && err.contains("XYZ"));
        err.clear();
        CHECK(!c.exportar(T("no_existe/carpeta/x.jpg"), "jpg", 90, false, &err) && !err.isEmpty());
        // con una transformación a medias exporta lo aplicado, ya cerrada
        c.setSeleccion(1);
        QTest::mousePress(&c, Qt::LeftButton, {}, org + QPoint(50, 40));
        QTest::mouseMove(&c, org + QPoint(300, 250));
        QTest::mouseRelease(&c, Qt::LeftButton, {}, org + QPoint(300, 250));
        c.setSeleccion(0);
        c.iniciarTransformacion();
        CHECK(c.transformandoAhora());
        CHECK(c.exportar(T("xt.jpg"), "jpg", 90, false, &err) && !c.transformandoAhora());
    }

    std::printf(fallos ? "\n%d FALLOS\n" : "\nSMOKE OK\n", fallos);
    return fallos ? 1 : 0;
}
