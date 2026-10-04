#include <memory>
#include "mini_test.h"
#include "core/documento.h"
#include "core/transformar.h"

using namespace plz;

namespace {
constexpr Pixel ROJO = 0xFFFF0000, AZUL = 0xFF0000FF;
constexpr double PI = 3.14159265358979323846;

// Lienzo 40x40 transparente con un bloque rojo de 8x8 en (10,10) y un pixel azul suelto en (30,30)
std::unique_ptr<Documento> docBloque() {
    auto d = std::make_unique<Documento>(40, 40);
    for (int y = 10; y < 18; ++y) for (int x = 10; x < 18; ++x) d->capas[0].img.en(x, y) = ROJO;
    d->capas[0].img.en(30, 30) = AZUL;
    d->historial.limpiar();
    return d;
}
int contar(const Imagen &im, Pixel p) { int n = 0; for (Pixel q : im.px) if (q == p) ++n; return n; }
Seleccion selBloque() { return Seleccion::rectangulo(40, 40, Rect{10, 10, 18, 18}); }
Rect cajaDe(const Imagen &im, Pixel p) {
    Rect r;
    for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) if (im.en(x, y) == p) r.unir(Rect{x, y, x + 1, y + 1});
    return r;
}
Rect cajaVisible(const Imagen &im) {   // todo lo rojizo, bordes suaves incluidos (ignora el pixel azul suelto)
    Rect r;
    for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) if ((im.en(x, y) >> 16) & 255u) r.unir(Rect{x, y, x + 1, y + 1});
    return r;
}
}  // namespace

PRUEBA(afin_inversa_y_desde) {
    const Afin m = Afin::desde(5, 5, 2, 3, PI / 6, 20, 30);
    const pf::Vec c = m.aplicar(5, 5);
    VERIFICAR(std::fabs(c.x - 20) < 1e-9 && std::fabs(c.y - 30) < 1e-9);          // el origen va al destino
    const pf::Vec p = m.aplicar(7.5, -2);
    const pf::Vec q = m.inversa().aplicar(p.x, p.y);
    VERIFICAR(std::fabs(q.x - 7.5) < 1e-9 && std::fabs(q.y + 2) < 1e-9);          // inversa de ida y vuelta
    VERIFICAR(Afin().esIdentidad() && !m.esIdentidad());
    Afin plana; plana.a = 0;                                                      // aplastada: sin inversa
    VERIFICAR(!plana.invertible());
}

PRUEBA(mover_entero_es_copia_exacta) {
    auto d = docBloque();
    Afin m; m.tx = 12; m.ty = 5;
    VERIFICAR(d->transformar(0, selBloque(), m));
    const Imagen &im = d->capas[0].img;
    VERIFICAR(im.en(10, 10) == 0 && im.en(17, 17) == 0);                           // el sitio original queda vacío
    VERIFICAR(cajaDe(im, ROJO) == (Rect{22, 15, 30, 23}));
    VERIFICAR(contar(im, ROJO) == 64);                                             // sin bordes difuminados
    VERIFICAR(im.en(30, 30) == AZUL);                                              // lo no seleccionado no se toca
}

PRUEBA(mover_pone_lo_levantado_encima_de_lo_que_haya) {
    auto d = docBloque();
    Afin m; m.tx = 20; m.ty = 20;                                                  // el bloque cae sobre el pixel azul
    VERIFICAR(d->transformar(0, selBloque(), m));
    VERIFICAR(d->capas[0].img.en(30, 30) == ROJO);
    d->deshacer();
    VERIFICAR(d->capas[0].img.en(30, 30) == AZUL && d->capas[0].img.en(12, 12) == ROJO);
}

PRUEBA(transformar_deshacer_rehacer_y_reconstruir) {
    auto d = docBloque();
    const Imagen original = d->capas[0].img;
    const Afin m = Afin::desde(14, 14, 1.5, 0.75, PI / 7, 24.3, 20.2);
    VERIFICAR(d->transformar(0, selBloque(), m));
    const std::vector<Pixel> hecho = d->capas[0].img.px;
    VERIFICAR(hecho != original.px);
    VERIFICAR(d->deshacer() && d->capas[0].img.px == original.px);                 // vuelve exacto
    VERIFICAR(d->rehacer() && d->capas[0].img.px == hecho);
    VERIFICAR(d->capas[0].ops.size() == 1);
    d->capas[0].img = Imagen(40, 40);                                              // reconstruir desde cero NO tiene la
    d->capas[0].ops.clear();                                                       // imagen original: se parte de ella
    d->capas[0].base = original;
    d->capas[0].ops.push_back(std::get<Transformacion>([&] {
        Transformacion t; t.origen = selBloque().tramos(); t.m = m; return Operacion(t); }()));
    d->reconstruir(d->capas[0]);
    VERIFICAR(d->capas[0].img.px == hecho);                                        // repetirla da lo mismo
}

PRUEBA(rotar_90_grados) {
    auto d = std::make_unique<Documento>(40, 40);
    for (int y = 10; y < 14; ++y) for (int x = 10; x < 20; ++x) d->capas[0].img.en(x, y) = ROJO;   // 10x4 horizontal
    d->historial.limpiar();
    const Seleccion s = Seleccion::rectangulo(40, 40, Rect{10, 10, 20, 14});
    const Afin m = Afin::desde(15, 12, 1, 1, PI / 2, 15, 12);                      // gira sobre su centro
    VERIFICAR(d->transformar(0, s, m));
    const Rect r = cajaDe(d->capas[0].img, ROJO);
    VERIFICAR(r.w() == 4 && r.h() == 10);                                          // ahora es vertical
    VERIFICAR(std::abs((r.x0 + r.x1) / 2.0 - 15.0) <= 1.0 && std::abs((r.y0 + r.y1) / 2.0 - 12.0) <= 1.0);
}

PRUEBA(escalar_agranda_y_reduce) {
    auto d = docBloque();
    VERIFICAR(d->transformar(0, selBloque(), Afin::desde(14, 14, 2, 2, 0, 14, 14)));
    Rect r = cajaVisible(d->capas[0].img);
    VERIFICAR(r == (Rect{5, 5, 23, 23}));                                          // 8x8 -> 16x16 centrado + 1 px de borde suave
    VERIFICAR(d->capas[0].img.en(14, 14) == ROJO);                                 // el interior sigue siendo rojo puro
    d->deshacer();
    VERIFICAR(d->transformar(0, selBloque(), Afin::desde(14, 14, 0.5, 0.5, 0, 14, 14)));
    r = cajaVisible(d->capas[0].img);
    VERIFICAR(r.w() >= 3 && r.w() <= 5 && r.h() >= 3 && r.h() <= 5);
}

PRUEBA(espejo_con_escala_negativa) {
    auto d = std::make_unique<Documento>(40, 40);
    for (int x = 10; x < 14; ++x) d->capas[0].img.en(x, 10) = ROJO;
    d->capas[0].img.en(10, 10) = AZUL;                                             // marca en el extremo izquierdo
    d->historial.limpiar();
    const Seleccion s = Seleccion::rectangulo(40, 40, Rect{10, 10, 14, 11});
    VERIFICAR(d->transformar(0, s, Afin::desde(12, 10.5, -1, 1, 0, 12, 10.5)));
    VERIFICAR(d->capas[0].img.en(13, 10) == AZUL && d->capas[0].img.en(10, 10) == ROJO);
}

PRUEBA(vecino_mas_cercano_no_mezcla_colores) {
    auto d = docBloque();
    const Transformacion t0;                                                       // (solo para comprobar el valor por defecto)
    VERIFICAR(t0.suave);
    VERIFICAR(d->transformar(0, selBloque(), Afin::desde(14, 14, 1.7, 1.7, PI / 5, 20, 20), false));
    for (Pixel p : d->capas[0].img.px) VERIFICAR(p == 0 || p == ROJO || p == AZUL);
}

PRUEBA(salirse_del_lienzo_recorta) {
    auto d = docBloque();
    Afin m; m.tx = 26; m.ty = 0;                                                   // el bloque queda en x 36..43: la mitad fuera
    VERIFICAR(d->transformar(0, selBloque(), m));
    VERIFICAR(d->capas[0].img.en(36, 12) == ROJO && d->capas[0].img.en(39, 17) == ROJO);   // lo que cabe, se queda
    VERIFICAR(d->capas[0].img.en(35, 12) == 0 && d->capas[0].img.en(39, 9) == 0 && d->capas[0].img.en(10, 12) == 0);
    d->deshacer();
    VERIFICAR(d->capas[0].img.en(10, 10) == ROJO && d->capas[0].img.en(17, 17) == ROJO);
    Afin lejos; lejos.tx = 500;                                                    // todo fuera: se pierde, pero se puede deshacer
    VERIFICAR(d->transformar(0, selBloque(), lejos));
    VERIFICAR(contar(d->capas[0].img, ROJO) == 0);
    d->deshacer();
    VERIFICAR(contar(d->capas[0].img, ROJO) == 64);
}

PRUEBA(sin_seleccion_o_identidad_no_hace_nada) {
    auto d = docBloque();
    VERIFICAR(!d->transformar(0, Seleccion(), Afin::desde(0, 0, 2, 2, 0, 0, 0)));
    VERIFICAR(!d->transformar(0, selBloque(), Afin()));
    VERIFICAR(d->capas[0].ops.empty() && !d->historial.puedeDeshacer());
}

PRUEBA(lazo_se_mueve_solo_lo_de_dentro) {
    auto d = std::make_unique<Documento>(40, 40);
    d->capas[0].img.llenar(ROJO);
    d->historial.limpiar();
    const Contorno tri{{5, 5}, {25, 5}, {5, 25}};
    const Seleccion s = Seleccion::poligono(40, 40, tri);
    const int dentro = int(s.tramos().size());
    VERIFICAR(dentro > 0);
    Afin m; m.tx = 10; m.ty = 10;
    VERIFICAR(d->transformar(0, s, m));
    VERIFICAR(d->capas[0].img.en(6, 6) == 0);                                      // hueco donde estaba el triángulo
    VERIFICAR(d->capas[0].img.en(0, 0) == ROJO);                                   // fuera del triángulo, intacto
    const Contorno nuevo = transformarContorno(tri, m);
    VERIFICAR(nuevo[0].x == 15 && nuevo[0].y == 15 && nuevo[2].y == 35);
    VERIFICAR(Seleccion::poligono(40, 40, nuevo).contiene(17, 17));
}
