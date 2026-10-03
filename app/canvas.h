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


public:
    struct OpcionesCubo {
        int tolerancia = 32;        // 0..255
        bool todasCapas = false;    // decidir la zona mirando todas las capas visibles
    };
    OpcionesCubo cubo;
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

    Canvas() {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        // Así se rasteriza un trazo (el core no sabe de Qt)
        doc.pintor = [](plz::Imagen &im, const Trazo &t) {
            puente::pintarContornos(im, t.contornos, QColor::fromRgba(t.color), t.borrar);
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
                p.drawPath(contornoVivo);
            }
        }

        // Círculo de vista previa del pincel (en coordenadas de pantalla)
        p.resetTransform();
        p.setClipping(false);
        p.setOpacity(1.0);
            if (hoverValido && !paneando && !espacio && !modoCuentagotas && !modoCubo) {
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
            else if (dibujando) agregar(aDoc(p), aplicarPresion(e->pressure()));
            break;
        case QEvent::TabletRelease:
            if (paneando) terminarPan();
            else terminar();
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
        if (modoCubo) { rellenarEn(aDoc(e->position())); return; }
        empezar(aDoc(e->position()), 0.5, true);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->source() != Qt::MouseEventNotSynthesized) return;
        hover = e->position(); hoverValido = true;
        if (paneando) moverPan(e->position());
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
        const Pincel &pb = pinceles()[pincelActual];
        QColor col = colorPincel;
        if (!borrando) col.setAlphaF(pb.opacidad);
        actual.color = col.rgba();
        actual.grosor = grosorActual();
        actual.thinning = std::clamp(lapiz.efectoPresion * (borrando ? 1.0 : pb.presion), 0.0, 0.95);
        actual.streamline = std::clamp(lapiz.suavizado + (borrando ? 0.0 : pb.suavizado), 0.0, 0.95);
        actual.puntos.push_back({p.x(), p.y(), presion});
        actual.completo.push_back({p.x(), p.y(), presion});
        congelados.clear();
        contornoVivo = puente::aPath(plz::calcularContorno(actual, false));
        dibujando = true;
        antesTrazo.copiarDe(c->img);
        if (borrando) {
            respaldo.copiarDe(c->img);
            rectVivo = QRect();
            actualizarBorradoVivo();
        }
        update();
    }

    void agregar(const QPointF &p, qreal presion) {
        const plz::Punto &u = actual.puntos.back();
        const double dx = p.x() - u.x, dy = p.y() - u.y;
        if ((dx * dx + dy * dy) * zoom * zoom < 2.25) return;
        actual.puntos.push_back({p.x(), p.y(), presion});
        actual.completo.push_back({p.x(), p.y(), presion});
        // El borrador congela tramos cortos: borrar un contorno largo con antialiasing es lo más caro
        // que hace el programa, y su coste crece con el largo del tramo vivo.
        if (actual.puntos.size() >= (actual.borrar ? 40u : 250u)) {
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
        puente::pintarContorno(c.img, cc, QColor::fromRgba(actual.color), actual.borrar);
        const QRect zonaCc = puente::rectDeContorno(cc);
        congelados.push_back(std::move(cc));
        if (actual.borrar) { respaldo.copiarRegionDe(c.img, puente::aRect(zonaCc)); rectVivo = QRect(); }
        std::vector<plz::Punto> resto(actual.puntos.end() - 12, actual.puntos.end());
        actual.puntos = resto;
        contornoVivo = puente::aPath(plz::calcularContorno(actual, false));
        if (actual.borrar) actualizarBorradoVivo();
    }

    void terminar() {
        if (!dibujando) return;
        dibujando = false;
        Capa &c = doc.capas[std::size_t(idxDibujo)];
        if (actual.borrar) restaurarVivo(c);
        plz::Contorno ultimo = plz::calcularContorno(actual, true);
        puente::pintarContorno(c.img, ultimo, QColor::fromRgba(actual.color), actual.borrar);
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
        if (doc.rellenar(doc.activa, x, y, col.rgba(), o, cubo.todasCapas)) { info(); update(); }
        else if (alCambiarInfo) alCambiarInfo("Nada que rellenar ahí");
    }

    // Color visible en un punto del lienzo (todas las capas combinadas sobre blanco)
    void tomarColor(const QPointF &d) {
        const int x = int(std::floor(d.x())), y = int(std::floor(d.y()));
        if (x < 0 || y < 0 || x >= doc.ancho || y >= doc.alto) return;
        if (alElegirColor) alElegirColor(QColor::fromRgb(QRgb(doc.colorEn(x, y))));
    }
};
