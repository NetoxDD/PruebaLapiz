#pragma once
// Pinceles de estampado: repiten un sello a lo largo del trazo. Sin Qt.
// Es "en flujo": se alimenta punto a punto y siempre da lo mismo que repetir el trazo entero, así se puede
// dibujar en vivo y luego rehacer, abrir o reconstruir el trazo desde sus puntos.
#include "imagen.h"
#include "seleccion.h"
#include "trazo.h"

namespace plz {

class Estampador {
public:
    Estampador(Imagen &img, const Trazo &t);       // copia los parámetros del trazo (no sus puntos)
    void punto(const Punto &p);                    // un punto más del recorrido
    void terminar();                               // fin del trazo (un simple toque deja un sello)

private:
    Imagen &img_;
    Estampa e_;
    std::uint32_t rgb_ = 0;                        // color sin alfa
    double alfaBase_ = 1;                          // alfa del color elegido
    double grosor_ = 16;
    std::uint32_t semilla_ = 1;
    Seleccion clip_;
    bool hayClip_ = false;
    bool hayPrevio_ = false, sellado_ = false, hayDir_ = false;
    Punto prev_;
    double resto_ = 0;                             // distancia hasta el siguiente sello
    double dirX_ = 1, dirY_ = 0;

    double escalaPresion(double p) const;
    double paso(double presion) const;
    void sello(double x, double y, double presion);
};

// Pinta el trazo completo (sobre lo que ya haya en la imagen)
void pintarEstampado(Imagen &img, const Trazo &t);

}  // namespace plz
