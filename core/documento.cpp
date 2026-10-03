#include "documento.h"
#include <algorithm>
#include "comandos.h"
#include "mezcla.h"

namespace plz {

void Documento::reiniciar(int w, int h) {
    ancho = w; alto = h;
    capas.clear();
    siguienteId = 1;
    capas.push_back(Capa(siguienteId, "Capa 1", w, h));
    ++siguienteId;
    activa = 0;
    historial.limpiar();
    sucioExtra = false;
}

void Documento::cargar(int w, int h, std::vector<Capa> nuevas, int siguiente) {
    ancho = w; alto = h;
    capas = std::move(nuevas);
    siguienteId = siguiente;
    if (capas.empty()) {
        capas.push_back(Capa(siguienteId, "Capa 1", w, h));
        ++siguienteId;
    }
    activa = numCapas() - 1;
    historial.limpiar();
    sucioExtra = false;
}

Capa *Documento::porId(int id) {
    for (Capa &c : capas) if (c.id == id) return &c;
    return nullptr;
}
const Capa *Documento::porId(int id) const {
    for (const Capa &c : capas) if (c.id == id) return &c;
    return nullptr;
}

// ---------- Acciones con historial ----------
void Documento::nuevaCapa() {
    const int id = siguienteId++;
    const int indice = activa + 1;
    historial.ejecutar(*this, std::make_unique<CmdCapa>(
        true, indice, Capa(id, "Capa " + std::to_string(id), ancho, alto), activa, indice));
}

bool Documento::borrarCapa(int i) {
    if (numCapas() <= 1 || !indiceValido(i)) return false;
    int despues = activa;
    if (i < activa) despues = activa - 1;
    else despues = std::min(activa, numCapas() - 2);
    historial.ejecutar(*this, std::make_unique<CmdCapa>(false, i, Capa(), activa, despues));
    return true;
}

bool Documento::moverCapa(int i, int delta) {
    const int j = i + delta;
    if (!indiceValido(i) || !indiceValido(j) || delta == 0) return false;
    int despues = activa;
    if (activa == i) despues = j; else if (activa == j) despues = i;
    historial.ejecutar(*this, std::make_unique<CmdMoverCapa>(i, j, activa, despues));
    return true;
}

void Documento::insertarPlantilla(Capa c) {
    historial.ejecutar(*this, std::make_unique<CmdCapa>(true, 0, std::move(c), activa, activa + 1));
}

bool Documento::cambiarPropiedades(int i, const PropCapa &nuevas, const char *clave) {
    if (!indiceValido(i)) return false;
    const Capa &c = capas[std::size_t(i)];
    const PropCapa antes{c.nombre, c.bloqueada, c.opacidad};
    PropCapa p = nuevas;
    p.opacidad = std::clamp(p.opacidad, 0.0, 1.0);
    if (p == antes) return false;
    historial.ejecutar(*this, std::make_unique<CmdPropiedades>(c.id, antes, p, clave));
    return true;
}

void Documento::registrarTrazo(int capaId, Trazo t, const Imagen &antes) {
    Capa *c = porId(capaId);
    if (!c) return;
    const Rect zona = rectDeTrazo(t, ancho, alto);
    auto cmd = std::make_unique<CmdOperacion>(capaId, zona, antes.recortar(zona), c->img.recortar(zona));
    c->ops.push_back(std::move(t));
    historial.registrar(std::move(cmd));
}

bool Documento::rellenar(int i, int x, int y, std::uint32_t color, const OpcionesRelleno &o, bool todasLasCapas,
                         const Seleccion *sel) {
    if (!indiceValido(i)) return false;
    Capa &c = capas[std::size_t(i)];
    Imagen combinada;
    if (todasLasCapas) combinada = componer(true);
    const Imagen &muestra = todasLasCapas ? combinada : c.img;
    if (sel && !sel->contiene(x, y)) return false;
    Relleno r = calcularRelleno(muestra, x, y, o, color);
    if (sel) r = recortarRelleno(r, *sel);
    if (r.nucleo.empty()) return false;
    const Rect zona = rectDeRelleno(r);
    Imagen antes = c.img.recortar(zona);
    aplicarRelleno(c.img, r);
    auto cmd = std::make_unique<CmdOperacion>(c.id, zona, std::move(antes), c.img.recortar(zona));
    c.ops.push_back(std::move(r));
    historial.registrar(std::move(cmd));
    return true;
}

bool Documento::borrarSeleccion(int i, const Seleccion &s) {
    if (!indiceValido(i) || s.vacia()) return false;
    Capa &c = capas[std::size_t(i)];
    Relleno r;
    r.borrar = true;
    r.nucleo = s.tramos();
    const Rect zona = s.caja;
    Imagen antes = c.img.recortar(zona);
    aplicarRelleno(c.img, r);
    auto cmd = std::make_unique<CmdOperacion>(c.id, zona, std::move(antes), c.img.recortar(zona));
    c.ops.push_back(std::move(r));
    historial.registrar(std::move(cmd));
    return true;
}

Imagen Documento::copiarSeleccion(int i, const Seleccion &s) const {
    if (!indiceValido(i) || s.vacia()) return Imagen();
    Imagen out = capas[std::size_t(i)].img.recortar(s.caja);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            if (!s.contiene(s.caja.x0 + x, s.caja.y0 + y)) out.en(x, y) = TRANSPARENTE;
    return out;
}

bool Documento::pegar(int i, Imagen img, int x, int y) {
    if (!indiceValido(i) || img.vacia()) return false;
    Capa &c = capas[std::size_t(i)];
    const Rect zona = Rect{x, y, x + img.w, y + img.h}.interseccion(c.img.rectTotal());
    if (zona.vacio()) return false;
    Imagen antes = c.img.recortar(zona);
    Pegado p{x, y, std::move(img)};
    aplicarPegado(c.img, p);
    auto cmd = std::make_unique<CmdOperacion>(c.id, zona, std::move(antes), c.img.recortar(zona));
    c.ops.push_back(std::move(p));
    historial.registrar(std::move(cmd));
    return true;
}

void aplicarPegado(Imagen &dst, const Pegado &p) {
    const Rect zona = Rect{p.x, p.y, p.x + p.img.w, p.y + p.img.h}.interseccion(dst.rectTotal());
    for (int yy = zona.y0; yy < zona.y1; ++yy)
        for (int xx = zona.x0; xx < zona.x1; ++xx) {
            const Pixel s = p.img.en(xx - p.x, yy - p.y);
            if (s) dst.en(xx, yy) = sobre(dst.en(xx, yy), s);
        }
    dst.tocar(zona);
}

// ---------- Sin historial ----------
void Documento::setVisible(int i, bool v) {
    if (!indiceValido(i) || capas[std::size_t(i)].visible == v) return;
    capas[std::size_t(i)].visible = v;
    sucioExtra = true;
}

// ---------- Pixeles ----------
void Documento::reconstruir(Capa &c) {
    if (!c.base.vacia()) c.img.copiarDe(c.base); else c.img.llenar(TRANSPARENTE);
    for (const Operacion &op : c.ops) {
        if (const Trazo *t = std::get_if<Trazo>(&op)) { if (pintor) pintor(c.img, *t); }
        else if (const Relleno *r = std::get_if<Relleno>(&op)) aplicarRelleno(c.img, *r);
        else aplicarPegado(c.img, std::get<Pegado>(op));
    }
    c.img.tocar();
}

Imagen Documento::componer(bool conPlantilla) const {
    Imagen out(ancho, alto, BLANCO);
    for (const Capa &c : capas) {
        if (!c.visible) continue;
        if (c.esPlantilla && !conPlantilla) continue;
        mezclar(out, c.img, c.opacidad);
    }
    return out;
}

Pixel Documento::colorEn(int x, int y) const {
    if (x < 0 || y < 0 || x >= ancho || y >= alto) return BLANCO;
    Pixel p = BLANCO;
    for (const Capa &c : capas) {
        if (!c.visible || !c.img.dentro(x, y)) continue;
        p = sobre(p, escalar(c.img.en(x, y), opacidadA255(c.opacidad)));
    }
    return p;
}

std::size_t Documento::totalOperaciones() const {
    std::size_t n = 0;
    for (const Capa &c : capas) n += c.ops.size();
    return n;
}

}  // namespace plz
