#pragma once
// Imagen RGBA en memoria, sin Qt.
// Formato de pixel: 0xAARRGGBB con alfa PREMULTIPLICADO (igual que QImage::Format_ARGB32_Premultiplied,
// así la interfaz puede mostrarla sin copiar).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace plz {

using Pixel = std::uint32_t;

// Rectángulo [x0,x1) × [y0,y1)
struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool vacio() const { return x1 <= x0 || y1 <= y0; }
    int w() const { return std::max(0, x1 - x0); }
    int h() const { return std::max(0, y1 - y0); }
    void unir(const Rect &o) {
        if (o.vacio()) return;
        if (vacio()) { *this = o; return; }
        x0 = std::min(x0, o.x0); y0 = std::min(y0, o.y0);
        x1 = std::max(x1, o.x1); y1 = std::max(y1, o.y1);
    }
    Rect interseccion(const Rect &o) const {
        Rect r{std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
        return r.vacio() ? Rect{} : r;
    }
    bool operator==(const Rect &o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
};
constexpr Pixel TRANSPARENTE = 0x00000000;
constexpr Pixel BLANCO = 0xFFFFFFFF;

struct Imagen {
    int w = 0, h = 0;
    std::vector<Pixel> px;
    // Sube cada vez que cambian los pixeles.
    std::uint64_t version = 0;
    // Zona cambiada desde la última vez que la interfaz la consumió (para subir a la GPU solo lo que cambió)
    Rect sucio;

    Imagen() = default;
    Imagen(int ancho, int alto, Pixel c = TRANSPARENTE)
        : w(ancho), h(alto), px(std::size_t(ancho) * std::size_t(alto), c) {}

    bool vacia() const { return px.empty(); }
    bool dentro(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    Pixel &en(int x, int y) { return px[std::size_t(y) * std::size_t(w) + std::size_t(x)]; }
    const Pixel &en(int x, int y) const { return px[std::size_t(y) * std::size_t(w) + std::size_t(x)]; }

    Rect rectTotal() const { return Rect{0, 0, w, h}; }
    void tocar() { ++version; sucio = rectTotal(); }                       // cambió todo
    void tocar(const Rect &r) { ++version; sucio.unir(r.interseccion(rectTotal())); }   // cambió una zona
    Rect consumirSucio() { Rect r = sucio; sucio = Rect{}; return r; }
    void llenar(Pixel c) { std::fill(px.begin(), px.end(), c); tocar(); }
    // Copia los pixeles reutilizando la memoria si el tamaño coincide (la interfaz guarda punteros a ella).
    void copiarDe(const Imagen &o) {
        if (o.w == w && o.h == h) {
            std::copy(o.px.begin(), o.px.end(), px.begin());
        } else {
            w = o.w; h = o.h; px = o.px;
        }
        tocar();
    }

    // Copia de una zona (se recorta a la imagen)
    Imagen recortar(const Rect &pedido) const {
        const Rect r = pedido.interseccion(rectTotal());
        Imagen out(r.w(), r.h());
        for (int y = 0; y < r.h(); ++y)
            std::copy_n(px.begin() + std::ptrdiff_t(std::size_t(r.y0 + y) * std::size_t(w) + std::size_t(r.x0)),
                        r.w(), out.px.begin() + std::ptrdiff_t(std::size_t(y) * std::size_t(r.w())));
        return out;
    }
    // Pega otra imagen con su esquina en (x, y) (se recorta a los límites)
    void pegar(const Imagen &src, int x, int y) {
        const Rect r = Rect{x, y, x + src.w, y + src.h}.interseccion(rectTotal());
        for (int yy = r.y0; yy < r.y1; ++yy)
            std::copy_n(src.px.begin() + std::ptrdiff_t(std::size_t(yy - y) * std::size_t(src.w) + std::size_t(r.x0 - x)),
                        r.w(), px.begin() + std::ptrdiff_t(std::size_t(yy) * std::size_t(w) + std::size_t(r.x0)));
        tocar(r);
    }
    // Copia solo una zona de otra imagen del mismo tamaño
    void copiarRegionDe(const Imagen &o, const Rect &pedido) {
        if (o.w != w || o.h != h) { copiarDe(o); return; }
        const Rect r = pedido.interseccion(rectTotal());
        for (int yy = r.y0; yy < r.y1; ++yy) {
            const std::size_t i = std::size_t(yy) * std::size_t(w) + std::size_t(r.x0);
            std::copy_n(o.px.begin() + std::ptrdiff_t(i), r.w(), px.begin() + std::ptrdiff_t(i));
        }
        tocar(r);
    }
};

}  // namespace plz
