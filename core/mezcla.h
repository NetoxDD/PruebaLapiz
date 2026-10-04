#pragma once
// Composición de pixeles premultiplicados (0xAARRGGBB). Sin Qt.
#include <algorithm>
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

// ---------- Modos de fusión ----------
// El orden y los números son parte del formato de archivo: no reordenar, solo añadir al final.
enum class Fusion : int {
    Normal = 0, Multiplicar, Trama, Superponer, Oscurecer, Aclarar,
    Sobreexponer, Subexponer, LuzFuerte, LuzSuave, Diferencia, Exclusion,
    Cantidad
};
inline Fusion fusionDeInt(int i) { return (i > 0 && i < int(Fusion::Cantidad)) ? Fusion(i) : Fusion::Normal; }
inline const char *nombreFusion(Fusion f) {
    static const char *const n[] = {"Normal", "Multiplicar", "Trama", "Superponer", "Oscurecer", "Aclarar",
                                    "Sobreexponer", "Subexponer", "Luz fuerte", "Luz suave", "Diferencia", "Exclusión"};
    const int i = int(f);
    return (i >= 0 && i < int(Fusion::Cantidad)) ? n[i] : n[0];
}

namespace detalle {
inline float luzFuerte(float cb, float cs) {          // multiplicar o trama según el color de arriba
    return cs <= 0.5f ? cb * 2 * cs : cb + (2 * cs - 1) - cb * (2 * cs - 1);
}
// B(Cb, Cs) por canal con colores SIN premultiplicar (0..1): fórmulas estándar de composición
inline float mezclaCanal(Fusion m, float cb, float cs) {
    switch (m) {
    case Fusion::Multiplicar:  return cb * cs;
    case Fusion::Trama:        return cb + cs - cb * cs;
    case Fusion::Superponer:   return luzFuerte(cs, cb);
    case Fusion::Oscurecer:    return std::min(cb, cs);
    case Fusion::Aclarar:      return std::max(cb, cs);
    case Fusion::Sobreexponer: return cb == 0 ? 0.f : (cs >= 1 ? 1.f : std::min(1.f, cb / (1 - cs)));
    case Fusion::Subexponer:   return cb >= 1 ? 1.f : (cs <= 0 ? 0.f : 1 - std::min(1.f, (1 - cb) / cs));
    case Fusion::LuzFuerte:    return luzFuerte(cb, cs);
    case Fusion::LuzSuave: {
        if (cs <= 0.5f) return cb - (1 - 2 * cs) * cb * (1 - cb);
        const float d = cb <= 0.25f ? ((16 * cb - 12) * cb + 4) * cb : std::sqrt(cb);
        return cb + (2 * cs - 1) * (d - cb);
    }
    case Fusion::Diferencia:   return std::fabs(cb - cs);
    case Fusion::Exclusion:    return cb + cs - 2 * cb * cs;
    default:                   return cs;
    }
}
}  // namespace detalle

// src (ya con su opacidad aplicada) sobre dst con un modo de fusión. Ambos premultiplicados.
//   co = cs·(1-αb) + cb·(1-αs) + αs·αb·B(Cb, Cs)      αo = αs + αb - αs·αb
inline Pixel fusionar(Pixel dst, Pixel src, Fusion modo) {
    if (modo == Fusion::Normal) return sobre(dst, src);
    const unsigned sa8 = src >> 24, da8 = dst >> 24;
    if (sa8 == 0) return dst;
    if (da8 == 0) return src;                       // sin nada debajo, el modo no cambia nada
    const float sa = float(sa8) / 255.0f, da = float(da8) / 255.0f;
    const unsigned ao8 = unsigned(std::lround((sa + da - sa * da) * 255.0f));
    Pixel out = Pixel(ao8) << 24;
    for (int sh = 16; sh >= 0; sh -= 8) {
        const float cs = float((src >> sh) & 255u) / 255.0f, cb = float((dst >> sh) & 255u) / 255.0f;
        const float b = detalle::mezclaCanal(modo, std::min(1.0f, cb / da), std::min(1.0f, cs / sa));
        const float co = cs * (1 - da) + cb * (1 - sa) + sa * da * b;
        out |= Pixel(std::min(ao8, unsigned(std::lround(std::clamp(co, 0.0f, 1.0f) * 255.0f)))) << sh;
    }
    return out;
}

// Mezcla src sobre dst con una opacidad 0..1 y un modo (mismo tamaño)
inline void mezclar(Imagen &dst, const Imagen &src, double opacidad, Fusion modo = Fusion::Normal) {
    if (dst.w != src.w || dst.h != src.h) return;
    const unsigned op = opacidadA255(opacidad);
    if (op == 0) return;
    for (std::size_t i = 0; i < dst.px.size(); ++i) {
        const Pixel s = src.px[i];
        if (s == 0) continue;
        dst.px[i] = fusionar(dst.px[i], escalar(s, op), modo);
    }
    dst.tocar();
}

// Igual, pero 'dst' es un trozo (p. ej. una tesela) que corresponde a la zona de 'src' que empieza en (ox, oy)
inline void mezclarZona(Imagen &dst, int ox, int oy, const Imagen &src, double opacidad, Fusion modo = Fusion::Normal) {
    const unsigned op = opacidadA255(opacidad);
    if (op == 0) return;
    for (int y = 0; y < dst.h; ++y) {
        const int sy = oy + y;
        if (sy < 0 || sy >= src.h) continue;
        for (int x = 0; x < dst.w; ++x) {
            const int sx = ox + x;
            if (sx < 0 || sx >= src.w) continue;
            const Pixel s = src.en(sx, sy);
            if (s == 0) continue;
            dst.en(x, y) = fusionar(dst.en(x, y), escalar(s, op), modo);
        }
    }
    dst.tocar();
}

}  // namespace plz
