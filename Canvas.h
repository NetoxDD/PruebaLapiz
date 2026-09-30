#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QFile>
#include <functional>
#include <vector>
#include <algorithm>
#include "freehand.h"

struct Punto {
    QPointF pos;
    qreal presion;
};

struct Trazo {
    std::vector<Punto> puntos;     // puntos del tramo en curso (se recorta al congelar)
    std::vector<Punto> completo;   // TODOS los puntos del trazo (lo que se guarda)
    QColor color = Qt::black;
    qreal grosor = 16.0;
    bool simular = false;          // true con mouse: presión simulada por velocidad
    QPainterPath contorno;         // contorno ya calculado
};

class Canvas : public QWidget {
    std::vector<Trazo> trazos;
    std::vector<Trazo> pilaRehacer;
    Trazo actual;
    bool dibujando = false;
    bool sucio = false;         // hay cambios sin guardar

    QPixmap cache;
    bool cacheSucio = true;
    QPainterPath congelado;

    QColor colorPincel = Qt::black;
    qreal grosorPincel = 16.0;
    bool modoBorrador = false;
    bool puntaBorrador = false;

public:
    std::function<void(const QString &)> alCambiarInfo;

    Canvas() {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::CrossCursor);
    }

    QColor color() const { return colorPincel; }
    void setColor(const QColor &c) { colorPincel = c; }
    void setGrosor(qreal g) { grosorPincel = g; }
    void setBorrador(bool b) { modoBorrador = b; }
    bool modificado() const { return sucio; }

    void deshacer() {
        if (dibujando || trazos.empty()) return;
        pilaRehacer.push_back(trazos.back());
        trazos.pop_back();
        cacheSucio = true;
        sucio = true;
        info();
        update();
    }
    void rehacer() {
        if (dibujando || pilaRehacer.empty()) return;
        trazos.push_back(pilaRehacer.back());
        pilaRehacer.pop_back();
        cacheSucio = true;
        sucio = true;
        info();
        update();
    }

    // Documento vacío
    void limpiar() {
        if (dibujando) return;
        trazos.clear();
        pilaRehacer.clear();
        cacheSucio = true;
        sucio = false;
        info();
        update();
    }

    // ---------- Guardar / abrir / exportar ----------
    bool guardar(const QString &ruta) {
        QJsonArray arr;
        for (const Trazo &t : trazos) {
            QJsonArray pts;
            for (const Punto &p : t.completo) {
                pts.append(std::round(p.pos.x() * 100) / 100.0);
                pts.append(std::round(p.pos.y() * 100) / 100.0);
                pts.append(std::round(p.presion * 1000) / 1000.0);
            }
            QJsonObject o;
            o["color"] = t.color.name(QColor::HexArgb);
            o["grosor"] = t.grosor;
            o["simular"] = t.simular;
            o["puntos"] = pts;
            arr.append(o);
        }
        QJsonObject raiz;
        raiz["formato"] = "PruebaLapiz";
        raiz["version"] = 1;
        raiz["trazos"] = arr;

        QSaveFile f(ruta);   // escritura segura: no corrompe el archivo si falla
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(raiz).toJson(QJsonDocument::Compact));
        if (!f.commit()) return false;
        sucio = false;
        return true;
    }

    bool abrir(const QString &ruta) {
        if (dibujando) return false;
        QFile f(ruta);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        const QJsonObject raiz = doc.object();
        if (raiz["formato"].toString() != "PruebaLapiz") return false;

        std::vector<Trazo> nuevos;
        for (const QJsonValue &v : raiz["trazos"].toArray()) {
            const QJsonObject o = v.toObject();
            const QJsonArray pts = o["puntos"].toArray();
            if (pts.size() < 3 || pts.size() % 3 != 0) continue;
            Trazo t;
            t.color = QColor(o["color"].toString());
            t.grosor = o["grosor"].toDouble(16.0);
            t.simular = o["simular"].toBool();
            for (int i = 0; i + 2 < pts.size(); i += 3)
                t.completo.push_back({QPointF(pts[i].toDouble(), pts[i + 1].toDouble()),
                                      pts[i + 2].toDouble()});
            t.puntos = t.completo;
            t.contorno = calcularContorno(t, true);
            nuevos.push_back(std::move(t));
        }
        trazos = std::move(nuevos);
        pilaRehacer.clear();
        cacheSucio = true;
        sucio = false;
        info();
        update();
        return true;
    }

    bool exportarPNG(const QString &ruta) const {
        const int esc = 2;   // el doble de resolución que la pantalla
        QImage img(width() * esc, height() * esc, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter g(&img);
        g.setRenderHint(QPainter::Antialiasing);
        g.scale(esc, esc);
        g.setPen(Qt::NoPen);
        for (const Trazo &t : trazos) {
            g.setBrush(t.color);
            g.drawPath(t.contorno);
        }
        g.end();
        return img.save(ruta, "PNG");
    }

protected:
    // ---------- Lápiz ----------
    void tabletEvent(QTabletEvent *e) override {
        const QPointF p = e->position();
        switch (e->type()) {
        case QEvent::TabletPress:
            puntaBorrador = (e->pointerType() == QPointingDevice::PointerType::Eraser);
            empezar(p, e->pressure(), false);
            break;
        case QEvent::TabletMove:
            if (dibujando) agregar(p, e->pressure());
            break;
        case QEvent::TabletRelease:
            terminar();
            puntaBorrador = false;
            break;
        default: break;
        }
        if (alCambiarInfo)
            alCambiarInfo(QString("presión %1  tilt %2,%3  |  trazos: %4")
                              .arg(e->pressure(), 0, 'f', 2).arg(e->xTilt()).arg(e->yTilt())
                              .arg(trazos.size()));
        e->accept();
    }

    // ---------- Mouse ----------
    void mousePressEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (e->button() != Qt::LeftButton) return;
        empezar(e->position(), 0.5, true);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (!dibujando) return;
        agregar(e->position(), 0.5);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (e->button() != Qt::LeftButton) return;
        terminar();
    }

    // ---------- Pintado ----------
    void resizeEvent(QResizeEvent *) override { cacheSucio = true; }

    void paintEvent(QPaintEvent *) override {
        if (cacheSucio) reconstruirCache();
        QPainter g(this);
        g.drawPixmap(0, 0, cache);
        if (dibujando) {
            g.setRenderHint(QPainter::Antialiasing);
            g.setPen(Qt::NoPen);
            g.setBrush(actual.color);
            g.drawPath(calcularContorno(actual, false));
        }
    }

private:
    void empezar(const QPointF &p, qreal presion, bool simular) {
        const bool borrando = modoBorrador || puntaBorrador;
        actual = Trazo();
        actual.simular = simular;
        actual.color = borrando ? QColor(Qt::white) : colorPincel;
        actual.grosor = borrando ? grosorPincel * 1.5 : grosorPincel;
        actual.puntos.push_back({p, presion});
        actual.completo.push_back({p, presion});
        congelado = QPainterPath();
        congelado.setFillRule(Qt::WindingFill);
        dibujando = true;
        update();
    }

    void agregar(const QPointF &p, qreal presion) {
        const QPointF d = p - actual.puntos.back().pos;
        if (d.x() * d.x() + d.y() * d.y() < 2.25) return;
        actual.puntos.push_back({p, presion});
        actual.completo.push_back({p, presion});
        if (actual.puntos.size() >= 250) congelarTramo();
        update();
    }

    void terminar() {
        if (!dibujando) return;
        dibujando = false;
        const QPainterPath ultimo = calcularContorno(actual, true);
        pintarEnCache(ultimo, actual.color);
        actual.contorno = congelado;
        actual.contorno.addPath(ultimo);
        trazos.push_back(actual);
        pilaRehacer.clear();
        sucio = true;
        info();
        update();
    }

    void info() {
        if (alCambiarInfo)
            alCambiarInfo(QString("trazos: %1").arg(trazos.size()));
    }

    void congelarTramo() {
        const QPainterPath c = calcularContorno(actual, true);
        congelado.addPath(c);
        pintarEnCache(c, actual.color);
        std::vector<Punto> resto(actual.puntos.end() - 12, actual.puntos.end());
        actual.puntos = resto;
    }

    void pintarEnCache(const QPainterPath &p, const QColor &color) {
        if (cacheSucio) return;
        QPainter g(&cache);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(Qt::NoPen);
        g.setBrush(color);
        g.drawPath(p);
    }

    void reconstruirCache() {
        const qreal dpr = devicePixelRatioF();
        cache = QPixmap(qRound(width() * dpr), qRound(height() * dpr));
        cache.setDevicePixelRatio(dpr);
        cache.fill(Qt::white);
        QPainter g(&cache);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(Qt::NoPen);
        for (const Trazo &t : trazos) {
            g.setBrush(t.color);
            g.drawPath(t.contorno);
        }
        if (dibujando) {
            g.setBrush(actual.color);
            g.drawPath(congelado);
        }
        cacheSucio = false;
    }

    static QPainterPath calcularContorno(const Trazo &t, bool terminado) {
        std::vector<pf::InPoint> entrada;
        entrada.reserve(t.puntos.size());
        for (const Punto &p : t.puntos)
            entrada.push_back({{p.pos.x(), p.pos.y()}, t.simular ? -1.0 : p.presion});

        pf::Options o;
        o.size = t.grosor;
        o.thinning = 0.5;
        o.smoothing = 0.5;
        o.streamline = 0.5;
        o.simulatePressure = t.simular;
        o.last = terminado;

        const std::vector<pf::Vec> c = pf::getStroke(entrada, o);

        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        const size_t n = c.size();
        if (n == 0) return path;
        if (n < 3) {
            path.moveTo(c[0].x, c[0].y);
            for (size_t i = 1; i < n; ++i) path.lineTo(c[i].x, c[i].y);
            return path;
        }
        auto medio = [](const pf::Vec &a, const pf::Vec &b) {
            return QPointF((a.x + b.x) / 2, (a.y + b.y) / 2);
        };
        path.moveTo(medio(c[0], c[1]));
        for (size_t i = 1; i <= n; ++i) {
            const pf::Vec &a = c[i % n];
            const pf::Vec &b = c[(i + 1) % n];
            path.quadTo(QPointF(a.x, a.y), medio(a, b));
        }
        path.closeSubpath();
        return path;
    }
};