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

struct Trazo {
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
