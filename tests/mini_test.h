#pragma once
// Mini marco de pruebas (sin dependencias externas).
#include <cstdio>
#include <vector>

struct PruebaReg { const char *nombre; void (*f)(); };
inline std::vector<PruebaReg> &pruebas() { static std::vector<PruebaReg> v; return v; }
inline int &fallos() { static int n = 0; return n; }
struct Registrar { Registrar(const char *n, void (*f)()) { pruebas().push_back({n, f}); } };

#define PRUEBA(nombre) \
    static void nombre(); \
    static Registrar reg_##nombre(#nombre, nombre); \
    static void nombre()
#define VERIFICAR(c) \
    do { if (!(c)) { std::printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); ++fallos(); } } while (0)
