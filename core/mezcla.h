#pragma once
// Composición de pixeles premultiplicados (0xAARRGGBB). Sin Qt.
#include <cmath>
#include <cstdint>
#include "imagen.h"

namespace plz {

// Multiplica los cuatro canales por op/255
inline Pixel escalar(Pixel p, unsigned op) {
    if (op >= 255) return p;
    auto canal = [&](int sh) -> Pixel { return Pixel((((p >> sh) & 255u) * op + 127u) / 255u) << sh; };
    return canal(24) | canal(16) | canal(8) | canal(0);
}

// src sobre dst (ambos premultiplicados)
inline Pixel sobre(Pixel dst, Pixel src) {
    const unsigned sa = src >> 24;
    if (sa == 255) return src;
    if (sa == 0) return dst;
    const unsigned inv = 255u - sa;
    auto canal = [&](int sh) -> Pixel {
        const unsigned s = (src >> sh) & 255u, d = (dst >> sh) & 255u;
        return Pixel(std::min(255u, s + (d * inv + 127u) / 255u)) << sh;
    };
    return canal(24) | canal(16) | canal(8) | canal(0);
}

// ARGB sin premultiplicar (como QColor::rgba()) -> pixel premultiplicado
inline Pixel premultiplicar(std::uint32_t argb) {
    const unsigned a = argb >> 24;
    if (a == 255) return argb;
    auto canal = [&](int sh) -> Pixel { return Pixel((((argb >> sh) & 255u) * a + 127u) / 255u) << sh; };
    return (Pixel(a) << 24) | canal(16) | canal(8) | canal(0);
}

inline unsigned opacidadA255(double o) {
    return unsigned(std::lround(std::clamp(o, 0.0, 1.0) * 255.0));
}

// Mezcla src sobre dst con una opacidad 0..1 (mismo tamaño)
inline void mezclar(Imagen &dst, const Imagen &src, double opacidad) {
    if (dst.w != src.w || dst.h != src.h) return;
    const unsigned op = opacidadA255(opacidad);
    if (op == 0) return;
    for (std::size_t i = 0; i < dst.px.size(); ++i) {
        const Pixel s = src.px[i];
        if (s == 0) continue;
        dst.px[i] = sobre(dst.px[i], escalar(s, op));
    }
    dst.tocar();
}

}  // namespace plz
