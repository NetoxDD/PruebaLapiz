#pragma once
// Historial de deshacer/rehacer basado en comandos. Sin Qt.
#include <cstddef>
#include <memory>
#include <vector>

namespace plz {

class Documento;

class Comando {
public:
    virtual ~Comando() = default;
    virtual void aplicar(Documento &) = 0;
    virtual void deshacer(Documento &) = 0;
    // Permite absorber un comando posterior del mismo tipo (p. ej. arrastrar un slider = un solo paso).
    virtual bool fusionar(const Comando &) { return false; }
    // Memoria que retiene (para limitar el historial)
    virtual std::size_t bytes() const { return 0; }
};

class Historial {
    std::vector<std::unique_ptr<Comando>> hechos, deshechos;
    long marca = 0;              // nº de pasos hechos al guardar; -1 = ese estado ya no se puede alcanzar
    std::size_t limite_ = 500;
    std::size_t bytesHechos = 0;
    std::size_t presupuesto_ = std::size_t(512) * 1024 * 1024;   // memoria máxima de los pasos guardados
    void recortarAntiguos();

public:
    void ejecutar(Documento &d, std::unique_ptr<Comando> c);   // aplica y registra
    void registrar(std::unique_ptr<Comando> c);                // registra algo que ya se aplicó
    bool deshacer(Documento &d);
    bool rehacer(Documento &d);

    bool puedeDeshacer() const { return !hechos.empty(); }
    bool puedeRehacer() const { return !deshechos.empty(); }
    std::size_t pasos() const { return hechos.size(); }

    void marcarGuardado() { marca = long(hechos.size()); }
    bool modificado() const { return marca != long(hechos.size()); }
    void limpiar() { hechos.clear(); deshechos.clear(); marca = 0; bytesHechos = 0; }
    void setLimite(std::size_t n) { limite_ = n ? n : 1; }
    void setPresupuesto(std::size_t bytes) { presupuesto_ = bytes; }
    std::size_t bytesUsados() const { return bytesHechos; }
};

}  // namespace plz
