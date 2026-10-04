#pragma once
// Modelo del dibujo: capas, trazos e historial. Sin Qt.
#include <functional>
#include <string>
#include <variant>
#include <vector>
#include "historial.h"
#include "imagen.h"
#include "relleno.h"
#include "estampa.h"
#include "mezcla.h"
#include "seleccion.h"
#include "transformar.h"
#include "trazo.h"

namespace plz {

// Lo que se ha hecho en una capa, en orden: es lo que se guarda en el archivo
// Una imagen pegada con su esquina en (x, y)
struct Pegado {
    int x = 0, y = 0;
    Imagen img;
};
void aplicarPegado(Imagen &dst, const Pegado &p);

using Operacion = std::variant<Trazo, Relleno, Pegado, Transformacion>;

struct Capa {
    int id = 0;
    std::string nombre;
    bool visible = true;
    bool bloqueada = false;
    bool esPlantilla = false;
    double opacidad = 1.0;
    Fusion fusion = Fusion::Normal;    // cómo se mezcla con lo que hay debajo
    Imagen img;                    // pixeles actuales
    Imagen base;                   // pixeles de partida (plantillas); vacía = transparente
    std::vector<Operacion> ops;    // trazos y rellenos que la forman (base del guardado)

    Capa() = default;
    Capa(int id_, std::string n, int w, int h) : id(id_), nombre(std::move(n)), img(w, h) {}
};

// Propiedades de capa que entran en el historial
struct PropCapa {
    std::string nombre;
    bool bloqueada = false;
    double opacidad = 1.0;
    Fusion fusion = Fusion::Normal;
    bool operator==(const PropCapa &o) const {
        return nombre == o.nombre && bloqueada == o.bloqueada && opacidad == o.opacidad && fusion == o.fusion;
    }
};

class Documento {
public:
    // La interfaz inyecta aquí cómo se rasteriza un trazo sobre una imagen.
    using Pintor = std::function<void(Imagen &, const Trazo &)>;

    int ancho = 2000, alto = 1500;
    std::vector<Capa> capas;       // índice 0 = la de más abajo
    int activa = 0;
    int siguienteId = 1;
    Historial historial;
    Pintor pintor;
    // Pinta un trazo en una imagen: los de estampado los resuelve el core; el resto, el 'pintor' de la interfaz
    void pintarTrazo(Imagen &im, const Trazo &t) const {
        if (t.estampado) pintarEstampado(im, t);
        else if (pintor) pintor(im, t);
    }
    bool sucioExtra = false;       // cambios que no entran en el historial (p. ej. visibilidad)
    std::uint64_t revisionExtra = 0;

    explicit Documento(int w = 2000, int h = 1500) { reiniciar(w, h); }

    void reiniciar(int w, int h);
    // Sustituye todo el contenido (al abrir un archivo)
    void cargar(int w, int h, std::vector<Capa> nuevas, int siguiente);

    int numCapas() const { return int(capas.size()); }
    bool indiceValido(int i) const { return i >= 0 && i < numCapas(); }
    Capa *porId(int id);
    const Capa *porId(int id) const;

    // --- Acciones con historial ---
    void nuevaCapa();
    bool borrarCapa(int i);                    // false si es la única o el índice no existe
    bool moverCapa(int i, int delta);
    void insertarPlantilla(Capa c);            // la deja debajo de todo y conserva la capa activa
    bool cambiarPropiedades(int i, const PropCapa &nuevas, const char *clave);
    // El trazo ya está pintado en la capa. 'antes' es la capa completa tal como estaba ANTES del trazo:
    // de ahí se saca la zona que permite deshacerlo sin repintar nada.
    void registrarTrazo(int capaId, Trazo t, const Imagen &antes);
    // Cubo de relleno en el punto (x, y) de la capa i. Con todasLasCapas mira la imagen combinada para
    // decidir la zona (siempre pinta solo en la capa i). false si no hay nada que rellenar.
    bool rellenar(int i, int x, int y, std::uint32_t color, const OpcionesRelleno &o, bool todasLasCapas,
                  const Seleccion *sel = nullptr);   // con sel, solo rellena dentro de la selección

    // --- Selección: borrar, copiar y pegar (borrar y pegar entran en el historial) ---
    bool borrarSeleccion(int i, const Seleccion &s);
    Imagen copiarSeleccion(int i, const Seleccion &s) const;   // la caja de la selección; lo de fuera, transparente
    bool pegar(int i, Imagen img, int x, int y);
    // Mueve, escala o rota lo seleccionado de la capa i. false si no hay selección o no cambia nada.
    bool transformar(int i, const Seleccion &s, const Afin &m, bool suave = true);

    bool deshacer() { return historial.deshacer(*this); }
    bool rehacer() { return historial.rehacer(*this); }

    // --- Sin historial ---
    void setVisible(int i, bool v);

    bool modificado() const { return sucioExtra || historial.modificado(); }
    // Sube con cualquier cambio del dibujo; sirve para saber si hay algo nuevo que guardar
    std::uint64_t revision() const { return historial.revision() + revisionExtra; }
    void marcarModificado() { sucioExtra = true; ++revisionExtra; }
    void marcarGuardado() { historial.marcarGuardado(); sucioExtra = false; }

    // Vuelve a pintar una capa desde su base y sus operaciones (al abrir un archivo)
    void reconstruir(Capa &c);

    // Todas las capas visibles sobre blanco. La plantilla solo si se pide.
    Imagen componer(bool conPlantilla) const;
    // Color visible en un punto (todas las capas visibles, plantilla incluida, sobre blanco)
    Pixel colorEn(int x, int y) const;

    std::size_t totalOperaciones() const;
};

}  // namespace plz
