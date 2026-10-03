#include "mini_test.h"

int main() {
    int total = 0;
    for (const PruebaReg &p : pruebas()) {
        const int antes = fallos();
        p.f();
        std::printf("[%s] %s\n", fallos() == antes ? " OK " : "FAIL", p.nombre);
        ++total;
    }
    std::printf("\n%d pruebas, %d verificaciones fallidas\n", total, fallos());
    return fallos() == 0 ? 0 : 1;
}
