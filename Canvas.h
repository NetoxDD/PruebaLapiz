#pragma once
#include <QWidget>
#include <QPainter>
#include <QColor>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <vector>
#include <algorithm>

struct Punto {
    QPointF pos;
    qreal presion;
};

struct Trazo {
    std::vector<Punto> puntos;
    QColor color = Qt::black;
    qreal grosor = 20.0;   // grosor máximo (con presión 1.0)
};

class Canvas : public QWidget {
    std::vector<Trazo> trazos;    // trazos terminados
    std::vector<Trazo> rehacer;   // trazos deshechosi
    Trazo actual;                 // trazo en curso
    bool dibujando = false;
    bool usandoTableta = false;

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
        case QEvent::TabletPress:
            usandoTableta = true;
            empezar(p, e->pressure());
            break;
        case QEvent::TabletMove:
            if (dibujando) agregar(p, e->pressure());
            break;
        case QEvent::TabletRelease:
            terminar();
            usandoTableta = false;
            break;
        default: break;
        }
        setWindowTitle(QString("presión %1  tilt %2,%3  |  trazos: %4")
                           .arg(e->pressure(), 0, 'f', 2).arg(e->xTilt()).arg(e->yTilt())
                           .arg(trazos.size()));
        e->accept();
    }

    // ---------- Mouse (presión fija) ----------
    void mousePressEvent(QMouseEvent *e) override {
        if (usandoTableta || e->button() != Qt::LeftButton) return;
        empezar(e->position(), 0.5);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (usandoTableta || !dibujando) return;
        agregar(e->position(), 0.5);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (usandoTableta || e->button() != Qt::LeftButton) return;
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
        actualizarTitulo();
        update();
    }

    // ---------- Pintado ----------
    void paintEvent(QPaintEvent *) override {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        g.fillRect(rect(), Qt::white);
        for (const Trazo &t : trazos) dibujarTrazo(g, t);
        if (dibujando) dibujarTrazo(g, actual);
    }

private:
    void empezar(const QPointF &p, qreal presion) {
        actual = Trazo();
        actual.puntos.push_back({p, presion});
        dibujando = true;
        update();
    }
    void agregar(const QPointF &p, qreal presion) {
        actual.puntos.push_back({p, presion});
        update();
    }
    void terminar() {
        if (!dibujando) return;
        dibujando = false;
        trazos.push_back(actual);
        rehacer.clear();          // un trazo nuevo invalida el rehacer
        actualizarTitulo();
        update();
    }
    void actualizarTitulo() {
        setWindowTitle(QString("trazos: %1  (Ctrl+Z deshacer, Ctrl+Y rehacer)")
                           .arg(trazos.size()));
    }
    static void dibujarTrazo(QPainter &g, const Trazo &t) {
        if (t.puntos.empty()) return;
        if (t.puntos.size() == 1) {
            const qreal r = std::max(1.0, t.grosor * t.puntos[0].presion) / 2.0;
            g.setPen(Qt::NoPen);
            g.setBrush(t.color);
            g.drawEllipse(t.puntos[0].pos, r, r);
            return;
        }
        for (size_t i = 1; i < t.puntos.size(); ++i) {
            const qreal w = std::max(1.0, t.grosor * t.puntos[i].presion);
            g.setPen(QPen(t.color, w, Qt::SolidLine, Qt::RoundCap));
            g.drawLine(t.puntos[i - 1].pos, t.puntos[i].pos);
        }
    }
};