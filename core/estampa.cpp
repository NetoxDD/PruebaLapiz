#include "estampa.h"
#include <algorithm>
#include <cmath>
#include "mezcla.h"

namespace plz {

namespace {

constexpr double PI = 3.14159265358979323846;

// Mezcla de bits (finalizador de MurmurHash3): de aquí sale todo el azar
inline std::uint32_t mezcla(std::uint32_t h) {
    h ^= h >> 16; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}
inline std::uint32_t hash(std::uint32_t semilla, int a, int b, std::uint32_t k) {
    return mezcla(semilla ^ mezcla(std::uint32_t(a) * 0x9E3779B1u + mezcla(std::uint32_t(b) * 0x85EBCA77u + k)));
}
inline double u01(std::uint32_t h) { return double(h >> 8) / 16777216.0; }   // [0, 1)

inline double acotar(double v) { return std::clamp(v, 0.0, 1.0); }

// Opacidad del sello (0..1) en el punto (u, v) de su espacio: el sello ocupa el disco unidad.
// 'R' es su radio en pixeles, para que el borde quede suave sin importar el tamaño.
double mascara(int punta, double u, double v, double R) {
    const double r = std::hypot(u, v);
    switch (punta) {
    case PUNTA_REDONDA:
        return acotar((1 - r) * R + 0.5);
    case PUNTA_ESTRELLA: {                           // cinco puntas
        const double k = 1 - std::fabs(std::sin(2.5 * std::atan2(v, u)));   // 1 en cada punta, 0 en cada valle
        return acotar((0.42 + 0.58 * k - r) * R + 0.5);
    }
    case PUNTA_HOJA: {                               // dos parábolas que se juntan en las puntas
        if (std::fabs(u) >= 1) return 0;
        return acotar((0.5 * (1 - u * u) - std::fabs(v)) * R + 0.5);
    }
    case PUNTA_SUAVE:
    default: {
        const double s = acotar(1 - r);
        return s * s * (3 - 2 * s);
    }
    }
}

}  // namespace

Estampador::Estampador(Imagen &img, const Trazo &t)
    : img_(img), e_(t.est), rgb_(t.color & 0x00FFFFFFu), alfaBase_((t.color >> 24) / 255.0),
      grosor_(t.grosor), semilla_(t.semilla) {
    if (!t.recorte.empty()) {
        clip_ = Seleccion::poligono(img.w, img.h, t.recorte);
        hayClip_ = true;
    }
}

double Estampador::escalaPresion(double p) const {
    const double efecto = acotar(e_.presion);
    return std::max(0.05, 1 - efecto + efecto * std::min(1.0, 2 * acotar(p)));   // con presión 0.5 (ratón) = tamaño pleno
}

double Estampador::paso(double presion) const {
    return std::max(1.0, e_.espaciado * grosor_ * escalaPresion(presion));
}

void Estampador::punto(const Punto &p) {
    if (!hayPrevio_) {
        prev_ = p;
        hayPrevio_ = true;
        return;                                      // el primer sello espera a conocer la dirección
    }
    const double dx = p.x - prev_.x, dy = p.y - prev_.y;
    const double len = std::hypot(dx, dy);
    if (len < 1e-9) return;
    // dirección suavizada: un ratón da recorridos con mucho temblor
    const double nx = dx / len, ny = dy / len;
    if (hayDir_) {
        const double sx = 0.6 * dirX_ + 0.4 * nx, sy = 0.6 * dirY_ + 0.4 * ny;
        const double sl = std::hypot(sx, sy);
        if (sl > 1e-9) { dirX_ = sx / sl; dirY_ = sy / sl; }
    } else {
        dirX_ = nx; dirY_ = ny; hayDir_ = true;
    }
    double d = resto_;
    while (d <= len) {
        const double t = d / len;
        const double pres = prev_.presion + (p.presion - prev_.presion) * t;
        sello(prev_.x + dx * t, prev_.y + dy * t, pres);
        d += paso(pres);
    }
    resto_ = d - len;
    prev_ = p;
}

void Estampador::terminar() {
    if (hayPrevio_ && !sellado_) sello(prev_.x, prev_.y, prev_.presion);
}

void Estampador::sello(double x, double y, double presion) {
    sellado_ = true;
    // el azar depende de la posición (no del número de sello): si al reabrir el archivo cambia un sello de más
    // o de menos, el resto no se desplaza
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double r1 = u01(hash(semilla_, ix, iy, 1)), r2 = u01(hash(semilla_, ix, iy, 2)),
                 r3 = u01(hash(semilla_, ix, iy, 3)), r4 = u01(hash(semilla_, ix, iy, 4));

    const double tam = grosor_ * escalaPresion(presion) * (1 - acotar(e_.variaTam) * r1);
    const double R = tam / 2;
    if (R < 0.25) return;
    const double ang = e_.rotacion * PI / 180.0 + (e_.sigueTrazo ? std::atan2(dirY_, dirX_) : 0.0) +
                       e_.variaRot * PI / 180.0 * (2 * r2 - 1);
    const double cx = x + (2 * r3 - 1) * e_.dispersion * grosor_;
    const double cy = y + (2 * r4 - 1) * e_.dispersion * grosor_;
    const double co = std::cos(ang), si = std::sin(ang);

    const double b = R * 1.4143 + 1.5;
    const Rect zona = Rect{int(std::floor(cx - b)), int(std::floor(cy - b)), int(std::ceil(cx + b)), int(std::ceil(cy + b))}
                          .interseccion(img_.rectTotal());
    if (zona.vacio()) return;
    const double flujo = acotar(e_.flujo) * alfaBase_;
    const double grano = acotar(e_.grano);
    for (int py = zona.y0; py < zona.y1; ++py)
        for (int px = zona.x0; px < zona.x1; ++px) {
            const double dx = px + 0.5 - cx, dy = py + 0.5 - cy;
            double a = mascara(e_.punta, (co * dx + si * dy) / R, (-si * dx + co * dy) / R, R);
            if (a <= 0) continue;
            if (grano > 0)                           // el grano es del papel (fijo en el lienzo), no del sello
                a *= acotar((u01(hash(semilla_, px >> 1, py >> 1, 99)) - grano) * 8 + 0.5);   // granos de 2x2 px
            const unsigned a8 = unsigned(std::lround(a * flujo * 255.0));
            if (a8 == 0) continue;
            if (hayClip_ && !clip_.contiene(px, py)) continue;
            img_.en(px, py) = sobre(img_.en(px, py), premultiplicar((Pixel(a8) << 24) | rgb_));
        }
    img_.tocar(zona);
}

void pintarEstampado(Imagen &img, const Trazo &t) {
    Estampador e(img, t);
    for (const Punto &p : t.completo) e.punto(p);
    e.terminar();
}

}  // namespace plz
