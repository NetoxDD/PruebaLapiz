#pragma once
// Selección (rectángulo o lazo) como máscara de pixeles. Sin Qt.
#include <cstdint>
#include <vector>
#include "imagen.h"
#include "relleno.h"
#include "trazo.h"

namespace plz {

struct Seleccion {
    int w = 0, h = 0;                // tamaño del lienzo
    std::vector<std::uint8_t> m;     // 1 = seleccionado
    Rect caja;                       // caja ajustada de lo seleccionado; vacía = no hay selección

    bool vacia() const { return caja.vacio(); }
    bool contiene(int x, int y) const {
        return x >= 0 && y >= 0 && x < w && y < h && !m.empty() && m[std::size_t(y) * std::size_t(w) + std::size_t(x)];
    }
    static Seleccion rectangulo(int w, int h, Rect r);
    static Seleccion poligono(int w, int h, const Contorno &pts);   // relleno par-impar (lazo)
    std::vector<Span> tramos() const;                               // la máscara como tramos por fila
};

// Deja de un relleno solo lo que cae dentro de la selección
Relleno recortarRelleno(const Relleno &r, const Seleccion &s);

}  // namespace plz
