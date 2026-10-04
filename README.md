# PruebaLapiz

Aplicación de dibujo con soporte para tableta (Qt 6 + OpenGL).

## Estructura

```
core/    Modelo del dibujo SIN Qt: Documento, Capa, Historial (comandos), mezcla y modos de fusión,
         freehand, relleno, selección, transformar, estampa.
app/     Interfaz Qt:
           canvas.h        vista e input (herramientas, sesiones de transformar/polígono, pantalla)
           puente.h        conversión entre el core y Qt
           archivo.h       guardar y abrir (JSON)
           autoguardado.h  copia de recuperación
           ventana*.cpp    ventana principal repartida por zonas (ver cabecera de ventana.h)
           main.cpp        solo el arranque
tests/   Pruebas del core (sin dependencias) y pruebas de humo de la interfaz.
```

Regla: `core/` nunca incluye cabeceras de Qt. Toda función nueva se escribe primero en `core/` con su
prueba, y `app/` solo la conecta.

## Compilar

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build            # pruebas del core
```

Solo el core y sus pruebas (sin Qt): `-DPLZ_BUILD_APP=OFF`.

Pruebas de humo de la interfaz (necesitan OpenGL; en Linux sin monitor usa xvfb):

```
cmake -S . -B build -DPLZ_BUILD_SMOKE=ON && cmake --build build
QT_QPA_PLATFORM=xcb xvfb-run -a build/smoke_canvas
QT_QPA_PLATFORM=xcb xvfb-run -a build/smoke_ventana
```

Con `PLZ_CAPTURAS=<carpeta>` las pruebas guardan capturas de pantalla en puntos clave para revisarlas a ojo.

## Herramientas

- **Cubo (G):** rellena la zona parecida al punto donde haces clic. *Tolerancia* decide cuánto puede
  diferir un color; *Todas las capas* decide la zona mirando la imagen combinada y siempre pinta solo en
  la capa activa. El relleno cubre 1 px bajo los bordes de las líneas para no dejar el halo del antialiasing.
- **Selección (M):** rectángulo o lazo. Un clic fuera la quita. Copiar, cortar, pegar y borrar actúan sobre ella,
  y los trazos se recortan a la selección.
- **Transformar (Ctrl+T):** mueve, escala y rota lo seleccionado. Arrastra dentro para mover; las 8 asas escalan
  (Shift en una esquina = proporcional; pasar al otro lado voltea la imagen); arrastrar *fuera* de la caja rota
  (Shift = saltos de 15°). Las flechas mueven 1 px (Shift = 10). **Enter** aplica, **Esc** cancela (también
  *Deshacer*); con tableta, los botones ✔ Aplicar / ✖ Cancelar de la barra. Un movimiento sin escalar ni girar
  es una copia exacta de píxeles, sin emborronar. La selección acompaña a lo transformado.
- **Polígono** (en *Forma*): un clic por vértice. Se cierra haciendo clic en el primero, con doble clic o con
  Enter; Retroceso quita el último vértice, Esc cancela, Shift ajusta a ángulos de 15°. Con *Relleno* se rellena;
  sin él, solo el contorno.
- **Modos de fusión** (panel de capas): Normal, Multiplicar, Trama, Superponer, Oscurecer, Aclarar,
  Sobreexponer, Subexponer, Luz fuerte, Luz suave, Diferencia, Exclusión. La hoja cuenta como un fondo
  blanco: por ejemplo *Trama* sobre la capa más baja no se ve. Mientras todas las capas visibles sean
  *Normal* se dibuja en la GPU; si alguna usa otro modo, la pantalla se compone en CPU con el mismo código
  que la exportación, así que lo que ves es lo que exportas.
- **Pinceles de estampado:** Aerógrafo, Tiza, Estrellas, Hojas y Puntos repiten un sello a lo largo del trazo
  (espaciado, dispersión, variación de tamaño y giro, grano). Su azar queda fijado en el trazo
  (semilla): deshacer, rehacer y reabrir dan el mismo dibujo. Con una *Forma* distinta de Libre o con el
  borrador se usa el trazo vectorial normal.

## Guardado automático

*Archivo → Guardado automático* (activo por defecto; intervalo de 30 s a 5 min). **No toca tu archivo**: escribe
una copia de recuperación en la carpeta de datos de la aplicación, solo si hay cambios nuevos y no estás
dibujando. Se borra al guardar, abrir otro dibujo o cerrar con normalidad. Si el programa se cierra mal, al
abrirlo te ofrece recuperarla (queda sin nombre, para no pisar el original por accidente). Si hay dos
instancias abiertas, solo la primera usa la recuperación.

## Exportar

*Archivo → Exportar imagen…* guarda PNG, JPEG o WebP según la extensión o el filtro elegido. JPEG y WebP piden
la calidad (1–100; en WebP, 100 es sin pérdida). WebP necesita el complemento `qwebp` de Qt
(`plugins/imageformats`); si falta, se avisa con un mensaje claro.

## Deshacer

Entran en el historial: trazos, rellenos, pegados, transformaciones, crear/borrar/mover capas, opacidad,
modo de fusión, bloqueo y nombre de capa. Cambiar la visibilidad de una capa no entra, pero sí marca el
archivo como modificado.

Cada operación guarda solo la zona de la capa que tocó (antes y después), así que deshacer y rehacer cuestan lo
mismo con 10 operaciones que con 10.000. El historial tiene un tope de 500 pasos y 512 MB; al pasarlo olvida
los más antiguos.

## Formato de archivo

JSON versión 5: cada capa guarda sus operaciones (trazos, rellenos, pegados y transformaciones) en el orden en
que se hicieron, más su opacidad y modo de fusión. Abre también las versiones 1 a 4. Una versión anterior de la
aplicación no sabrá leer las transformaciones, los polígonos ni los pinceles de estampado.
