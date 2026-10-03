#pragma once
// Comandos de edición del documento. Sin Qt.
#include "documento.h"

namespace plz {

// Un trazo o un relleno. Se deshace y se rehace pegando la zona que cambió (antes/después),
// así que cuesta lo mismo con 10 operaciones en la capa que con 10.000.
class CmdOperacion : public Comando {
    int capaId;
    Rect zona;
    Imagen antes, despues;
    Operacion op;   // vacía mientras la operación está dentro de la capa
public:
    CmdOperacion(int id, Rect z, Imagen a, Imagen d)
        : capaId(id), zona(z), antes(std::move(a)), despues(std::move(d)) {}
    void aplicar(Documento &d) override;
    void deshacer(Documento &d) override;
    std::size_t bytes() const override { return (antes.px.size() + despues.px.size()) * sizeof(Pixel); }
};

// Crear o borrar una capa (el borrado es el inverso exacto de la creación)
class CmdCapa : public Comando {
    bool crear;
    int indice;
    Capa capa;
    int activaAntes, activaDespues;
    void insertar(Documento &d);
    void quitar(Documento &d);
public:
    CmdCapa(bool crear_, int indice_, Capa c, int antes, int despues)
        : crear(crear_), indice(indice_), capa(std::move(c)), activaAntes(antes), activaDespues(despues) {}
    void aplicar(Documento &d) override;
    void deshacer(Documento &d) override;
};

class CmdMoverCapa : public Comando {
    int i, j, activaAntes, activaDespues;
public:
    CmdMoverCapa(int i_, int j_, int antes, int despues)
        : i(i_), j(j_), activaAntes(antes), activaDespues(despues) {}
    void aplicar(Documento &d) override;
    void deshacer(Documento &d) override;
};

class CmdPropiedades : public Comando {
    int capaId;
    PropCapa antes, despues;
    std::string clave;
    void poner(Documento &d, const PropCapa &p);
public:
    CmdPropiedades(int id, PropCapa a, PropCapa b, std::string k)
        : capaId(id), antes(std::move(a)), despues(std::move(b)), clave(std::move(k)) {}
    void aplicar(Documento &d) override { poner(d, despues); }
    void deshacer(Documento &d) override { poner(d, antes); }
    bool fusionar(const Comando &otro) override;
};

}  // namespace plz
