#pragma once
// Puente entre el core (sin Qt) y Qt: imágenes, contornos y colores.
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <vector>
#include "core/imagen.h"
#include "core/trazo.h"

namespace puente {

// QImage que ESCRIBE directamente en la memoria de la Imagen (sin copiar).
// La Imagen debe seguir viva y sin cambiar de tamaño mientras se use.
inline QImage vista(plz::Imagen &im) {
    return QImage(reinterpret_cast<uchar *>(im.px.data()), im.w, im.h, im.w * 4,
                  QImage::Format_ARGB32_Premultiplied);
}
// Igual pero solo para leer
inline QImage lectura(const plz::Imagen &im) {
    return QImage(reinterpret_cast<const uchar *>(im.px.data()), im.w, im.h, im.w * 4,
                  QImage::Format_ARGB32_Premultiplied);
}

inline plz::Rect aRect(const QRect &r) {
    return plz::Rect{r.x(), r.y(), r.x() + r.width(), r.y() + r.height()};
}

// Zona (con margen para el antialiasing) que ocupa un contorno
inline QRect rectDeContorno(const plz::Contorno &c) {
    if (c.empty()) return QRect();
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const pf::Vec &v : c) { x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); }
    return QRect(int(std::floor(x0)) - 3, int(std::floor(y0)) - 3,
                 int(std::ceil(x1 - x0)) + 7, int(std::ceil(y1 - y0)) + 7);
}

// Añade un contorno cerrado suavizado con curvas cuadráticas
inline void agregarContorno(QPainterPath &path, const plz::Contorno &c) {
    const size_t n = c.size();
    if (n == 0) return;
    if (n < 3) {
        path.moveTo(c[0].x, c[0].y);
        for (size_t i = 1; i < n; ++i) path.lineTo(c[i].x, c[i].y);
        return;
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
}

inline QPainterPath aPath(const plz::Contorno &c) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    agregarContorno(path, c);
    return path;
}

// Pinta (o borra) varios contornos como una sola forma sobre la imagen
inline void pintarContornos(plz::Imagen &img, const std::vector<plz::Contorno> &cs,
                            const QColor &color, bool borrar) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const plz::Contorno &c : cs) agregarContorno(path, c);
    QImage v = vista(img);
    {
        QPainter g(&v);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(Qt::NoPen);
        g.setBrush(borrar ? QColor(Qt::black) : color);
        g.setCompositionMode(borrar ? QPainter::CompositionMode_DestinationOut
                                    : QPainter::CompositionMode_SourceOver);
        g.drawPath(path);
    }
    img.tocar(aRect(path.boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2)));   // solo lo que cambió
}
inline void pintarContorno(plz::Imagen &img, const plz::Contorno &c, const QColor &color, bool borrar) {
    pintarContornos(img, std::vector<plz::Contorno>{c}, color, borrar);
}

// Dibuja una QImage dentro de la Imagen (para plantillas)
inline void dibujarImagen(plz::Imagen &dst, const QImage &src, const QRectF &destino, bool suave) {
    QImage v = vista(dst);
    {
        QPainter g(&v);
        g.setRenderHint(QPainter::SmoothPixmapTransform, suave);
        g.drawImage(destino, src.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    dst.tocar();
}

}  // namespace puente
