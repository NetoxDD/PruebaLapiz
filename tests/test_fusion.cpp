#include <cstdlib>
#include "mini_test.h"
#include "core/documento.h"
#include "core/mezcla.h"

using namespace plz;

namespace {
int canal(Pixel p, int sh) { return int((p >> sh) & 255u); }
// Iguales salvo ±tol en cada canal
bool casi(Pixel a, Pixel b, int tol = 1) {
    for (int sh = 0; sh < 32; sh += 8) if (std::abs(canal(a, sh) - canal(b, sh)) > tol) return false;
    return true;
}
Pixel argb(int a, int r, int g, int b) { return (Pixel(a) << 24) | (Pixel(r) << 16) | (Pixel(g) << 8) | Pixel(b); }
}  // namespace

PRUEBA(fusion_multiplicar_opaco) {
    const Pixel dst = argb(255, 255, 128, 64), src = argb(255, 128, 128, 128);
    VERIFICAR(casi(fusionar(dst, src, Fusion::Multiplicar), argb(255, 128, 64, 32)));
    VERIFICAR(fusionar(dst, argb(255, 255, 255, 255), Fusion::Multiplicar) == dst);        // por blanco no cambia
    VERIFICAR(fusionar(dst, argb(255, 0, 0, 0), Fusion::Multiplicar) == argb(255, 0, 0, 0));
}

PRUEBA(fusion_trama_diferencia_exclusion) {
    const Pixel dst = argb(255, 200, 100, 50);
    VERIFICAR(fusionar(dst, argb(255, 0, 0, 0), Fusion::Trama) == dst);                    // por negro no cambia
    VERIFICAR(casi(fusionar(dst, argb(255, 255, 255, 255), Fusion::Trama), argb(255, 255, 255, 255)));
    VERIFICAR(casi(fusionar(dst, argb(255, 50, 100, 200), Fusion::Diferencia), argb(255, 150, 0, 150)));
    VERIFICAR(casi(fusionar(dst, argb(255, 255, 255, 255), Fusion::Exclusion), argb(255, 55, 155, 205)));   // invierte
}

PRUEBA(fusion_oscurecer_aclarar) {
    const Pixel dst = argb(255, 200, 100, 50), src = argb(255, 50, 150, 50);
    VERIFICAR(casi(fusionar(dst, src, Fusion::Oscurecer), argb(255, 50, 100, 50)));
    VERIFICAR(casi(fusionar(dst, src, Fusion::Aclarar), argb(255, 200, 150, 50)));
}

PRUEBA(fusion_superponer_y_luz_fuerte_son_pareja) {
    const Pixel a = argb(255, 200, 100, 30), b = argb(255, 90, 180, 240);
    VERIFICAR(casi(fusionar(a, b, Fusion::Superponer), fusionar(b, a, Fusion::LuzFuerte)));   // intercambiar capas
    VERIFICAR(casi(fusionar(argb(255, 128, 128, 128), argb(255, 128, 128, 128), Fusion::LuzSuave), argb(255, 128, 128, 128), 2));
}

PRUEBA(fusion_con_transparencia) {
    const Pixel dst = argb(255, 200, 100, 50);
    // sin nada debajo, el modo no cambia lo de arriba; sin nada arriba, no cambia lo de abajo
    const Pixel semi = argb(128, 64, 0, 0);
    VERIFICAR(fusionar(0, semi, Fusion::Multiplicar) == semi);
    VERIFICAR(fusionar(dst, 0, Fusion::Multiplicar) == dst);
    // multiplicar al 50 % de alfa = mitad entre el fondo y el resultado completo
    const Pixel src50 = premultiplicar(argb(128, 100, 100, 100));
    const Pixel r = fusionar(dst, src50, Fusion::Multiplicar);
    VERIFICAR(canal(r, 24) == 255);
    const int pleno = 200 * 100 / 255;                                                     // multiplicar al 100 %
    const int esperado = int(200 + (pleno - 200) * (128 / 255.0) + 0.5);
    VERIFICAR(std::abs(canal(r, 16) - esperado) <= 2);
}

PRUEBA(fusion_normal_es_sobre) {
    unsigned s = 12345;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s; };
    for (int i = 0; i < 2000; ++i) {
        const Pixel d = premultiplicar(rnd()), p = premultiplicar(rnd());
        VERIFICAR(fusionar(d, p, Fusion::Normal) == sobre(d, p));
    }
}

PRUEBA(fusion_siempre_da_pixeles_validos) {            // premultiplicado: ningún canal puede pasar del alfa
    unsigned s = 987654321u;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s; };
    int malos = 0;
    for (int m = 0; m < int(Fusion::Cantidad); ++m)
        for (int i = 0; i < 3000; ++i) {
            const Pixel d = premultiplicar(rnd()), p = premultiplicar(rnd());
            const Pixel r = fusionar(d, p, Fusion(m));
            const int a = canal(r, 24);
            if (canal(r, 16) > a || canal(r, 8) > a || canal(r, 0) > a) ++malos;
        }
    VERIFICAR(malos == 0);
}

PRUEBA(fusion_nombres_y_numeros) {
    VERIFICAR(fusionDeInt(0) == Fusion::Normal && fusionDeInt(1) == Fusion::Multiplicar);
    VERIFICAR(fusionDeInt(-3) == Fusion::Normal && fusionDeInt(999) == Fusion::Normal);   // archivo raro: vuelve a Normal
    VERIFICAR(std::string(nombreFusion(Fusion::Normal)) == "Normal");
    for (int m = 0; m < int(Fusion::Cantidad); ++m) VERIFICAR(nombreFusion(Fusion(m))[0] != 0);
}

PRUEBA(documento_compone_con_modos_y_el_gotero_coincide) {
    Documento d(4, 4);
    d.capas[0].img.llenar(argb(255, 255, 0, 0));                      // capa de abajo: rojo
    d.nuevaCapa();
    d.capas[1].img.llenar(argb(255, 0, 255, 0));                      // capa de arriba: verde
    d.historial.limpiar();
    VERIFICAR(d.componer(true).en(1, 1) == argb(255, 0, 255, 0));    // normal: tapa
    PropCapa p{d.capas[1].nombre, false, 1.0, Fusion::Multiplicar};
    VERIFICAR(d.cambiarPropiedades(1, p, "fusion"));
    VERIFICAR(d.capas[1].fusion == Fusion::Multiplicar);
    VERIFICAR(d.componer(true).en(1, 1) == argb(255, 0, 0, 0));      // rojo × verde = negro
    VERIFICAR(d.colorEn(1, 1) == d.componer(true).en(1, 1));         // el cuentagotas ve lo mismo
    p.fusion = Fusion::Diferencia;
    VERIFICAR(d.cambiarPropiedades(1, p, "fusion"));
    VERIFICAR(d.colorEn(1, 1) == d.componer(true).en(1, 1) && casi(d.colorEn(1, 1), argb(255, 255, 255, 0)));
    // cambiar de modo entra en el historial; dos cambios seguidos de la misma propiedad son un solo paso
    VERIFICAR(d.deshacer() && d.capas[1].fusion == Fusion::Normal);
    VERIFICAR(!d.deshacer());
    VERIFICAR(d.rehacer() && d.capas[1].fusion == Fusion::Diferencia);
}

PRUEBA(mezclar_zona_equivale_a_mezclar_entero) {
    Imagen src(10, 10);
    for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) src.en(x, y) = premultiplicar(argb(200, x * 25, y * 25, 90));
    Imagen completo(10, 10, BLANCO), trozo(4, 5, BLANCO);
    mezclar(completo, src, 0.7, Fusion::Superponer);
    mezclarZona(trozo, 3, 2, src, 0.7, Fusion::Superponer);          // la zona (3,2) de tamaño 4x5
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 4; ++x) VERIFICAR(trozo.en(x, y) == completo.en(3 + x, 2 + y));
}
