#include <array>
#include <cmath>
#include "mini_test.h"
#include "core/documento.h"
#include "core/mezcla.h"

using namespace plz;

namespace {
// Pintor de prueba: un pixel opaco por cada punto del trazo
void pintorPrueba(Imagen &im, const Trazo &t) {
    for (const Punto &p : t.completo)
        if (im.dentro(int(p.x), int(p.y))) im.en(int(p.x), int(p.y)) = 0xFF0000FF;
    im.tocar();
}
Trazo trazoEn(double x, double y) {
    Trazo t;
    t.completo.push_back({x, y, 0.5});
    return t;
}
// Simula lo que hace la interfaz: pinta y luego registra
void dibujar(Documento &d, double x, double y) {
    Capa &c = d.capas[std::size_t(d.activa)];
    const Imagen antes = c.img;
    Trazo t = trazoEn(x, y);
    pintorPrueba(c.img, t);
    d.registrarTrazo(c.id, std::move(t), antes);
}
Documento nuevoDoc() {
    Documento d(10, 10);
    d.pintor = pintorPrueba;
    return d;
}
}  // namespace

PRUEBA(documento_inicial) {
    Documento d(10, 10);
    VERIFICAR(d.numCapas() == 1);
    VERIFICAR(d.activa == 0);
    VERIFICAR(!d.modificado());
}

PRUEBA(deshacer_trazo_restaura_pixeles) {
    Documento d = nuevoDoc();
    dibujar(d, 1, 1);
    dibujar(d, 2, 2);
    VERIFICAR(d.capas[0].img.en(2, 2) == 0xFF0000FF);
    VERIFICAR(d.deshacer());
    VERIFICAR(d.capas[0].img.en(2, 2) == 0);
    VERIFICAR(d.capas[0].img.en(1, 1) == 0xFF0000FF);
    VERIFICAR(d.rehacer());
    VERIFICAR(d.capas[0].img.en(2, 2) == 0xFF0000FF);
    VERIFICAR(d.capas[0].ops.size() == 2);
}

PRUEBA(nueva_capa_deshacer_rehacer) {
    Documento d = nuevoDoc();
    d.nuevaCapa();
    VERIFICAR(d.numCapas() == 2 && d.activa == 1);
    VERIFICAR(d.deshacer());
    VERIFICAR(d.numCapas() == 1 && d.activa == 0);
    VERIFICAR(d.rehacer());
    VERIFICAR(d.numCapas() == 2 && d.activa == 1);
}

PRUEBA(borrar_capa_se_puede_deshacer_con_contenido) {
    Documento d = nuevoDoc();
    dibujar(d, 3, 3);
    d.nuevaCapa();
    dibujar(d, 5, 5);
    const int id = d.capas[1].id;
    VERIFICAR(d.borrarCapa(1));
    VERIFICAR(d.numCapas() == 1 && d.activa == 0);
    VERIFICAR(d.deshacer());
    VERIFICAR(d.numCapas() == 2 && d.activa == 1);
    VERIFICAR(d.capas[1].id == id);
    VERIFICAR(d.capas[1].img.en(5, 5) == 0xFF0000FF);
    VERIFICAR(d.capas[1].ops.size() == 1);
}

PRUEBA(deshacer_trazo_tras_borrar_y_restaurar_capa) {
    Documento d = nuevoDoc();
    d.nuevaCapa();
    dibujar(d, 4, 4);
    d.borrarCapa(1);
    d.deshacer();          // vuelve la capa
    d.deshacer();          // deshace el trazo
    VERIFICAR(d.capas[1].ops.empty());
    VERIFICAR(d.capas[1].img.en(4, 4) == 0);
}

PRUEBA(no_se_borra_la_ultima_capa) {
    Documento d = nuevoDoc();
    VERIFICAR(!d.borrarCapa(0));
    VERIFICAR(d.numCapas() == 1);
    VERIFICAR(!d.historial.puedeDeshacer());
}

PRUEBA(borrar_capa_inferior_mantiene_la_activa) {
    Documento d = nuevoDoc();
    d.nuevaCapa(); d.nuevaCapa();             // 3 capas, activa = 2
    const int idActiva = d.capas[2].id;
    d.borrarCapa(0);
    VERIFICAR(d.capas[std::size_t(d.activa)].id == idActiva);
    d.deshacer();
    VERIFICAR(d.capas[std::size_t(d.activa)].id == idActiva);
}

PRUEBA(mover_capa_sigue_a_la_activa) {
    Documento d = nuevoDoc();
    d.nuevaCapa();
    const int idArriba = d.capas[1].id;
    VERIFICAR(d.moverCapa(1, -1));
    VERIFICAR(d.capas[0].id == idArriba && d.activa == 0);
    VERIFICAR(!d.moverCapa(0, -1));
    d.deshacer();
    VERIFICAR(d.capas[1].id == idArriba && d.activa == 1);
}

PRUEBA(propiedades_se_fusionan_en_un_paso) {
    Documento d = nuevoDoc();
    PropCapa p{d.capas[0].nombre, false, 1.0};
    for (double o : {0.9, 0.7, 0.5, 0.3}) { p.opacidad = o; d.cambiarPropiedades(0, p, "opacidad"); }
    VERIFICAR(d.capas[0].opacidad == 0.3);
    VERIFICAR(d.historial.pasos() == 1);
    d.deshacer();
    VERIFICAR(d.capas[0].opacidad == 1.0);
    d.rehacer();
    VERIFICAR(d.capas[0].opacidad == 0.3);
}

PRUEBA(propiedades_distintas_no_se_fusionan) {
    Documento d = nuevoDoc();
    PropCapa p{d.capas[0].nombre, false, 0.5};
    d.cambiarPropiedades(0, p, "opacidad");
    p.bloqueada = true;
    d.cambiarPropiedades(0, p, "bloqueo");
    VERIFICAR(d.historial.pasos() == 2);
    d.deshacer();
    VERIFICAR(!d.capas[0].bloqueada && d.capas[0].opacidad == 0.5);
}

PRUEBA(sin_cambios_no_crea_paso) {
    Documento d = nuevoDoc();
    PropCapa p{d.capas[0].nombre, false, 1.0};
    VERIFICAR(!d.cambiarPropiedades(0, p, "opacidad"));
    VERIFICAR(d.historial.pasos() == 0);
}

PRUEBA(visibilidad_no_va_al_historial_pero_ensucia) {
    Documento d = nuevoDoc();
    d.setVisible(0, false);
    VERIFICAR(!d.capas[0].visible);
    VERIFICAR(d.historial.pasos() == 0);
    VERIFICAR(d.modificado());
    d.marcarGuardado();
    VERIFICAR(!d.modificado());
}

PRUEBA(marca_de_guardado) {
    Documento d = nuevoDoc();
    dibujar(d, 1, 1);
    VERIFICAR(d.modificado());
    d.marcarGuardado();
    VERIFICAR(!d.modificado());
    d.deshacer();
    VERIFICAR(d.modificado());
    d.rehacer();
    VERIFICAR(!d.modificado());
    d.deshacer();
    dibujar(d, 2, 2);          // el estado guardado ya no es alcanzable
    VERIFICAR(d.modificado());
    d.deshacer();
    VERIFICAR(d.modificado());
}

PRUEBA(fusionar_sobre_paso_guardado_ensucia) {
    Documento d = nuevoDoc();
    PropCapa p{d.capas[0].nombre, false, 0.8};
    d.cambiarPropiedades(0, p, "opacidad");
    d.marcarGuardado();
    p.opacidad = 0.6;
    d.cambiarPropiedades(0, p, "opacidad");
    VERIFICAR(d.modificado());
}

PRUEBA(limite_de_historial) {
    Documento d = nuevoDoc();
    d.historial.setLimite(3);
    for (int i = 0; i < 6; ++i) dibujar(d, i, 0);
    VERIFICAR(d.historial.pasos() == 3);
    d.marcarGuardado();
    VERIFICAR(!d.modificado());
}

PRUEBA(plantilla_sobrevive_al_deshacer) {
    Documento d = nuevoDoc();
    Capa plantilla(d.siguienteId++, "Plantilla", 10, 10);
    plantilla.esPlantilla = true;
    plantilla.img.en(0, 0) = 0xFF00FF00;
    plantilla.base = plantilla.img;
    d.insertarPlantilla(std::move(plantilla));
    VERIFICAR(d.activa == 1 && d.capas[0].esPlantilla);
    d.capas[0].bloqueada = false;
    d.activa = 0;
    dibujar(d, 5, 5);
    d.deshacer();
    VERIFICAR(d.capas[0].img.en(0, 0) == 0xFF00FF00);   // antes se perdía
    VERIFICAR(d.capas[0].img.en(5, 5) == 0);
}

PRUEBA(componer_sobre_blanco_con_opacidad) {
    Documento d(2, 1);
    d.capas[0].img.en(0, 0) = 0xFF000000;               // negro opaco
    d.capas[0].opacidad = 0.5;
    Imagen r = d.componer(true);
    const unsigned g = (r.en(0, 0) >> 8) & 255;
    VERIFICAR(g >= 126 && g <= 129);
    VERIFICAR(r.en(1, 0) == BLANCO);
    VERIFICAR((r.en(0, 0) >> 24) == 255);
}

PRUEBA(componer_omite_ocultas_y_plantilla) {
    Documento d(1, 1);
    d.capas[0].img.en(0, 0) = 0xFF000000;
    d.capas[0].esPlantilla = true;
    VERIFICAR(d.componer(false).en(0, 0) == BLANCO);
    VERIFICAR(d.componer(true).en(0, 0) == 0xFF000000);
    d.setVisible(0, false);
    VERIFICAR(d.componer(true).en(0, 0) == BLANCO);
}

PRUEBA(color_en_punto) {
    Documento d(2, 2);
    d.capas[0].img.en(1, 1) = 0xFFFF0000;
    VERIFICAR(d.colorEn(1, 1) == 0xFFFF0000);
    VERIFICAR(d.colorEn(0, 0) == BLANCO);
    VERIFICAR(d.colorEn(-1, 0) == BLANCO);
}

PRUEBA(contorno_de_trazo) {
    Trazo t;
    for (int i = 0; i < 20; ++i) t.puntos.push_back({10.0 + i * 3, 10.0 + i, 0.5});
    t.simular = true;
    VERIFICAR(calcularContorno(t, true).size() >= 3);
}

PRUEBA(cargar_reemplaza_y_limpia_historial) {
    Documento d = nuevoDoc();
    dibujar(d, 1, 1);
    std::vector<Capa> cs;
    cs.push_back(Capa(7, "A", 4, 4));
    cs.push_back(Capa(8, "B", 4, 4));
    d.cargar(4, 4, std::move(cs), 9);
    VERIFICAR(d.numCapas() == 2 && d.activa == 1 && d.ancho == 4);
    VERIFICAR(!d.modificado() && !d.historial.puedeDeshacer());
    d.nuevaCapa();
    VERIFICAR(d.capas[2].id == 9);
}

PRUEBA(forma_linea_rect_elipse) {
    auto caja = [](const std::vector<Contorno> &cs) {
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (auto &c : cs) for (auto &v : c) { x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); }
        return std::array<double, 4>{x0, y0, x1, y1};
    };
    Trazo t; t.grosor = 10; t.completo = {{10, 10, .5}, {100, 10, .5}};
    t.forma = FORMA_LINEA;
    auto b = caja(contornosDeForma(t));
    VERIFICAR(b[0] < 10 && b[0] > 0 && b[2] > 100 && b[2] < 110 && b[1] < 10 && b[3] > 10);
    t.forma = FORMA_RECT; t.relleno = true; t.completo = {{20, 30, .5}, {80, 90, .5}};
    b = caja(contornosDeForma(t));
    VERIFICAR(b[0] == 20 && b[1] == 30 && b[2] == 80 && b[3] == 90);
    t.forma = FORMA_ELIPSE;
    b = caja(contornosDeForma(t));
    VERIFICAR(std::fabs(b[0] - 20) < 0.5 && std::fabs(b[2] - 80) < 0.5);
    t.completo = {{5, 5, .5}, {5, 5, .5}};
    VERIFICAR(contornosDeForma(t).empty());                         // sin tamaño no hay forma
}

PRUEBA(forma_shift_restringe) {
    Punto a{0, 0, .5};
    Punto l = restringirForma(FORMA_LINEA, a, {100, 3, .5});
    VERIFICAR(std::fabs(l.y) < 1e-9 && l.x > 99);                   // casi horizontal -> horizontal
    Punto d = restringirForma(FORMA_LINEA, a, {50, 52, .5});
    VERIFICAR(std::fabs(d.x - d.y) < 1e-6);                         // casi diagonal -> 45°
    Punto r = restringirForma(FORMA_RECT, a, {-40, 90, .5});
    VERIFICAR(r.x == -90 && r.y == 90);                             // cuadrado, respetando el sentido
}

PRUEBA(revision_sube_con_cada_cambio_y_nunca_baja) {
    Documento d(10, 10);
    std::uint64_t r = d.revision();
    auto sube = [&]() { const bool ok = d.revision() > r; r = d.revision(); return ok; };
    VERIFICAR(d.pegar(0, Imagen(2, 2, 0xFFFF0000), 1, 1));
    VERIFICAR(sube());
    VERIFICAR(d.deshacer());
    VERIFICAR(sube());                       // deshacer también cambia el dibujo
    VERIFICAR(d.rehacer());
    VERIFICAR(sube());
    d.setVisible(0, false);                  // no entra en el historial, pero cuenta
    VERIFICAR(sube());
    d.marcarModificado();
    VERIFICAR(sube() && d.modificado());
    d.marcarGuardado();
    VERIFICAR(!d.modificado() && d.revision() == r);   // guardar no cambia el dibujo
}
