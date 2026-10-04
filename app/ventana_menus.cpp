#include "ventana.h"
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <functional>

void VentanaPrincipal::crearMenus() {
    // ---------- Archivo ----------
    QMenu *mArchivo = menuBar()->addMenu("&Archivo");

    QAction *aNuevo = mArchivo->addAction("Nuevo...");
    aNuevo->setShortcut(QKeySequence::New);
    connect(aNuevo, &QAction::triggered, this, [this]() { pantallaInicio(); });

    QAction *aImportar = mArchivo->addAction("Importar imagen como plantilla...");
    connect(aImportar, &QAction::triggered, this, [this]() {
        const QString r = QFileDialog::getOpenFileName(this, "Imagen de plantilla", "",
                                                       "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)");
        if (!r.isEmpty()) importarPlantilla(r);
    });

    QAction *aAbrir = mArchivo->addAction("Abrir...");
    aAbrir->setShortcut(QKeySequence::Open);
    connect(aAbrir, &QAction::triggered, this, [this]() {
        if (!confirmar()) return;
        const QString r = QFileDialog::getOpenFileName(this, "Abrir dibujo", ruta, "Dibujo (*.plz)");
        if (!r.isEmpty()) abrirArchivo(r);
    });

    QAction *aGuardar = mArchivo->addAction("Guardar");
    aGuardar->setShortcut(QKeySequence::Save);
    connect(aGuardar, &QAction::triggered, this, [this]() { guardar(); });

    QAction *aGuardarComo = mArchivo->addAction("Guardar como...");
    aGuardarComo->setShortcut(QKeySequence::SaveAs);
    connect(aGuardarComo, &QAction::triggered, this, [this]() { guardarComo(); });

    mArchivo->addSeparator();

    QAction *aExportar = mArchivo->addAction("Exportar imagen (PNG, JPEG, WebP)...");
    aExportar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));

    QAction *aIncluir = mArchivo->addAction("Incluir plantilla al exportar");
    aIncluir->setCheckable(true);

    connect(aExportar, &QAction::triggered, this, [this, aIncluir]() { exportarImagen(aIncluir->isChecked()); });

    mArchivo->addSeparator();

    // Guardado automático: una copia de recuperación cada cierto tiempo; nunca pisa tu archivo
    QAction *aAuto = mArchivo->addAction("Guardado automático");
    aAuto->setCheckable(true);
    aAuto->setChecked(autoguardado->activo());
    aAuto->setToolTip("Guarda una copia de recuperación cada cierto tiempo. Si el programa se cae, te la ofrece al abrirlo.");
    connect(aAuto, &QAction::toggled, this, [this](bool on) {
        autoguardado->setActivo(on);
        cfg.setValue("autoguardado/activo", on);
    });
    QMenu *mIntervalo = mArchivo->addMenu("Intervalo del guardado automático");
    QActionGroup *grupo = new QActionGroup(this);
    for (const auto &op : {std::make_pair(QString("30 segundos"), 30), std::make_pair(QString("1 minuto"), 60),
                           std::make_pair(QString("2 minutos"), 120), std::make_pair(QString("5 minutos"), 300)}) {
        QAction *a = mIntervalo->addAction(op.first);
        a->setCheckable(true);
        a->setChecked(autoguardado->intervalo() == op.second);
        grupo->addAction(a);
        const int seg = op.second;
        connect(a, &QAction::triggered, this, [this, seg]() {
            autoguardado->setIntervalo(seg);
            cfg.setValue("autoguardado/segundos", seg);
        });
    }

    // ---------- Editar ----------
    QMenu *mEditar = menuBar()->addMenu("&Editar");
    auto accionEditar = [this, mEditar](const QString &texto, const QKeySequence &tecla, std::function<void()> f) {
        QAction *a = mEditar->addAction(texto);
        a->setShortcut(tecla);
        connect(a, &QAction::triggered, this, [f]() { f(); });
    };
    accionEditar("Copiar", QKeySequence(Qt::CTRL | Qt::Key_C), [this]() { canvas->copiar(false); });
    accionEditar("Cortar", QKeySequence(Qt::CTRL | Qt::Key_X), [this]() { canvas->copiar(true); });
    accionEditar("Pegar", QKeySequence(Qt::CTRL | Qt::Key_V), [this]() { canvas->pegar(); });
    accionEditar("Borrar selección", QKeySequence(Qt::Key_Delete), [this]() { canvas->borrarSel(); });
    mEditar->addSeparator();
    accionEditar("Seleccionar todo", QKeySequence(Qt::CTRL | Qt::Key_A), [this]() { canvas->seleccionarTodo(); });
    accionEditar("Deseleccionar", QKeySequence(Qt::CTRL | Qt::Key_D), [this]() { canvas->deseleccionar(); });

    // ---------- Ver ----------
    QMenu *mVer = menuBar()->addMenu("&Ver");

    QAction *aAcercar = mVer->addAction("Acercar");
    aAcercar->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(aAcercar, &QAction::triggered, this, [this]() { canvas->acercar(); });

    QAction *aAlejar = mVer->addAction("Alejar");
    aAlejar->setShortcut(QKeySequence::ZoomOut);
    connect(aAlejar, &QAction::triggered, this, [this]() { canvas->alejar(); });

    QAction *aAjustar = mVer->addAction("Ajustar a la ventana");
    aAjustar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(aAjustar, &QAction::triggered, this, [this]() { canvas->ajustar(); });

    QAction *aCien = mVer->addAction("Tamaño real (100%)");
    aCien->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(aCien, &QAction::triggered, this, [this]() { canvas->zoom100(); });
}
