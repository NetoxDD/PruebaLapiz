#pragma once
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QColor>
#include <QTransform>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <functional>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "core/documento.h"
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

    Trazo actual;
    QPainterPath contornoVivo;                 // contorno del tramo en curso
    std::vector<plz::Contorno> congelados;     // tramos ya pasados a la capa
    plz::Imagen respaldo;                      // capa antes del tramo vivo (solo al borrar)
    plz::Imagen antesTrazo;                    // capa antes de empezar el trazo (para poder deshacerlo)
    QRect rectVivo;                            // zona de la capa tocada por el borrado en curso
    int idxDibujo = 0;                         // capa donde se dibuja el trazo en curso
    bool dibujando = false;

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
        modoCuentagotas = b;
        setCursor(b ? Qt::PointingHandCursor : Qt::CrossCursor);
        update();
    }

    void setCubo(bool b) {
        modoCubo = b;
        setCursor(b ? Qt::PointingHandCursor : Qt::CrossCursor);
        update();
    }

    // ---------- Selección ----------
    void setSeleccion(int modo) { modoSel = modo; seleccionando = false; update(); }   // la selección actual se conserva
    bool haySeleccion() const { return !sel.vacia(); }
    void deseleccionar() { sel = plz::Seleccion(); selPoly.clear(); update(); }
    void seleccionarTodo() {
        const double W = doc.ancho, H = doc.alto;
        fijarSeleccion(plz::Seleccion::rectangulo(doc.ancho, doc.alto, plz::Rect{0, 0, doc.ancho, doc.alto}),
                       {{0, 0}, {W, 0}, {W, H}, {0, H}});
    }
    void borrarSel() {
        if (sel.vacia() || !capaEditable()) return;
        if (doc.borrarSeleccion(doc.activa, sel)) { info(); update(); }
    }
    void copiar(bool cortar) {
        if (sel.vacia()) return;
        const plz::Imagen im = doc.copiarSeleccion(doc.activa, sel);
        if (im.vacia()) return;
        QGuiApplication::clipboard()->setImage(puente::lectura(im).convertToFormat(QImage::Format_ARGB32));
        if (cortar) borrarSel();
    }
    void pegar() {
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
                                               };
        return v;
    }
    void setPincel(int i) {
        if (i >= 0 && i < int(pinceles().size())) pincelActual = i;
        update();
    }
    bool modificado() const { return doc.modificado(); }
    int ancho() const { return doc.ancho; }
    int alto() const { return doc.alto; }

    // ---------- Capas (crear, borrar y mover entran en el historial) ----------
    int numCapas() const { return doc.numCapas(); }
    const Capa &capa(int i) const { return doc.capas[std::size_t(i)]; }
    int indiceActivo() const { return doc.activa; }
    void setActiva(int i) {
        if (dibujando || !doc.indiceValido(i)) return;
        doc.activa = i;
        info();
    }
    void nuevaCapa() {
        if (dibujando) return;
        doc.nuevaCapa();
        cambiaronCapas();
    }
    void borrarCapa(int i) {
        if (dibujando) return;
        if (doc.borrarCapa(i)) cambiaronCapas();
    }
    void moverCapa(int i, int delta) {
        if (dibujando) return;
        if (doc.moverCapa(i, delta)) cambiaronCapas();
    }
    // Inserta una imagen como capa de guía: bloqueada, semitransparente y debajo de todo
    void cargarPlantilla(const QImage &src, const QString &nombre) {
        if (dibujando || src.isNull()) return;
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
    void acercar() { establecerZoom(zoom * 1.25, centro()); }
    void alejar()  { establecerZoom(zoom / 1.25, centro()); }
    void zoom100() { establecerZoom(1.0, centro()); }

    // ---------- Edición ----------
    void deshacer() {
        if (dibujando) return;
        if (doc.deshacer()) cambiaronCapas();
    }
    void rehacer() {
        if (dibujando) return;
        if (doc.rehacer()) cambiaronCapas();
    }
    void nuevoLienzo(int w, int h) {
        if (dibujando) return;
        doc.reiniciar(w, h);
        deseleccionar();                     // la selección era del dibujo anterior
        espejos.clear();
        cambiaronCapas();
        ajustar();
    }

    // ---------- Guardar / abrir / exportar ----------
    bool guardar(const QString &ruta) {
        if (!archivo::guardar(doc, ruta)) return false;
        doc.marcarGuardado();
        info();
        return true;
    }
    bool abrir(const QString &ruta) {
        if (dibujando) return false;
        if (!archivo::abrir(doc, ruta)) return false;
        deseleccionar();
        espejos.clear();
        cambiaronCapas();
        ajustar();
        return true;
    }
    bool exportarPNG(const QString &ruta, bool conPlantilla = false) const {
        const plz::Imagen im = doc.componer(conPlantilla);
        return puente::lectura(im).save(ruta, "PNG");
    }
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
        for (int i = 0; i < numCapas(); ++i) {
            Capa &c = doc.capas[std::size_t(i)];
            if (!c.visible) continue;
            p.setOpacity(c.opacidad);
            dibujarCapa(p, c, visible);
            // al pintar se ve el trazo vivo encima; al borrar ya está aplicado en la imagen
            if (dibujando && i == idxDibujo && !actual.borrar) {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor::fromRgba(actual.color));
                p.save();
                if (!actual.recorte.empty()) p.setClipPath(puente::aPoligono(actual.recorte), Qt::IntersectClip);
                p.drawPath(contornoVivo);
                p.restore();
            }
        }

        // Contorno de la selección ("hormigas"): línea negra con trazos blancos encima
        if (seleccionando || !selPoly.empty()) {
            QPainterPath cont;
            if (seleccionando && modoSel == 1) cont.addRect(QRectF(selA, selB).normalized());
            else if (seleccionando) { QPolygonF pg; for (const QPointF &q : selPuntos) pg << q; cont.addPolygon(pg); }
            else cont = puente::aPoligono(selPoly);
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
            if (hoverValido && !paneando && !espacio && !modoCuentagotas && !modoCubo && !modoSel) {
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
            } else if (modoSel) {
                iniciarSel(aDoc(p));
            } else if (modoCubo) {
                rellenarEn(aDoc(p));
            } else {
                puntaBorrador = (e->pointerType() == QPointingDevice::PointerType::Eraser)
                || (barril && lapiz.botonLapiz == 1);
                empezar(aDoc(p), aplicarPresion(e->pressure()), false);
            }
            break;
        case QEvent::TabletMove:
            if (paneando) moverPan(p);
            else if (seleccionando) moverSel(aDoc(p));
            else if (dibujando) agregar(aDoc(p), aplicarPresion(e->pressure()));
            break;
        case QEvent::TabletRelease:
            if (paneando) terminarPan();
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
        if (modoSel) { iniciarSel(aDoc(e->position())); return; }
        if (modoCubo) { rellenarEn(aDoc(e->position())); return; }
        empezar(aDoc(e->position()), 0.5, true);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        hover = e->position(); hoverValido = true;
        if (paneando) moverPan(e->position());
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

    template <class F>
    void cambiarProp(int i, F cambio, const char *clave) {
        if (!ok(i)) return;
        const Capa &c = doc.capas[std::size_t(i)];
        plz::PropCapa p{c.nombre, c.bloqueada, c.opacidad};
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
    Espejo &espejoDe(Capa &c) {
        Espejo &e = espejos[c.id];
        if (e.w != c.img.w || e.h != c.img.h) {
            e.w = c.img.w; e.h = c.img.h;
            e.tx = (e.w + TILE - 1) / TILE; e.ty = (e.h + TILE - 1) / TILE;
            e.tiles.assign(std::size_t(e.tx) * std::size_t(e.ty), QImage());
            e.sucia.assign(e.tiles.size(), 1);
            c.img.consumirSucio();
            return e;
        }
        const plz::Rect s = c.img.consumirSucio();
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
