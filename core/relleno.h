#pragma once
// Cubo de relleno: calcula la zona a rellenar y la aplica. Sin Qt.
#include <cstdint>
#include <vector>
#include "imagen.h"

namespace plz {

struct Span { int y, x0, x1; };   // fila y, columnas x0..x1 (ambas incluidas)

// Resultado de un relleno. Se guarda como máscara (no como orden "rellenar aquí"), así rehacerlo
// o abrir el archivo da siempre lo mismo aunque las demás capas hayan cambiado.
struct Relleno {
    std::uint32_t color = 0xFF000000;   // ARGB sin premultiplicar
    std::vector<Span> nucleo;           // pixeles parecidos a la semilla: se reemplazan por el color
    std::vector<Span> borde;            // franja extra: el color queda DEBAJO (cubre el halo del antialiasing)
};

struct OpcionesRelleno {
    int tolerancia = 32;   // 0..255: diferencia máxima por canal (RGBA premultiplicado) respecto a la semilla
    int expandir = 1;      // pixeles que se ensancha la zona bajo los bordes de las líneas
};

// 'muestra' es la imagen donde se mira qué es "parecido" (una capa, o todas las capas combinadas)
Relleno calcularRelleno(const Imagen &muestra, int sx, int sy, const OpcionesRelleno &o, std::uint32_t color);
void aplicarRelleno(Imagen &img, const Relleno &r);
Rect rectDeRelleno(const Relleno &r);

}  // namespace plz
