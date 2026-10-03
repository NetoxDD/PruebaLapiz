#include <QApplication>
#include <QMainWindow>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QColorDialog>
#include <QCheckBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QCloseEvent>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QSurfaceFormat>
#include <QTimer>
#include <QPixmap>
#include <QIcon>
#include <functional>
#include <cmath>
#include "canvas.h"
#include "selector_color.h"
#include <QToolButton>
#include <QStringList>
#include <QList>
#include <QLineEdit>
#include <QSpinBox>
#include <QPainter>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QImage>
#include <QDialogButtonBox>
#include <QDir>
#include <QSettings>
#include <QRadioButton>
#include <QCoreApplication>

// 1 = pedir la GPU dedicada en portátiles con dos GPU (Windows).
// 0 = dejar que Windows decida (recomendado por batería y latencia).
#define PREFERIR_GPU_DEDICADA 0

#if defined(_WIN32) && PREFERIR_GPU_DEDICADA
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

static QIcon iconoColor(const QColor &c) {
    QPixmap pm(24, 24);
    pm.fill(c);
    return QIcon(pm);
}

class Ventana : public QMainWindow {
public:
    std::function<bool()> puedeCerrar;
protected:
    void closeEvent(QCloseEvent *e) override {
        if (!puedeCerrar || puedeCerrar()) e->accept(); else e->ignore();
    }
};
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
    QSettings cfg;   // aquí se guardan los recientes

    Ventana ventana;
    ventana.resize(1300, 800);

    Canvas *canvas = new Canvas;
    ventana.setCentralWidget(canvas);

    // ---------- Panel de capas ----------
    QDockWidget *dock = new QDockWidget("Capas", &ventana);
    QWidget *panel = new QWidget;
    QVBoxLayout *vl = new QVBoxLayout(panel);
    QListWidget *lista = new QListWidget;
    vl->addWidget(lista);

    QHBoxLayout *hl = new QHBoxLayout;
    QPushButton *bNueva = new QPushButton("+");
    QPushButton *bBorrar = new QPushButton("−");
    QPushButton *bSube = new QPushButton("▲");
    QPushButton *bBaja = new QPushButton("▼");
    for (QPushButton *b : {bNueva, bBorrar, bSube, bBaja}) {
        b->setFocusPolicy(Qt::NoFocus);
        hl->addWidget(b);
    }
    vl->addLayout(hl);

    QSlider *sOp = new QSlider(Qt::Horizontal);
    sOp->setRange(0, 100);
    sOp->setValue(100);
    sOp->setFocusPolicy(Qt::NoFocus);
    QCheckBox *cBloq = new QCheckBox("Bloqueada");
    cBloq->setFocusPolicy(Qt::NoFocus);
    QFormLayout *fl = new QFormLayout;
    fl->addRow("Opacidad:", sOp);
    fl->addRow(cBloq);
    vl->addLayout(fl);

    dock->setWidget(panel);
    ventana.addDockWidget(Qt::RightDockWidgetArea, dock);

    auto sincronizar = [=]() {
        QSignalBlocker b1(sOp), b2(cBloq);
        const Capa &c = canvas->capa(canvas->indiceActivo());
        sOp->setValue(qRound(c.opacidad * 100));
        cBloq->setChecked(c.bloqueada);
    };
    auto refrescar = [=]() {
        {
            QSignalBlocker b(lista);
            lista->clear();
            const int n = canvas->numCapas();
            for (int r = 0; r < n; ++r) {        // la capa de arriba va primero en la lista
                const Capa &c = canvas->capa(n - 1 - r);
                QListWidgetItem *it = new QListWidgetItem(QString::fromStdString(c.nombre));
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
                it->setCheckState(c.visible ? Qt::Checked : Qt::Unchecked);
                lista->addItem(it);
            }
            lista->setCurrentRow(n - 1 - canvas->indiceActivo());
        }
        sincronizar();
    };
    canvas->alCambiarCapas = [=]() { refrescar(); };

    QObject::connect(lista, &QListWidget::currentRowChanged, [=](int r) {
        if (r < 0) return;
        canvas->setActiva(canvas->numCapas() - 1 - r);
        sincronizar();
    });
    QObject::connect(lista, &QListWidget::itemChanged, [=](QListWidgetItem *it) {
        const int idx = canvas->numCapas() - 1 - lista->row(it);
        canvas->setVisible(idx, it->checkState() == Qt::Checked);
        canvas->setNombre(idx, it->text());
    });
    QObject::connect(bNueva, &QPushButton::clicked, [=]() { canvas->nuevaCapa(); });
    QObject::connect(bBorrar, &QPushButton::clicked, [=]() { canvas->borrarCapa(canvas->indiceActivo()); });
    QObject::connect(bSube, &QPushButton::clicked, [=]() { canvas->moverCapa(canvas->indiceActivo(), +1); });
    QObject::connect(bBaja, &QPushButton::clicked, [=]() { canvas->moverCapa(canvas->indiceActivo(), -1); });
    QObject::connect(sOp, &QSlider::valueChanged, [=](int v) {
        canvas->setOpacidad(canvas->indiceActivo(), v / 100.0);
    });
    QObject::connect(cBloq, &QCheckBox::toggled, [=](bool b) {
        canvas->setBloqueada(canvas->indiceActivo(), b);
    });

    // ---------- Diálogo de opciones de tableta ----------
    QDialog *dlg = new QDialog(&ventana);
    dlg->setWindowTitle("Opciones de tableta");
    QFormLayout *form = new QFormLayout(dlg);

    auto nuevoSlider = [](int min, int max, int val) {
        QSlider *s = new QSlider(Qt::Horizontal);
        s->setRange(min, max);
        s->setValue(val);
        s->setMinimumWidth(240);
        return s;
    };

    QCheckBox *cPresion = new QCheckBox("Usar la presión del lápiz");
    cPresion->setChecked(true);
    form->addRow(cPresion);
    QObject::connect(cPresion, &QCheckBox::toggled, [=](bool v) { canvas->lapiz.usarPresion = v; });

    QSlider *sCurva = nuevoSlider(0, 100, 50);
    form->addRow("Curva (firme ↔ suave):", sCurva);
    QObject::connect(sCurva, &QSlider::valueChanged, [=](int v) {
        canvas->lapiz.gamma = std::pow(2.0, (50 - v) / 25.0);
    });

    QSlider *sEfecto = nuevoSlider(0, 95, 50);
    form->addRow("Efecto de la presión en el grosor:", sEfecto);
    QObject::connect(sEfecto, &QSlider::valueChanged, [=](int v) {
        canvas->lapiz.efectoPresion = v / 100.0;
    });

    QSlider *sSuav = nuevoSlider(0, 90, 50);
    form->addRow("Suavizado del trazo:", sSuav);
    QObject::connect(sSuav, &QSlider::valueChanged, [=](int v) {
        canvas->lapiz.suavizado = v / 100.0;
    });

    QComboBox *cBoton = new QComboBox;
    cBoton->addItems({"Sin acción", "Borrador", "Mover el lienzo"});
    form->addRow("Botón del lápiz:", cBoton);
    QObject::connect(cBoton, &QComboBox::currentIndexChanged, [=](int i) { canvas->lapiz.botonLapiz = i; });

    QLabel *diag = new QLabel("Acerca el lápiz al lienzo para ver sus datos.");
    diag->setWordWrap(true);
    diag->setMinimumWidth(380);
    form->addRow("Diagnóstico:", diag);

    QLabel *lblGPU = new QLabel("GPU: (iniciando...)");
    lblGPU->setWordWrap(true);
    form->addRow(lblGPU);

    // ---------- Recientes, abrir e importar ----------
    QString ruta;
    auto titulo = [&]() {
        const QString nombre = ruta.isEmpty() ? "Sin título" : QFileInfo(ruta).fileName();
        ventana.setWindowTitle(QString("%1%2 - PruebaLapiz")
                                   .arg(nombre, canvas->modificado() ? "*" : ""));
    };

    auto recordar = [&](const QString &clave, const QString &r) {
        QStringList l = cfg.value(clave).toStringList();
        l.removeAll(r);
        l.prepend(r);
        while (l.size() > 12) l.removeLast();
        cfg.setValue(clave, l);
    };
    auto existentes = [&](const QString &clave) {
        QStringList out;
        for (const QString &r : cfg.value(clave).toStringList())
            if (QFileInfo::exists(r)) out << r;
        return out;
    };

    auto abrirArchivo = [&](const QString &r) -> bool {
        if (!canvas->abrir(r)) {
            QMessageBox::warning(&ventana, "Error", "No se pudo abrir el archivo.");
            return false;
        }
        ruta = r;
        recordar("proyectosRecientes", r);
        titulo();
        return true;
    };

    auto importarPlantilla = [&](const QString &r) -> bool {
        const QImage im(r);
        if (im.isNull()) {
            QMessageBox::warning(&ventana, "Error", "No se pudo leer la imagen.");
            return false;
        }
        canvas->cargarPlantilla(im, QFileInfo(r).completeBaseName());
        recordar("plantillasRecientes", r);
        return true;
    };


    // ---------- Título y estado ----------

    canvas->alCambiarInfo = [&](const QString &s) {
        ventana.statusBar()->showMessage(s);
        titulo();
    };
    canvas->alDatosLapiz = [=](const QString &s) { diag->setText(s); };
    canvas->alIniciarGL = [=, &ventana](const QString &s) {
        lblGPU->setText(s);
        ventana.statusBar()->showMessage(s, 8000);
    };
    refrescar();
    titulo();

    // ---------- Archivo ----------
    auto guardarComo = [&]() -> bool {
        QString r = QFileDialog::getSaveFileName(&ventana, "Guardar dibujo", ruta, "Dibujo (*.plz)");
        if (r.isEmpty()) return false;
        if (!r.endsWith(".plz", Qt::CaseInsensitive)) r += ".plz";
        if (!canvas->guardar(r)) {
            QMessageBox::warning(&ventana, "Error", "No se pudo guardar el archivo.");
            return false;
        }
        ruta = r;
        recordar("proyectosRecientes", r);
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
    auto confirmar = [&]() -> bool {
        if (!canvas->modificado()) return true;
        const auto r = QMessageBox::question(&ventana, "Cambios sin guardar",
                                             "Hay cambios sin guardar. ¿Quieres guardarlos?",
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Save) return guardar();
        return r == QMessageBox::Discard;
    };
    ventana.puedeCerrar = confirmar;


    // ---------- Pantalla de inicio ----------
    auto pantallaInicio = [&]() {
        QDialog d(&ventana);
        d.setWindowTitle("Inicio");
        QVBoxLayout *principal = new QVBoxLayout(&d);
        QHBoxLayout *columnas = new QHBoxLayout;
        principal->addLayout(columnas);

        // --- Izquierda: nuevo dibujo ---
        QVBoxLayout *izq = new QVBoxLayout;
        columnas->addLayout(izq);
        izq->addWidget(new QLabel("<b>Nuevo dibujo</b>"));

        QComboBox *preset = new QComboBox;
        preset->addItem("Personalizado", QSize(0, 0));
        preset->addItem("2000 × 1500 (por defecto)", QSize(2000, 1500));
        preset->addItem("1920 × 1080 (HD)", QSize(1920, 1080));
        preset->addItem("2480 × 3508 (A4 a 300 ppp)", QSize(2480, 3508));
        preset->addItem("2000 × 2000 (cuadrado)", QSize(2000, 2000));
        preset->addItem("3840 × 2160 (4K)", QSize(3840, 2160));
        preset->addItem("1080 × 1920 (vertical)", QSize(1080, 1920));
        QSpinBox *sW = new QSpinBox, *sH = new QSpinBox;
        for (QSpinBox *s : {sW, sH}) { s->setRange(100, 6000); s->setSuffix(" px"); }
        sW->setValue(canvas->ancho());
        sH->setValue(canvas->alto());
        QFormLayout *ft = new QFormLayout;
        ft->addRow("Medidas:", preset);
        ft->addRow("Ancho:", sW);
        ft->addRow("Alto:", sH);
        izq->addLayout(ft);

        QRadioButton *rBlanco = new QRadioButton("Lienzo en blanco");
        QRadioButton *rPlant = new QRadioButton("Con la plantilla seleccionada");
        rBlanco->setChecked(true);
        QCheckBox *cTam = new QCheckBox("Ajustar el lienzo al tamaño de la imagen");
        izq->addSpacing(10);
        izq->addWidget(rBlanco);
        izq->addWidget(rPlant);
        izq->addWidget(cTam);
        izq->addStretch();

        // --- Derecha: plantillas y proyectos ---
        QVBoxLayout *der = new QVBoxLayout;
        columnas->addLayout(der);
        der->addWidget(new QLabel("<b>Plantillas que has usado</b>"));
        QListWidget *gal = new QListWidget;
        gal->setViewMode(QListView::IconMode);
        gal->setIconSize(QSize(140, 100));
        gal->setResizeMode(QListView::Adjust);
        gal->setMovement(QListView::Static);
        gal->setSpacing(8);
        gal->setMinimumSize(520, 230);
        der->addWidget(gal);
        QPushButton *bImportar = new QPushButton("Importar imagen...");
        der->addWidget(bImportar, 0, Qt::AlignLeft);

        der->addWidget(new QLabel("<b>Proyectos recientes</b>"));
        QListWidget *proy = new QListWidget;
        proy->setMinimumHeight(110);
        der->addWidget(proy);

        auto llenar = [&]() {
            gal->clear();
            for (const QString &r : existentes("plantillasRecientes")) {
                const QImage im(r);
                if (im.isNull()) continue;
                const QImage m = im.scaled(140, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(m)),
                                               QFileInfo(r).completeBaseName());
                it->setData(Qt::UserRole, r);
                it->setToolTip(r);
                gal->addItem(it);
            }
        };
        llenar();
        for (const QString &r : existentes("proyectosRecientes")) {
            auto *it = new QListWidgetItem(QFileInfo(r).fileName());
            it->setData(Qt::UserRole, r);
            it->setToolTip(r);
            proy->addItem(it);
        }

        // Tamaño según preset y según la imagen elegida
        QObject::connect(preset, &QComboBox::currentIndexChanged, [&](int i) {
            const QSize s = preset->itemData(i).toSize();
            if (s.width() > 0) { sW->setValue(s.width()); sH->setValue(s.height()); }
        });
        auto ajustarTam = [&]() {
            if (!cTam->isChecked() || !gal->currentItem()) return;
            const QImage im(gal->currentItem()->data(Qt::UserRole).toString());
            if (im.isNull()) return;
            QSize s = im.size();
            if (s.width() > 6000 || s.height() > 6000) s.scale(6000, 6000, Qt::KeepAspectRatio);
            sW->setValue(std::max(100, s.width()));
            sH->setValue(std::max(100, s.height()));
        };
        QObject::connect(gal, &QListWidget::itemSelectionChanged, [&]() {
            if (gal->currentItem()) rPlant->setChecked(true);
            ajustarTam();
        });
        QObject::connect(cTam, &QCheckBox::toggled, [&](bool) { ajustarTam(); });

        QObject::connect(bImportar, &QPushButton::clicked, [&]() {
            const QString r = QFileDialog::getOpenFileName(&d, "Imagen de plantilla", "",
                                                           "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)");
            if (r.isEmpty()) return;
            if (QImage(r).isNull()) {
                QMessageBox::warning(&d, "Error", "No se pudo leer la imagen.");
                return;
            }
            recordar("plantillasRecientes", r);
            llenar();
            gal->setCurrentRow(0);        // la más reciente queda arriba y seleccionada
        });

        // --- Botones ---
        int accion = 0;                   // 0 cancelar, 1 crear, 2 abrir proyecto
        QString destino;
        QHBoxLayout *botones = new QHBoxLayout;
        QPushButton *bAbrir = new QPushButton("Abrir proyecto...");
        QPushButton *bCrear = new QPushButton("Crear");
        QPushButton *bCancel = new QPushButton("Cancelar");
        bCrear->setDefault(true);
        botones->addWidget(bAbrir);
        botones->addStretch();
        botones->addWidget(bCancel);
        botones->addWidget(bCrear);
        principal->addLayout(botones);

        QObject::connect(bCrear, &QPushButton::clicked, [&]() { accion = 1; d.accept(); });
        QObject::connect(bCancel, &QPushButton::clicked, &d, &QDialog::reject);
        QObject::connect(bAbrir, &QPushButton::clicked, [&]() {
            const QString r = QFileDialog::getOpenFileName(&d, "Abrir dibujo", ruta, "Dibujo (*.plz)");
            if (r.isEmpty()) return;
            destino = r; accion = 2; d.accept();
        });
        QObject::connect(proy, &QListWidget::itemDoubleClicked, [&](QListWidgetItem *it) {
            destino = it->data(Qt::UserRole).toString(); accion = 2; d.accept();
        });

        d.exec();
        if (accion == 0) return;
        if (!confirmar()) return;

        if (accion == 2) { abrirArchivo(destino); return; }

        QString imagen;
        if (rPlant->isChecked() && gal->currentItem())
            imagen = gal->currentItem()->data(Qt::UserRole).toString();
        canvas->nuevoLienzo(sW->value(), sH->value());
        ruta.clear();
        if (!imagen.isEmpty()) importarPlantilla(imagen);
        titulo();
    };

    QMenu *mArchivo = ventana.menuBar()->addMenu("&Archivo");

    QAction *aNuevo = mArchivo->addAction("Nuevo...");
    aNuevo->setShortcut(QKeySequence::New);
    QObject::connect(aNuevo, &QAction::triggered, [&]() { pantallaInicio(); });

    QAction *aImportar = mArchivo->addAction("Importar imagen como plantilla...");
    QObject::connect(aImportar, &QAction::triggered, [&]() {
        const QString r = QFileDialog::getOpenFileName(&ventana, "Imagen de plantilla", "",
                                                       "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)");
        if (!r.isEmpty()) importarPlantilla(r);
    });

    QAction *aAbrir = mArchivo->addAction("Abrir...");
    aAbrir->setShortcut(QKeySequence::Open);
    QObject::connect(aAbrir, &QAction::triggered, [&]() {
        if (!confirmar()) return;
        const QString r = QFileDialog::getOpenFileName(&ventana, "Abrir dibujo", ruta, "Dibujo (*.plz)");
        if (!r.isEmpty()) abrirArchivo(r);
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

    QAction *aIncluir = mArchivo->addAction("Incluir plantilla al exportar");
    aIncluir->setCheckable(true);

    QObject::connect(aExportar, &QAction::triggered, [&]() {
        QString r = QFileDialog::getSaveFileName(&ventana, "Exportar imagen", "", "Imagen PNG (*.png)");
        if (r.isEmpty()) return;
        if (!r.endsWith(".png", Qt::CaseInsensitive)) r += ".png";
        if (!canvas->exportarPNG(r, aIncluir->isChecked()))
            QMessageBox::warning(&ventana, "Error", "No se pudo exportar la imagen.");
        else
            ventana.statusBar()->showMessage("Exportado: " + r, 4000);
    });

    // ---------- Ver ----------
    QMenu *mEditar = ventana.menuBar()->addMenu("&Editar");
    auto accionEditar = [&](const QString &texto, const QKeySequence &tecla, std::function<void()> f) {
        QAction *a = mEditar->addAction(texto);
        a->setShortcut(tecla);
        QObject::connect(a, &QAction::triggered, [f]() { f(); });
    };
    accionEditar("Copiar", QKeySequence(Qt::CTRL | Qt::Key_C), [=]() { canvas->copiar(false); });
    accionEditar("Cortar", QKeySequence(Qt::CTRL | Qt::Key_X), [=]() { canvas->copiar(true); });
    accionEditar("Pegar", QKeySequence(Qt::CTRL | Qt::Key_V), [=]() { canvas->pegar(); });
    accionEditar("Borrar selección", QKeySequence(Qt::Key_Delete), [=]() { canvas->borrarSel(); });
    mEditar->addSeparator();
    accionEditar("Seleccionar todo", QKeySequence(Qt::CTRL | Qt::Key_A), [=]() { canvas->seleccionarTodo(); });
    accionEditar("Deseleccionar", QKeySequence(Qt::CTRL | Qt::Key_D), [=]() { canvas->deseleccionar(); });

    QMenu *mVer = ventana.menuBar()->addMenu("&Ver");

    QAction *aAcercar = mVer->addAction("Acercar");
    aAcercar->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    QObject::connect(aAcercar, &QAction::triggered, [=]() { canvas->acercar(); });

    QAction *aAlejar = mVer->addAction("Alejar");
    aAlejar->setShortcut(QKeySequence::ZoomOut);
    QObject::connect(aAlejar, &QAction::triggered, [=]() { canvas->alejar(); });

    QAction *aAjustar = mVer->addAction("Ajustar a la ventana");
    aAjustar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    QObject::connect(aAjustar, &QAction::triggered, [=]() { canvas->ajustar(); });

    QAction *aCien = mVer->addAction("Tamaño real (100%)");
    aCien->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    QObject::connect(aCien, &QAction::triggered, [=]() { canvas->zoom100(); });

    // ---------- Barra de herramientas ----------
    QToolBar *barra = ventana.addToolBar("Herramientas");
    barra->setMovable(false);

    QAction *aColor = barra->addAction(iconoColor(canvas->color()), "Color...");
    std::function<void(const QColor &)> usarColor;   // se define más abajo, junto al selector de color
    QObject::connect(aColor, &QAction::triggered, [&]() {
        const QColor c = QColorDialog::getColor(canvas->color(), &ventana, "Elige un color");
        if (c.isValid()) usarColor(c);
    });

    barra->addSeparator();

    barra->addWidget(new QLabel(" Tamaño: "));
    QSlider *tamano = new QSlider(Qt::Horizontal);
    tamano->setRange(1, 200);
    tamano->setValue(16);
    tamano->setFixedWidth(160);
    tamano->setFocusPolicy(Qt::NoFocus);
    barra->addWidget(tamano);
    QLabel *etiqueta = new QLabel(" 16 ");
    barra->addWidget(etiqueta);
    QObject::connect(tamano, &QSlider::valueChanged, [=](int v) {
        canvas->setGrosor(v);
        etiqueta->setText(QString(" %1 ").arg(v));
    });

    // Atajos de tamaño: [ y ] (en teclado latino: , y .)
    auto cambiarTamano = [=](int dir) {
        const int v = tamano->value();
        tamano->setValue(std::clamp(v + dir * std::max(1, v / 10), 1, 200));
    };
    for (int dir : {-1, 1}) {
        QAction *a = new QAction(&ventana);
        a->setShortcuts(dir < 0 ? QList<QKeySequence>{QKeySequence(Qt::Key_BracketLeft), QKeySequence(Qt::Key_Comma)}
                                : QList<QKeySequence>{QKeySequence(Qt::Key_BracketRight), QKeySequence(Qt::Key_Period)});
        ventana.addAction(a);
        QObject::connect(a, &QAction::triggered, [=]() { cambiarTamano(dir); });
    }

    barra->addSeparator();

    QAction *aBorrador = barra->addAction("Borrador");
    aBorrador->setCheckable(true);
    aBorrador->setShortcut(QKeySequence(Qt::Key_E));
    QObject::connect(aBorrador, &QAction::toggled, [=](bool activo) { canvas->setBorrador(activo); });

    barra->addSeparator();
    barra->addWidget(new QLabel(" Pincel: "));
    QComboBox *cPincel = new QComboBox;
    for (const Pincel &p : Canvas::pinceles()) cPincel->addItem(p.nombre);
    cPincel->setFocusPolicy(Qt::NoFocus);
    barra->addWidget(cPincel);
    QObject::connect(cPincel, &QComboBox::currentIndexChanged, [=](int i) {
        canvas->setPincel(i);
        aBorrador->setChecked(false);      // elegir un pincel vuelve a pintar
    });

    QAction *aGotero = barra->addAction("Cuentagotas");
    aGotero->setCheckable(true);
    aGotero->setShortcut(QKeySequence(Qt::Key_I));
    QObject::connect(aGotero, &QAction::toggled, [=](bool a) { canvas->setCuentagotas(a); });

    QAction *aCubo = barra->addAction("Cubo");
    aCubo->setCheckable(true);
    aCubo->setShortcut(QKeySequence(Qt::Key_G));
    aCubo->setToolTip("Cubo de relleno (G)");

    // Opciones del cubo: solo se ven cuando está activo
    QWidget *opcionesCubo = new QWidget;
    QHBoxLayout *hoc = new QHBoxLayout(opcionesCubo);
    hoc->setContentsMargins(4, 0, 4, 0);
    hoc->addWidget(new QLabel("Tolerancia:"));
    QSlider *sTol = new QSlider(Qt::Horizontal);
    sTol->setRange(0, 100);
    sTol->setValue(12);
    sTol->setFixedWidth(110);
    sTol->setFocusPolicy(Qt::NoFocus);
    hoc->addWidget(sTol);
    QLabel *lTol = new QLabel("12%");
    lTol->setMinimumWidth(32);
    hoc->addWidget(lTol);
    QCheckBox *cTodas = new QCheckBox("Todas las capas");
    cTodas->setFocusPolicy(Qt::NoFocus);
    cTodas->setToolTip("Decide la zona mirando todas las capas visibles; pinta solo en la capa activa");
    hoc->addWidget(cTodas);
    QAction *aOpcionesCubo = barra->addWidget(opcionesCubo);
    aOpcionesCubo->setVisible(false);

    canvas->cubo.tolerancia = qRound(12 * 2.55);
    QObject::connect(sTol, &QSlider::valueChanged, [=](int v) {
        canvas->cubo.tolerancia = qRound(v * 2.55);
        lTol->setText(QString("%1%").arg(v));
    });
    QObject::connect(cTodas, &QCheckBox::toggled, [=](bool b) { canvas->cubo.todasCapas = b; });
    // El cubo, el borrador y el cuentagotas son excluyentes
    QObject::connect(aCubo, &QAction::toggled, [=](bool activo) {
        canvas->setCubo(activo);
        aOpcionesCubo->setVisible(activo);
        if (activo) { aBorrador->setChecked(false); aGotero->setChecked(false); }
    });
    QObject::connect(aBorrador, &QAction::toggled, [=](bool a) { if (a) aCubo->setChecked(false); });
    QObject::connect(aGotero, &QAction::toggled, [=](bool a) { if (a) aCubo->setChecked(false); });
    QObject::connect(cPincel, &QComboBox::currentIndexChanged, [=](int) { aCubo->setChecked(false); });

    barra->addWidget(new QLabel(" Forma: "));
    QComboBox *cForma = new QComboBox;
    cForma->addItems({"Libre", "Línea", "Rectángulo", "Elipse"});
    cForma->setFocusPolicy(Qt::NoFocus);
    cForma->setToolTip("Shift: líneas a 15°, cuadrado y círculo exactos");
    barra->addWidget(cForma);
    QCheckBox *cRelleno = new QCheckBox("Relleno");
    cRelleno->setFocusPolicy(Qt::NoFocus);
    barra->addWidget(cRelleno);
    QObject::connect(cForma, &QComboBox::currentIndexChanged, [=](int i) {
        canvas->forma = i;
        if (i) aCubo->setChecked(false);
    });
    QObject::connect(cRelleno, &QCheckBox::toggled, [=](bool b) { canvas->formaRelleno = b; });

    QAction *aSel = barra->addAction("Selección");
    aSel->setCheckable(true);
    aSel->setShortcut(QKeySequence(Qt::Key_M));
    aSel->setToolTip("Selección (M). Un clic fuera la quita.");
    QComboBox *cSel = new QComboBox;
    cSel->addItems({"Rectángulo", "Lazo"});
    cSel->setFocusPolicy(Qt::NoFocus);
    QAction *aOpcionesSel = barra->addWidget(cSel);
    aOpcionesSel->setVisible(false);
    auto aplicarSel = [=]() { canvas->setSeleccion(aSel->isChecked() ? cSel->currentIndex() + 1 : 0); };
    QObject::connect(aSel, &QAction::toggled, [=](bool on) {
        aOpcionesSel->setVisible(on);
        if (on) { aBorrador->setChecked(false); aGotero->setChecked(false); aCubo->setChecked(false); }
        aplicarSel();
    });
    QObject::connect(cSel, &QComboBox::currentIndexChanged, [=](int) { aplicarSel(); });
    // las demás herramientas apagan la selección (la zona seleccionada se conserva)
    QObject::connect(aBorrador, &QAction::toggled, [=](bool a) { if (a) aSel->setChecked(false); });
    QObject::connect(aGotero, &QAction::toggled, [=](bool a) { if (a) aSel->setChecked(false); });
    QObject::connect(aCubo, &QAction::toggled, [=](bool a) { if (a) aSel->setChecked(false); });
    QObject::connect(cPincel, &QComboBox::currentIndexChanged, [=](int) { aSel->setChecked(false); });
    QObject::connect(cForma, &QComboBox::currentIndexChanged, [=](int i) { if (i) aSel->setChecked(false); });

    QAction *aTableta = barra->addAction("Tableta...");
    QObject::connect(aTableta, &QAction::triggered, [=]() {
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });

    barra->addSeparator();

    QAction *aDeshacer = barra->addAction("Deshacer");
    aDeshacer->setShortcut(QKeySequence::Undo);
    QObject::connect(aDeshacer, &QAction::triggered, [=]() { canvas->deshacer(); });

    QAction *aRehacer = barra->addAction("Rehacer");
    // OJO: no repetir la misma secuencia en una acción (Qt la marca "ambigua" y no se dispara nunca)
    aRehacer->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    QObject::connect(aRehacer, &QAction::triggered, [=]() { canvas->rehacer(); });

    // ---------- Selector de color (espectro RGB) ----------
    QDockWidget *dockColor = new QDockWidget("Color", &ventana);
    QWidget *pc = new QWidget;
    QVBoxLayout *vc = new QVBoxLayout(pc);
    SelectorColor *selector = new SelectorColor;
    vc->addWidget(selector);

    QLineEdit *hex = new QLineEdit("#000000");
    hex->setMaxLength(7);
    QSpinBox *sbR = new QSpinBox, *sbG = new QSpinBox, *sbB = new QSpinBox;
    for (QSpinBox *sb : {sbR, sbG, sbB}) sb->setRange(0, 255);
    QHBoxLayout *hrgb = new QHBoxLayout;
    hrgb->addWidget(new QLabel("R")); hrgb->addWidget(sbR);
    hrgb->addWidget(new QLabel("G")); hrgb->addWidget(sbG);
    hrgb->addWidget(new QLabel("B")); hrgb->addWidget(sbB);
    QFormLayout *fh = new QFormLayout;
    fh->addRow("Hex:", hex);
    vc->addLayout(hrgb);
    vc->addLayout(fh);
    vc->addStretch();
    dockColor->setWidget(pc);
    ventana.addDockWidget(Qt::LeftDockWidgetArea, dockColor);

    auto sincronizarCampos = [=](const QColor &c) {
        QSignalBlocker b1(hex), b2(sbR), b3(sbG), b4(sbB);
        hex->setText(c.name());
        sbR->setValue(c.red());
        sbG->setValue(c.green());
        sbB->setValue(c.blue());
    };

    // ---------- Color ----------
    // Mientras arrastras en el selector llegan cientos de cambios por segundo. El color del lienzo se
    // actualiza al instante, pero lo caro (icono de la barra y campos RGB/hex) se junta en un solo
    // refresco cada 30 ms.
    QColor colorPendiente = canvas->color();
    QTimer *sincronizador = new QTimer(&ventana);
    sincronizador->setSingleShot(true);
    sincronizador->setInterval(30);
    QObject::connect(sincronizador, &QTimer::timeout, [&]() {
        aColor->setIcon(iconoColor(colorPendiente));
        sincronizarCampos(colorPendiente);
    });
    auto aplicarColor = [&](const QColor &c, bool sincronizarSelector) {
        canvas->setColor(c);
        colorPendiente = c;
        aBorrador->setChecked(false);            // elegir un color vuelve al pincel (el cubo sigue activo)
        if (sincronizarSelector) selector->setColor(c);
        if (!sincronizador->isActive()) sincronizador->start();
    };
    usarColor = [&](const QColor &c) { aplicarColor(c, true); };
    selector->alCambiar = [&](const QColor &c) { aplicarColor(c, false); };

    // Campos hex y RGB
    QObject::connect(hex, &QLineEdit::editingFinished, [&]() {
        const QColor c(hex->text());
        if (c.isValid()) usarColor(c); else sincronizarCampos(canvas->color());
    });
    for (QSpinBox *sb : {sbR, sbG, sbB}) {
        QObject::connect(sb, &QSpinBox::valueChanged, [&](int) {
            aplicarColor(QColor(sbR->value(), sbG->value(), sbB->value()), true);
        });
    }

    // Cuentagotas: usa el color y vuelve al pincel
    canvas->alElegirColor = [&](const QColor &c) {
        usarColor(c);
        aGotero->setChecked(false);
    };

    sincronizarCampos(canvas->color());
    selector->setColor(canvas->color());

    ventana.show();

    // Si no hubo forma de crear el contexto de OpenGL, avisar
    QTimer::singleShot(1500, &ventana, [=, &ventana]() {
        if (!canvas->isValid())
            QMessageBox::warning(&ventana, "Sin aceleración gráfica",
                                 "No se pudo iniciar OpenGL con la GPU.\n\n"
                                 "Actualiza el driver de la tarjeta gráfica o inicia el programa con el "
                                 "argumento --software.");
    });

    QTimer::singleShot(0, &ventana, [&]() { pantallaInicio(); });

    return app.exec();
    };