#pragma once
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QImageWriter>
#include <QColor>
#include <QTransform>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>
#include <cstring>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "core/documento.h"
#include "core/estampa.h"
#include "core/transformar.h"
#include "puente.h"
#include "archivo.h"

using plz::Capa;      // la interfaz sigue hablando de "Capa" y "Trazo"
using plz::Trazo;

struct Pincel {
    QString nombre;
    double tam;        // multiplica el tamaño elegido
    double presion;    // multiplica el efecto de la presión
    double suavizado;  // se suma al suavizado de la tableta
    double opacidad;   // 0..1
    bool estampado = false;   // true = repite un sello a lo largo del trazo (ver 'est')
    plz::Estampa est;
};

struct OpcionesLapiz {
    bool usarPresion = true;
    double gamma = 1.0;            // curva: presion^gamma (<1 suave, >1 firme)
    double efectoPresion = 0.5;    // 0 = grosor constante
    double suavizado = 0.5;        // 0 = sigue el lápiz exacto
    int botonLapiz = 0;            // 0 nada, 1 borrador, 2 mover lienzo
};

class Canvas : public QOpenGLWidget {
    plz::Documento doc;            // capas, trazos e historial (sin Qt)

    // Copia de cada capa en teselas de TILE×TILE para dibujar en pantalla. Cuando algo cambia solo se
    // vuelven a copiar (y a subir a la GPU) las teselas tocadas, no la capa entera.
    static constexpr int TILE = 256;
    struct Espejo {
        int w = 0, h = 0, tx = 0, ty = 0;
        std::vector<QImage> tiles;
        std::vector<char> sucia;
    };
    std::map<int, Espejo> espejos;

    // Cuando alguna capa visible usa un modo de fusión distinto de Normal, la pantalla se compone en CPU con
    // el mismo código que la exportación (el motor GL de QPainter no garantiza esos modos). Se guarda por
    // teselas y solo se recalculan las tocadas. Con todas las capas en Normal no se usa: se dibuja en GPU.
    struct Compuesto {
        int w = 0, h = 0, tx = 0, ty = 0;
        std::vector<QImage> tiles;
        std::vector<char> sucia;
    };
    Compuesto comp;
    std::vector<double> firmaPila;       // capas, visibilidad, opacidad y modo: si cambia, todo se recompone
    plz::Imagen tesela;                  // espacio de trabajo para una tesela

    Trazo actual;
    QPainterPath contornoVivo;                 // contorno del tramo en curso
    std::vector<plz::Contorno> congelados;     // tramos ya pasados a la capa
    plz::Imagen respaldo;                      // capa antes del tramo vivo (solo al borrar)
    plz::Imagen antesTrazo;                    // capa antes de empezar el trazo (para poder deshacerlo)
    QRect rectVivo;                            // zona de la capa tocada por el borrado en curso
    int idxDibujo = 0;                         // capa donde se dibuja el trazo en curso
    bool dibujando = false;
    std::unique_ptr<plz::Estampador> estampador;   // pinceles de estampado: pone sellos sobre la capa mientras se dibuja

    // Lienzo y vista
    double zoom = 1.0;
    QPointF offset{0, 0};
    bool vistaAuto = true;
    bool paneando = false;
    QPointF panUltimo;
    bool espacio = false;
    QPointF hover;
    bool hoverValido = false;

    QColor colorPincel = Qt::black;
    qreal grosorPincel = 16.0;
    bool modoBorrador = false;
    bool puntaBorrador = false;
    int pincelActual = 0;
    bool modoCuentagotas = false;
    bool modoCubo = false;

    // Selección
    int modoSel = 0;                     // 0 = herramienta apagada, 1 = rectángulo, 2 = lazo
    bool seleccionando = false;
    QPointF selA, selB;
    std::vector<QPointF> selPuntos;
    plz::Seleccion sel;                  // máscara de lo seleccionado
    plz::Contorno selPoly;               // su contorno (en coordenadas del lienzo)
    QElapsedTimer relojBorrado;          // para juntar eventos mientras se borra

    // Polígono en construcción: un clic por vértice; no se pinta hasta cerrarlo
    std::vector<plz::Punto> polyPts;

    // Transformar lo seleccionado (mover, escalar, rotar). Mientras dura la sesión, lo seleccionado está
    // "levantado" de la capa (queda el hueco) y se dibuja flotante encima. Al aplicar se devuelve la capa a
    // su estado original y el core repite la operación: lo guardado y rehecho es idéntico a lo que se ve.
    enum Agarre { SinAgarre, AgMover, AgEscalar, AgRotar };
    bool transformando = false;
    int idxTrans = 0;                    // capa donde se transforma
    plz::Imagen capaAntesTrans;          // la capa antes de levantar
    QImage flotante;                     // lo levantado
    plz::Seleccion selOrig;              // selección al empezar
    plz::Contorno polyOrig;
    QPointF cajaIni;                     // esquina de la caja original (coordenadas del lienzo)
    QSizeF cajaTam;
    double tEscX = 1, tEscY = 1, tAng = 0;
    QPointF tCentro;                     // dónde está ahora el centro de la caja
    Agarre agarre = SinAgarre;
    int agarreHx = 0, agarreHy = 0;      // asa de escala: -1, 0 o 1 en cada eje
    QPointF agarreIni, aCentro0;         // estado al empezar a arrastrar
    double aEscX0 = 1, aEscY0 = 1, aAng0 = 0, agarreAngIni = 0;


public:
    struct OpcionesCubo {
        int tolerancia = 32;        // 0..255
        bool todasCapas = false;    // decidir la zona mirando todas las capas visibles
    };
    OpcionesCubo cubo;
    int forma = plz::FORMA_LIBRE;   // línea, rectángulo o elipse en vez de trazo libre
    bool formaRelleno = false;
    OpcionesLapiz lapiz;
    std::function<void(const QString &)> alCambiarInfo;
    std::function<void(const QString &)> alDatosLapiz;
    std::function<void(const QString &)> alIniciarGL;
    std::function<void()> alCambiarCapas;
    std::function<void(const QColor &)> alElegirColor;
    void setCuentagotas(bool b) {
        cerrarSesiones();
        modoCuentagotas = b;
        setCursor(b ? Qt::PointingHandCursor : Qt::CrossCursor);
        update();
    }

    void setCubo(bool b) {
        cerrarSesiones();
        modoCubo = b;
        setCursor(b ? Qt::PointingHandCursor : Qt::CrossCursor);
        update();
    }

    // ---------- Selección ----------
    void setSeleccion(int modo) { cerrarSesiones(); modoSel = modo; seleccionando = false; update(); }   // la selección actual se conserva
    bool haySeleccion() const { return !sel.vacia(); }
    void deseleccionar() { cerrarSesiones(); sel = plz::Seleccion(); selPoly.clear(); update(); }
    void seleccionarTodo() {
        cerrarSesiones();
        const double W = doc.ancho, H = doc.alto;
        fijarSeleccion(plz::Seleccion::rectangulo(doc.ancho, doc.alto, plz::Rect{0, 0, doc.ancho, doc.alto}),
                       {{0, 0}, {W, 0}, {W, H}, {0, H}});
    }
    // ---------- Transformar lo seleccionado ----------
    bool transformandoAhora() const { return transformando; }
    bool sesionActiva() const { return transformando || !polyPts.empty(); }
    void cancelarSesiones() { cancelarTransformacion(); cancelarPoligono(); }
    void setForma(int f) { cerrarSesiones(); forma = f; update(); }
    std::function<void()> alCambiarSesion;       // empieza o termina una sesión (transformar, polígono...)
    void iniciarTransformacion() {
        if (transformando || dibujando) return;
        if (sel.vacia()) { if (alCambiarInfo) alCambiarInfo("Selecciona algo primero (M) y luego transforma"); return; }
        if (!capaEditable()) return;
        Capa &c = doc.capas[std::size_t(doc.activa)];
        const plz::Imagen im = doc.copiarSeleccion(doc.activa, sel);
        if (im.vacia()) return;
        flotante = puente::lectura(im).copy();           // copia propia: no depende de la memoria de la capa
        capaAntesTrans.copiarDe(c.img);
        selOrig = sel;
        polyOrig = selPoly;
        cajaIni = QPointF(sel.caja.x0, sel.caja.y0);
        cajaTam = QSizeF(sel.caja.w(), sel.caja.h());
        tEscX = tEscY = 1; tAng = 0;
        tCentro = cajaIni + QPointF(cajaTam.width() / 2, cajaTam.height() / 2);
        idxTrans = doc.activa;
        plz::Relleno r;                                  // levantar: dejar el hueco en la capa
        r.borrar = true;
        r.nucleo = sel.tramos();
        plz::aplicarRelleno(c.img, r);
        transformando = true;
        agarre = SinAgarre;
        if (alCambiarSesion) alCambiarSesion();
        if (alCambiarInfo) alCambiarInfo("Transformar: arrastra dentro para mover, las asas escalan (Shift = proporcional), "
                                         "fuera de la caja rota (Shift = 15°). Enter aplica, Esc cancela");
        update();
    }
    void confirmarTransformacion() {
        if (!transformando) return;
        transformando = false;
        Capa &c = doc.capas[std::size_t(idxTrans)];
        c.img.copiarDe(capaAntesTrans);                  // se repite desde el estado original, ya con historial
        const plz::Afin m = afinActual();
        if (doc.transformar(idxTrans, selOrig, m, true)) {
            const plz::Contorno nuevo = plz::transformarContorno(polyOrig, m);
            plz::Seleccion s = plz::Seleccion::poligono(doc.ancho, doc.alto, nuevo);
            if (s.vacia()) deseleccionar(); else fijarSeleccion(std::move(s), nuevo);
        }
        liberarTransformacion();
    }
    void cancelarTransformacion() {
        if (!transformando) return;
        transformando = false;
        doc.capas[std::size_t(idxTrans)].img.copiarDe(capaAntesTrans);
        liberarTransformacion();
    }
    // Cierra cualquier sesión abierta antes de hacer otra cosa (cambiar de capa, guardar, exportar...)
    void cerrarSesiones() { confirmarTransformacion(); terminarPoligono(); }

    void borrarSel() {
        cerrarSesiones();
        if (sel.vacia() || !capaEditable()) return;
        if (doc.borrarSeleccion(doc.activa, sel)) { info(); update(); }
    }
    void copiar(bool cortar) {
        cerrarSesiones();
        if (sel.vacia()) return;
        const plz::Imagen im = doc.copiarSeleccion(doc.activa, sel);
        if (im.vacia()) return;
        QGuiApplication::clipboard()->setImage(puente::lectura(im).convertToFormat(QImage::Format_ARGB32));
        if (cortar) borrarSel();
    }
    void pegar() {
        cerrarSesiones();
        const QImage q = QGuiApplication::clipboard()->image();
        if (q.isNull()) { if (alCambiarInfo) alCambiarInfo("No hay ninguna imagen copiada"); return; }
        if (!capaEditable()) return;
        plz::Imagen im = puente::aImagen(q);
        const int w = im.w, h = im.h;
        const QPointF c = aDoc(hoverValido ? hover : QPointF(width() / 2.0, height() / 2.0));   // centrada donde está el cursor
        const int x = int(std::lround(c.x() - w / 2.0)), y = int(std::lround(c.y() - h / 2.0));
        if (!doc.pegar(doc.activa, std::move(im), x, y)) return;
        fijarSeleccion(plz::Seleccion::rectangulo(doc.ancho, doc.alto, plz::Rect{x, y, x + w, y + h}),
                       {{double(x), double(y)}, {double(x + w), double(y)}, {double(x + w), double(y + h)}, {double(x), double(y + h)}});
        info();
    }

    Canvas() {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        // Así se rasteriza un trazo (el core no sabe de Qt)
        doc.pintor = [](plz::Imagen &im, const Trazo &t) {
            puente::pintarContornos(im, t.contornos, QColor::fromRgba(t.color), t.borrar, &t.recorte);
        };
    }

    QColor color() const { return colorPincel; }
    void setColor(const QColor &c) { colorPincel = c; }
    void setGrosor(qreal g) { grosorPincel = g; }
    void setBorrador(bool b) { modoBorrador = b; }
    static const std::vector<Pincel> &pinceles() {
        static const std::vector<Pincel> v = {
                                               {"Tinta",      1.0, 1.0, 0.0, 1.00},
                                               {"Lápiz",      0.6, 0.6, 0.0, 0.85},
                                               {"Marcador",   1.6, 0.0, 0.1, 0.45},
                                               {"Plumilla",   0.8, 1.6, 0.2, 1.00},
                                               {"Rotulador",  1.0, 0.0, 0.4, 1.00},
                                               // Estampado: tamaño, presión, suavizado, opacidad, sí, {punta, espaciado, dispersión,
                                               // varía tamaño, giro, sigue el trazo, varía giro, flujo, presión, grano}
                                               {"Aerógrafo",  2.0, 0.6, 0.0, 1.00, true, {plz::PUNTA_SUAVE,    0.08, 0.00, 0.0, 0, false,   0, 0.12, 0.6, 0.00}},
                                               {"Tiza",       1.2, 0.4, 0.0, 1.00, true, {plz::PUNTA_REDONDA,  0.12, 0.05, 0.2, 0, false,   0, 0.85, 0.5, 0.30}},
                                               {"Estrellas",  1.0, 0.3, 0.0, 1.00, true, {plz::PUNTA_ESTRELLA, 1.20, 0.30, 0.5, 0, false, 180, 1.00, 0.3, 0.00}},
                                               {"Hojas",      1.4, 0.3, 0.0, 1.00, true, {plz::PUNTA_HOJA,     0.90, 0.40, 0.4, 0, true,   40, 0.95, 0.3, 0.00}},
                                               {"Puntos",     0.8, 0.3, 0.0, 1.00, true, {plz::PUNTA_REDONDA,  1.50, 0.60, 0.7, 0, false,   0, 1.00, 0.3, 0.00}},
                                               };
        return v;
    }
    void setPincel(int i) {
        if (i >= 0 && i < int(pinceles().size())) pincelActual = i;
        update();
    }
    bool modificado() const { return doc.modificado(); }
    std::uint64_t revision() const { return doc.revision(); }
    void marcarModificado() { doc.marcarModificado(); info(); }
    // Dibujando, arrastrando una selección o moviendo la vista: no es buen momento para guardar copias
    bool ocupado() const { return dibujando || transformando || seleccionando || paneando; }
    // Guarda el dibujo en otro archivo SIN darlo por guardado (el título y los avisos de cambios no se tocan).
    // Una transformación a medias no está aún en el documento: se guarda lo ya aplicado.
    bool guardarCopia(const QString &ruta) const { return archivo::guardar(doc, ruta); }
    int ancho() const { return doc.ancho; }
    int alto() const { return doc.alto; }

    // ---------- Capas (crear, borrar y mover entran en el historial) ----------
    int numCapas() const { return doc.numCapas(); }
    const Capa &capa(int i) const { return doc.capas[std::size_t(i)]; }
    int indiceActivo() const { return doc.activa; }
    void setActiva(int i) {
        if (dibujando || !doc.indiceValido(i)) return;
        cerrarSesiones();
        doc.activa = i;
        info();
    }
    void nuevaCapa() {
        if (dibujando) return;
        cerrarSesiones();
        doc.nuevaCapa();
        cambiaronCapas();
    }
    void borrarCapa(int i) {
        if (dibujando) return;
        cerrarSesiones();
        if (doc.borrarCapa(i)) cambiaronCapas();
    }
    void moverCapa(int i, int delta) {
        if (dibujando) return;
        cerrarSesiones();
        if (doc.moverCapa(i, delta)) cambiaronCapas();
    }
    // Inserta una imagen como capa de guía: bloqueada, semitransparente y debajo de todo
    void cargarPlantilla(const QImage &src, const QString &nombre) {
        if (dibujando || src.isNull()) return;
        cerrarSesiones();
        Capa c(doc.siguienteId++, nombre.toStdString(), doc.ancho, doc.alto);
        c.esPlantilla = true;
        c.bloqueada = true;
        c.opacidad = 0.6;
        const QSizeF dest = QSizeF(src.size()).scaled(doc.ancho, doc.alto, Qt::KeepAspectRatio);
        const QRectF r((doc.ancho - dest.width()) / 2, (doc.alto - dest.height()) / 2,
                       dest.width(), dest.height());
        puente::dibujarImagen(c.img, src, r, true);
        c.base = c.img;
        doc.insertarPlantilla(std::move(c));
        cambiaronCapas();
    }
    // Estos no avisan a la lista (evita refrescarla mientras se edita).
    // La visibilidad no entra en el historial; bloqueo, opacidad y nombre sí.
    void setVisible(int i, bool v) {
        if (!doc.indiceValido(i)) return;
        doc.setVisible(i, v);
        info(); update();
    }
    void setBloqueada(int i, bool b) { cambiarProp(i, [&](plz::PropCapa &p) { p.bloqueada = b; }, "bloqueo"); }
    void setOpacidad(int i, double o) { cambiarProp(i, [&](plz::PropCapa &p) { p.opacidad = o; }, "opacidad"); }
    void setFusion(int i, plz::Fusion f) { cambiarProp(i, [&](plz::PropCapa &p) { p.fusion = f; }, "fusion"); }
    void setNombre(int i, const QString &n) { cambiarProp(i, [&](plz::PropCapa &p) { p.nombre = n.toStdString(); }, "nombre"); }

    // ---------- Vista ----------
    void ajustar() {
        const double m = 30;
        const double zx = (width() - 2 * m) / doc.ancho;
        const double zy = (height() - 2 * m) / doc.alto;
        zoom = std::clamp(std::min(zx, zy), 0.05, 32.0);
        offset = QPointF((width() - doc.ancho * zoom) / 2, (height() - doc.alto * zoom) / 2);
        vistaAuto = true;
        info();
        update();
    }
    void establecerZoom(double z, const QPointF &ancla) {
        z = std::clamp(z, 0.05, 32.0);
        const QPointF d = (ancla - offset) / zoom;
        zoom = z;
        offset = ancla - d * zoom;
        vistaAuto = false;
        info();
        update();
    }
    QPointF centro() const { return QPointF(width() / 2.0, height() / 2.0); }
    QPointF pantallaDe(const QPointF &docPunto) const { return aPantalla(docPunto); }   // lienzo -> widget (para pruebas)
    void acercar() { establecerZoom(zoom * 1.25, centro()); }
    void alejar()  { establecerZoom(zoom / 1.25, centro()); }
    void zoom100() { establecerZoom(1.0, centro()); }

    // ---------- Edición ----------
    void deshacer() {
        if (dibujando) return;
        if (sesionActiva()) { cancelarSesiones(); return; }   // deshacer a mitad de una transformación o un polígono la cancela
        if (doc.deshacer()) cambiaronCapas();
    }
    void rehacer() {
        if (dibujando) return;
        if (sesionActiva()) { cancelarSesiones(); return; }
        if (doc.rehacer()) cambiaronCapas();
    }
    void nuevoLienzo(int w, int h) {
        if (dibujando) return;
        cancelarSesiones();
        doc.reiniciar(w, h);
        deseleccionar();                     // la selección era del dibujo anterior
        espejos.clear(); comp = Compuesto(); firmaPila.clear();
        cambiaronCapas();
        ajustar();
    }

    // ---------- Guardar / abrir / exportar ----------
    bool guardar(const QString &ruta) {
        confirmarTransformacion();         // el polígono a medias no está en el documento: se deja como está
        if (!archivo::guardar(doc, ruta)) return false;
        doc.marcarGuardado();
        info();
        return true;
    }
    bool abrir(const QString &ruta) {
        if (dibujando) return false;
        cancelarSesiones();
        if (!archivo::abrir(doc, ruta)) return false;
        deseleccionar();
        espejos.clear(); comp = Compuesto(); firmaPila.clear();
        cambiaronCapas();
        ajustar();
        return true;
    }
    // Exporta la imagen final. formato: "png", "jpg" o "webp". calidad 1..100 (no afecta al PNG; en WebP,
    // 100 = sin pérdida). Si falla, 'error' dice por qué.
    bool exportar(const QString &ruta, const QString &formato, int calidad, bool conPlantilla, QString *error = nullptr) {
        confirmarTransformacion();
        const QString f = formato.toLower();
       const QByteArray nombre = (f == "jpg" || f == "jpeg") ? QByteArray("jpeg") : f.toLatin1();
        if (!QImageWriter::supportedImageFormats().contains(nombre)) {
            if (error) *error = QString("Esta instalación de Qt no incluye el formato %1 (falta su complemento en "
                                        "plugins/imageformats).").arg(f.toUpper());
            return false;
        }
        const plz::Imagen im = doc.componer(conPlantilla);          // siempre opaca: la hoja es blanca
        QImage img = puente::lectura(im);
        if (nombre != "png") img = img.convertToFormat(QImage::Format_RGB32);   // JPEG no tiene transparencia
        QImageWriter w(ruta, nombre);
        if (nombre != "png") w.setQuality(std::clamp(calidad, 1, 100));
        if (!w.write(img)) {
            if (error) *error = w.errorString();
            return false;
        }
        return true;
    }
    bool exportarPNG(const QString &ruta, bool conPlantilla = false) { return exportar(ruta, "png", 100, conPlantilla); }
protected:
    // ---------- OpenGL ----------
    void initializeGL() override {
        QOpenGLFunctions *f = context()->functions();
        const char *r = reinterpret_cast<const char *>(f->glGetString(GL_RENDERER));
        const char *v = reinterpret_cast<const char *>(f->glGetString(GL_VERSION));
        if (alIniciarGL)
            alIniciarGL(QString("GPU: %1 (OpenGL %2)")
                            .arg(QString::fromLatin1(r ? r : "desconocida"),
                                 QString::fromLatin1(v ? v : "?")));
    }

    void paintGL() override {
        QPainter p(this);
        p.fillRect(rect(), QColor(90, 90, 90));
        p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 2.0);
        p.setTransform(vista());
        p.fillRect(QRectF(0, 0, doc.ancho, doc.alto), Qt::white);
        p.setClipRect(QRectF(0, 0, doc.ancho, doc.alto));

        const QRectF visible = vista().inverted().mapRect(QRectF(rect()));
        sincronizarSucios();
        const bool compuesta = hayFusion();
        if (compuesta) { p.setOpacity(1.0); dibujarCompuesto(p, visible); }
        for (int i = 0; i < numCapas(); ++i) {
            Capa &c = doc.capas[std::size_t(i)];
            if (!c.visible) continue;
            p.setOpacity(c.opacidad);
            if (!compuesta) dibujarCapa(p, c, visible);
            if (transformando && i == idxTrans) dibujarFlotante(p);
            // al pintar se ve el trazo vivo encima; al borrar ya está aplicado en la imagen
            if (dibujando && i == idxDibujo && !actual.borrar && !actual.estampado) {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor::fromRgba(actual.color));
                p.save();
                if (!actual.recorte.empty()) p.setClipPath(puente::aPoligono(actual.recorte), Qt::IntersectClip);
                p.drawPath(contornoVivo);
                p.restore();
            }
        }

        if (!polyPts.empty()) dibujarPoligonoVivo(p);

        // Contorno de la selección ("hormigas"): línea negra con trazos blancos encima
        if (seleccionando || !selPoly.empty()) {
            QPainterPath cont;
            if (seleccionando && modoSel == 1) cont.addRect(QRectF(selA, selB).normalized());
            else if (seleccionando) { QPolygonF pg; for (const QPointF &q : selPuntos) pg << q; cont.addPolygon(pg); }
            else cont = puente::aPoligono(transformando ? plz::transformarContorno(selPoly, afinActual()) : selPoly);
            p.setOpacity(1.0);
            p.setRenderHint(QPainter::Antialiasing, false);
            QPen negro(Qt::black, 1); negro.setCosmetic(true);
            QPen blanco(Qt::white, 1, Qt::DashLine); blanco.setCosmetic(true);
            p.setBrush(Qt::NoBrush);
            p.setPen(negro); p.drawPath(cont);
            p.setPen(blanco); p.drawPath(cont);
        }

        // Círculo de vista previa del pincel (en coordenadas de pantalla)
        p.resetTransform();
        p.setClipping(false);
        p.setOpacity(1.0);
        if (transformando) dibujarAsas(p);
        if (!polyPts.empty()) dibujarVerticesPoligono(p);
        if (hoverValido && !paneando && !espacio && !modoCuentagotas && !modoCubo && !modoSel && !transformando) {
            p.setRenderHint(QPainter::Antialiasing, true);
            const qreal r = grosorActual() * zoom / 2.0;
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 200), 3));
            p.drawEllipse(hover, r, r);
            p.setPen(QPen(QColor(0, 0, 0, 200), 1));
            p.drawEllipse(hover, r, r);
        }
    }

    void resizeEvent(QResizeEvent *e) override {
        QOpenGLWidget::resizeEvent(e);    // imprescindible en QOpenGLWidget
        if (vistaAuto) ajustar();
    }

    // ---------- Lápiz ----------
    void tabletEvent(QTabletEvent *e) override {
        const QPointF p = e->position();
        hover = p; hoverValido = true;
        const bool barril = e->buttons() & (Qt::RightButton | Qt::MiddleButton);

        switch (e->type()) {
        case QEvent::TabletPress:
            setFocus();
            if (modoCuentagotas || (e->modifiers() & Qt::AltModifier)) {
                tomarColor(aDoc(p));
                break;
            }
            if (espacio || (barril && lapiz.botonLapiz == 2)) {
                iniciarPan(p);
            } else if (transformando) {
                agarrar(p);
            } else if (modoSel) {
                iniciarSel(aDoc(p));
            } else if (modoCubo) {
                rellenarEn(aDoc(p));
            } else {
                puntaBorrador = (e->pointerType() == QPointingDevice::PointerType::Eraser)
                || (barril && lapiz.botonLapiz == 1);
                if (forma == plz::FORMA_POLIGONO && !modoBorrador && !puntaBorrador) clicPoligono(aDoc(p));
                else empezar(aDoc(p), aplicarPresion(e->pressure()), false);
            }
            break;
        case QEvent::TabletMove:
            if (paneando) moverPan(p);
            else if (transformando) arrastrar(aDoc(p));
            else if (seleccionando) moverSel(aDoc(p));
            else if (dibujando) agregar(aDoc(p), aplicarPresion(e->pressure()));
            break;
        case QEvent::TabletRelease:
            if (paneando) terminarPan();
            else if (transformando) agarre = SinAgarre;
            else { terminarSel(); terminar(); }
            puntaBorrador = false;
            break;
        default: break;
        }

        if (alDatosLapiz) {
            QString disp = "otro";
            if (e->pointerType() == QPointingDevice::PointerType::Pen) disp = "lápiz";
            else if (e->pointerType() == QPointingDevice::PointerType::Eraser) disp = "borrador";
            QString b;
            if (e->buttons() & Qt::LeftButton) b += "punta ";
            if (e->buttons() & Qt::RightButton) b += "botón1 ";
            if (e->buttons() & Qt::MiddleButton) b += "botón2 ";
            alDatosLapiz(QString("%1 | presión %2 | tilt %3,%4 | giro %5° | botones: %6")
                             .arg(disp)
                             .arg(e->pressure(), 0, 'f', 2)
                             .arg(e->xTilt(), 0, 'f', 0).arg(e->yTilt(), 0, 'f', 0)
                             .arg(e->rotation(), 0, 'f', 0)
                             .arg(b.isEmpty() ? QString("-") : b.trimmed()));
        }
        update();
        e->accept();
    }

    // ---------- Mouse ----------
    void mousePressEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        setFocus();
        if (e->button() == Qt::LeftButton &&
            (modoCuentagotas || (e->modifiers() & Qt::AltModifier))) {
            tomarColor(aDoc(e->position()));
            return;
        }
        if (e->button() == Qt::MiddleButton ||
            (e->button() == Qt::LeftButton && espacio)) {
            iniciarPan(e->position());
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        if (transformando) { agarrar(e->position()); return; }
        if (modoSel) { iniciarSel(aDoc(e->position())); return; }
        if (modoCubo) { rellenarEn(aDoc(e->position())); return; }
        if (forma == plz::FORMA_POLIGONO && !modoBorrador) { clicPoligono(aDoc(e->position())); return; }
        empezar(aDoc(e->position()), 0.5, true);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (e->button() == Qt::LeftButton && !polyPts.empty()) { terminarPoligono(); return; }   // doble clic cierra
        QOpenGLWidget::mouseDoubleClickEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        hover = e->position(); hoverValido = true;
        if (paneando) moverPan(e->position());
        else if (transformando) arrastrar(aDoc(e->position()));
        else if (seleccionando) moverSel(aDoc(e->position()));
        else if (dibujando) agregar(aDoc(e->position()), 0.5);
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        if (paneando && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
            terminarPan();
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        if (transformando) { agarre = SinAgarre; return; }
        terminarSel();
        terminar();
    }
    void wheelEvent(QWheelEvent *e) override {
        const int d = e->angleDelta().y();
        if (d == 0) return;
        establecerZoom(zoom * std::pow(1.0015, d), e->position());
        e->accept();
    }
    void leaveEvent(QEvent *) override { hoverValido = false; update(); }

    // ---------- Teclado (barra espaciadora = mover) ----------
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Space) {
            if (!e->isAutoRepeat()) { espacio = true; if (!paneando) setCursor(Qt::OpenHandCursor); }
            return;
        }
        if (transformando) {
            const int paso = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
            switch (e->key()) {
            case Qt::Key_Return: case Qt::Key_Enter: confirmarTransformacion(); return;
            case Qt::Key_Escape: cancelarTransformacion(); return;
            case Qt::Key_Left:  tCentro += QPointF(-paso, 0); update(); return;
            case Qt::Key_Right: tCentro += QPointF(paso, 0);  update(); return;
            case Qt::Key_Up:    tCentro += QPointF(0, -paso); update(); return;
            case Qt::Key_Down:  tCentro += QPointF(0, paso);  update(); return;
            default: break;
            }
        }
        if (!polyPts.empty()) {
            switch (e->key()) {
            case Qt::Key_Return: case Qt::Key_Enter: terminarPoligono(); return;
            case Qt::Key_Escape: cancelarPoligono(); return;
            case Qt::Key_Backspace:                        // quita el último vértice
                polyPts.pop_back();
                if (polyPts.empty() && alCambiarSesion) alCambiarSesion();
                update();
                return;
            default: break;
            }
        }
        QOpenGLWidget::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Space) {
            if (!e->isAutoRepeat()) { espacio = false; if (!paneando) setCursor(Qt::CrossCursor); }
            return;
        }
        QOpenGLWidget::keyReleaseEvent(e);
    }
    void focusOutEvent(QFocusEvent *e) override {
        QOpenGLWidget::focusOutEvent(e);
        espacio = false;
        if (!paneando) setCursor(Qt::CrossCursor);
    }

private:

    bool ok(int i) const { return doc.indiceValido(i); }

    // ----- Polígono -----
    static bool conShift() { return QGuiApplication::keyboardModifiers() & Qt::ShiftModifier; }
    // Vértices actuales + el punto bajo el cursor (con Shift, a múltiplos de 15° respecto al último vértice)
    std::vector<plz::Punto> poligonoConCursor() const {
        std::vector<plz::Punto> pts = polyPts;
        if (hoverValido && !pts.empty()) {
            const QPointF h = aDoc(hover);
            plz::Punto q{h.x(), h.y(), 0.5};
            if (conShift()) q = plz::restringirForma(plz::FORMA_POLIGONO, pts.back(), q);
            pts.push_back(q);
        }
        return pts;
    }
    void clicPoligono(const QPointF &d) {
        if (dibujando) return;
        if (polyPts.empty() && !capaEditable()) return;
        plz::Punto q{d.x(), d.y(), 0.5};
        if (!polyPts.empty()) {
            if (conShift()) q = plz::restringirForma(plz::FORMA_POLIGONO, polyPts.back(), q);
            const QPointF primero(polyPts.front().x, polyPts.front().y);
            if (polyPts.size() >= 3 && QLineF(aPantalla(primero), aPantalla(QPointF(q.x, q.y))).length() <= 10.0) {
                terminarPoligono();                       // clic sobre el primer vértice: cierra
                return;
            }
            const plz::Punto &u = polyPts.back();
            if (std::hypot(q.x - u.x, q.y - u.y) * zoom < 2.0) return;     // mismo sitio (p. ej. el doble clic)
        }
        polyPts.push_back(q);
        if (polyPts.size() == 1) {
            if (alCambiarSesion) alCambiarSesion();
            if (alCambiarInfo) alCambiarInfo("Polígono: un clic por vértice. Clic en el primero, doble clic o Enter lo cierra; "
                                             "Retroceso quita el último; Esc cancela; Shift = ángulos de 15°");
        }
        update();
    }
    void cancelarPoligono() {
        if (polyPts.empty()) return;
        polyPts.clear();
        if (alCambiarSesion) alCambiarSesion();
        info();
        update();
    }
    // Cierra el polígono y lo pinta como un trazo más (con 3 o más vértices; con menos se descarta)
    void terminarPoligono() {
        if (polyPts.empty()) return;
        if (polyPts.size() < 3 || !capaEditable()) { cancelarPoligono(); return; }
        Capa &c = doc.capas[std::size_t(doc.activa)];
        Trazo t;
        t.forma = plz::FORMA_POLIGONO;
        t.relleno = formaRelleno;
        t.recorte = selPoly;
        QColor col = colorPincel;
        col.setAlphaF(pinceles()[pincelActual].opacidad);
        t.color = col.rgba();
        t.grosor = grosorActual();
        t.completo = polyPts;
        t.puntos = polyPts;
        t.contornos = plz::contornosDeForma(t);
        polyPts.clear();
        if (!t.contornos.empty()) {
            const plz::Imagen antes = c.img;                // la capa tal como estaba, para poder deshacer
            puente::pintarContornos(c.img, t.contornos, QColor::fromRgba(t.color), false, &t.recorte);
            doc.registrarTrazo(c.id, std::move(t), antes);
        }
        if (alCambiarSesion) alCambiarSesion();
        info();
        update();
    }
    void dibujarPoligonoVivo(QPainter &p) {
        const std::vector<plz::Punto> pts = poligonoConCursor();
        Trazo t;
        t.forma = plz::FORMA_POLIGONO;
        t.relleno = formaRelleno;
        t.grosor = grosorActual();
        t.completo = pts;
        const std::vector<plz::Contorno> cont = plz::contornosDeForma(t);
        p.save();
        p.setOpacity(1.0);
        if (!cont.empty()) {                                // lo que quedaría pintado
            QColor col = colorPincel;
            col.setAlphaF(pinceles()[pincelActual].opacidad * 0.8);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            if (!selPoly.empty()) p.setClipPath(puente::aPoligono(selPoly), Qt::IntersectClip);
            p.drawPath(puente::aPathMulti(cont));
        }
        p.restore();
        // guía fina: la línea que une los vértices y, discontinua, la que cerraría la figura
        QPen negro(Qt::black, 1); negro.setCosmetic(true);
        QPen blanco(Qt::white, 1, Qt::DashLine); blanco.setCosmetic(true);
        QPolygonF linea;
        for (const plz::Punto &q : pts) linea << QPointF(q.x, q.y);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        for (const QPen &pen : {negro, blanco}) {
            p.setPen(pen);
            p.drawPolyline(linea);
            if (pts.size() >= 3) p.drawLine(linea.last(), linea.first());
        }
        p.restore();
    }
    void dibujarVerticesPoligono(QPainter &p) {            // cuadritos en pantalla; el primero resalta cuando ya se puede cerrar
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(Qt::black, 1));
        for (std::size_t i = 0; i < polyPts.size(); ++i) {
            const QPointF s = aPantalla(QPointF(polyPts[i].x, polyPts[i].y));
            const bool cierra = i == 0 && polyPts.size() >= 3;
            p.setBrush(cierra ? QColor(80, 200, 120) : QColor(Qt::white));
            const double r = cierra ? 5.5 : 3.5;
            p.drawRect(QRectF(s.x() - r, s.y() - r, 2 * r, 2 * r));
        }
    }

    // ----- Transformar: geometría -----
    void liberarTransformacion() {
        capaAntesTrans = plz::Imagen();
        flotante = QImage();
        selOrig = plz::Seleccion();
        polyOrig.clear();
        agarre = SinAgarre;
        if (alCambiarSesion) alCambiarSesion();
        info();
        update();
    }
    // La matriz actual. Un movimiento puro se redondea a pixeles enteros para no emborronar la imagen.
    plz::Afin afinActual() const {
        const QPointF o = cajaIni + QPointF(cajaTam.width() / 2, cajaTam.height() / 2);
        QPointF c = tCentro;
        if (std::fabs(tEscX - 1) < 1e-9 && std::fabs(tEscY - 1) < 1e-9 && std::fabs(tAng) < 1e-9)
            c = o + QPointF(std::round(c.x() - o.x()), std::round(c.y() - o.y()));
        return plz::Afin::desde(o.x(), o.y(), tEscX, tEscY, tAng, c.x(), c.y());
    }
    // Punto de la caja (lx, ly en -1..1, 0 = centro) ya transformado, en coordenadas del lienzo
    QPointF puntoCaja(double lx, double ly) const {
        const QPointF o = cajaIni + QPointF(cajaTam.width() / 2, cajaTam.height() / 2);
        const pf::Vec v = afinActual().aplicar(o.x() + lx * cajaTam.width() / 2, o.y() + ly * cajaTam.height() / 2);
        return QPointF(v.x, v.y);
    }
    QPointF aPantalla(const QPointF &d) const { return d * zoom + offset; }
    static constexpr int ASA_X[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    static constexpr int ASA_Y[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

    void agarrar(const QPointF &pant) {
        const QPointF d = aDoc(pant);
        agarre = AgRotar;
        for (int i = 0; i < 8; ++i)
            if (QLineF(aPantalla(puntoCaja(ASA_X[i], ASA_Y[i])), pant).length() <= 9.0) {
                agarre = AgEscalar; agarreHx = ASA_X[i]; agarreHy = ASA_Y[i];
                break;
            }
        if (agarre == AgRotar) {                          // dentro de la caja = mover; fuera = rotar
            const pf::Vec u = afinActual().inversa().aplicar(d.x(), d.y());
            if (u.x >= cajaIni.x() && u.x <= cajaIni.x() + cajaTam.width() &&
                u.y >= cajaIni.y() && u.y <= cajaIni.y() + cajaTam.height()) agarre = AgMover;
        }
        agarreIni = d; aCentro0 = tCentro;
        aEscX0 = tEscX; aEscY0 = tEscY; aAng0 = tAng;
        agarreAngIni = std::atan2(d.y() - tCentro.y(), d.x() - tCentro.x());
    }
    void arrastrar(const QPointF &d) {
        if (agarre == SinAgarre) return;
        const bool shift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
        constexpr double PI = 3.14159265358979323846;
        if (agarre == AgMover) {
            tCentro = aCentro0 + (d - agarreIni);
        } else if (agarre == AgRotar) {
            double a = aAng0 + (std::atan2(d.y() - aCentro0.y(), d.x() - aCentro0.x()) - agarreAngIni);
            if (shift) a = std::round(a / (PI / 12)) * (PI / 12);
            tAng = a;
        } else {
            // Se escala desde el asa opuesta, que se queda quieta. Se trabaja en los ejes girados de la caja.
            const double hw = cajaTam.width() / 2, hh = cajaTam.height() / 2;
            const double co = std::cos(aAng0), si = std::sin(aAng0);
            auto aDocDesdeLocal = [&](const QPointF &centro, double ex, double ey, double lx, double ly) {
                const double x = lx * ex, y = ly * ey;
                return centro + QPointF(co * x - si * y, si * x + co * y);
            };
            const QPointF ancla = aDocDesdeLocal(aCentro0, aEscX0, aEscY0, -agarreHx * hw, -agarreHy * hh);
            const QPointF dv = d - ancla;
            const double vx = co * dv.x() + si * dv.y(), vy = -si * dv.x() + co * dv.y();
            double sx = agarreHx ? vx / (2 * agarreHx * hw) : aEscX0;
            double sy = agarreHy ? vy / (2 * agarreHy * hh) : aEscY0;
            if (shift && agarreHx && agarreHy) {          // esquina con Shift: mismas proporciones
                const double rx = sx / aEscX0, ry = sy / aEscY0;
                const double r = std::max(std::fabs(rx), std::fabs(ry));
                sx = aEscX0 * r * (rx < 0 ? -1 : 1);
                sy = aEscY0 * r * (ry < 0 ? -1 : 1);
            }
            auto minimo = [](double s) { return std::fabs(s) < 0.02 ? (s < 0 ? -0.02 : 0.02) : s; };
            tEscX = minimo(sx); tEscY = minimo(sy);
            const double ax = -agarreHx * hw * tEscX, ay = -agarreHy * hh * tEscY;
            tCentro = ancla - QPointF(co * ax - si * ay, si * ax + co * ay);
        }
        update();
    }
    void dibujarFlotante(QPainter &p) {
        const plz::Afin m = afinActual();
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(QTransform(m.a, m.b, m.c, m.d, m.tx, m.ty) * vista());   // el recorte al lienzo ya está puesto
        p.drawImage(cajaIni, flotante);
        p.restore();
    }
    // Caja y asas en pantalla (tamaño fijo, sin importar el zoom)
    void dibujarAsas(QPainter &p) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPolygonF caja;
        for (int i = 0; i < 8; i += 2) caja << aPantalla(puntoCaja(ASA_X[i], ASA_Y[i]));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 220), 3));
        p.drawPolygon(caja);
        p.setPen(QPen(QColor(30, 120, 255), 1));
        p.drawPolygon(caja);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(Qt::white);
        for (int i = 0; i < 8; ++i) {
            const QPointF c = aPantalla(puntoCaja(ASA_X[i], ASA_Y[i]));
            p.drawRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9, 9));
        }
    }

    template <class F>
    void cambiarProp(int i, F cambio, const char *clave) {
        if (!ok(i)) return;
        const Capa &c = doc.capas[std::size_t(i)];
        plz::PropCapa p{c.nombre, c.bloqueada, c.opacidad, c.fusion};
        cambio(p);
        if (doc.cambiarPropiedades(i, p, clave)) { info(); update(); }
    }
    void cambiaronCapas() {
        for (auto it = espejos.begin(); it != espejos.end();)       // olvida las capas que ya no existen
            it = doc.porId(it->first) ? std::next(it) : espejos.erase(it);
        if (alCambiarCapas) alCambiarCapas();
        info();
        update();
    }
    // Marca las teselas tocadas desde el último dibujo (o crea el espejo si no existe)
    // 'cambio' (opcional) recibe la zona de la capa que cambió desde la última vez
    Espejo &espejoDe(Capa &c, plz::Rect *cambio = nullptr) {
        Espejo &e = espejos[c.id];
        if (e.w != c.img.w || e.h != c.img.h) {
            e.w = c.img.w; e.h = c.img.h;
            e.tx = (e.w + TILE - 1) / TILE; e.ty = (e.h + TILE - 1) / TILE;
            e.tiles.assign(std::size_t(e.tx) * std::size_t(e.ty), QImage());
            e.sucia.assign(e.tiles.size(), 1);
            c.img.consumirSucio();
            if (cambio) *cambio = c.img.rectTotal();
            return e;
        }
        const plz::Rect s = c.img.consumirSucio();
        if (cambio) *cambio = s;
        if (!s.vacio())
            for (int ty = s.y0 / TILE; ty <= (s.y1 - 1) / TILE && ty < e.ty; ++ty)
                for (int tx = s.x0 / TILE; tx <= (s.x1 - 1) / TILE && tx < e.tx; ++tx)
                    e.sucia[std::size_t(ty) * std::size_t(e.tx) + std::size_t(tx)] = 1;
        return e;
    }
    void refrescarTesela(Espejo &e, const Capa &c, int tx, int ty) {
        const std::size_t i = std::size_t(ty) * std::size_t(e.tx) + std::size_t(tx);
        const int x0 = tx * TILE, y0 = ty * TILE;
        const int w = std::min(TILE, e.w - x0), h = std::min(TILE, e.h - y0);
        QImage &t = e.tiles[i];
        if (t.isNull() || t.width() != w || t.height() != h)
            t = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        uchar *dst = t.bits();                  // acceso de escritura: Qt genera una clave de textura nueva
        const int paso = t.bytesPerLine();
        for (int y = 0; y < h; ++y)
            std::memcpy(dst + std::ptrdiff_t(y) * paso,
                        &c.img.px[std::size_t(y0 + y) * std::size_t(e.w) + std::size_t(x0)],
                        std::size_t(w) * sizeof(plz::Pixel));
        e.sucia[i] = 0;
    }
    // Dibuja solo las teselas que se ven. Sin antialiasing: así las teselas vecinas encajan sin rendijas.
    void dibujarCapa(QPainter &p, Capa &c, const QRectF &visible) {
        Espejo &e = espejoDe(c);
        const int tx0 = std::max(0, int(std::floor(visible.left() / TILE)));
        const int ty0 = std::max(0, int(std::floor(visible.top() / TILE)));
        const int tx1 = std::min(e.tx - 1, int(std::floor(visible.right() / TILE)));
        const int ty1 = std::min(e.ty - 1, int(std::floor(visible.bottom() / TILE)));
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx) {
                const std::size_t i = std::size_t(ty) * std::size_t(e.tx) + std::size_t(tx);
                if (e.sucia[i] || e.tiles[i].isNull()) refrescarTesela(e, c, tx, ty);
                p.drawImage(tx * TILE, ty * TILE, e.tiles[i]);
            }
    }

    // ----- Pantalla compuesta en CPU (solo con modos de fusión) -----
    bool hayFusion() const {
        for (const Capa &c : doc.capas) if (c.visible && c.fusion != plz::Fusion::Normal) return true;
        return false;
    }
    // Pone al día los dos cachés (por capa y compuesto) con lo que cambió desde el último dibujo
    void sincronizarSucios() {
        if (comp.w != doc.ancho || comp.h != doc.alto) {
            comp.w = doc.ancho; comp.h = doc.alto;
            comp.tx = (comp.w + TILE - 1) / TILE; comp.ty = (comp.h + TILE - 1) / TILE;
            comp.tiles.assign(std::size_t(comp.tx) * std::size_t(comp.ty), QImage());
            comp.sucia.assign(comp.tiles.size(), 1);
        }
        std::vector<double> firma;
        for (const Capa &c : doc.capas) {
            firma.push_back(c.id); firma.push_back(c.visible ? 1 : 0);
            firma.push_back(c.opacidad); firma.push_back(int(c.fusion));
        }
        if (firma != firmaPila) {
            firmaPila = std::move(firma);
            std::fill(comp.sucia.begin(), comp.sucia.end(), char(1));
        }
        for (Capa &c : doc.capas) {
            if (!c.visible) continue;
            plz::Rect s;
            espejoDe(c, &s);
            if (s.vacio()) continue;
            for (int ty = s.y0 / TILE; ty <= (s.y1 - 1) / TILE && ty < comp.ty; ++ty)
                for (int tx = s.x0 / TILE; tx <= (s.x1 - 1) / TILE && tx < comp.tx; ++tx)
                    comp.sucia[std::size_t(ty) * std::size_t(comp.tx) + std::size_t(tx)] = 1;
        }
    }
    void refrescarCompuesto(int tx, int ty) {
        const std::size_t i = std::size_t(ty) * std::size_t(comp.tx) + std::size_t(tx);
        const int x0 = tx * TILE, y0 = ty * TILE;
        const int w = std::min(TILE, comp.w - x0), h = std::min(TILE, comp.h - y0);
        if (tesela.w != w || tesela.h != h) tesela = plz::Imagen(w, h, plz::BLANCO); else tesela.llenar(plz::BLANCO);
        for (const Capa &c : doc.capas)
            if (c.visible) plz::mezclarZona(tesela, x0, y0, c.img, c.opacidad, c.fusion);
        QImage &t = comp.tiles[i];
        if (t.isNull() || t.width() != w || t.height() != h) t = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        uchar *dst = t.bits();
        const int paso = t.bytesPerLine();
        for (int y = 0; y < h; ++y)
            std::memcpy(dst + std::ptrdiff_t(y) * paso, &tesela.px[std::size_t(y) * std::size_t(w)], std::size_t(w) * sizeof(plz::Pixel));
        comp.sucia[i] = 0;
    }
    void dibujarCompuesto(QPainter &p, const QRectF &visible) {
        const int tx0 = std::max(0, int(std::floor(visible.left() / TILE)));
        const int ty0 = std::max(0, int(std::floor(visible.top() / TILE)));
        const int tx1 = std::min(comp.tx - 1, int(std::floor(visible.right() / TILE)));
        const int ty1 = std::min(comp.ty - 1, int(std::floor(visible.bottom() / TILE)));
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx) {
                const std::size_t i = std::size_t(ty) * std::size_t(comp.tx) + std::size_t(tx);
                if (comp.sucia[i] || comp.tiles[i].isNull()) refrescarCompuesto(tx, ty);
                p.drawImage(tx * TILE, ty * TILE, comp.tiles[i]);
            }
    }

    QTransform vista() const {
        QTransform t;
        t.translate(offset.x(), offset.y());
        t.scale(zoom, zoom);
        return t;
    }
    QPointF aDoc(const QPointF &s) const { return (s - offset) / zoom; }

    qreal grosorActual() const {
        if (modoBorrador || puntaBorrador) return grosorPincel * 1.5;
        return grosorPincel * pinceles()[pincelActual].tam;
    }
    qreal aplicarPresion(qreal p) const {
        if (!lapiz.usarPresion) return 0.5;
        return std::pow(std::clamp<double>(p, 0.0, 1.0), lapiz.gamma);
    }

    void iniciarPan(const QPointF &p) {
        paneando = true;
        panUltimo = p;
        setCursor(Qt::ClosedHandCursor);
    }
    void moverPan(const QPointF &p) {
        offset += p - panUltimo;
        panUltimo = p;
        vistaAuto = false;
        update();
    }
    void terminarPan() {
        paneando = false;
        setCursor(espacio ? Qt::OpenHandCursor : Qt::CrossCursor);
    }

    void empezar(const QPointF &p, qreal presion, bool simular) {
        Capa *c = doc.indiceValido(doc.activa) ? &doc.capas[std::size_t(doc.activa)] : nullptr;
        if (!c || c->bloqueada || !c->visible) {
            if (alCambiarInfo) alCambiarInfo("La capa activa está oculta o bloqueada");
            return;
        }
        const bool borrando = modoBorrador || puntaBorrador;
        if (forma == plz::FORMA_POLIGONO && !borrando) return;      // el polígono va por clicPoligono
        idxDibujo = doc.activa;
        actual = Trazo();
        actual.simular = simular;
        actual.borrar = borrando;
        actual.forma = borrando ? int(plz::FORMA_LIBRE) : forma;   // el borrador siempre es libre
        actual.relleno = formaRelleno;
        actual.recorte = selPoly;                  // si hay selección, solo se pinta dentro
        const Pincel &pb = pinceles()[pincelActual];
        QColor col = colorPincel;
        if (!borrando) col.setAlphaF(pb.opacidad);
        actual.color = col.rgba();
        actual.grosor = grosorActual();
        actual.thinning = std::clamp(lapiz.efectoPresion * (borrando ? 1.0 : pb.presion), 0.0, 0.95);
        actual.streamline = std::clamp(lapiz.suavizado + (borrando ? 0.0 : pb.suavizado), 0.0, 0.95);
        actual.puntos.push_back({p.x(), p.y(), presion});
        actual.completo.push_back({p.x(), p.y(), presion});
        if (actual.forma) actual.completo.push_back({p.x(), p.y(), presion});   // inicio y fin del arrastre
        congelados.clear();
        if (!borrando && pb.estampado && actual.forma == plz::FORMA_LIBRE) {      // estampado: se sella directamente en la capa
            actual.estampado = true;
            actual.est = pb.est;
            actual.semilla = std::uint32_t(QDateTime::currentMSecsSinceEpoch()) | 1u;   // el azar queda fijado en el trazo
            contornoVivo = QPainterPath();
            dibujando = true;
            antesTrazo.copiarDe(c->img);
            estampador = std::make_unique<plz::Estampador>(c->img, actual);
            estampador->punto(actual.completo.back());
            update();
            return;
        }
        contornoVivo = puente::aPath(plz::calcularContorno(actual, false));
        dibujando = true;
        antesTrazo.copiarDe(c->img);
        if (borrando) {
            respaldo.copiarDe(c->img);
            relojBorrado.start();
            rectVivo = QRect();
            actualizarBorradoVivo();
        }
        update();
    }

    void agregar(const QPointF &p, qreal presion) {
        if (actual.estampado) {                   // a la capa solo llegan los puntos que también se guardan en el trazo
            const plz::Punto &u = actual.completo.back();
            const double dx = p.x() - u.x, dy = p.y() - u.y;
            if ((dx * dx + dy * dy) * zoom * zoom < 2.25) return;
            actual.completo.push_back({p.x(), p.y(), presion});
            estampador->punto(actual.completo.back());
            update();
            return;
        }
        if (actual.forma) {                       // las formas solo siguen el punto final
            plz::Punto fin{p.x(), p.y(), presion};
            if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
                fin = plz::restringirForma(actual.forma, actual.completo[0], fin);
            actual.completo[1] = fin;
            contornoVivo = puente::aPathMulti(plz::contornosDeForma(actual));
            update();
            return;
        }
        const plz::Punto &u = actual.puntos.back();
        const double dx = p.x() - u.x, dy = p.y() - u.y;
        if ((dx * dx + dy * dy) * zoom * zoom < 2.25) return;
        actual.puntos.push_back({p.x(), p.y(), presion});
        actual.completo.push_back({p.x(), p.y(), presion});
        // Borrar con antialiasing es lo más caro que hace el programa y su coste crece con el área del
        // tramo vivo. Se junta el trabajo (como mucho una actualización cada 8 ms; los puntos se guardan
        // todos) y se congela antes cuanto más área cubre el borrado.
        if (actual.borrar && relojBorrado.isValid() && relojBorrado.elapsed() < 8) return;
        if (actual.borrar) relojBorrado.restart();
        const bool tramoGrande = actual.borrar && actual.puntos.size() >= 16 &&
                                 qint64(rectVivo.width()) * rectVivo.height() > 150000;
        if (actual.puntos.size() >= (actual.borrar ? 40u : 250u) || tramoGrande) {
            congelarTramo();
        } else {
            contornoVivo = puente::aPath(plz::calcularContorno(actual, false));
            if (actual.borrar) actualizarBorradoVivo();
        }
        update();
    }

    // Pasa el tramo a la imagen de la capa y sigue con uno nuevo
    void congelarTramo() {
        Capa &c = doc.capas[std::size_t(idxDibujo)];
        plz::Contorno cc = plz::calcularContorno(actual, true);
        if (actual.borrar) restaurarVivo(c);
        puente::pintarContorno(c.img, cc, QColor::fromRgba(actual.color), actual.borrar, &actual.recorte);
        const QRect zonaCc = puente::rectDeContorno(cc);
        congelados.push_back(std::move(cc));
        if (actual.borrar) { respaldo.copiarRegionDe(c.img, puente::aRect(zonaCc)); rectVivo = QRect(); }
        const std::ptrdiff_t solape = actual.borrar ? 8 : 12;
        std::vector<plz::Punto> resto(actual.puntos.end() - solape, actual.puntos.end());
        actual.puntos = resto;
        contornoVivo = puente::aPath(plz::calcularContorno(actual, false));
        if (actual.borrar) actualizarBorradoVivo();
    }

    void terminar() {
        if (!dibujando) return;
        dibujando = false;
        Capa &c = doc.capas[std::size_t(idxDibujo)];
        if (actual.estampado) {
            estampador->terminar();               // un simple toque deja un sello
            estampador.reset();
            doc.registrarTrazo(c.id, std::move(actual), antesTrazo);
            actual = Trazo();
            info();
            update();
            return;
        }
        if (actual.forma) {
            actual.contornos = plz::contornosDeForma(actual);
            if (!actual.contornos.empty()) {
                puente::pintarContornos(c.img, actual.contornos, QColor::fromRgba(actual.color), false, &actual.recorte);
                doc.registrarTrazo(c.id, std::move(actual), antesTrazo);
            }
            actual = Trazo();
            congelados.clear();
            contornoVivo = QPainterPath();
            info();
            update();
            return;
        }
        if (actual.borrar) restaurarVivo(c);
        plz::Contorno ultimo = plz::calcularContorno(actual, true);
        puente::pintarContorno(c.img, ultimo, QColor::fromRgba(actual.color), actual.borrar, &actual.recorte);
        actual.contornos = std::move(congelados);
        actual.contornos.push_back(std::move(ultimo));
        actual.puntos.clear();
        doc.registrarTrazo(c.id, std::move(actual), antesTrazo);   // ya está pintado: se guarda la zona para deshacer
        actual = Trazo();
        congelados.clear();
        contornoVivo = QPainterPath();
        respaldo = plz::Imagen();
        rectVivo = QRect();
        info();
        update();
    }

    // Deja la imagen de la capa como estaba antes del tramo vivo (solo la zona tocada)
    void restaurarVivo(Capa &c) {
        if (rectVivo.isEmpty() || respaldo.vacia()) return;
        QImage dst = puente::vista(c.img);
        {
            QPainter g(&dst);
            g.setCompositionMode(QPainter::CompositionMode_Source);
            g.drawImage(rectVivo.topLeft(), puente::lectura(respaldo), rectVivo);
        }
        c.img.tocar(puente::aRect(rectVivo));
    }

    // Borra el contorno vivo directamente sobre la capa, tocando solo su zona
    void actualizarBorradoVivo() {
        Capa &c = doc.capas[std::size_t(idxDibujo)];
        const QRect nuevo = contornoVivo.boundingRect().toAlignedRect()
                                .adjusted(-2, -2, 2, 2)
                                .intersected(QRect(0, 0, doc.ancho, doc.alto));
        const QRect sucia = rectVivo.isEmpty() ? nuevo : rectVivo.united(nuevo);
        if (sucia.isEmpty()) return;
        QImage dst = puente::vista(c.img);
        {
            QPainter g(&dst);
            g.setCompositionMode(QPainter::CompositionMode_Source);
            g.drawImage(sucia.topLeft(), puente::lectura(respaldo), sucia);   // 1) restaura la zona
            g.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            g.setRenderHint(QPainter::Antialiasing);
            g.setPen(Qt::NoPen);
            g.setBrush(Qt::black);
            if (!actual.recorte.empty()) g.setClipPath(puente::aPoligono(actual.recorte));
            g.drawPath(contornoVivo);                                          // 2) borra con el contorno actual
        }
        c.img.tocar(puente::aRect(sucia));
        rectVivo = nuevo;
    }

    void info() {
        if (alCambiarInfo)
            alCambiarInfo(QString("trazos: %1  |  capa: %2  |  zoom: %3%  |  lienzo: %4×%5")
                              .arg(doc.totalOperaciones())
                              .arg(QString::fromStdString(doc.capas[std::size_t(doc.activa)].nombre))
                              .arg(qRound(zoom * 100)).arg(doc.ancho).arg(doc.alto));
    }

    bool capaEditable() {
        const Capa &c = doc.capas[std::size_t(doc.activa)];
        if (c.bloqueada || !c.visible) {
            if (alCambiarInfo) alCambiarInfo("La capa activa está oculta o bloqueada");
            return false;
        }
        return true;
    }
    void fijarSeleccion(plz::Seleccion s, plz::Contorno poly) {
        sel = std::move(s);
        selPoly = std::move(poly);
        update();
    }
    void iniciarSel(const QPointF &p) { seleccionando = true; selA = selB = p; selPuntos = {p}; update(); }
    void moverSel(const QPointF &p) {
        selB = p;
        if (modoSel == 2 && QLineF(selPuntos.back(), p).length() * zoom > 2) selPuntos.push_back(p);
        update();
    }
    void terminarSel() {
        if (!seleccionando) return;
        seleccionando = false;
        if (modoSel == 1) {
            const QRectF r = QRectF(selA, selB).normalized();
            const int x0 = std::clamp(int(std::floor(r.left())), 0, doc.ancho), y0 = std::clamp(int(std::floor(r.top())), 0, doc.alto);
            const int x1 = std::clamp(int(std::ceil(r.right())), 0, doc.ancho), y1 = std::clamp(int(std::ceil(r.bottom())), 0, doc.alto);
            if (x1 - x0 < 2 || y1 - y0 < 2) { deseleccionar(); return; }       // un clic quita la selección
            fijarSeleccion(plz::Seleccion::rectangulo(doc.ancho, doc.alto, plz::Rect{x0, y0, x1, y1}),
                           {{double(x0), double(y0)}, {double(x1), double(y0)}, {double(x1), double(y1)}, {double(x0), double(y1)}});
        } else {
            if (selPuntos.size() < 3) { deseleccionar(); return; }
            plz::Contorno poly;
            for (const QPointF &q : selPuntos) poly.push_back({q.x(), q.y()});
            plz::Seleccion s = plz::Seleccion::poligono(doc.ancho, doc.alto, poly);
            if (s.vacia()) { deseleccionar(); return; }
            fijarSeleccion(std::move(s), std::move(poly));
        }
    }

    void rellenarEn(const QPointF &d) {
        if (dibujando) return;
        const int x = int(std::floor(d.x())), y = int(std::floor(d.y()));
        if (x < 0 || y < 0 || x >= doc.ancho || y >= doc.alto) return;
        const Capa &c = doc.capas[std::size_t(doc.activa)];
        if (c.bloqueada || !c.visible) {
            if (alCambiarInfo) alCambiarInfo("La capa activa está oculta o bloqueada");
            return;
        }
        QColor col = colorPincel;
        col.setAlpha(255);
        const plz::OpcionesRelleno o{cubo.tolerancia, 1};
        if (doc.rellenar(doc.activa, x, y, col.rgba(), o, cubo.todasCapas, sel.vacia() ? nullptr : &sel)) { info(); update(); }
        else if (alCambiarInfo) alCambiarInfo("Nada que rellenar ahí");
    }

    // Color visible en un punto del lienzo (todas las capas combinadas sobre blanco)
    void tomarColor(const QPointF &d) {
        const int x = int(std::floor(d.x())), y = int(std::floor(d.y()));
        if (x < 0 || y < 0 || x >= doc.ancho || y >= doc.alto) return;
        if (alElegirColor) alElegirColor(QColor::fromRgb(QRgb(doc.colorEn(x, y))));
    }
};
