#include <memory>
#include "mini_test.h"
#include "core/documento.h"
#include "core/seleccion.h"

using namespace plz;

namespace {
constexpr Pixel ROJO = 0xFFFF0000, AZUL = 0xFF0000FF;
std::unique_ptr<Documento> docRojo(int w = 20, int h = 20) {
    auto d = std::make_unique<Documento>(w, h);
    d->capas[0].img.llenar(ROJO);
    d->historial.limpiar();
    return d;
}
int contar(const Imagen &im, Pixel p) { int n = 0; for (Pixel q : im.px) if (q == p) ++n; return n; }
}  // namespace

PRUEBA(seleccion_rectangulo_y_poligono) {
    Seleccion r = Seleccion::rectangulo(20, 20, Rect{5, 5, 10, 8});
    VERIFICAR(r.caja == (Rect{5, 5, 10, 8}) && r.contiene(5, 5) && !r.contiene(10, 5) && !r.contiene(4, 5));
    VERIFICAR(r.tramos().size() == 3);
    VERIFICAR(Seleccion::rectangulo(20, 20, Rect{30, 30, 40, 40}).vacia());      // fuera del lienzo
    Contorno tri{{2, 2}, {18, 2}, {2, 18}};                                      // triángulo
    Seleccion t = Seleccion::poligono(20, 20, tri);
    VERIFICAR(t.contiene(4, 4) && !t.contiene(16, 16) && !t.contiene(19, 19));
    VERIFICAR(Seleccion::poligono(20, 20, Contorno{{1, 1}, {5, 5}}).vacia());    // menos de 3 puntos
}

PRUEBA(borrar_seleccion_deshacer_rehacer) {
    auto d = docRojo();
    const Seleccion s = Seleccion::rectangulo(20, 20, Rect{5, 5, 15, 15});
    VERIFICAR(d->borrarSeleccion(0, s));
    VERIFICAR(d->capas[0].img.en(10, 10) == 0 && d->capas[0].img.en(2, 2) == ROJO);
    VERIFICAR(contar(d->capas[0].img, ROJO) == 400 - 100);
    VERIFICAR(d->deshacer() && d->capas[0].img.en(10, 10) == ROJO);
    VERIFICAR(d->rehacer() && d->capas[0].img.en(10, 10) == 0);
}

PRUEBA(copiar_seleccion_deja_transparente_lo_de_fuera_del_lazo) {
    auto d = docRojo();
    const Seleccion t = Seleccion::poligono(20, 20, Contorno{{2, 2}, {18, 2}, {2, 18}});
    Imagen c = d->copiarSeleccion(0, t);
    VERIFICAR(c.w == t.caja.w() && c.h == t.caja.h());
    VERIFICAR(c.en(1, 1) == ROJO);                        // dentro del triángulo
    VERIFICAR(c.en(c.w - 1, c.h - 1) == 0);               // esquina de la caja, fuera del triángulo
}

PRUEBA(pegar_deshacer_y_reconstruir) {
    auto d = std::make_unique<Documento>(20, 20);
    Imagen im(4, 4, AZUL);
    VERIFICAR(d->pegar(0, im, 3, 3));
    VERIFICAR(d->capas[0].img.en(3, 3) == AZUL && d->capas[0].img.en(6, 6) == AZUL && d->capas[0].img.en(7, 7) == 0);
    VERIFICAR(d->pegar(0, im, 18, 18));                   // medio fuera: se recorta
    VERIFICAR(d->capas[0].img.en(19, 19) == AZUL);
    VERIFICAR(!d->pegar(0, im, 50, 50));                  // totalmente fuera: nada
    const std::vector<Pixel> esperado = d->capas[0].img.px;
    d->reconstruir(d->capas[0]);                          // repetir las operaciones da lo mismo
    VERIFICAR(d->capas[0].img.px == esperado);
    d->deshacer();
    VERIFICAR(d->capas[0].img.en(19, 19) == 0 && d->capas[0].img.en(3, 3) == AZUL);
}

PRUEBA(relleno_dentro_de_la_seleccion) {
    auto d = std::make_unique<Documento>(20, 20);
    const Seleccion s = Seleccion::rectangulo(20, 20, Rect{5, 5, 15, 15});
    VERIFICAR(!d->rellenar(0, 2, 2, ROJO, {0, 0}, false, &s));            // semilla fuera de la selección
    VERIFICAR(d->rellenar(0, 8, 8, ROJO, {0, 0}, false, &s));
    VERIFICAR(contar(d->capas[0].img, ROJO) == 100);                       // solo la selección, no todo el lienzo
    VERIFICAR(d->capas[0].img.en(2, 2) == 0);
    d->deshacer();
    VERIFICAR(contar(d->capas[0].img, ROJO) == 0);
}

PRUEBA(recortar_relleno_a_seleccion) {
    Relleno r; r.nucleo = {{3, 0, 19}}; r.borde = {{3, 0, 19}};
    const Seleccion s = Seleccion::rectangulo(20, 20, Rect{4, 0, 8, 20});
    Relleno o = recortarRelleno(r, s);
    VERIFICAR(o.nucleo.size() == 1 && o.nucleo[0].x0 == 4 && o.nucleo[0].x1 == 7 && o.borde.size() == 1);
}
