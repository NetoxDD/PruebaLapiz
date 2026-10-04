#pragma once
// Ventana principal. Su código se reparte por zonas para que ningún archivo crezca sin control:
//   ventana.cpp          arranque, título, archivos (abrir/guardar/recientes) y cierre
//   ventana_menus.cpp    menús Archivo, Editar y Ver
//   ventana_barra.cpp    barra de herramientas
//   ventana_paneles.cpp  panel de capas, panel de color y diálogo de la tableta
//   ventana_inicio.cpp   pantalla de inicio (nuevo dibujo, plantillas, proyectos recientes)
#include <QColor>
#include <QIcon>
#include <QMainWindow>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <memory>
#include "autoguardado.h"
#include "canvas.h"
#include "selector_color.h"

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QTimer;

QIcon iconoColor(const QColor &c);

class VentanaPrincipal : public QMainWindow {
public:
    // inicio=false: no abre la pantalla de inicio ni los avisos modales (para las pruebas)
    // carpetaRecuperacion vacía = la carpeta de datos de la aplicación (se cambia en las pruebas)
    explicit VentanaPrincipal(bool inicio = true, const QString &carpetaRecuperacion = QString());

    Canvas *canvas = nullptr;
    std::unique_ptr<Autoguardado> autoguardado;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QSettings cfg;       // recientes y preferencias
    QString ruta;        // archivo abierto ("" = sin título)

    // ventana.cpp
    void titulo();
    void recordar(const QString &clave, const QString &r);
    QStringList existentes(const QString &clave);
    bool abrirArchivo(const QString &r);
    bool importarPlantilla(const QString &r);
    bool guardarComo();
    bool guardar();
    bool confirmar();            // true si se puede seguir (guardó, descartó o no había cambios)
    void exportarImagen(bool conPlantilla);   // diálogo de PNG / JPEG / WebP

    // ventana_inicio.cpp
    void iniciar();              // al arrancar: ofrece recuperar una sesión caída o abre la pantalla de inicio
    void pantallaInicio();

    // ventana_menus.cpp
    void crearMenus();

    // ventana_barra.cpp
    void crearBarra();
    QAction *aColor = nullptr, *aBorrador = nullptr, *aGotero = nullptr, *aCubo = nullptr, *aSel = nullptr;
    QSlider *tamano = nullptr;

    // ventana_paneles.cpp: capas
    void crearPanelCapas();
    void refrescarCapas();
    void sincronizarCapaActiva();
    QListWidget *lista = nullptr;
    QSlider *sOp = nullptr;
    QCheckBox *cBloq = nullptr;
    QComboBox *cFusion = nullptr;

    // ventana_paneles.cpp: tableta
    void crearDialogoTableta();
    QDialog *dlgTableta = nullptr;
    QLabel *diag = nullptr, *lblGPU = nullptr;

    // ventana_paneles.cpp: color
    void crearPanelColor();
    void usarColor(const QColor &c) { aplicarColor(c, true); }
    void aplicarColor(const QColor &c, bool sincronizarSelector);
    void sincronizarCampos(const QColor &c);
    SelectorColor *selector = nullptr;
    QLineEdit *hex = nullptr;
    QSpinBox *sbR = nullptr, *sbG = nullptr, *sbB = nullptr;
    QColor colorPendiente;
    QTimer *sincronizador = nullptr;
};
