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
#include <QToolButton>
#include <QStringList>
#include <QList>
#include <QLineEdit>
#include <QSpinBox>
#include <QPainter>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QImage>

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
// Cuadro de saturación/brillo + barra de tono (todos los colores RGB)
class SelectorColor : public QWidget {
    double h = 0.0, s = 0.0, v = 0.0;
    QImage imgSV;
    double hImg = -1;
    enum Arrastre { Nada, SV, Tono } arrastre = Nada;

    QRect rSV() const { return QRect(0, 0, width(), height() - 28); }
    QRect rTono() const { return QRect(0, height() - 20, width(), 20); }

    void mover(const QPointF &p) {
        if (arrastre == SV) {
            const QRect r = rSV();
            s = std::clamp((p.x() - r.left()) / double(r.width() - 1), 0.0, 1.0);
            v = 1.0 - std::clamp((p.y() - r.top()) / double(r.height() - 1), 0.0, 1.0);
        } else if (arrastre == Tono) {
            const QRect r = rTono();
            h = std::clamp((p.x() - r.left()) / double(r.width() - 1), 0.0, 0.999);
        }
        update();
        if (alCambiar) alCambiar(color());
    }

public:
    std::function<void(const QColor &)> alCambiar;   // mientras arrastras
    std::function<void(const QColor &)> alSoltar;    // al soltar el botón

    SelectorColor() {
        setMinimumSize(200, 220);
        setFocusPolicy(Qt::NoFocus);
    }

    QColor color() const { return QColor::fromHsvF(h, s, v); }

    void setColor(const QColor &c) {          // no dispara los avisos
        if (c.hsvHueF() >= 0) h = std::min<double>(c.hsvHueF(), 0.999);   // en grises se conserva el tono   // en grises se conserva el tono
        s = c.hsvSaturationF();
        v = c.valueF();
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        const QPoint p = e->position().toPoint();
        if (rSV().contains(p)) arrastre = SV;
        else if (rTono().adjusted(0, -6, 0, 6).contains(p)) arrastre = Tono;
        else return;
        mover(e->position());
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (arrastre != Nada) mover(e->position());
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (arrastre == Nada) return;
        arrastre = Nada;
        if (alSoltar) alSoltar(color());
    }

    void paintEvent(QPaintEvent *) override {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);

        // Cuadro SV (se dibuja a 128×128 y se escala)
        if (imgSV.isNull() || hImg != h) {
            imgSV = QImage(128, 128, QImage::Format_RGB32);
            for (int y = 0; y < 128; ++y) {
                QRgb *fila = reinterpret_cast<QRgb *>(imgSV.scanLine(y));
                for (int x = 0; x < 128; ++x)
                    fila[x] = QColor::fromHsvF(h, x / 127.0, 1.0 - y / 127.0).rgb();
            }
            hImg = h;
        }
        const QRect r = rSV();
        g.setRenderHint(QPainter::SmoothPixmapTransform);
        g.drawImage(r, imgSV);

        const QPointF m(r.left() + s * (r.width() - 1), r.top() + (1.0 - v) * (r.height() - 1));
        g.setBrush(Qt::NoBrush);
        g.setPen(QPen(Qt::white, 2));
        g.drawEllipse(m, 6, 6);
        g.setPen(QPen(Qt::black, 1));
        g.drawEllipse(m, 7.5, 7.5);

        // Barra de tono
        const QRect t = rTono();
        QLinearGradient gr(t.left(), 0, t.right(), 0);
        for (int i = 0; i <= 6; ++i)
            gr.setColorAt(i / 6.0, QColor::fromHsvF(i == 6 ? 0.0 : i / 6.0, 1.0, 1.0));
        g.fillRect(t, gr);
        const qreal x = t.left() + h * (t.width() - 1);
        g.setPen(QPen(Qt::white, 2));
        g.drawRect(QRectF(x - 3, t.top() - 1, 6, t.height() + 2));
        g.setPen(QPen(Qt::black, 1));
        g.drawRect(QRectF(x - 4, t.top() - 2, 8, t.height() + 4));
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
                QListWidgetItem *it = new QListWidgetItem(c.nombre);
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

    // ---------- Título y estado ----------
    QString ruta;
    auto titulo = [&]() {
        const QString nombre = ruta.isEmpty() ? "Sin título" : QFileInfo(ruta).fileName();
        ventana.setWindowTitle(QString("%1%2 - PruebaLapiz")
                                   .arg(nombre, canvas->modificado() ? "*" : ""));
    };
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

    QMenu *mArchivo = ventana.menuBar()->addMenu("&Archivo");

    QAction *aNuevo = mArchivo->addAction("Nuevo...");
    aNuevo->setShortcut(QKeySequence::New);
    QObject::connect(aNuevo, &QAction::triggered, [&]() {
        if (!confirmar()) return;
        bool ok = false;
        const int w = QInputDialog::getInt(&ventana, "Nuevo lienzo", "Ancho (px, máx. 6000):",
                                           canvas->ancho(), 100, 6000, 100, &ok);
        if (!ok) return;
        const int h = QInputDialog::getInt(&ventana, "Nuevo lienzo", "Alto (px, máx. 6000):",
                                           canvas->alto(), 100, 6000, 100, &ok);
        if (!ok) return;
        canvas->nuevoLienzo(w, h);
        ruta.clear();
        titulo();
    });

    QAction *aAbrir = mArchivo->addAction("Abrir...");
    aAbrir->setShortcut(QKeySequence::Open);
    QObject::connect(aAbrir, &QAction::triggered, [&]() {
        if (!confirmar()) return;
        const QString r = QFileDialog::getOpenFileName(&ventana, "Abrir dibujo", ruta, "Dibujo (*.plz)");
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

    // ---------- Ver ----------
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
    std::function<void(const QColor &)> usarColor;   // se define junto a la paleta
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

    barra->addSeparator();

    QAction *aBorrador = barra->addAction("Borrador");
    aBorrador->setCheckable(true);
    aBorrador->setShortcut(QKeySequence(Qt::Key_E));
    QObject::connect(aBorrador, &QAction::toggled, [=](bool activo) { canvas->setBorrador(activo); });

    QAction *aGotero = barra->addAction("Cuentagotas");
    aGotero->setCheckable(true);
    aGotero->setShortcut(QKeySequence(Qt::Key_I));
    QObject::connect(aGotero, &QAction::toggled, [=](bool a) { canvas->setCuentagotas(a); });

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
    aRehacer->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    QObject::connect(aRehacer, &QAction::triggered, [=]() { canvas->rehacer(); });

    // ---------- Paleta de colores ----------
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

    // ---------- Paleta de colores ----------
    QToolBar *paleta = new QToolBar("Colores");
    paleta->setMovable(false);
    ventana.addToolBarBreak();
    ventana.addToolBar(paleta);

    auto estilo = [](QToolButton *b, const QColor &c, bool vacio) {
        if (vacio)
            b->setStyleSheet("QToolButton{background:transparent;border:1px dashed #666;}");
        else
            b->setStyleSheet(QString("QToolButton{background:%1;border:1px solid #666;}"
                                     "QToolButton:hover{border:2px solid #ffffff;}").arg(c.name()));
    };
    auto nuevoBoton = [&](const QColor &c, bool vacio) {
        QToolButton *b = new QToolButton;
        b->setFixedSize(24, 24);
        b->setFocusPolicy(Qt::NoFocus);
        estilo(b, c, vacio);
        return b;
    };

    const QStringList base = {
                               "#000000", "#404040", "#808080", "#c0c0c0", "#ffffff", "#7f0000",
                               "#e53935", "#ff9800", "#ffeb3b", "#8bc34a", "#2e7d32", "#00bcd4",
                               "#1e88e5", "#1a237e", "#8e24aa", "#e91e8c", "#795548", "#ffcc99"};
    for (const QString &hx : base) {
        const QColor c(hx);
        QToolButton *b = nuevoBoton(c, false);
        b->setToolTip(hx);
        paleta->addWidget(b);
        QObject::connect(b, &QToolButton::clicked, [&, c]() { usarColor(c); });
    }

    paleta->addSeparator();
    paleta->addWidget(new QLabel(" Recientes: "));
    QList<QColor> recientes;
    std::vector<QToolButton *> botonesRec;
    for (int i = 0; i < 8; ++i) {
        QToolButton *b = nuevoBoton(QColor(), true);
        b->setEnabled(false);
        paleta->addWidget(b);
        botonesRec.push_back(b);
        QObject::connect(b, &QToolButton::clicked, [&, i]() {
            if (i < recientes.size()) usarColor(recientes[i]);
        });
    }

    // Cambia el color activo (sin tocar la lista de recientes)
    auto aplicarColor = [&](const QColor &c, bool sincronizarSelector) {
        canvas->setColor(c);
        aColor->setIcon(iconoColor(c));
        aBorrador->setChecked(false);            // elegir un color vuelve al pincel
        sincronizarCampos(c);
        if (sincronizarSelector) selector->setColor(c);
    };
    auto guardarReciente = [&](const QColor &c) {
        recientes.removeAll(c);
        recientes.prepend(c);
        while (recientes.size() > 8) recientes.removeLast();
        for (int i = 0; i < int(botonesRec.size()); ++i) {
            if (i < recientes.size()) {
                estilo(botonesRec[i], recientes[i], false);
                botonesRec[i]->setEnabled(true);
                botonesRec[i]->setToolTip(recientes[i].name());
            }
        }
    };
    usarColor = [&](const QColor &c) {
        aplicarColor(c, true);
        guardarReciente(c);
    };

    // Selector: cambia en vivo, y entra a recientes solo al soltar
    selector->alCambiar = [&](const QColor &c) { aplicarColor(c, false); };
    selector->alSoltar = [&](const QColor &c) { guardarReciente(c); };

    // Campos hex y RGB
    QObject::connect(hex, &QLineEdit::editingFinished, [&]() {
        const QColor c(hex->text());
        if (c.isValid()) usarColor(c); else sincronizarCampos(canvas->color());
    });
    for (QSpinBox *sb : {sbR, sbG, sbB}) {
        QObject::connect(sb, &QSpinBox::valueChanged, [&](int) {
            aplicarColor(QColor(sbR->value(), sbG->value(), sbB->value()), true);
        });
        QObject::connect(sb, &QSpinBox::editingFinished, [&]() { guardarReciente(canvas->color()); });
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

    return app.exec();
}