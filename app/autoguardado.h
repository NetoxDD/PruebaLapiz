#pragma once
// Guardado automático. NO toca el archivo del usuario: escribe una copia de recuperación en la carpeta de datos
// de la aplicación. Se borra al guardar o al cerrar con normalidad; si el programa se cae, queda ahí y al
// siguiente arranque se ofrece recuperarla.
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <functional>
#include <memory>
#include "canvas.h"

class Autoguardado {
public:
    // 'carpeta' vacía = la carpeta de datos de la aplicación
    explicit Autoguardado(Canvas *canvas, QString carpeta = QString()) : canvas_(canvas), carpeta_(std::move(carpeta)) {
        if (carpeta_.isEmpty())
            carpeta_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recuperacion";
        QDir().mkpath(carpeta_);
        // Una sola instancia a la vez usa la carpeta. Si otra ya la tiene, esta ni guarda ni ofrece recuperar.
        // (Un bloqueo que dejó un programa caído se detecta como viejo y se retoma.)
        bloqueo_ = std::make_unique<QLockFile>(carpeta_ + "/sesion.lock");
        bloqueo_->setStaleLockTime(30000);
        propia_ = bloqueo_->tryLock(100);
        temporizador_.setSingleShot(false);
        temporizador_.setInterval(intervaloSeg_ * 1000);
        QObject::connect(&temporizador_, &QTimer::timeout, [this]() { tick(); });
    }

    std::function<QString()> origen;                     // ruta del archivo abierto ("" = sin título)
    std::function<void(const QString &)> alGuardar;      // aviso para la barra de estado

    bool disponible() const { return propia_; }          // false: otra instancia usa la carpeta
    QString archivo() const { return carpeta_ + "/autoguardado.plz"; }
    QString archivoOrigen() const { return carpeta_ + "/autoguardado.origen"; }
    bool activo() const { return activo_; }
    int intervalo() const { return intervaloSeg_; }

    void setActivo(bool a) {
        activo_ = a;
        if (a && propia_) temporizador_.start(); else temporizador_.stop();
    }
    void setIntervalo(int segundos) {
        intervaloSeg_ = std::max(5, segundos);
        temporizador_.setInterval(intervaloSeg_ * 1000);
    }

    // ¿Quedó una copia de una sesión anterior que no se cerró bien?
    bool hayRecuperacion() const { return propia_ && QFile::exists(archivo()); }
    QString origenRecuperado() const {
        QFile f(archivoOrigen());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
        return QString::fromUtf8(f.readAll()).trimmed();
    }
    void quitar() {
        if (!propia_) return;
        QFile::remove(archivo());
        QFile::remove(archivoOrigen());
        ultimaRev_ = ~std::uint64_t(0);
    }

    // Se llama cada intervalo: guarda solo si hay cambios nuevos y no se está dibujando
    void tick() {
        if (!activo_ || !propia_) return;
        if (!canvas_->modificado()) { quitar(); return; }        // ya está todo guardado: la copia sobra
        if (canvas_->ocupado()) return;                           // a medio trazo: se intenta en el siguiente tick
        if (canvas_->revision() == ultimaRev_) return;            // nada nuevo desde la última copia
        guardarAhora();
    }

    bool guardarAhora() {
        if (!propia_) return false;
        QDir().mkpath(carpeta_);
        const std::uint64_t rev = canvas_->revision();
        if (!canvas_->guardarCopia(archivo())) return false;
        QFile f(archivoOrigen());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            f.write((origen ? origen() : QString()).toUtf8());
        ultimaRev_ = rev;
        if (alGuardar) alGuardar("Copia de seguridad guardada " + QTime::currentTime().toString("HH:mm"));
        return true;
    }

private:
    Canvas *canvas_;
    QString carpeta_;
    std::unique_ptr<QLockFile> bloqueo_;
    bool propia_ = false;
    bool activo_ = false;
    int intervaloSeg_ = 60;
    std::uint64_t ultimaRev_ = ~std::uint64_t(0);
    QTimer temporizador_;
};
