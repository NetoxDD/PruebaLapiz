#pragma once
// Mover, escalar y rotar lo seleccionado. Sin Qt.
#include <cmath>
#include <vector>
#include "imagen.h"
#include "relleno.h"
#include "trazo.h"

namespace plz {

// Transformación afín del plano:  x' = a·x + c·y + tx ;  y' = b·x + d·y + ty
struct Afin {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    pf::Vec aplicar(double x, double y) const { return {a * x + c * y + tx, b * x + d * y + ty}; }
    double det() const { return a * d - b * c; }
    bool invertible() const { return std::fabs(det()) > 1e-12; }
    bool esIdentidad(double eps = 1e-9) const {
        return std::fabs(a - 1) < eps && std::fabs(b) < eps && std::fabs(c) < eps &&
               std::fabs(d - 1) < eps && std::fabs(tx) < eps && std::fabs(ty) < eps;
    }
    Afin inversa() const {
        const double k = 1.0 / det();
        Afin r;
        r.a = d * k; r.c = -c * k; r.b = -b * k; r.d = a * k;
        r.tx = -(r.a * tx + r.c * ty);
        r.ty = -(r.b * tx + r.d * ty);
        return r;
    }
    // Lleva el punto (ox, oy) a (dx, dy) escalando (sx, sy) y girando 'ang' radianes alrededor de él.
    static Afin desde(double ox, double oy, double sx, double sy, double ang, double dx, double dy) {
        const double co = std::cos(ang), si = std::sin(ang);
        Afin r;
        r.a = co * sx; r.b = si * sx; r.c = -si * sy; r.d = co * sy;
        r.tx = dx - (r.a * ox + r.c * oy);
        r.ty = dy - (r.b * ox + r.d * oy);
        return r;
    }
};

// Lo que se guarda en la capa: qué pixeles se levantaron (como tramos, igual que un relleno)
// y cómo se colocaron. Repetirla sobre la misma capa da siempre el mismo resultado.
struct Transformacion {
    std::vector<Span> origen;
    Afin m;
    bool suave = true;     // true: interpolación bilineal; false: pixel más cercano (pixel art)
};

Rect rectDeSpans(const std::vector<Span> &v);
// Zona de la capa que cambia (origen + destino), recortada al lienzo
Rect rectDeTransformacion(const Transformacion &t, int ancho, int alto);
// Levanta los pixeles de 'origen', deja ese sitio transparente y los pone transformados encima de lo que haya
void aplicarTransformacion(Imagen &img, const Transformacion &t);
Contorno transformarContorno(const Contorno &c, const Afin &m);

}  // namespace plz
