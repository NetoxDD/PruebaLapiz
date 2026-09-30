#include <QApplication>
#include <QMainWindow>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSlider>
#include <QLabel>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QCloseEvent>
#include <QStatusBar>
#include <QPixmap>
#include <QIcon>
#include <functional>
#include "canvas.h"

static QIcon iconoColor(const QColor &c) {
    QPixmap pm(24, 24);
    pm.fill(c);
    return QIcon(pm);
}

// Ventana que pregunta antes de cerrar si hay cambios sin guardar
class Ventana : public QMainWindow {
public:
    std::function<bool()> puedeCerrar;
protected:
    void closeEvent(QCloseEvent *e) override {
        if (!puedeCerrar || puedeCerrar()) e->accept(); else e->ignore();
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    Ventana ventana;
    ventana.resize(1200, 800);

    Canvas *canvas = new Canvas;
    ventana.setCentralWidget(canvas);

    QString ruta;   // archivo actual (vacío = sin guardar todavía)

    auto titulo = [&]() {
        const QString nombre = ruta.isEmpty() ? "Sin título" : QFileInfo(ruta).fileName();
        ventana.setWindowTitle(QString("%1%2 - PruebaLapiz")
                                   .arg(nombre, canvas->modificado() ? "*" : ""));
    };
    canvas->alCambiarInfo = [&](const QString &s) {
        ventana.statusBar()->showMessage(s);
        titulo();
    };
    titulo();

    // ---------- Archivo ----------
    auto guardarComo = [&]() -> bool {
        QString r = QFileDialog::getSaveFileName(&ventana, "Guardar dibujo", ruta,
                                                 "Dibujo (*.plz)");
        if (r.isEmpty()) return false;
        if (!r.endsWith(".plz", Qt::CaseInsensitive)) r += ".plz";
        if (!canvas->guardar(r)) {
            QMessageBox::warning(&ventana, "Error", "No se pudo guardar el archivo.");
            return false;
        }
        ruta = r;
        titulo();
        return true;
    };
    auto guardar = [&]() -> bool {
        if (ruta.isEmpty()) return guardarComo();
        if (!canvas->guardar(ruta)) {
            QMessageBox::warning(&ventana, "Error", "No se pudo guardar el archivo.");
            return false;
        }
        titulo();
        return true;
    };
    // true = se puede continuar (se guardó o se descartó), false = cancelar
    auto confirmar = [&]() -> bool {
        if (!canvas->modificado()) return true;
        const auto r = QMessageBox::question(&ventana, "Cambios sin guardar",
                                             "Hay cambios sin guardar. ¿Quieres guardarlos?",
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Save) return guardar();
        return r == QMessageBox::Discard;
    };
    ventana.puedeCerrar = confirmar;

    QMenu *mArchivo = ventana.menuBar()->addMenu("&Archivo");

    QAction *aNuevo = mArchivo->addAction("Nuevo");
    aNuevo->setShortcut(QKeySequence::New);
    QObject::connect(aNuevo, &QAction::triggered, [&]() {
        if (!confirmar()) return;
        canvas->limpiar();
        ruta.clear();
        titulo();
    });

    QAction *aAbrir = mArchivo->addAction("Abrir...");
    aAbrir->setShortcut(QKeySequence::Open);
    QObject::connect(aAbrir, &QAction::triggered, [&]() {
        if (!confirmar()) return;
        const QString r = QFileDialog::getOpenFileName(&ventana, "Abrir dibujo", ruta,
                                                       "Dibujo (*.plz)");
        if (r.isEmpty()) return;
        if (!canvas->abrir(r)) {
            QMessageBox::warning(&ventana, "Error", "No se pudo abrir el archivo.");
            return;
        }
        ruta = r;
        titulo();
    });

    QAction *aGuardar = mArchivo->addAction("Guardar");
    aGuardar->setShortcut(QKeySequence::Save);
    QObject::connect(aGuardar, &QAction::triggered, [&]() { guardar(); });

    QAction *aGuardarComo = mArchivo->addAction("Guardar como...");
    aGuardarComo->setShortcut(QKeySequence::SaveAs);
    QObject::connect(aGuardarComo, &QAction::triggered, [&]() { guardarComo(); });

    mArchivo->addSeparator();

    QAction *aExportar = mArchivo->addAction("Exportar PNG...");
    aExportar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    QObject::connect(aExportar, &QAction::triggered, [&]() {
        QString r = QFileDialog::getSaveFileName(&ventana, "Exportar imagen", "", "Imagen PNG (*.png)");
        if (r.isEmpty()) return;
        if (!r.endsWith(".png", Qt::CaseInsensitive)) r += ".png";
        if (!canvas->exportarPNG(r))
            QMessageBox::warning(&ventana, "Error", "No se pudo exportar la imagen.");
        else
            ventana.statusBar()->showMessage("Exportado: " + r, 4000);
    });

    // ---------- Barra de herramientas ----------
    QToolBar *barra = ventana.addToolBar("Herramientas");
    barra->setMovable(false);

    QAction *aColor = barra->addAction(iconoColor(canvas->color()), "Color");
    QObject::connect(aColor, &QAction::triggered, [=, &ventana]() {
        const QColor c = QColorDialog::getColor(canvas->color(), &ventana, "Elige un color");
        if (c.isValid()) {
            canvas->setColor(c);
            aColor->setIcon(iconoColor(c));
        }
    });

    barra->addSeparator();

    barra->addWidget(new QLabel(" Tamaño: "));
    QSlider *tamano = new QSlider(Qt::Horizontal);
    tamano->setRange(1, 100);
    tamano->setValue(16);
    tamano->setFixedWidth(160);
    barra->addWidget(tamano);
    QLabel *etiqueta = new QLabel(" 16 ");
    barra->addWidget(etiqueta);
    QObject::connect(tamano, &QSlider::valueChanged, [=](int v) {
        canvas->setGrosor(v);
        etiqueta->setText(QString(" %1 ").arg(v));
    });

    barra->addSeparator();

    QAction *aBorrador = barra->addAction("Borrador");
    aBorrador->setCheckable(true);
    aBorrador->setShortcut(QKeySequence(Qt::Key_E));
    QObject::connect(aBorrador, &QAction::toggled, [=](bool activo) {
        canvas->setBorrador(activo);
    });

    barra->addSeparator();

    QAction *aDeshacer = barra->addAction("Deshacer");
    aDeshacer->setShortcut(QKeySequence::Undo);
    QObject::connect(aDeshacer, &QAction::triggered, [=]() { canvas->deshacer(); });

    QAction *aRehacer = barra->addAction("Rehacer");
    aRehacer->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    QObject::connect(aRehacer, &QAction::triggered, [=]() { canvas->rehacer(); });

    ventana.show();
    return app.exec();
}