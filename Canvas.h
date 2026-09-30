#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <vector>
#include "freehand.h"
#include <QPixmap>

struct Punto {
    QPointF pos;
    qreal presion;
};

struct Trazo {
    std::vector<Punto> puntos;
    QColor color = Qt::black;
    qreal grosor = 16.0;
    bool simular = false;       // true con mouse: presión simulada por velocidad
    QPainterPath contorno;      // caché del contorno ya calculado
};

class Canvas : public QWidget {
    std::vector<Trazo> trazos;
    std::vector<Trazo> rehacer;
    Trazo actual;
    bool dibujando = false;
    QPixmap cache;
    bool cacheSucio = true;

public:
    Canvas() {
        resize(1200, 800);
        setFocusPolicy(Qt::StrongFocus);
        actualizarTitulo();
    }

protected:
    // ---------- Lápiz ----------
    void tabletEvent(QTabletEvent *e) override {
        const QPointF p = e->position();
        switch (e->type()) {
        case QEvent::TabletPress:   empezar(p, e->pressure(), false); break;
        case QEvent::TabletMove:    if (dibujando) agregar(p, e->pressure()); break;
        case QEvent::TabletRelease: terminar(); break;
        default: break;
        }
        setWindowTitle(QString("presión %1  tilt %2,%3  |  trazos: %4")
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

    // ---------- Deshacer / rehacer ----------
    void keyPressEvent(QKeyEvent *e) override {
        if (e->matches(QKeySequence::Undo) && !trazos.empty()) {
            rehacer.push_back(trazos.back());
            trazos.pop_back();
        } else if (e->matches(QKeySequence::Redo) && !rehacer.empty()) {
            trazos.push_back(rehacer.back());
            rehacer.pop_back();
        } else {
            QWidget::keyPressEvent(e);
            return;
        }
        cacheSucio = true;
        actualizarTitulo();
        update();
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
        actual = Trazo();
        actual.simular = simular;
        actual.puntos.push_back({p, presion});
        dibujando = true;
        update();
    }
        void agregar(const QPointF &p, qreal presion) {
        // Descarta puntos a menos de 1.5 px del anterior
        const QPointF d = p - actual.puntos.back().pos;
        if (d.x() * d.x() + d.y() * d.y() < 2.25) return;
        actual.puntos.push_back({p, presion});
        update();
    }
    void terminar() {
        if (!dibujando) return;
        dibujando = false;
        actual.contorno = calcularContorno(actual, true);
        if (!cacheSucio) {                       // pinta el trazo en la caché
            QPainter g(&cache);
            g.setRenderHint(QPainter::Antialiasing);
            g.setPen(Qt::NoPen);
            g.setBrush(actual.color);
            g.drawPath(actual.contorno);
        }
        trazos.push_back(actual);
        rehacer.clear();
        actualizarTitulo();
        update();
    }
    void actualizarTitulo() {
        setWindowTitle(QString("trazos: %1  (Ctrl+Z deshacer, Ctrl+Y rehacer)")
                           .arg(trazos.size()));
    }

    // Convierte los puntos del trazo en un contorno suave usando perfect-freehand
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
    void reconstruirCache() {
        const qreal dpr = devicePixelRatioF();
        cache = QPixmap(QSizeF(size() * dpr).toSize());
        cache.setDevicePixelRatio(dpr);
        cache.fill(Qt::white);
        QPainter g(&cache);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(Qt::NoPen);
        for (const Trazo &t : trazos) {
            g.setBrush(t.color);
            g.drawPath(t.contorno);
        }
        cacheSucio = false;
    }
};