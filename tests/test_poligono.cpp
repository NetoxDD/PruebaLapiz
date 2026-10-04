#include <cmath>
#include "mini_test.h"
#include "core/documento.h"
#include "core/seleccion.h"
#include "core/trazo.h"

using namespace plz;

namespace {
constexpr double PI = 3.14159265358979323846;
Trazo poligono(std::initializer_list<Punto> pts, bool relleno) {
    Trazo t;
    t.forma = FORMA_POLIGONO;
    t.relleno = relleno;
    t.grosor = 6;
    t.completo.assign(pts.begin(), pts.end());
    return t;
}
int area(const Seleccion &s) { int n = 0; for (const Span &sp : s.tramos()) n += sp.x1 - sp.x0 + 1; return n; }
}  // namespace

PRUEBA(poligono_relleno_cubre_su_area) {
    const Trazo t = poligono({{10, 10}, {30, 10}, {30, 30}, {10, 30}}, true);
    const std::vector<Contorno> c = contornosDeForma(t);
    VERIFICAR(c.size() == 1);
    const Seleccion s = Seleccion::poligono(50, 50, c[0]);
    VERIFICAR(s.caja == (Rect{10, 10, 30, 30}) && area(s) == 400);
    // un triángulo rectángulo de catetos 20: la mitad del cuadrado
    const Seleccion tri = Seleccion::poligono(50, 50, contornosDeForma(poligono({{10, 10}, {30, 10}, {10, 30}}, true))[0]);
    VERIFICAR(area(tri) > 180 && area(tri) < 220);
}

PRUEBA(poligono_contorno_rodea_los_vertices) {
    const Trazo t = poligono({{20, 20}, {60, 25}, {40, 60}}, false);
    const std::vector<Contorno> c = contornosDeForma(t);
    VERIFICAR(c.size() == 1 && c[0].size() > 20);
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (const pf::Vec &v : c[0]) { x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y); }
    VERIFICAR(x0 < 20 && x1 > 60 && y0 < 20 && y1 > 60);              // el grosor sobresale de los vértices
    VERIFICAR(x0 > 14 && x1 < 66 && y0 > 14 && y1 < 66);              // pero sin pasarse
}

PRUEBA(poligono_con_pocos_vertices) {
    VERIFICAR(contornosDeForma(poligono({{5, 5}}, true)).empty());                       // uno solo: nada
    const std::vector<Contorno> l = contornosDeForma(poligono({{5, 5}, {45, 5}}, true)); // dos: una línea (aunque pida relleno)
    VERIFICAR(l.size() == 1 && l[0].size() > 4);
    VERIFICAR(contornosDeForma(poligono({{5, 5}, {5, 5}, {5, 5}}, true)).empty());        // todos repetidos: nada, sin romperse
}

PRUEBA(poligono_ignora_vertices_repetidos) {
    const Trazo t = poligono({{10, 10}, {10, 10}, {30, 10}, {30, 10}, {30, 30}, {10, 30}}, true);
    const std::vector<Contorno> c = contornosDeForma(t);
    VERIFICAR(c.size() == 1);
    for (const pf::Vec &v : c[0]) VERIFICAR(std::isfinite(v.x) && std::isfinite(v.y));
    VERIFICAR(area(Seleccion::poligono(50, 50, c[0])) == 400);
}

PRUEBA(poligono_shift_salta_de_15_en_15) {
    const Punto ant{50, 50};
    const Punto p = restringirForma(FORMA_POLIGONO, ant, Punto{150, 53});      // casi horizontal
    VERIFICAR(std::fabs(p.y - 50) < 1e-9 && p.x > 149);
    const Punto q = restringirForma(FORMA_POLIGONO, ant, Punto{120, 120});     // 45°
    VERIFICAR(std::fabs((q.x - 50) - (q.y - 50)) < 1e-6);
    const double ang = std::atan2(q.y - 50, q.x - 50);
    VERIFICAR(std::fabs(ang - PI / 4) < 1e-6);
}

PRUEBA(poligono_zona_de_deshacer_incluye_todo) {
    Documento d(80, 80);
    d.capas[0].img.llenar(0);
    d.historial.limpiar();
    Trazo t = poligono({{10, 12}, {70, 20}, {40, 66}}, true);
    t.contornos = contornosDeForma(t);
    const Rect z = rectDeTrazo(t, 80, 80);
    VERIFICAR(z.x0 <= 10 && z.y0 <= 12 && z.x1 >= 70 && z.y1 >= 66);
    const Imagen antes = d.capas[0].img;
    d.capas[0].img.en(40, 40) = 0xFF00FF00;            // "pintar" algo dentro de la zona
    d.registrarTrazo(d.capas[0].id, t, antes);
    VERIFICAR(d.capas[0].ops.size() == 1);
    VERIFICAR(d.deshacer() && d.capas[0].img.en(40, 40) == 0);
    VERIFICAR(d.rehacer() && d.capas[0].img.en(40, 40) == 0xFF00FF00);
}
