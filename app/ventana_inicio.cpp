#include "ventana.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

// Al arrancar: si la sesión anterior se cayó dejando una copia, se ofrece recuperarla antes de nada
void VentanaPrincipal::iniciar() {
    if (autoguardado->hayRecuperacion()) {
        const QString origen = autoguardado->origenRecuperado();
        QString texto = "La última vez el programa no se cerró bien y quedó un dibujo sin guardar.";
        if (!origen.isEmpty()) texto += "\n\nEra: " + origen;
        texto += "\n\n¿Quieres recuperarlo?";
        if (QMessageBox::question(this, "Recuperar dibujo", texto) == QMessageBox::Yes) {
            if (canvas->abrir(autoguardado->archivo())) {
                canvas->marcarModificado();     // sigue sin guardar: al cerrar preguntará
                ruta.clear();                   // y "Guardar" pedirá nombre: no se pisa el original por accidente
                titulo();
                return;
            }
            QMessageBox::warning(this, "Error", "No se pudo recuperar el dibujo.");
        }
        autoguardado->quitar();
    }
    pantallaInicio();
}

// Pantalla de inicio: nuevo dibujo (medidas y plantilla), plantillas usadas y proyectos recientes
void VentanaPrincipal::pantallaInicio() {
    QDialog d(this);
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
            auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(m)), QFileInfo(r).completeBaseName());
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
    connect(preset, &QComboBox::currentIndexChanged, &d, [&](int i) {
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
    connect(gal, &QListWidget::itemSelectionChanged, &d, [&]() {
        if (gal->currentItem()) rPlant->setChecked(true);
        ajustarTam();
    });
    connect(cTam, &QCheckBox::toggled, &d, [&](bool) { ajustarTam(); });

    connect(bImportar, &QPushButton::clicked, &d, [&]() {
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

    connect(bCrear, &QPushButton::clicked, &d, [&]() { accion = 1; d.accept(); });
    connect(bCancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(bAbrir, &QPushButton::clicked, &d, [&]() {
        const QString r = QFileDialog::getOpenFileName(&d, "Abrir dibujo", ruta, "Dibujo (*.plz)");
        if (r.isEmpty()) return;
        destino = r; accion = 2; d.accept();
    });
    connect(proy, &QListWidget::itemDoubleClicked, &d, [&](QListWidgetItem *it) {
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
    autoguardado->quitar();
    if (!imagen.isEmpty()) importarPlantilla(imagen);
    titulo();
}
