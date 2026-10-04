#include "ventana.h"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolBar>
#include <algorithm>

void VentanaPrincipal::crearBarra() {
    QToolBar *barra = addToolBar("Herramientas");
    barra->setMovable(false);

    aColor = barra->addAction(iconoColor(canvas->color()), "Color...");
    connect(aColor, &QAction::triggered, this, [this]() {
        const QColor c = QColorDialog::getColor(canvas->color(), this, "Elige un color");
        if (c.isValid()) usarColor(c);
    });

    barra->addSeparator();

    // ---------- Tamaño ----------
    barra->addWidget(new QLabel(" Tamaño: "));
    tamano = new QSlider(Qt::Horizontal);
    tamano->setRange(1, 200);
    tamano->setValue(16);
    tamano->setFixedWidth(160);
    tamano->setFocusPolicy(Qt::NoFocus);
    barra->addWidget(tamano);
    QLabel *etiqueta = new QLabel(" 16 ");
    barra->addWidget(etiqueta);
    connect(tamano, &QSlider::valueChanged, this, [this, etiqueta](int v) {
        canvas->setGrosor(v);
        etiqueta->setText(QString(" %1 ").arg(v));
    });

    // Atajos de tamaño: [ y ] (en teclado latino: , y .)
    for (int dir : {-1, 1}) {
        QAction *a = new QAction(this);
        a->setShortcuts(dir < 0 ? QList<QKeySequence>{QKeySequence(Qt::Key_BracketLeft), QKeySequence(Qt::Key_Comma)}
                                : QList<QKeySequence>{QKeySequence(Qt::Key_BracketRight), QKeySequence(Qt::Key_Period)});
        addAction(a);
        connect(a, &QAction::triggered, this, [this, dir]() {
            const int v = tamano->value();
            tamano->setValue(std::clamp(v + dir * std::max(1, v / 10), 1, 200));
        });
    }

    barra->addSeparator();

    // ---------- Borrador y pincel ----------
    aBorrador = barra->addAction("Borrador");
    aBorrador->setCheckable(true);
    aBorrador->setShortcut(QKeySequence(Qt::Key_E));
    connect(aBorrador, &QAction::toggled, this, [this](bool activo) { canvas->setBorrador(activo); });

    barra->addSeparator();
    barra->addWidget(new QLabel(" Pincel: "));
    QComboBox *cPincel = new QComboBox;
    for (const Pincel &p : Canvas::pinceles()) cPincel->addItem(p.nombre);
    cPincel->setFocusPolicy(Qt::NoFocus);
    barra->addWidget(cPincel);
    connect(cPincel, &QComboBox::currentIndexChanged, this, [this](int i) {
        canvas->setPincel(i);
        aBorrador->setChecked(false);      // elegir un pincel vuelve a pintar
    });

    // ---------- Cuentagotas y cubo ----------
    aGotero = barra->addAction("Cuentagotas");
    aGotero->setCheckable(true);
    aGotero->setShortcut(QKeySequence(Qt::Key_I));
    connect(aGotero, &QAction::toggled, this, [this](bool a) { canvas->setCuentagotas(a); });

    aCubo = barra->addAction("Cubo");
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
    connect(sTol, &QSlider::valueChanged, this, [this, lTol](int v) {
        canvas->cubo.tolerancia = qRound(v * 2.55);
        lTol->setText(QString("%1%").arg(v));
    });
    connect(cTodas, &QCheckBox::toggled, this, [this](bool b) { canvas->cubo.todasCapas = b; });
    // El cubo, el borrador y el cuentagotas son excluyentes
    connect(aCubo, &QAction::toggled, this, [this, aOpcionesCubo](bool activo) {
        canvas->setCubo(activo);
        aOpcionesCubo->setVisible(activo);
        if (activo) { aBorrador->setChecked(false); aGotero->setChecked(false); }
    });
    connect(aBorrador, &QAction::toggled, this, [this](bool a) { if (a) aCubo->setChecked(false); });
    connect(aGotero, &QAction::toggled, this, [this](bool a) { if (a) aCubo->setChecked(false); });
    connect(cPincel, &QComboBox::currentIndexChanged, this, [this](int) { aCubo->setChecked(false); });

    // ---------- Formas ----------
    barra->addWidget(new QLabel(" Forma: "));
    QComboBox *cForma = new QComboBox;
    cForma->addItems({"Libre", "Línea", "Rectángulo", "Elipse", "Polígono"});
    cForma->setFocusPolicy(Qt::NoFocus);
    cForma->setToolTip("Shift: líneas a 15°, cuadrado y círculo exactos.\n"
                       "Polígono: un clic por vértice; clic en el primero, doble clic o Enter lo cierra; "
                       "Retroceso quita el último; Esc cancela.");
    barra->addWidget(cForma);
    QCheckBox *cRelleno = new QCheckBox("Relleno");
    cRelleno->setFocusPolicy(Qt::NoFocus);
    barra->addWidget(cRelleno);
    connect(cForma, &QComboBox::currentIndexChanged, this, [this](int i) {
        canvas->setForma(i);
        if (i) aCubo->setChecked(false);
    });
    connect(cRelleno, &QCheckBox::toggled, this, [this](bool b) { canvas->formaRelleno = b; });

    // ---------- Selección ----------
    aSel = barra->addAction("Selección");
    aSel->setCheckable(true);
    aSel->setShortcut(QKeySequence(Qt::Key_M));
    aSel->setToolTip("Selección (M). Un clic fuera la quita.");
    QComboBox *cSel = new QComboBox;
    cSel->addItems({"Rectángulo", "Lazo"});
    cSel->setFocusPolicy(Qt::NoFocus);
    QAction *aOpcionesSel = barra->addWidget(cSel);
    aOpcionesSel->setVisible(false);
    auto aplicarSel = [this, cSel]() { canvas->setSeleccion(aSel->isChecked() ? cSel->currentIndex() + 1 : 0); };
    connect(aSel, &QAction::toggled, this, [this, aOpcionesSel, aplicarSel](bool on) {
        aOpcionesSel->setVisible(on);
        if (on) { aBorrador->setChecked(false); aGotero->setChecked(false); aCubo->setChecked(false); }
        aplicarSel();
    });
    connect(cSel, &QComboBox::currentIndexChanged, this, [aplicarSel](int) { aplicarSel(); });
    // las demás herramientas apagan la selección (la zona seleccionada se conserva)
    connect(aBorrador, &QAction::toggled, this, [this](bool a) { if (a) aSel->setChecked(false); });
    connect(aGotero, &QAction::toggled, this, [this](bool a) { if (a) aSel->setChecked(false); });
    connect(aCubo, &QAction::toggled, this, [this](bool a) { if (a) aSel->setChecked(false); });
    connect(cPincel, &QComboBox::currentIndexChanged, this, [this](int) { aSel->setChecked(false); });
    connect(cForma, &QComboBox::currentIndexChanged, this, [this](int i) { if (i) aSel->setChecked(false); });

    // ---------- Transformar lo seleccionado ----------
    QAction *aTransformar = barra->addAction("Transformar");
    aTransformar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    aTransformar->setToolTip("Mover, escalar y rotar lo seleccionado (Ctrl+T)");
    connect(aTransformar, &QAction::triggered, this, [this]() { canvas->iniciarTransformacion(); });
    // Aplicar y Cancelar solo aparecen mientras hay una sesión abierta (transformar o polígono):
    // útiles con tableta, cuando no hay teclado para Enter y Esc
    QAction *aAplicar = barra->addAction("✔ Aplicar");
    QAction *aCancelarSesion = barra->addAction("✖ Cancelar");
    aAplicar->setVisible(false);
    aCancelarSesion->setVisible(false);
    connect(aAplicar, &QAction::triggered, this, [this]() { canvas->cerrarSesiones(); });
    connect(aCancelarSesion, &QAction::triggered, this, [this]() { canvas->cancelarSesiones(); });
    canvas->alCambiarSesion = [this, aAplicar, aCancelarSesion, aTransformar]() {
        const bool s = canvas->sesionActiva();
        aAplicar->setVisible(s);
        aCancelarSesion->setVisible(s);
        aTransformar->setEnabled(!s);
    };

    // ---------- Tableta y deshacer ----------
    QAction *aTableta = barra->addAction("Tableta...");
    connect(aTableta, &QAction::triggered, this, [this]() {
        dlgTableta->show();
        dlgTableta->raise();
        dlgTableta->activateWindow();
    });

    barra->addSeparator();

    QAction *aDeshacer = barra->addAction("Deshacer");
    aDeshacer->setShortcut(QKeySequence::Undo);
    connect(aDeshacer, &QAction::triggered, this, [this]() { canvas->deshacer(); });

    QAction *aRehacer = barra->addAction("Rehacer");
    // OJO: no repetir la misma secuencia en una acción (Qt la marca "ambigua" y no se dispara nunca)
    aRehacer->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(aRehacer, &QAction::triggered, this, [this]() { canvas->rehacer(); });
}
