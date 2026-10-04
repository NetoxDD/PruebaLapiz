#include "transformar.h"
#include <algorithm>
#include "mezcla.h"

namespace plz {

namespace {

int aEntero(double v) { return int(std::clamp(v, -1e7, 1e7)); }

// Caja (con 1 px de margen por la interpolación) que ocupa un rectángulo ya transformado
Rect cajaTransformada(const Rect &r, const Afin &m) {
    const pf::Vec p[4] = {m.aplicar(r.x0, r.y0), m.aplicar(r.x1, r.y0), m.aplicar(r.x1, r.y1), m.aplicar(r.x0, r.y1)};
    double x0 = p[0].x, y0 = p[0].y, x1 = p[0].x, y1 = p[0].y;
    for (const pf::Vec &v : p) { x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); }
    return Rect{aEntero(std::floor(x0)) - 1, aEntero(std::floor(y0)) - 1, aEntero(std::ceil(x1)) + 1, aEntero(std::ceil(y1)) + 1};
}

inline Pixel leer(const Imagen &im, int x, int y) {
    return (x < 0 || y < 0 || x >= im.w || y >= im.h) ? TRANSPARENTE : im.en(x, y);
}

// Bilineal sobre pixeles premultiplicados; fuera de la imagen es transparente (el borde queda suave).
// (fx, fy) son coordenadas de índice: el centro del pixel i está en i.
Pixel muestrear(const Imagen &im, double fx, double fy) {
    if (fx <= -1 || fy <= -1 || fx >= im.w || fy >= im.h) return TRANSPARENTE;
    const double fx0 = std::floor(fx), fy0 = std::floor(fy);
    double tx = fx - fx0, ty = fy - fy0;
    int x0 = int(fx0), y0 = int(fy0);
    // Con desplazamientos enteros el resultado debe ser una copia exacta, sin error de redondeo
    if (tx < 1e-6) tx = 0; else if (tx > 1 - 1e-6) { tx = 0; ++x0; }
    if (ty < 1e-6) ty = 0; else if (ty > 1 - 1e-6) { ty = 0; ++y0; }
    const Pixel p00 = leer(im, x0, y0);
    if (tx == 0 && ty == 0) return p00;
    const Pixel p10 = leer(im, x0 + 1, y0), p01 = leer(im, x0, y0 + 1), p11 = leer(im, x0 + 1, y0 + 1);
    const double w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
    Pixel out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const double v = w00 * ((p00 >> sh) & 255u) + w10 * ((p10 >> sh) & 255u) +
                         w01 * ((p01 >> sh) & 255u) + w11 * ((p11 >> sh) & 255u);
        out |= Pixel(std::min(255.0, v + 0.5)) << sh;
    }
    return out;
}

}  // namespace

Rect rectDeSpans(const std::vector<Span> &v) {
    Rect r;
    for (const Span &s : v) r.unir(Rect{s.x0, s.y, s.x1 + 1, s.y + 1});
    return r;
}

Rect rectDeTransformacion(const Transformacion &t, int ancho, int alto) {
    Rect r = rectDeSpans(t.origen);
    if (r.vacio()) return Rect{};
    r.unir(cajaTransformada(r, t.m));
    return r.interseccion(Rect{0, 0, ancho, alto});
}

void aplicarTransformacion(Imagen &img, const Transformacion &t) {
    const Rect src = rectDeSpans(t.origen).interseccion(img.rectTotal());
    if (src.vacio()) return;

    // 1) levantar: copiar los pixeles seleccionados y dejar su sitio transparente
    Imagen fuente(src.w(), src.h());
    for (const Span &s : t.origen) {
        if (s.y < src.y0 || s.y >= src.y1) continue;
        const int x0 = std::max(s.x0, src.x0), x1 = std::min(s.x1, src.x1 - 1);
        for (int x = x0; x <= x1; ++x) {
            fuente.en(x - src.x0, s.y - src.y0) = img.en(x, s.y);
            img.en(x, s.y) = TRANSPARENTE;
        }
    }
    Rect zona = src;
    if (!t.m.invertible()) { img.tocar(zona); return; }   // aplastado a una línea: desaparece

    // 2) colocar: por cada pixel del destino se mira de dónde viene en el origen
    const Afin inv = t.m.inversa();
    const Rect dst = cajaTransformada(src, t.m).interseccion(img.rectTotal());
    for (int y = dst.y0; y < dst.y1; ++y)
        for (int x = dst.x0; x < dst.x1; ++x) {
            const pf::Vec u = inv.aplicar(x + 0.5, y + 0.5);
            const double ux = u.x - src.x0, uy = u.y - src.y0;
            Pixel p;
            if (t.suave) p = muestrear(fuente, ux - 0.5, uy - 0.5);
            else p = leer(fuente, int(std::floor(ux)), int(std::floor(uy)));
            if (p) img.en(x, y) = sobre(img.en(x, y), p);
        }
    zona.unir(dst);
    img.tocar(zona);
}

Contorno transformarContorno(const Contorno &c, const Afin &m) {
    Contorno out;
    out.reserve(c.size());
    for (const pf::Vec &v : c) out.push_back(m.aplicar(v.x, v.y));
    return out;
}

}  // namespace plz
