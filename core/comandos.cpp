#include "comandos.h"
#include <utility>

namespace plz {

// ---------- Trazo / relleno ----------
void CmdOperacion::aplicar(Documento &d) {
    Capa *c = d.porId(capaId);
    if (!c) return;
    c->img.pegar(despues, zona.x0, zona.y0);
    c->ops.push_back(std::move(op));
}
void CmdOperacion::deshacer(Documento &d) {
    Capa *c = d.porId(capaId);
    if (!c || c->ops.empty()) return;
    c->img.pegar(antes, zona.x0, zona.y0);
    op = std::move(c->ops.back());
    c->ops.pop_back();
}

// ---------- Crear / borrar capa ----------
void CmdCapa::insertar(Documento &d) {
    d.capas.insert(d.capas.begin() + indice, std::move(capa));
}
void CmdCapa::quitar(Documento &d) {
    capa = std::move(d.capas[std::size_t(indice)]);
    d.capas.erase(d.capas.begin() + indice);
}
void CmdCapa::aplicar(Documento &d) {
    if (crear) insertar(d); else quitar(d);
    d.activa = activaDespues;
}
void CmdCapa::deshacer(Documento &d) {
    if (crear) quitar(d); else insertar(d);
    d.activa = activaAntes;
}

// ---------- Mover capa ----------
void CmdMoverCapa::aplicar(Documento &d) {
    std::swap(d.capas[std::size_t(i)], d.capas[std::size_t(j)]);
    d.activa = activaDespues;
}
void CmdMoverCapa::deshacer(Documento &d) {
    std::swap(d.capas[std::size_t(i)], d.capas[std::size_t(j)]);
    d.activa = activaAntes;
}

// ---------- Propiedades ----------
void CmdPropiedades::poner(Documento &d, const PropCapa &p) {
    Capa *c = d.porId(capaId);
    if (!c) return;
    c->nombre = p.nombre;
    c->bloqueada = p.bloqueada;
    c->opacidad = p.opacidad;
}
bool CmdPropiedades::fusionar(const Comando &otro) {
    const auto *o = dynamic_cast<const CmdPropiedades *>(&otro);
    if (!o || o->capaId != capaId || o->clave != clave) return false;
    despues = o->despues;
    return true;
}

}  // namespace plz
