#pragma once
// Cuadro de saturación/brillo + barra de tono (todos los colores RGB)
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>
#include <functional>

class SelectorColor : public QWidget {
    double h = 0.0, s = 0.0, v = 0.0;
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
        if (c.hsvHueF() >= 0) h = std::min<double>(c.hsvHueF(), 0.999);   // en grises se conserva el tono
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

        // Cuadro SV con dos degradados superpuestos (sin calcular pixel a pixel):
        // blanco -> tono de izquierda a derecha, y transparente -> negro de arriba abajo.
        const QRect r = rSV();
        QLinearGradient gs(r.left(), 0, r.right() + 1, 0);
        gs.setColorAt(0.0, Qt::white);
        gs.setColorAt(1.0, QColor::fromHsvF(h, 1.0, 1.0));
        g.fillRect(r, gs);
        QLinearGradient gv(0, r.top(), 0, r.bottom() + 1);
        gv.setColorAt(0.0, QColor(0, 0, 0, 0));
        gv.setColorAt(1.0, Qt::black);
        g.fillRect(r, gv);

        g.setRenderHint(QPainter::Antialiasing);
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
