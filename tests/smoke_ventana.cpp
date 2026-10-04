// Prueba de humo de la ventana completa: se construye, se manejan sus botones, paneles y menús.
// (Linux sin monitor: QT_QPA_PLATFORM=xcb xvfb-run -a ./smoke_ventana)
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <cstdio>
#include "ventana.h"

static int fallos = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALLO linea %d: %s\n", __LINE__, #c); ++fallos; } } while (0)

static QAction *accion(QWidget &w, const QString &texto) {
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text() == texto) return a;
    return nullptr;
}
static QPushButton *boton(QWidget &w, const QString &texto) {
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text() == texto) return b;
    return nullptr;
}

int main(int argc, char **argv) {
    QSurfaceFormat fmt; fmt.setDepthBufferSize(24); fmt.setStencilBufferSize(8); fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir ajustes;                                  // los recientes y preferencias no tocan los reales
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, ajustes.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, ajustes.path());
    QCoreApplication::setOrganizationName("PruebaLapizTest");
    QCoreApplication::setApplicationName("PruebaLapizTest");

    QTemporaryDir carpetaRec;                               // la copia de recuperación tampoco toca la real
    VentanaPrincipal v(false, carpetaRec.path());
    v.show();
    v.canvas->nuevoLienzo(800, 600);
    v.canvas->zoom100();
    QTest::qWait(60);

    // Título y capas
    CHECK(v.windowTitle().startsWith("Sin título"));
    QListWidget *lista = v.findChild<QListWidget *>();
    CHECK(lista && lista->count() == 1);
    QPushButton *mas = boton(v, "+");
    CHECK(mas);
    if (mas) mas->click();
    CHECK(v.canvas->numCapas() == 2 && lista->count() == 2);
    if (QPushButton *menos = boton(v, "−")) menos->click();
    CHECK(v.canvas->numCapas() == 1 && lista->count() == 1);

    // Herramientas excluyentes
    QAction *aCubo = accion(v, "Cubo"), *aBorrador = accion(v, "Borrador"), *aSel = accion(v, "Selección");
    CHECK(aCubo && aBorrador && aSel);
    aCubo->setChecked(true);
    aBorrador->setChecked(true);
    CHECK(!aCubo->isChecked() && aBorrador->isChecked());
    aSel->setChecked(true);
    CHECK(!aBorrador->isChecked());
    aSel->setChecked(false);

    // Dibujar marca el documento como modificado (el título lleva *)
    v.canvas->setColor(Qt::red);
    QTest::mousePress(v.canvas, Qt::LeftButton, {}, QPoint(200, 200));
    for (int i = 1; i <= 8; ++i) QTest::mouseMove(v.canvas, QPoint(200 + i * 30, 200 + i * 10));
    QTest::mouseRelease(v.canvas, Qt::LeftButton, {}, QPoint(440, 280));
    CHECK(v.canvas->modificado());
    CHECK(v.windowTitle().contains("*"));

    // Polígono desde el selector de forma de la barra; Aplicar aparece solo durante la sesión
    QComboBox *cForma = nullptr;
    for (QComboBox *c : v.findChildren<QComboBox *>()) if (c->findText("Polígono") >= 0) cForma = c;
    CHECK(cForma);
    QAction *aAplicar = accion(v, "✔ Aplicar"), *aCancelar = accion(v, "✖ Cancelar"), *aTransformar = accion(v, "Transformar");
    CHECK(aAplicar && aCancelar && aTransformar);
    CHECK(!aAplicar->isVisible() && aTransformar->isEnabled());
    cForma->setCurrentIndex(cForma->findText("Polígono"));
    for (QPoint p : {QPoint(100, 400), QPoint(250, 400), QPoint(250, 520)}) QTest::mouseClick(v.canvas, Qt::LeftButton, {}, p);
    CHECK(v.canvas->sesionActiva() && aAplicar->isVisible() && aCancelar->isVisible() && !aTransformar->isEnabled());
    aAplicar->trigger();
    CHECK(!v.canvas->sesionActiva() && !aAplicar->isVisible() && aTransformar->isEnabled());
    cForma->setCurrentIndex(0);

    // Transformar desde la barra: sin selección no hace nada; con selección abre la sesión y Cancelar la cierra
    aTransformar->trigger();
    CHECK(!v.canvas->sesionActiva());
    v.canvas->seleccionarTodo();
    aTransformar->trigger();
    CHECK(v.canvas->transformandoAhora() && aAplicar->isVisible());
    aCancelar->trigger();
    CHECK(!v.canvas->sesionActiva() && !aAplicar->isVisible());

    // Color: el campo hex llega al lienzo
    QLineEdit *hex = nullptr;
    for (QLineEdit *e : v.findChildren<QLineEdit *>()) if (e->maxLength() == 7) hex = e;
    CHECK(hex);
    hex->setText("#00ff00");
    hex->editingFinished();
    CHECK(v.canvas->color() == QColor("#00ff00"));

    if (qEnvironmentVariableIsSet("PLZ_CAPTURAS")) {       // captura de la ventana entera para revisarla a ojo
        v.resize(1300, 800);
        QTest::qWait(80);
        v.grab().save(QDir(qEnvironmentVariable("PLZ_CAPTURAS")).filePath("ventana.png"));
    }

    // Fusión y pinceles nuevos aparecen en los paneles
    QComboBox *cFus = nullptr, *cPin = nullptr;
    for (QComboBox *c : v.findChildren<QComboBox *>()) {
        if (c->findText("Multiplicar") >= 0) cFus = c;
        if (c->findText("Aerógrafo") >= 0) cPin = c;
    }
    CHECK(cFus && cPin && cPin->findText("Hojas") >= 0 && cPin->findText("Estrellas") >= 0);
    cFus->setCurrentIndex(cFus->findText("Multiplicar"));
    CHECK(v.canvas->capa(v.canvas->indiceActivo()).fusion == plz::Fusion::Multiplicar);
    cFus->setCurrentIndex(0);
    CHECK(v.canvas->capa(v.canvas->indiceActivo()).fusion == plz::Fusion::Normal);

    // ---------- Guardado automático ----------
    {
        Autoguardado &a = *v.autoguardado;
        auto trazo = [&](int y) {
            v.canvas->setColor(Qt::black);
            QTest::mousePress(v.canvas, Qt::LeftButton, {}, QPoint(100, y));
            for (int i = 1; i <= 6; ++i) QTest::mouseMove(v.canvas, QPoint(100 + i * 40, y + i));
            QTest::mouseRelease(v.canvas, Qt::LeftButton, {}, QPoint(340, y + 6));
        };
        v.canvas->nuevoLienzo(800, 600);
        v.canvas->zoom100();
        CHECK(a.disponible() && a.activo() && !a.hayRecuperacion());
        a.tick();
        CHECK(!QFile::exists(a.archivo()));                              // nada que guardar
        trazo(100);
        CHECK(v.canvas->modificado());
        a.tick();
        CHECK(QFile::exists(a.archivo()));
        { QFile f(a.archivo()); f.open(QIODevice::ReadOnly);
          const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
          CHECK(o["version"].toInt() == 5 && !o["capas"].toArray().isEmpty()); }
        CHECK(v.canvas->modificado());                                   // una copia no cuenta como "guardado"
        QFile::remove(a.archivo());
        a.tick();
        CHECK(!QFile::exists(a.archivo()));                              // sin cambios nuevos no reescribe
        trazo(200);
        a.tick();
        CHECK(QFile::exists(a.archivo()));                               // con cambios nuevos, sí
        // a medio trazo no guarda
        QFile::remove(a.archivo());
        QTest::mousePress(v.canvas, Qt::LeftButton, {}, QPoint(100, 300));
        QTest::mouseMove(v.canvas, QPoint(200, 310));
        CHECK(v.canvas->ocupado());
        a.tick();
        CHECK(!QFile::exists(a.archivo()));
        QTest::mouseRelease(v.canvas, Qt::LeftButton, {}, QPoint(200, 310));
        a.tick();
        CHECK(QFile::exists(a.archivo()));
        // ocultar una capa también es un cambio
        QFile::remove(a.archivo());
        v.canvas->setVisible(0, false); v.canvas->setVisible(0, true);
        a.tick();
        CHECK(QFile::exists(a.archivo()));
        // menú: apagar y encender, y se recuerda
        QAction *aAuto = accion(v, "Guardado automático");
        CHECK(aAuto && aAuto->isChecked());
        aAuto->setChecked(false);
        CHECK(!a.activo());
        QFile::remove(a.archivo());
        trazo(400);
        a.tick();
        CHECK(!QFile::exists(a.archivo()));                              // apagado: no guarda
        aAuto->setChecked(true);
        a.tick();
        CHECK(QFile::exists(a.archivo()));

        // otra instancia a la vez: no toca la carpeta ni ofrece recuperar lo ajeno
        { VentanaPrincipal otra(false, carpetaRec.path());
          CHECK(!otra.autoguardado->disponible() && !otra.autoguardado->hayRecuperacion());
          otra.autoguardado->tick(); }
        CHECK(QFile::exists(a.archivo()));

        // cierre normal: tras guardar, la copia desaparece
        CHECK(v.canvas->guardar(QDir(carpetaRec.path()).filePath("final.plz")));
        CHECK(QFile::exists(a.archivo()));
        v.close();                                                       // no hay cambios: cierra sin preguntar
        CHECK(!QFile::exists(a.archivo()));
    }

    // Caída: la ventana desaparece sin cerrarse y la copia queda; la siguiente vez se puede recuperar
    {
        QTemporaryDir rec2;
        QPoint puntoDoc;                                                     // un punto dentro del rectángulo, en coordenadas del lienzo
        {
            VentanaPrincipal caida(false, rec2.path());
            caida.show();
            caida.canvas->nuevoLienzo(800, 600);
            caida.canvas->zoom100();
            caida.canvas->setColor(Qt::red); caida.canvas->forma = plz::FORMA_RECT; caida.canvas->formaRelleno = true;
            QTest::mousePress(caida.canvas, Qt::LeftButton, {}, QPoint(200, 200));
            for (int i = 1; i <= 6; ++i) QTest::mouseMove(caida.canvas, QPoint(200 + i * 20, 200 + i * 15));
            QTest::mouseRelease(caida.canvas, Qt::LeftButton, {}, QPoint(320, 290));
            puntoDoc = QPoint(260, 245) - caida.canvas->pantallaDe(QPointF(0, 0)).toPoint();
            caida.autoguardado->guardarAhora();
        }                                                                // (se destruye sin closeEvent = como una caída)
        VentanaPrincipal nueva(false, rec2.path());
        CHECK(nueva.autoguardado->disponible() && nueva.autoguardado->hayRecuperacion());
        CHECK(nueva.autoguardado->origenRecuperado().isEmpty());
        nueva.show();
        nueva.canvas->nuevoLienzo(800, 600);
        CHECK(nueva.canvas->abrir(nueva.autoguardado->archivo()));
        nueva.canvas->marcarModificado();
        CHECK(nueva.canvas->modificado());
        nueva.canvas->exportarPNG(QDir(rec2.path()).filePath("r.png"));
        const QImage im(QDir(rec2.path()).filePath("r.png"));
        CHECK(im.pixelColor(puntoDoc) == QColor(Qt::red));                   // lo dibujado volvió
        nueva.autoguardado->quitar();
        CHECK(!nueva.autoguardado->hayRecuperacion());
    }

    std::printf(fallos ? "\n%d FALLOS\n" : "\nSMOKE VENTANA OK\n", fallos);
    return fallos ? 1 : 0;
}
