#include "relleno.h"
#include <algorithm>
#include <cstdlib>
#include "mezcla.h"

namespace plz {

namespace {

inline int distancia(Pixel a, Pixel b) {
    int d = 0;
    for (int sh = 0; sh < 32; sh += 8)
        d = std::max(d, std::abs(int((a >> sh) & 255u) - int((b >> sh) & 255u)));
    return d;
}

// Convierte una máscara (1 = dentro) en tramos por fila, solo de los pixeles con valor 'quiero'
void aTramos(const std::vector<std::uint8_t> &m, int w, const Rect &zona, std::uint8_t quiero, std::vector<Span> &out) {
    for (int y = zona.y0; y < zona.y1; ++y) {
        int x = zona.x0;
        while (x < zona.x1) {
            if (m[std::size_t(y) * std::size_t(w) + std::size_t(x)] != quiero) { ++x; continue; }
            int x1 = x;
            while (x1 + 1 < zona.x1 && m[std::size_t(y) * std::size_t(w) + std::size_t(x1 + 1)] == quiero) ++x1;
            out.push_back({y, x, x1});
            x = x1 + 1;
        }
    }
}

}  // namespace

Relleno calcularRelleno(const Imagen &im, int sx, int sy, const OpcionesRelleno &o, std::uint32_t color) {
    Relleno r;
    r.color = color;
    if (!im.dentro(sx, sy)) return r;

    const int w = im.w, h = im.h;
    const int tol = std::clamp(o.tolerancia, 0, 255);
    const Pixel semilla = im.en(sx, sy);
    std::vector<std::uint8_t> m(std::size_t(w) * std::size_t(h), 0);   // 1 = núcleo, 2 = borde
    auto idx = [&](int x, int y) { return std::size_t(y) * std::size_t(w) + std::size_t(x); };
    auto vale = [&](int x, int y) { return m[idx(x, y)] == 0 && distancia(im.en(x, y), semilla) <= tol; };

    // Relleno por líneas (scanline flood fill)
    std::vector<std::pair<int, int>> pila{{sx, sy}};
    Rect caja;
    while (!pila.empty()) {
        const auto [x, y] = pila.back();
        pila.pop_back();
        if (!vale(x, y)) continue;
        int x0 = x, x1 = x;
        while (x0 > 0 && vale(x0 - 1, y)) --x0;
        while (x1 < w - 1 && vale(x1 + 1, y)) ++x1;
        for (int xx = x0; xx <= x1; ++xx) m[idx(xx, y)] = 1;
        caja.unir(Rect{x0, y, x1 + 1, y + 1});
        for (int yy : {y - 1, y + 1}) {
            if (yy < 0 || yy >= h) continue;
            bool dentro = false;
            for (int xx = x0; xx <= x1; ++xx) {
                const bool ok = vale(xx, yy);
                if (ok && !dentro) pila.push_back({xx, yy});
                dentro = ok;
            }
        }
    }
    if (caja.vacio()) return r;
    aTramos(m, w, caja, 1, r.nucleo);

    // Franja de borde: ensancha la zona sin tocar el núcleo
    if (o.expandir > 0) {
        Rect zona{caja.x0 - o.expandir, caja.y0 - o.expandir, caja.x1 + o.expandir, caja.y1 + o.expandir};
        zona = zona.interseccion(im.rectTotal());
        std::vector<std::uint8_t> prev(m);
        for (int k = 0; k < o.expandir; ++k) {
            std::vector<std::uint8_t> sig(prev);
            for (int y = zona.y0; y < zona.y1; ++y)
                for (int x = zona.x0; x < zona.x1; ++x) {
                    if (prev[idx(x, y)]) continue;
                    if ((x > 0 && prev[idx(x - 1, y)]) || (x < w - 1 && prev[idx(x + 1, y)]) ||
                        (y > 0 && prev[idx(x, y - 1)]) || (y < h - 1 && prev[idx(x, y + 1)]))
                        sig[idx(x, y)] = 2;
                }
            prev.swap(sig);
        }
        aTramos(prev, w, zona, 2, r.borde);
    }
    return r;
}

void aplicarRelleno(Imagen &img, const Relleno &r) {
    const Pixel c = premultiplicar(r.color);
    Rect sucia;
    for (const Span &s : r.nucleo) {
        if (s.y < 0 || s.y >= img.h) continue;
        const int x0 = std::max(0, s.x0), x1 = std::min(img.w - 1, s.x1);
        for (int x = x0; x <= x1; ++x) img.en(x, s.y) = c;
        sucia.unir(Rect{x0, s.y, x1 + 1, s.y + 1});
    }
    for (const Span &s : r.borde) {
        if (s.y < 0 || s.y >= img.h) continue;
        const int x0 = std::max(0, s.x0), x1 = std::min(img.w - 1, s.x1);
        for (int x = x0; x <= x1; ++x) img.en(x, s.y) = sobre(c, img.en(x, s.y));   // color debajo de lo que hay
        sucia.unir(Rect{x0, s.y, x1 + 1, s.y + 1});
    }
    img.tocar(sucia);
}

Rect rectDeRelleno(const Relleno &r) {
    Rect c;
    for (const Span &s : r.nucleo) c.unir(Rect{s.x0, s.y, s.x1 + 1, s.y + 1});
    for (const Span &s : r.borde) c.unir(Rect{s.x0, s.y, s.x1 + 1, s.y + 1});
    return c;
}

}  // namespace plz
