#pragma once
// Datos de un trazo y cálculo de su contorno. Sin Qt.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "freehand.h"
#include "imagen.h"

namespace plz {

struct Punto {
    double x = 0, y = 0;
    double presion = 0.5;
};

using Contorno = std::vector<pf::Vec>;   // polígono cerrado que rodea el trazo

enum Forma { FORMA_LIBRE = 0, FORMA_LINEA, FORMA_RECT, FORMA_ELIPSE };

struct Trazo {
    int forma = FORMA_LIBRE;       // si no es libre, 'completo' tiene solo 2 puntos: inicio y fin del arrastre
    bool relleno = false;          // formas cerradas: relleno en vez de solo contorno
    std::vector<Punto> puntos;     // tramo en curso (se recorta al congelar)
    std::vector<Punto> completo;   // TODOS los puntos (lo que se guarda)
    std::uint32_t color = 0xFF000000;   // ARGB sin premultiplicar (como QColor::rgba())
    double grosor = 16.0;
    double thinning = 0.5;
    double streamline = 0.5;
    bool simular = false;          // true con mouse: presión simulada por velocidad
    bool borrar = false;           // true = borra en vez de pintar
    std::vector<Contorno> contornos;   // tramos ya calculados; se pintan juntos (relleno WindingFill)
};

inline Contorno calcularContorno(const Trazo &t, bool terminado) {
    std::vector<pf::InPoint> entrada;
    entrada.reserve(t.puntos.size());
    for (const Punto &p : t.puntos)
        entrada.push_back({{p.x, p.y}, t.simular ? -1.0 : p.presion});

    pf::Options o;
    o.size = t.grosor;
    o.thinning = t.thinning;
    o.smoothing = 0.5;
    o.streamline = t.streamline;
    o.simulatePressure = t.simular;
    o.last = terminado;
    return pf::getStroke(entrada, o);
}

// Con Shift: la línea salta de 15° en 15°; rectángulo y elipse se vuelven cuadrado y círculo
inline Punto restringirForma(int forma, const Punto &a, Punto b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    if (forma == FORMA_LINEA) {
        const double paso = 3.14159265358979323846 / 12.0;
        const double ang = std::round(std::atan2(dy, dx) / paso) * paso, len = std::hypot(dx, dy);
        b.x = a.x + len * std::cos(ang);
        b.y = a.y + len * std::sin(ang);
    } else if (forma == FORMA_RECT || forma == FORMA_ELIPSE) {
        const double l = std::max(std::fabs(dx), std::fabs(dy));
        b.x = a.x + (dx < 0 ? -l : l);
        b.y = a.y + (dy < 0 ? -l : l);
    }
    return b;
}

// Contornos de una forma. Los bordes se trazan como una línea gruesa; con relleno se devuelve el polígono.
inline std::vector<Contorno> contornosDeForma(const Trazo &t) {
    std::vector<Contorno> res;
    if (t.completo.size() < 2 || t.forma == FORMA_LIBRE) return res;
    const Punto &a = t.completo.front(), &b = t.completo.back();
    const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    std::vector<pf::Vec> ruta;
    auto lado = [&](double ax, double ay, double bx, double by) {   // puntos cada ~2 px
        const int n = std::max(1, int(std::ceil(std::hypot(bx - ax, by - ay) / 2.0)));
        for (int i = 0; i < n; ++i) ruta.push_back({ax + (bx - ax) * i / n, ay + (by - ay) * i / n});
    };
    bool cerrada = false;
    if (t.forma == FORMA_LINEA) {
        lado(a.x, a.y, b.x, b.y);
        ruta.push_back({b.x, b.y});
    } else if (t.forma == FORMA_RECT) {
        if (x1 - x0 < 1 || y1 - y0 < 1) return res;
        lado(x0, y0, x1, y0); lado(x1, y0, x1, y1); lado(x1, y1, x0, y1); lado(x0, y1, x0, y0);
        cerrada = true;
    } else {
        const double rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
        if (rx < 0.5 || ry < 0.5) return res;
        const double perim = 3.14159265358979323846 * (3 * (rx + ry) - std::sqrt((3 * rx + ry) * (rx + 3 * ry)));
        const int n = std::clamp(int(perim / 2.0), 24, 1500);
        for (int i = 0; i < n; ++i) {
            const double ang = 2 * 3.14159265358979323846 * i / n;
            ruta.push_back({x0 + rx + rx * std::cos(ang), y0 + ry + ry * std::sin(ang)});
        }
        cerrada = true;
    }
    if (t.relleno && cerrada) { res.push_back(ruta); return res; }
    std::vector<pf::InPoint> entrada;
    for (const pf::Vec &v : ruta) entrada.push_back({{v.x, v.y}, 0.5});
    if (cerrada) entrada.push_back(entrada.front());
    pf::Options o;
    o.size = t.grosor; o.thinning = 0; o.smoothing = 0.5; o.streamline = 0;
    o.simulatePressure = false; o.last = true;
    res.push_back(pf::getStroke(entrada, o));
    return res;
}

// Zona de la capa que toca el trazo (con margen para el antialiasing), recortada al lienzo
inline Rect rectDeTrazo(const Trazo &t, int ancho, int alto) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    auto ver = [&](double x, double y) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); };
    for (const Contorno &c : t.contornos) for (const pf::Vec &v : c) ver(v.x, v.y);
    if (x1 < x0) for (const Punto &p : t.completo) { ver(p.x - t.grosor, p.y - t.grosor); ver(p.x + t.grosor, p.y + t.grosor); }
    if (x1 < x0) return Rect{};
    const Rect r{int(std::floor(x0)) - 3, int(std::floor(y0)) - 3, int(std::ceil(x1)) + 4, int(std::ceil(y1)) + 4};
    return r.interseccion(Rect{0, 0, ancho, alto});
}

}  // namespace plz
