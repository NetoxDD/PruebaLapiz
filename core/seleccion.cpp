#include "seleccion.h"
#include <algorithm>
#include <cmath>

namespace plz {

static void ajustarCaja(Seleccion &s, Rect zona) {
    Rect c;
    for (int y = zona.y0; y < zona.y1; ++y)
        for (int x = zona.x0; x < zona.x1; ++x)
            if (s.m[std::size_t(y) * std::size_t(s.w) + std::size_t(x)]) c.unir(Rect{x, y, x + 1, y + 1});
    s.caja = c;
    if (c.vacio()) s.m.clear();
}

Seleccion Seleccion::rectangulo(int w, int h, Rect r) {
    Seleccion s;
    s.w = w; s.h = h;
    r = r.interseccion(Rect{0, 0, w, h});
    if (r.vacio()) return s;
    s.m.assign(std::size_t(w) * std::size_t(h), 0);
    for (int y = r.y0; y < r.y1; ++y)
        std::fill_n(s.m.begin() + std::ptrdiff_t(std::size_t(y) * std::size_t(w) + std::size_t(r.x0)), r.w(), std::uint8_t(1));
    s.caja = r;
    return s;
}

Seleccion Seleccion::poligono(int w, int h, const Contorno &pts) {
    Seleccion s;
    s.w = w; s.h = h;
    if (pts.size() < 3) return s;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const pf::Vec &v : pts) { x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); }
    const Rect zona = Rect{int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(x1)), int(std::ceil(y1))}
                          .interseccion(Rect{0, 0, w, h});
    if (zona.vacio()) return s;
    s.m.assign(std::size_t(w) * std::size_t(h), 0);
    std::vector<double> xs;
    const std::size_t n = pts.size();
    for (int y = zona.y0; y < zona.y1; ++y) {
        const double cy = y + 0.5;
        xs.clear();
        for (std::size_t i = 0; i < n; ++i) {
            const pf::Vec &a = pts[i], &b = pts[(i + 1) % n];
            if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy))
                xs.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int xa = std::max(0, int(std::ceil(xs[k] - 0.5))), xb = std::min(w - 1, int(std::floor(xs[k + 1] - 0.5)));
            for (int x = xa; x <= xb; ++x) s.m[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 1;
        }
    }
    ajustarCaja(s, zona);
    return s;
}

std::vector<Span> Seleccion::tramos() const {
    std::vector<Span> out;
    for (int y = caja.y0; y < caja.y1; ++y) {
        int x = caja.x0;
        while (x < caja.x1) {
            if (!contiene(x, y)) { ++x; continue; }
            int x1 = x;
            while (x1 + 1 < caja.x1 && contiene(x1 + 1, y)) ++x1;
            out.push_back({y, x, x1});
            x = x1 + 1;
        }
    }
    return out;
}

Relleno recortarRelleno(const Relleno &r, const Seleccion &s) {
    Relleno out;
    out.color = r.color;
    out.borrar = r.borrar;
    auto recortar = [&](const std::vector<Span> &in, std::vector<Span> &dst) {
        for (const Span &sp : in) {
            int x = std::max(sp.x0, 0);
            const int fin = std::min(sp.x1, s.w - 1);
            while (x <= fin) {
                if (!s.contiene(x, sp.y)) { ++x; continue; }
                int x1 = x;
                while (x1 + 1 <= fin && s.contiene(x1 + 1, sp.y)) ++x1;
                dst.push_back({sp.y, x, x1});
                x = x1 + 1;
            }
        }
    };
    recortar(r.nucleo, out.nucleo);
    recortar(r.borde, out.borde);
    return out;
}

}  // namespace plz
