#include <cmath>
#include "mini_test.h"
#include "core/documento.h"
#include "core/estampa.h"

using namespace plz;

namespace {
Trazo base(double grosor = 20) {
    Trazo t;
    t.estampado = true;
    t.grosor = grosor;
    t.color = 0xFFFF0000;
    t.semilla = 7;
    t.est.punta = PUNTA_REDONDA;
    t.est.espaciado = 0.25;
    return t;
}
void recorrido(Trazo &t, double x0, double y0, double x1, double y1, int n = 40, double pres = 0.5) {
    for (int i = 0; i <= n; ++i) t.completo.push_back({x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, pres});
}
int opacos(const Imagen &im) { int n = 0; for (Pixel p : im.px) if (p >> 24) ++n; return n; }
Rect caja(const Imagen &im) {
    Rect r;
    for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) if (im.en(x, y) >> 24) r.unir(Rect{x, y, x + 1, y + 1});
    return r;
}
}  // namespace

PRUEBA(estampado_linea_recta_con_sellos_redondos) {
    Imagen im(200, 100);
    Trazo t = base(20);
    recorrido(t, 30, 50, 170, 50);
    pintarEstampado(im, t);
    const Rect r = caja(im);
    VERIFICAR(r.x0 >= 19 && r.x0 <= 21 && r.x1 >= 179 && r.x1 <= 181);       // 20 px de grosor alrededor del recorrido
    VERIFICAR(r.y0 >= 39 && r.y0 <= 41 && r.y1 >= 59 && r.y1 <= 61);
    VERIFICAR(im.en(100, 50) == 0xFFFF0000);                                  // el centro es rojo pleno
    VERIFICAR(im.en(100, 20) == 0);
}

PRUEBA(estampado_en_flujo_es_igual_que_el_trazo_entero) {
    Trazo t = base(24);
    t.est.punta = PUNTA_HOJA; t.est.sigueTrazo = true; t.est.variaRot = 30; t.est.variaTam = 0.4;
    t.est.dispersion = 0.3; t.est.espaciado = 0.7; t.est.grano = 0.2; t.est.flujo = 0.8;
    recorrido(t, 20, 20, 150, 90, 30);
    recorrido(t, 150, 90, 40, 140, 30);
    Imagen entero(200, 160);
    pintarEstampado(entero, t);
    Imagen enVivo(200, 160);                         // punto a punto, como al dibujar
    Estampador e(enVivo, t);
    for (const Punto &p : t.completo) e.punto(p);
    e.terminar();
    VERIFICAR(entero.px == enVivo.px);
    VERIFICAR(opacos(entero) > 500);
}

PRUEBA(estampado_misma_semilla_igual_y_otra_distinta) {
    Trazo t = base(24);
    t.est.punta = PUNTA_ESTRELLA; t.est.variaRot = 180; t.est.variaTam = 0.5; t.est.dispersion = 0.4; t.est.espaciado = 1.2;
    recorrido(t, 20, 60, 180, 60, 30);
    Imagen a(200, 120), b(200, 120), c(200, 120);
    pintarEstampado(a, t);
    pintarEstampado(b, t);
    t.semilla = 8;
    pintarEstampado(c, t);
    VERIFICAR(a.px == b.px);
    VERIFICAR(a.px != c.px);
}

PRUEBA(estampado_un_toque_deja_un_sello) {
    Imagen im(60, 60);
    Trazo t = base(20);
    t.completo.push_back({30, 30, 0.5});
    pintarEstampado(im, t);
    const Rect r = caja(im);
    VERIFICAR(r.w() >= 19 && r.w() <= 22 && r.h() >= 19 && r.h() <= 22 && im.en(30, 30) == 0xFFFF0000);
    Imagen vacia(60, 60);
    pintarEstampado(vacia, base(20));                // sin puntos: nada
    VERIFICAR(opacos(vacia) == 0);
}

PRUEBA(estampado_espaciado_y_flujo_acumulan) {
    Trazo denso = base(20), ralo = base(20);
    denso.est.punta = ralo.est.punta = PUNTA_SUAVE;
    denso.est.espaciado = 0.05; ralo.est.espaciado = 2.0;
    denso.est.flujo = ralo.est.flujo = 0.2;
    recorrido(denso, 20, 30, 180, 30, 80);
    recorrido(ralo, 20, 30, 180, 30, 80);
    Imagen a(200, 60), b(200, 60);
    pintarEstampado(a, denso);
    pintarEstampado(b, ralo);
    VERIFICAR((a.en(100, 30) >> 24) > 200);          // muchos sellos suaves apilados llegan casi a pleno
    VERIFICAR(opacos(b) < opacos(a));                // espaciados no se tocan: hay huecos
    int huecos = 0;
    for (int x = 30; x < 170; ++x) if ((b.en(x, 30) >> 24) < 10) ++huecos;
    VERIFICAR(huecos > 20);
}

PRUEBA(estampado_la_presion_cambia_el_tamano) {
    Imagen fuerte(100, 60), suave(100, 60);
    Trazo a = base(30), b = base(30);
    recorrido(a, 20, 30, 80, 30, 20, 1.0);
    recorrido(b, 20, 30, 80, 30, 20, 0.1);
    pintarEstampado(fuerte, a);
    pintarEstampado(suave, b);
    VERIFICAR(caja(fuerte).h() >= 29 && caja(suave).h() < caja(fuerte).h() - 8);
    b.est.presion = 0;                               // sin efecto de la presión: siempre el mismo tamaño
    Imagen igual(100, 60);
    pintarEstampado(igual, b);
    VERIFICAR(caja(igual).h() == caja(fuerte).h());
}

PRUEBA(estampado_la_hoja_sigue_la_direccion) {
    Trazo h = base(40);
    h.est.punta = PUNTA_HOJA; h.est.sigueTrazo = true; h.est.espaciado = 0.2;
    Imagen horiz(200, 200), vert(200, 200);
    Trazo v = h;
    recorrido(h, 30, 100, 170, 100);
    recorrido(v, 100, 30, 100, 170);
    pintarEstampado(horiz, h);
    pintarEstampado(vert, v);
    const Rect a = caja(horiz), b = caja(vert);
    VERIFICAR(a.w() > a.h() * 4);                    // la hoja es alargada: tumbada, la banda es baja
    VERIFICAR(b.h() > b.w() * 4);
    VERIFICAR(a.h() < 30 && b.w() < 30);
}

PRUEBA(estampado_grano_come_parte_del_sello) {
    Trazo liso = base(40), granoso = base(40);
    granoso.est.grano = 0.5;
    liso.completo.push_back({50, 50, 0.5});
    granoso.completo.push_back({50, 50, 0.5});
    Imagen a(100, 100), b(100, 100);
    pintarEstampado(a, liso);
    pintarEstampado(b, granoso);
    VERIFICAR(opacos(b) < opacos(a) * 7 / 10 && opacos(b) > opacos(a) / 5);
}

PRUEBA(estampado_respeta_la_seleccion) {
    Trazo t = base(30);
    t.recorte = {{40, 0}, {60, 0}, {60, 100}, {40, 100}};                    // franja de 20 px de ancho
    recorrido(t, 10, 50, 90, 50);
    Imagen im(100, 100);
    pintarEstampado(im, t);
    const Rect r = caja(im);
    VERIFICAR(r.x0 == 40 && r.x1 == 60 && opacos(im) > 200);
}

PRUEBA(estampado_la_zona_de_deshacer_cubre_todo) {
    Documento d(150, 150);
    Trazo t = base(30);
    t.est.punta = PUNTA_ESTRELLA; t.est.dispersion = 0.5; t.est.variaRot = 180; t.est.variaTam = 0.3; t.est.espaciado = 0.6;
    recorrido(t, 20, 40, 130, 110, 30);
    t.semilla = 99;
    const Rect z = rectDeTrazo(t, 150, 150);
    Imagen prueba(150, 150);
    pintarEstampado(prueba, t);
    const Rect c = caja(prueba);
    VERIFICAR(z.x0 <= c.x0 && z.y0 <= c.y0 && z.x1 >= c.x1 && z.y1 >= c.y1);   // la zona guardada contiene todo lo pintado
    // y deshacer/rehacer con el documento deja la capa exacta
    d.capas[0].img.llenar(0);
    d.historial.limpiar();
    const Imagen antes = d.capas[0].img;
    pintarEstampado(d.capas[0].img, t);
    const std::vector<Pixel> pintado = d.capas[0].img.px;
    d.registrarTrazo(d.capas[0].id, t, antes);
    VERIFICAR(d.deshacer() && opacos(d.capas[0].img) == 0);
    VERIFICAR(d.rehacer() && d.capas[0].img.px == pintado);
    d.reconstruir(d.capas[0]);                      // reconstruir desde las operaciones da lo mismo
    VERIFICAR(d.capas[0].img.px == pintado);
}
