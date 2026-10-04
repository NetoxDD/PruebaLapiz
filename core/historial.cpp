#include "historial.h"
#include <algorithm>

namespace plz {

void Historial::ejecutar(Documento &d, std::unique_ptr<Comando> c) {
    c->aplicar(d);
    registrar(std::move(c));
}

void Historial::registrar(std::unique_ptr<Comando> c) {
    ++revision_;
    deshechos.clear();
    if (marca > long(hechos.size())) marca = -1;       // el estado guardado estaba en lo rehacible
    if (!hechos.empty() && hechos.back()->fusionar(*c)) {
        if (marca == long(hechos.size())) marca = -1;  // se modificó un paso ya guardado
        return;
    }
    bytesHechos += c->bytes();
    hechos.push_back(std::move(c));
    recortarAntiguos();
}

// Olvida los pasos más viejos si hay demasiados o ocupan demasiada memoria (siempre queda el último)
void Historial::recortarAntiguos() {
    while (hechos.size() > 1 && (hechos.size() > limite_ || bytesHechos > presupuesto_)) {
        bytesHechos -= std::min(bytesHechos, hechos.front()->bytes());
        hechos.erase(hechos.begin());
        marca = marca > 0 ? marca - 1 : -1;
    }
}

bool Historial::deshacer(Documento &d) {
    if (hechos.empty()) return false;
    std::unique_ptr<Comando> c = std::move(hechos.back());
    hechos.pop_back();
    bytesHechos -= std::min(bytesHechos, c->bytes());
    c->deshacer(d);
    deshechos.push_back(std::move(c));
    ++revision_;
    return true;
}

bool Historial::rehacer(Documento &d) {
    if (deshechos.empty()) return false;
    std::unique_ptr<Comando> c = std::move(deshechos.back());
    deshechos.pop_back();
    c->aplicar(d);
    bytesHechos += c->bytes();
    hechos.push_back(std::move(c));
    ++revision_;
    return true;
}

}  // namespace plz
