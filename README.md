# PruebaLapiz

Aplicación de dibujo con soporte para tableta (Qt 6 + OpenGL).

## Estructura

```
core/    Modelo del dibujo SIN Qt: Documento, Capa, Historial (comandos), mezcla de pixeles, freehand.
app/     Interfaz Qt: Canvas (vista e input), puente con el core, guardado JSON, main.
tests/   Pruebas del core (sin dependencias) y prueba de humo de la interfaz.
```

Regla: `core/` nunca incluye cabeceras de Qt. Toda función nueva (cubo de relleno, formas,
modos de fusión...) se escribe primero en `core/` con su prueba, y `app/` solo la conecta.

## Compilar

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build            # pruebas del core
```

Solo el core y sus pruebas (sin Qt): `-DPLZ_BUILD_APP=OFF`.

Prueba de humo de la interfaz (necesita OpenGL; en Linux sin monitor usa xvfb):

```
cmake -S . -B build -DPLZ_BUILD_SMOKE=ON && cmake --build build
QT_QPA_PLATFORM=xcb xvfb-run -a build/smoke_canvas
```

## Herramientas

- **Cubo (G):** rellena la zona parecida al punto donde haces clic. *Tolerancia* decide cuánto puede
  diferir un color; *Todas las capas* decide la zona mirando la imagen combinada (útil cuando las
  líneas están en otra capa) y siempre pinta solo en la capa activa. El relleno cubre 1 px bajo los
  bordes de las líneas para no dejar el halo blanco del antialiasing.

## Deshacer

Entran en el historial: trazos, rellenos, crear/borrar/mover capas, opacidad, bloqueo y nombre de capa.
Cambiar la visibilidad de una capa no entra, pero sí marca el archivo como modificado.

Cada trazo o relleno guarda solo la zona de la capa que tocó (antes y después), así que deshacer y
rehacer cuestan lo mismo con 10 operaciones que con 10.000. El historial tiene un tope de 500 pasos y
512 MB; al pasarlo olvida los más antiguos.

## Formato de archivo

JSON versión 4: cada capa guarda sus operaciones (trazos y rellenos) en el orden en que se hicieron.
Abre también las versiones 1, 2 y 3.
