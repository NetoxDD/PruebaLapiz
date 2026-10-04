#include "ventana.h"
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QTimer>

QIcon iconoColor(const QColor &c) {
    QPixmap pm(24, 24);
    pm.fill(c);
    return QIcon(pm);
}

VentanaPrincipal::VentanaPrincipal(bool inicio, const QString &carpetaRecuperacion) {
    resize(1300, 800);
    canvas = new Canvas;
    setCentralWidget(canvas);

    crearPanelCapas();
    crearDialogoTableta();

    autoguardado = std::make_unique<Autoguardado>(canvas, carpetaRecuperacion);
    autoguardado->origen = [this]() { return ruta; };
    autoguardado->alGuardar = [this](const QString &s) { statusBar()->showMessage(s, 4000); };
    autoguardado->setIntervalo(cfg.value("autoguardado/segundos", 60).toInt());
    autoguardado->setActivo(cfg.value("autoguardado/activo", true).toBool());

    // El lienzo avisa de su estado y de los datos del lápiz
    canvas->alCambiarInfo = [this](const QString &s) {
        statusBar()->showMessage(s);
        titulo();
    };
    canvas->alDatosLapiz = [this](const QString &s) { diag->setText(s); };
    canvas->alIniciarGL = [this](const QString &s) {
        lblGPU->setText(s);
        statusBar()->showMessage(s, 8000);
    };
    refrescarCapas();
    titulo();

    crearMenus();
    crearBarra();
    crearPanelColor();

    if (!inicio) return;

    // Si no hubo forma de crear el contexto de OpenGL, avisar
    QTimer::singleShot(1500, this, [this]() {
        if (!canvas->isValid())
            QMessageBox::warning(this, "Sin aceleración gráfica",
                                 "No se pudo iniciar OpenGL con la GPU.\n\n"
                                 "Actualiza el driver de la tarjeta gráfica o inicia el programa con el "
                                 "argumento --software.");
    });
    QTimer::singleShot(0, this, [this]() { iniciar(); });
}

void VentanaPrincipal::closeEvent(QCloseEvent *e) {
    if (confirmar()) {
        autoguardado->quitar();          // cierre normal: la copia de recuperación ya no hace falta
        e->accept();
    } else {
        e->ignore();
    }
}

// ---------- Título y recientes ----------
void VentanaPrincipal::titulo() {
    const QString nombre = ruta.isEmpty() ? "Sin título" : QFileInfo(ruta).fileName();
    setWindowTitle(QString("%1%2 - PruebaLapiz").arg(nombre, canvas->modificado() ? "*" : ""));
}

void VentanaPrincipal::recordar(const QString &clave, const QString &r) {
    QStringList l = cfg.value(clave).toStringList();
    l.removeAll(r);
    l.prepend(r);
    while (l.size() > 12) l.removeLast();
    cfg.setValue(clave, l);
}

QStringList VentanaPrincipal::existentes(const QString &clave) {
    QStringList out;
    for (const QString &r : cfg.value(clave).toStringList())
        if (QFileInfo::exists(r)) out << r;
    return out;
}

// ---------- Archivos ----------
bool VentanaPrincipal::abrirArchivo(const QString &r) {
    if (!canvas->abrir(r)) {
        QMessageBox::warning(this, "Error", "No se pudo abrir el archivo.");
        return false;
    }
    ruta = r;
    autoguardado->quitar();
    recordar("proyectosRecientes", r);
    titulo();
    return true;
}

bool VentanaPrincipal::importarPlantilla(const QString &r) {
    const QImage im(r);
    if (im.isNull()) {
        QMessageBox::warning(this, "Error", "No se pudo leer la imagen.");
        return false;
    }
    canvas->cargarPlantilla(im, QFileInfo(r).completeBaseName());
    recordar("plantillasRecientes", r);
    return true;
}

bool VentanaPrincipal::guardarComo() {
    QString r = QFileDialog::getSaveFileName(this, "Guardar dibujo", ruta, "Dibujo (*.plz)");
    if (r.isEmpty()) return false;
    if (!r.endsWith(".plz", Qt::CaseInsensitive)) r += ".plz";
    if (!canvas->guardar(r)) {
        QMessageBox::warning(this, "Error", "No se pudo guardar el archivo.");
        return false;
    }
    ruta = r;
    autoguardado->quitar();
    recordar("proyectosRecientes", r);
    titulo();
    return true;
}

bool VentanaPrincipal::guardar() {
    if (ruta.isEmpty()) return guardarComo();
    if (!canvas->guardar(ruta)) {
        QMessageBox::warning(this, "Error", "No se pudo guardar el archivo.");
        return false;
    }
    autoguardado->quitar();
    titulo();
    return true;
}

// Formato según la extensión escrita ("" si no es una conocida)
static QString formatoDeRuta(const QString &r) {
    const QString e = QFileInfo(r).suffix().toLower();
    if (e == "png") return "png";
    if (e == "jpg" || e == "jpeg") return "jpg";
    if (e == "webp") return "webp";
    return QString();
}

void VentanaPrincipal::exportarImagen(bool conPlantilla) {
    QString filtro;
    QString r = QFileDialog::getSaveFileName(this, "Exportar imagen", "",
                                             "PNG (*.png);;JPEG (*.jpg *.jpeg);;WebP (*.webp)", &filtro);
    if (r.isEmpty()) return;
    QString fmt = formatoDeRuta(r);
    if (fmt.isEmpty()) {                          // sin extensión válida: manda el filtro elegido
        fmt = filtro.startsWith("JPEG") ? "jpg" : filtro.startsWith("WebP") ? "webp" : "png";
        r += "." + fmt;
    }
    int calidad = 100;
    if (fmt != "png") {
        bool ok = false;
        calidad = QInputDialog::getInt(this, "Calidad",
                                       fmt == "webp" ? "Calidad (1-100; 100 = sin pérdida):" : "Calidad JPEG (1-100):",
                                       cfg.value("exportar/calidad", 90).toInt(), 1, 100, 1, &ok);
        if (!ok) return;
        cfg.setValue("exportar/calidad", calidad);
    }
    QString error;
    if (!canvas->exportar(r, fmt, calidad, conPlantilla, &error))
        QMessageBox::warning(this, "Error", "No se pudo exportar la imagen.\n" + error);
    else
        statusBar()->showMessage("Exportado: " + r, 4000);
}

bool VentanaPrincipal::confirmar() {
    if (!canvas->modificado()) return true;
    const auto r = QMessageBox::question(this, "Cambios sin guardar",
                                         "Hay cambios sin guardar. ¿Quieres guardarlos?",
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return guardar();
    return r == QMessageBox::Discard;
}
