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

enum Forma { FORMA_LIBRE = 0, FORMA_LINEA, FORMA_RECT, FORMA_ELIPSE, FORMA_POLIGONO };

// Pinceles de estampado: en vez de un contorno continuo se repite un "sello" a lo largo del trazo
enum Punta { PUNTA_REDONDA = 0, PUNTA_SUAVE, PUNTA_ESTRELLA, PUNTA_HOJA };

struct Estampa {
    int punta = PUNTA_SUAVE;
    double espaciado = 0.25;      // distancia entre sellos, como fracción del tamaño (mínimo 1 px)
    double dispersion = 0;        // desvío aleatorio del sello, como fracción del tamaño
    double variaTam = 0;          // 0..1: cuánto se encoge el sello al azar
    double rotacion = 0;          // giro fijo, en grados
    bool sigueTrazo = false;      // gira para seguir la dirección del trazo
    double variaRot = 0;          // giro aleatorio de ±grados
    double flujo = 1;             // opacidad de cada sello (0..1); se acumulan al solaparse
    double presion = 0.5;         // 0..1: cuánto cambia la presión el tamaño
    double grano = 0;             // 0..1: textura de papel que se come parte del sello
};

struct Trazo {
    int forma = FORMA_LIBRE;       // si no es libre, 'completo' tiene 2 puntos (inicio y fin del arrastre); el polígono, todos sus vértices
    bool relleno = false;          // formas cerradas: relleno en vez de solo contorno
    std::vector<Punto> puntos;     // tramo en curso (se recorta al congelar)
    std::vector<Punto> completo;   // TODOS los puntos (lo que se guarda)
    std::uint32_t color = 0xFF000000;   // ARGB sin premultiplicar (como QColor::rgba())
    double grosor = 16.0;
    double thinning = 0.5;
    double streamline = 0.5;
    bool simular = false;          // true con mouse: presión simulada por velocidad
    bool borrar = false;           // true = borra en vez de pintar
    bool estampado = false;        // true = se pinta repitiendo un sello (ver estampa.h) en vez de con 'contornos'
    Estampa est;
    std::uint32_t semilla = 1;     // fija el azar del estampado: el mismo trazo da siempre el mismo dibujo
    Contorno recorte;              // polígono de la selección activa al dibujar (vacío = sin recorte)
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
    if (forma == FORMA_LINEA || forma == FORMA_POLIGONO) {   // en el polígono, 'a' es el vértice anterior
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
    } else if (t.forma == FORMA_POLIGONO) {
        const std::size_t n = t.completo.size();
        if (n == 2) {                                  // con dos vértices todavía es una línea
            lado(a.x, a.y, b.x, b.y);
            ruta.push_back({b.x, b.y});
        } else {                                       // con tres o más se cierra solo
            for (std::size_t i = 0; i < n; ++i) {
                const Punto &p = t.completo[i], &q = t.completo[(i + 1) % n];
                if (std::hypot(q.x - p.x, q.y - p.y) < 1e-6) continue;   // vértices repetidos
                lado(p.x, p.y, q.x, q.y);
            }
            cerrada = true;
        }
        if (ruta.size() < 2) return res;
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
    if (t.estampado) {      // un sello girado cabe en un cuadrado de lado grosor·√2, más el desvío aleatorio
        const double r = t.grosor * (0.7072 + t.est.dispersion) + 2;
        for (const Punto &p : t.completo) { ver(p.x - r, p.y - r); ver(p.x + r, p.y + r); }
    } else
    for (const Contorno &c : t.contornos) for (const pf::Vec &v : c) ver(v.x, v.y);
    if (x1 < x0) for (const Punto &p : t.completo) { ver(p.x - t.grosor, p.y - t.grosor); ver(p.x + t.grosor, p.y + t.grosor); }
    if (x1 < x0) return Rect{};
    const Rect r{int(std::floor(x0)) - 3, int(std::floor(y0)) - 3, int(std::ceil(x1)) + 4, int(std::ceil(y1)) + 4};
    return r.interseccion(Rect{0, 0, ancho, alto});
}

}  // namespace plz
