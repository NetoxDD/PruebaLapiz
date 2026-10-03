#include <memory>
#include "mini_test.h"
#include "core/documento.h"
#include "core/mezcla.h"
#include "core/relleno.h"

using namespace plz;

namespace {
constexpr Pixel NEGRO = 0xFF000000;
constexpr Pixel ROJO = 0xFFFF0000;
constexpr Pixel AZUL = 0xFF0000FF;

// Dibuja un marco negro de 1 pixel (x0,y0)-(x1,y1) incluidos
void marco(Imagen &im, int x0, int y0, int x1, int y1) {
    for (int x = x0; x <= x1; ++x) { im.en(x, y0) = NEGRO; im.en(x, y1) = NEGRO; }
    for (int y = y0; y <= y1; ++y) { im.en(x0, y) = NEGRO; im.en(x1, y) = NEGRO; }
}
Documento docConMarco() {
    Documento d(20, 20);
    marco(d.capas[0].img, 5, 5, 14, 14);
    return d;
}
int contar(const Imagen &im, Pixel p) {
    int n = 0; for (Pixel q : im.px) if (q == p) ++n; return n;
}
}  // namespace

PRUEBA(imagen_recortar_y_pegar) {
    Imagen im(6, 6);
    im.en(2, 2) = 0xFF112233;
    Imagen c = im.recortar(Rect{1, 1, 4, 4});
    VERIFICAR(c.w == 3 && c.h == 3 && c.en(1, 1) == 0xFF112233);
    im.en(2, 2) = 0;
    im.pegar(c, 1, 1);
    VERIFICAR(im.en(2, 2) == 0xFF112233);
    im.pegar(c, 5, 5);                       // se sale del borde: se recorta sin romper nada
    VERIFICAR(im.en(5, 5) == 0);
    VERIFICAR(im.recortar(Rect{-5, -5, 100, 100}).w == 6);
}

PRUEBA(imagen_zona_sucia) {
    Imagen im(10, 10);
    im.consumirSucio();
    im.tocar(Rect{2, 2, 4, 4});
    im.tocar(Rect{6, 1, 8, 3});
    Rect s = im.consumirSucio();
    VERIFICAR(s == (Rect{2, 1, 8, 4}));
    VERIFICAR(im.consumirSucio().vacio());
    im.tocar();
    VERIFICAR(im.consumirSucio() == im.rectTotal());
}

PRUEBA(relleno_llena_el_interior_y_no_se_sale) {
    Documento d = docConMarco();
    VERIFICAR(d.rellenar(0, 10, 10, ROJO, {0, 0}, false));
    const Imagen &im = d.capas[0].img;
    VERIFICAR(im.en(10, 10) == ROJO && im.en(6, 6) == ROJO && im.en(13, 13) == ROJO);
    VERIFICAR(im.en(5, 5) == NEGRO);          // el marco sigue
    VERIFICAR(im.en(2, 2) == 0);              // fuera no se toca
    VERIFICAR(contar(im, ROJO) == 8 * 8);
}

PRUEBA(relleno_se_escapa_por_un_hueco) {
    Documento d = docConMarco();
    d.capas[0].img.en(5, 9) = 0;              // agujero en el marco
    d.rellenar(0, 10, 10, ROJO, {0, 0}, false);
    VERIFICAR(d.capas[0].img.en(2, 2) == ROJO);
}

PRUEBA(relleno_semilla_sobre_linea_rellena_la_linea) {
    Documento d = docConMarco();
    d.rellenar(0, 5, 5, ROJO, {0, 0}, false);
    VERIFICAR(d.capas[0].img.en(5, 5) == ROJO && d.capas[0].img.en(7, 5) == ROJO);
    VERIFICAR(d.capas[0].img.en(10, 10) == 0);
}

PRUEBA(relleno_fuera_del_lienzo_no_hace_nada) {
    Documento d = docConMarco();
    VERIFICAR(!d.rellenar(0, -1, 3, ROJO, {}, false));
    VERIFICAR(!d.rellenar(0, 3, 99, ROJO, {}, false));
    VERIFICAR(d.historial.pasos() == 0);
}

PRUEBA(relleno_tolerancia) {
    auto nuevo = [] {
        auto d = std::make_unique<Documento>(10, 1);
        for (int x = 0; x < 10; ++x) d->capas[0].img.en(x, 0) = 0xFF000000 | (unsigned(x) * 10) << 16;   // rojo creciente 0,10,20...
        return d;
    };
    auto pa = nuevo(), pb = nuevo();
    Documento &a = *pa, &b = *pb;
    a.rellenar(0, 0, 0, AZUL, {15, 0}, false);          // alcanza hasta el pixel 1 (diferencia 10)
    VERIFICAR(a.capas[0].img.en(1, 0) == AZUL && a.capas[0].img.en(2, 0) != AZUL);
    b.rellenar(0, 0, 0, AZUL, {255, 0}, false);
    VERIFICAR(contar(b.capas[0].img, AZUL) == 10);
}

PRUEBA(relleno_borde_se_pone_debajo_de_las_lineas) {
    // línea con antialiasing: pixel gris semitransparente entre el interior y el exterior
    Documento d(7, 1);
    Imagen &im = d.capas[0].img;
    im.en(3, 0) = escalar(NEGRO, 128);                  // 50% negro
    d.rellenar(0, 0, 0, ROJO, {10, 1}, false);          // tolerancia baja: el 50% no entra en el núcleo
    VERIFICAR(im.en(2, 0) == ROJO);
    const Pixel p = im.en(3, 0);                        // borde: negro 50% encima de rojo
    VERIFICAR((p >> 24) == 255);                        // ya no hay hueco blanco
    VERIFICAR(((p >> 16) & 255) > 100 && ((p >> 16) & 255) < 160);
}

PRUEBA(relleno_deshacer_y_rehacer) {
    Documento d = docConMarco();
    const Imagen antes = d.capas[0].img;
    d.rellenar(0, 10, 10, ROJO, {}, false);
    const Imagen despues = d.capas[0].img;
    VERIFICAR(d.deshacer());
    VERIFICAR(d.capas[0].img.px == antes.px);
    VERIFICAR(d.capas[0].ops.empty());
    VERIFICAR(d.rehacer());
    VERIFICAR(d.capas[0].img.px == despues.px);
    VERIFICAR(d.capas[0].ops.size() == 1);
}

PRUEBA(relleno_todas_las_capas_usa_la_imagen_combinada) {
    auto nuevo = [] {
        auto d = std::make_unique<Documento>(20, 20);
        marco(d->capas[0].img, 5, 5, 14, 14);   // las líneas están en la capa 0
        d->nuevaCapa();                         // capa 1, vacía, activa
        return d;
    };
    // solo con su propia capa, el relleno se desborda por toda la capa 1
    auto psolo = nuevo();
    psolo->rellenar(1, 10, 10, ROJO, {0, 0}, false);
    VERIFICAR(psolo->capas[1].img.en(2, 2) == ROJO);
    auto pd = nuevo();
    Documento &d = *pd;
    // mirando todas las capas, respeta el marco de la capa 0 y pinta en la capa 1
    d.rellenar(1, 10, 10, ROJO, {0, 0}, true);
    VERIFICAR(d.capas[1].img.en(10, 10) == ROJO);
    VERIFICAR(d.capas[1].img.en(2, 2) == 0);
    VERIFICAR(d.capas[0].img.en(10, 10) == 0);
    VERIFICAR(contar(d.capas[1].img, ROJO) == 8 * 8);
}

PRUEBA(deshacer_un_trazo_conserva_un_relleno_anterior) {
    // El caso que rompía el deshacer por repintado: relleno y después un trazo
    Documento d = docConMarco();
    d.rellenar(0, 10, 10, ROJO, {0, 0}, false);
    Imagen antes = d.capas[0].img;
    d.capas[0].img.en(10, 10) = AZUL;                   // "trazo" que pinta un pixel
    Trazo t; t.completo.push_back({10, 10, 0.5});
    d.registrarTrazo(d.capas[0].id, t, antes);
    VERIFICAR(d.deshacer());
    VERIFICAR(d.capas[0].img.en(10, 10) == ROJO);       // sigue el relleno
    VERIFICAR(d.deshacer());
    VERIFICAR(d.capas[0].img.en(10, 10) == 0);          // ahora sí, vacío
}

PRUEBA(reconstruir_repite_trazos_y_rellenos_en_orden) {
    Documento d = docConMarco();
    d.pintor = [](Imagen &im, const Trazo &t) { for (auto &p : t.completo) im.en(int(p.x), int(p.y)) = AZUL; };
    d.rellenar(0, 10, 10, ROJO, {0, 0}, false);
    Imagen antes = d.capas[0].img;
    Trazo t; t.completo.push_back({8, 8, 0.5});
    d.pintor(d.capas[0].img, t);
    d.registrarTrazo(d.capas[0].id, t, antes);
    const std::vector<Pixel> esperado = d.capas[0].img.px;
    Imagen marcoSolo(20, 20); marco(marcoSolo, 5, 5, 14, 14);
    d.capas[0].base = marcoSolo;
    d.reconstruir(d.capas[0]);                           // base + ops en orden = mismo resultado
    VERIFICAR(d.capas[0].img.px == esperado);
}

PRUEBA(historial_respeta_el_presupuesto_de_memoria) {
    Documento d(100, 100);
    d.historial.setPresupuesto(3 * 100 * 100 * 4 * 2 + 10);     // caben ~3 pasos que toquen toda la capa
    for (int i = 0; i < 8; ++i) {
        Imagen antes = d.capas[0].img;
        Trazo t; t.grosor = 1000; t.completo.push_back({50, 50, 0.5});   // zona = todo el lienzo
        d.registrarTrazo(d.capas[0].id, t, antes);
    }
    VERIFICAR(d.historial.pasos() <= 3 && d.historial.pasos() >= 1);
    VERIFICAR(d.historial.bytesUsados() <= 3 * 100 * 100 * 4 * 2 + 10);
    const int guardados = int(d.historial.pasos());
    int deshechos = 0; while (d.deshacer()) ++deshechos;
    VERIFICAR(deshechos == guardados);                       // se puede deshacer todo lo que se conservó
    VERIFICAR(d.capas[0].ops.size() == 8 - std::size_t(guardados));   // lo olvidado queda aplicado
}

PRUEBA(deshacer_cuesta_lo_mismo_con_muchas_operaciones) {
    // la zona guardada es pequeña aunque la capa tenga miles de trazos
    Documento d(500, 500);
    for (int i = 0; i < 400; ++i) {
        Imagen antes = d.capas[0].img;
        d.capas[0].img.en(i % 500, 7) = AZUL;
        Trazo t; t.grosor = 1; t.completo.push_back({double(i % 500), 7, 0.5});
        d.registrarTrazo(d.capas[0].id, t, antes);
    }
    VERIFICAR(d.historial.bytesUsados() < 400u * 200u * 4u * 2u);    // cada paso guarda una zona diminuta
    VERIFICAR(d.deshacer());
    VERIFICAR(d.capas[0].img.en(399, 7) == 0 && d.capas[0].img.en(398, 7) == AZUL);
}
