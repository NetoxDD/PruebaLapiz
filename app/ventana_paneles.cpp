#include "ventana.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

// ---------- Panel de capas ----------
void VentanaPrincipal::crearPanelCapas() {
    QDockWidget *dock = new QDockWidget("Capas", this);
    QWidget *panel = new QWidget;
    QVBoxLayout *vl = new QVBoxLayout(panel);
    lista = new QListWidget;
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

    sOp = new QSlider(Qt::Horizontal);
    sOp->setRange(0, 100);
    sOp->setValue(100);
    sOp->setFocusPolicy(Qt::NoFocus);
    cBloq = new QCheckBox("Bloqueada");
    cBloq->setFocusPolicy(Qt::NoFocus);
    cFusion = new QComboBox;
    for (int m = 0; m < int(plz::Fusion::Cantidad); ++m) cFusion->addItem(QString::fromUtf8(plz::nombreFusion(plz::Fusion(m))));
    cFusion->setFocusPolicy(Qt::NoFocus);
    cFusion->setToolTip("Cómo se mezcla esta capa con las de abajo (la hoja cuenta como fondo blanco)");
    QFormLayout *fl = new QFormLayout;
    fl->addRow("Fusión:", cFusion);
    fl->addRow("Opacidad:", sOp);
    fl->addRow(cBloq);
    vl->addLayout(fl);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    canvas->alCambiarCapas = [this]() { refrescarCapas(); };

    connect(lista, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r < 0) return;
        canvas->setActiva(canvas->numCapas() - 1 - r);
        sincronizarCapaActiva();
    });
    connect(lista, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        const int idx = canvas->numCapas() - 1 - lista->row(it);
        canvas->setVisible(idx, it->checkState() == Qt::Checked);
        canvas->setNombre(idx, it->text());
    });
    connect(bNueva, &QPushButton::clicked, this, [this]() { canvas->nuevaCapa(); });
    connect(bBorrar, &QPushButton::clicked, this, [this]() { canvas->borrarCapa(canvas->indiceActivo()); });
    connect(bSube, &QPushButton::clicked, this, [this]() { canvas->moverCapa(canvas->indiceActivo(), +1); });
    connect(bBaja, &QPushButton::clicked, this, [this]() { canvas->moverCapa(canvas->indiceActivo(), -1); });
    connect(sOp, &QSlider::valueChanged, this, [this](int v) {
        canvas->setOpacidad(canvas->indiceActivo(), v / 100.0);
    });
    connect(cBloq, &QCheckBox::toggled, this, [this](bool b) {
        canvas->setBloqueada(canvas->indiceActivo(), b);
    });
    connect(cFusion, &QComboBox::currentIndexChanged, this, [this](int i) {
        canvas->setFusion(canvas->indiceActivo(), plz::fusionDeInt(i));
    });
}

void VentanaPrincipal::sincronizarCapaActiva() {
    QSignalBlocker b1(sOp), b2(cBloq), b3(cFusion);
    const Capa &c = canvas->capa(canvas->indiceActivo());
    sOp->setValue(qRound(c.opacidad * 100));
    cFusion->setCurrentIndex(int(c.fusion));
    cBloq->setChecked(c.bloqueada);
}

void VentanaPrincipal::refrescarCapas() {
    {
        QSignalBlocker b(lista);
        lista->clear();
        const int n = canvas->numCapas();
        for (int r = 0; r < n; ++r) {            // la capa de arriba va primero en la lista
            const Capa &c = canvas->capa(n - 1 - r);
            QListWidgetItem *it = new QListWidgetItem(QString::fromStdString(c.nombre));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
            it->setCheckState(c.visible ? Qt::Checked : Qt::Unchecked);
            lista->addItem(it);
        }
        lista->setCurrentRow(n - 1 - canvas->indiceActivo());
    }
    sincronizarCapaActiva();
}

// ---------- Diálogo de opciones de tableta ----------
void VentanaPrincipal::crearDialogoTableta() {
    dlgTableta = new QDialog(this);
    dlgTableta->setWindowTitle("Opciones de tableta");
    QFormLayout *form = new QFormLayout(dlgTableta);

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
    connect(cPresion, &QCheckBox::toggled, this, [this](bool v) { canvas->lapiz.usarPresion = v; });

    QSlider *sCurva = nuevoSlider(0, 100, 50);
    form->addRow("Curva (firme ↔ suave):", sCurva);
    connect(sCurva, &QSlider::valueChanged, this, [this](int v) {
        canvas->lapiz.gamma = std::pow(2.0, (50 - v) / 25.0);
    });

    QSlider *sEfecto = nuevoSlider(0, 95, 50);
    form->addRow("Efecto de la presión en el grosor:", sEfecto);
    connect(sEfecto, &QSlider::valueChanged, this, [this](int v) {
        canvas->lapiz.efectoPresion = v / 100.0;
    });

    QSlider *sSuav = nuevoSlider(0, 90, 50);
    form->addRow("Suavizado del trazo:", sSuav);
    connect(sSuav, &QSlider::valueChanged, this, [this](int v) {
        canvas->lapiz.suavizado = v / 100.0;
    });

    QComboBox *cBoton = new QComboBox;
    cBoton->addItems({"Sin acción", "Borrador", "Mover el lienzo"});
    form->addRow("Botón del lápiz:", cBoton);
    connect(cBoton, &QComboBox::currentIndexChanged, this, [this](int i) { canvas->lapiz.botonLapiz = i; });

    diag = new QLabel("Acerca el lápiz al lienzo para ver sus datos.");
    diag->setWordWrap(true);
    diag->setMinimumWidth(380);
    form->addRow("Diagnóstico:", diag);

    lblGPU = new QLabel("GPU: (iniciando...)");
    lblGPU->setWordWrap(true);
    form->addRow(lblGPU);
}

// ---------- Selector de color (espectro RGB) ----------
void VentanaPrincipal::crearPanelColor() {
    QDockWidget *dockColor = new QDockWidget("Color", this);
    QWidget *pc = new QWidget;
    QVBoxLayout *vc = new QVBoxLayout(pc);
    selector = new SelectorColor;
    vc->addWidget(selector);

    hex = new QLineEdit("#000000");
    hex->setMaxLength(7);
    sbR = new QSpinBox; sbG = new QSpinBox; sbB = new QSpinBox;
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
    addDockWidget(Qt::LeftDockWidgetArea, dockColor);

    // Mientras arrastras en el selector llegan cientos de cambios por segundo. El color del lienzo se
    // actualiza al instante, pero lo caro (icono de la barra y campos RGB/hex) se junta en un solo
    // refresco cada 30 ms.
    colorPendiente = canvas->color();
    sincronizador = new QTimer(this);
    sincronizador->setSingleShot(true);
    sincronizador->setInterval(30);
    connect(sincronizador, &QTimer::timeout, this, [this]() {
        aColor->setIcon(iconoColor(colorPendiente));
        sincronizarCampos(colorPendiente);
    });
    selector->alCambiar = [this](const QColor &c) { aplicarColor(c, false); };

    // Campos hex y RGB
    connect(hex, &QLineEdit::editingFinished, this, [this]() {
        const QColor c(hex->text());
        if (c.isValid()) usarColor(c); else sincronizarCampos(canvas->color());
    });
    for (QSpinBox *sb : {sbR, sbG, sbB}) {
        connect(sb, &QSpinBox::valueChanged, this, [this](int) {
            aplicarColor(QColor(sbR->value(), sbG->value(), sbB->value()), true);
        });
    }

    // Cuentagotas: usa el color y vuelve al pincel
    canvas->alElegirColor = [this](const QColor &c) {
        usarColor(c);
        aGotero->setChecked(false);
    };

    sincronizarCampos(canvas->color());
    selector->setColor(canvas->color());
}

void VentanaPrincipal::sincronizarCampos(const QColor &c) {
    QSignalBlocker b1(hex), b2(sbR), b3(sbG), b4(sbB);
    hex->setText(c.name());
    sbR->setValue(c.red());
    sbG->setValue(c.green());
    sbB->setValue(c.blue());
}

void VentanaPrincipal::aplicarColor(const QColor &c, bool sincronizarSelector) {
    canvas->setColor(c);
    colorPendiente = c;
    aBorrador->setChecked(false);            // elegir un color vuelve al pincel (el cubo sigue activo)
    if (sincronizarSelector) selector->setColor(c);
    if (!sincronizador->isActive()) sincronizador->start();
}
