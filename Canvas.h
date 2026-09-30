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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QFile>
#include <functional>
#include <vector>
#include <algorithm>
#include <cmath>
#include "freehand.h"

struct Punto {
    QPointF pos;       // coordenadas del lienzo (no de la pantalla)
    qreal presion;
};

struct Trazo {
    std::vector<Punto> puntos;     // tramo en curso (se recorta al congelar)
    std::vector<Punto> completo;   // TODOS los puntos (lo que se guarda)
    QColor color = Qt::black;
    qreal grosor = 16.0;
    double thinning = 0.5;
    double streamline = 0.5;
    bool simular = false;          // true con mouse: presión simulada por velocidad
    bool borrar = false;           // true = borra (modo Clear) en vez de pintar
    QPainterPath contorno;
};

struct Capa {
    int id = 0;
    QString nombre;
    bool visible = true;
    bool bloqueada = false;
    double opacidad = 1.0;
    QImage img;                    // pixeles de la capa (transparente al inicio)
    std::vector<Trazo> trazos;     // trazos que la forman (base del deshacer y del guardado)

    Capa(int id_, const QString &n, int w, int h)
        : id(id_), nombre(n), img(w, h, QImage::Format_ARGB32_Premultiplied) {
        img.fill(Qt::transparent);
    }
};

struct OpcionesLapiz {
    bool usarPresion = true;
    double gamma = 1.0;            // curva: presion^gamma (<1 suave, >1 firme)
    double efectoPresion = 0.5;    // 0 = grosor constante
    double suavizado = 0.5;        // 0 = sigue el lápiz exacto
    int botonLapiz = 0;            // 0 nada, 1 borrador, 2 mover lienzo
};

class Canvas : public QOpenGLWidget {
    struct Rehacer { int capaId; Trazo t; };

    std::vector<Capa> capas;       // índice 0 = la de más abajo
    int activa = 0;
    int nextId = 1;
    std::vector<int> historial;    // id de la capa de cada trazo, en orden
    std::vector<Rehacer> pilaRehacer;

    Trazo actual;
    QPainterPath contornoVivo;     // contorno del tramo en curso
    QPainterPath congelado;        // tramos ya pasados a la capa
    QImage respaldo;               // capa antes del tramo vivo (solo al borrar)
    QRect rectVivo;                // zona de la capa tocada por el borrado en curso
    int idxDibujo = 0;             // capa donde se dibuja el trazo en curso
    bool dibujando = false;
    bool sucio = false;

    // Lienzo y vista
    int docW = 2000, docH = 1500;
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

public:
    OpcionesLapiz lapiz;
    std::function<void(const QString &)> alCambiarInfo;
    std::function<void(const QString &)> alDatosLapiz;
    std::function<void(const QString &)> alIniciarGL;
    std::function<void()> alCambiarCapas;

    Canvas() {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        reiniciarCapas(docW, docH);
    }

    QColor color() const { return colorPincel; }
    void setColor(const QColor &c) { colorPincel = c; }
    void setGrosor(qreal g) { grosorPincel = g; }
    void setBorrador(bool b) { modoBorrador = b; }
    bool modificado() const { return sucio; }
    int ancho() const { return docW; }
    int alto() const { return docH; }

    // ---------- Capas ----------
    int numCapas() const { return int(capas.size()); }
    const Capa &capa(int i) const { return capas[i]; }
    int indiceActivo() const { return activa; }
    void setActiva(int i) {
        if (dibujando || i < 0 || i >= numCapas()) return;
        activa = i;
        info();
    }
    void nuevaCapa() {
        if (dibujando) return;
        const int id = nextId++;
        capas.insert(capas.begin() + activa + 1, Capa(id, QString("Capa %1").arg(id), docW, docH));
        activa++;
        sucio = true;
        cambiaronCapas();
    }
    void borrarCapa(int i) {
        if (dibujando || numCapas() <= 1 || i < 0 || i >= numCapas()) return;
        const int id = capas[i].id;
        historial.erase(std::remove(historial.begin(), historial.end(), id), historial.end());
        pilaRehacer.erase(std::remove_if(pilaRehacer.begin(), pilaRehacer.end(),
                                         [id](const Rehacer &r) { return r.capaId == id; }), pilaRehacer.end());
        capas.erase(capas.begin() + i);
        activa = std::min(activa, numCapas() - 1);
        sucio = true;
        cambiaronCapas();
    }
    void moverCapa(int i, int delta) {
        const int j = i + delta;
        if (dibujando || i < 0 || j < 0 || i >= numCapas() || j >= numCapas()) return;
        std::swap(capas[i], capas[j]);
        if (activa == i) activa = j; else if (activa == j) activa = i;
        sucio = true;
        cambiaronCapas();
    }
    // Estos no avisan a la lista (evita refrescarla mientras se edita)
    void setVisible(int i, bool v)   { if (ok(i)) { capas[i].visible = v;   marcar(); } }
    void setBloqueada(int i, bool b) { if (ok(i)) { capas[i].bloqueada = b; marcar(); } }
    void setOpacidad(int i, double o){ if (ok(i)) { capas[i].opacidad = std::clamp(o, 0.0, 1.0); marcar(); } }
    void setNombre(int i, const QString &n) { if (ok(i)) { capas[i].nombre = n; marcar(); } }

    // ---------- Vista ----------
    void ajustar() {
        const double m = 30;
        const double zx = (width() - 2 * m) / docW;
        const double zy = (height() - 2 * m) / docH;
        zoom = std::clamp(std::min(zx, zy), 0.05, 32.0);
        offset = QPointF((width() - docW * zoom) / 2, (height() - docH * zoom) / 2);
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
        if (dibujando || historial.empty()) return;
        const int id = historial.back();
        historial.pop_back();
        Capa *c = porId(id);
        if (!c || c->trazos.empty()) return;
        pilaRehacer.push_back({id, std::move(c->trazos.back())});
        c->trazos.pop_back();
        rehacerImagen(*c);
        sucio = true;
        info(); update();
    }
    void rehacer() {
        if (dibujando || pilaRehacer.empty()) return;
        Rehacer r = std::move(pilaRehacer.back());
        pilaRehacer.pop_back();
        Capa *c = porId(r.capaId);
        if (!c) return;
        pintarContorno(c->img, r.t.contorno, r.t.color, r.t.borrar);
        c->trazos.push_back(std::move(r.t));
        historial.push_back(c->id);
        sucio = true;
        info(); update();
    }
    void nuevoLienzo(int w, int h) {
        if (dibujando) return;
        docW = w; docH = h;
        reiniciarCapas(w, h);
        sucio = false;
        cambiaronCapas();
        ajustar();
    }

    // ---------- Guardar / abrir / exportar ----------
    bool guardar(const QString &ruta) {
        QJsonArray capasJson;
        for (const Capa &c : capas) {
            QJsonArray arr;
            for (const Trazo &t : c.trazos) arr.append(trazoAJson(t));
            QJsonObject o;
            o["nombre"] = c.nombre;
            o["visible"] = c.visible;
            o["bloqueada"] = c.bloqueada;
            o["opacidad"] = c.opacidad;
            o["trazos"] = arr;
            capasJson.append(o);
        }
        QJsonObject lienzo;
        lienzo["ancho"] = docW;
        lienzo["alto"] = docH;
        QJsonObject raiz;
        raiz["formato"] = "PruebaLapiz";
        raiz["version"] = 3;
        raiz["lienzo"] = lienzo;
        raiz["capas"] = capasJson;

        QSaveFile f(ruta);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(raiz).toJson(QJsonDocument::Compact));
        if (!f.commit()) return false;
        sucio = false;
        return true;
    }

    bool abrir(const QString &ruta) {
        if (dibujando) return false;
        QFile f(ruta);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        const QJsonObject raiz = doc.object();
        if (raiz["formato"].toString() != "PruebaLapiz") return false;

        const QJsonObject l = raiz["lienzo"].toObject();
        int w = l["ancho"].toInt(2000), h = l["alto"].toInt(1500);
        if (w < 100 || w > 6000 || h < 100 || h > 6000) { w = 2000; h = 1500; }

        int id = 1;
        std::vector<Capa> nuevas;
        auto cargar = [&](const QJsonArray &arr, Capa &c) {
            for (const QJsonValue &v : arr) {
                Trazo t;
                if (!trazoDeJson(v.toObject(), t)) continue;
                t.contorno = calcularContorno(t, true);
                pintarContorno(c.img, t.contorno, t.color, t.borrar);
                c.trazos.push_back(std::move(t));
            }
        };
        if (raiz.contains("capas")) {
            for (const QJsonValue &v : raiz["capas"].toArray()) {
                const QJsonObject o = v.toObject();
                Capa c(id, o["nombre"].toString(QString("Capa %1").arg(id)), w, h);
                id++;
                c.visible = o["visible"].toBool(true);
                c.bloqueada = o["bloqueada"].toBool(false);
                c.opacidad = o["opacidad"].toDouble(1.0);
                cargar(o["trazos"].toArray(), c);
                nuevas.push_back(std::move(c));
            }
        } else {                      // archivos v1 y v2: una sola capa
            Capa c(id++, "Capa 1", w, h);
            cargar(raiz["trazos"].toArray(), c);
            nuevas.push_back(std::move(c));
        }
        if (nuevas.empty()) nuevas.push_back(Capa(id++, "Capa 1", w, h));

        docW = w; docH = h;
        capas = std::move(nuevas);
        nextId = id;
        activa = numCapas() - 1;
        historial.clear();
        pilaRehacer.clear();
        sucio = false;
        cambiaronCapas();
        ajustar();
        return true;
    }

    bool exportarPNG(const QString &ruta) const {
        QImage out(docW, docH, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::white);
        QPainter g(&out);
        for (const Capa &c : capas) {
            if (!c.visible) continue;
            g.setOpacity(c.opacidad);
            g.drawImage(0, 0, c.img);
        }
        g.end();
        return out.save(ruta, "PNG");
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
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 2.0);
        p.setTransform(vista());
        p.fillRect(QRectF(0, 0, docW, docH), Qt::white);
        p.setClipRect(QRectF(0, 0, docW, docH));

        for (int i = 0; i < numCapas(); ++i) {
            const Capa &c = capas[i];
            if (!c.visible) continue;
            p.setOpacity(c.opacidad);
            p.drawImage(0, 0, c.img);
            // al pintar se ve el trazo vivo encima; al borrar ya está aplicado en la imagen
            if (dibujando && i == idxDibujo && !actual.borrar) {
                p.setPen(Qt::NoPen);
                p.setBrush(actual.color);
                p.drawPath(contornoVivo);
            }
        }

        // Círculo de vista previa del pincel (en coordenadas de pantalla)
        p.resetTransform();
        p.setClipping(false);
        p.setOpacity(1.0);
        if (hoverValido && !paneando && !espacio) {
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
            if (espacio || (barril && lapiz.botonLapiz == 2)) {
                iniciarPan(p);
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
        if (e->button() == Qt::MiddleButton ||
            (e->button() == Qt::LeftButton && espacio)) {
            iniciarPan(e->position());
            return;
        }
        if (e->button() != Qt::LeftButton) return;
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
    bool ok(int i) const { return i >= 0 && i < numCapas(); }
    void marcar() { sucio = true; info(); update(); }
    void cambiaronCapas() {
        if (alCambiarCapas) alCambiarCapas();
        info();
        update();
    }
    void reiniciarCapas(int w, int h) {
        capas.clear();
        nextId = 1;
        capas.push_back(Capa(nextId++, "Capa 1", w, h));
        activa = 0;
        historial.clear();
        pilaRehacer.clear();
    }
    Capa *porId(int id) {
        for (Capa &c : capas) if (c.id == id) return &c;
        return nullptr;
    }
    size_t totalTrazos() const {
        size_t n = 0;
        for (const Capa &c : capas) n += c.trazos.size();
        return n;
    }

    QTransform vista() const {
        QTransform t;
        t.translate(offset.x(), offset.y());
        t.scale(zoom, zoom);
        return t;
    }
    QPointF aDoc(const QPointF &s) const { return (s - offset) / zoom; }

    qreal grosorActual() const {
        return (modoBorrador || puntaBorrador) ? grosorPincel * 1.5 : grosorPincel;
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

    // Pinta o borra un contorno sobre la imagen de una capa
    static void pintarContorno(QImage &img, const QPainterPath &c, const QColor &color, bool borrar) {
        QPainter g(&img);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(Qt::NoPen);
        g.setBrush(borrar ? QColor(Qt::black) : color);
        g.setCompositionMode(borrar ? QPainter::CompositionMode_DestinationOut
                                    : QPainter::CompositionMode_SourceOver);
        g.drawPath(c);
    }
    // Reconstruye la imagen de una capa desde sus trazos (tras deshacer)
    static void rehacerImagen(Capa &c) {
        c.img.fill(Qt::transparent);
        for (const Trazo &t : c.trazos) pintarContorno(c.img, t.contorno, t.color, t.borrar);
    }

    void empezar(const QPointF &p, qreal presion, bool simular) {
        Capa *c = (activa >= 0 && activa < numCapas()) ? &capas[activa] : nullptr;
        if (!c || c->bloqueada || !c->visible) {
            if (alCambiarInfo) alCambiarInfo("La capa activa está oculta o bloqueada");
            return;
        }
        const bool borrando = modoBorrador || puntaBorrador;
        idxDibujo = activa;
        actual = Trazo();
        actual.simular = simular;
        actual.borrar = borrando;
        actual.color = colorPincel;
        actual.grosor = grosorActual();
        actual.thinning = lapiz.efectoPresion;
        actual.streamline = lapiz.suavizado;
        actual.puntos.push_back({p, presion});
        actual.completo.push_back({p, presion});
        congelado = QPainterPath();
        congelado.setFillRule(Qt::WindingFill);
        contornoVivo = calcularContorno(actual, false);
        dibujando = true;
        if (borrando) {
            respaldo = c->img;          // copia compartida; se duplica al primer borrado
            rectVivo = QRect();
            actualizarBorradoVivo();
        }
        update();
    }

    void agregar(const QPointF &p, qreal presion) {
        const QPointF d = p - actual.puntos.back().pos;
        if ((d.x() * d.x() + d.y() * d.y()) * zoom * zoom < 2.25) return;
        actual.puntos.push_back({p, presion});
        actual.completo.push_back({p, presion});
        if (actual.puntos.size() >= 250) {
            congelarTramo();
        } else {
            contornoVivo = calcularContorno(actual, false);
            if (actual.borrar) actualizarBorradoVivo();
        }
        update();
    }

    // Pasa el tramo a la imagen de la capa y sigue con uno nuevo
    void congelarTramo() {
        Capa &c = capas[idxDibujo];
        const QPainterPath cc = calcularContorno(actual, true);
        congelado.addPath(cc);
        if (actual.borrar) restaurarVivo(c);
        pintarContorno(c.img, cc, actual.color, actual.borrar);
        if (actual.borrar) { respaldo = c.img; rectVivo = QRect(); }
        std::vector<Punto> resto(actual.puntos.end() - 12, actual.puntos.end());
        actual.puntos = resto;
        contornoVivo = calcularContorno(actual, false);
        if (actual.borrar) actualizarBorradoVivo();
    }

    void terminar() {
        if (!dibujando) return;
        dibujando = false;
        Capa &c = capas[idxDibujo];
        if (actual.borrar) restaurarVivo(c);
        const QPainterPath ultimo = calcularContorno(actual, true);
        pintarContorno(c.img, ultimo, actual.color, actual.borrar);
        actual.contorno = congelado;
        actual.contorno.addPath(ultimo);
        actual.puntos.clear();
        c.trazos.push_back(std::move(actual));
        historial.push_back(c.id);
        pilaRehacer.clear();
        contornoVivo = QPainterPath();
        respaldo = QImage();
        rectVivo = QRect();
        sucio = true;
        info();
        update();
    }

    // Deja la imagen de la capa como estaba antes del tramo vivo (solo la zona tocada)
    void restaurarVivo(Capa &c) {
        if (rectVivo.isEmpty() || respaldo.isNull()) return;
        QPainter g(&c.img);
        g.setCompositionMode(QPainter::CompositionMode_Source);
        g.drawImage(rectVivo.topLeft(), respaldo, rectVivo);
    }

    // Borra el contorno vivo directamente sobre la capa, tocando solo su zona
    void actualizarBorradoVivo() {
        Capa &c = capas[idxDibujo];
        const QRect nuevo = contornoVivo.boundingRect().toAlignedRect()
                                .adjusted(-2, -2, 2, 2)
                                .intersected(QRect(0, 0, docW, docH));
        const QRect sucia = rectVivo.isEmpty() ? nuevo : rectVivo.united(nuevo);
        if (sucia.isEmpty()) return;
        QPainter g(&c.img);
        g.setCompositionMode(QPainter::CompositionMode_Source);
        g.drawImage(sucia.topLeft(), respaldo, sucia);       // 1) restaura la zona
        g.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(Qt::NoPen);
        g.setBrush(Qt::black);
        g.drawPath(contornoVivo);                            // 2) borra con el contorno actual
        rectVivo = nuevo;
    }

    void info() {
        if (alCambiarInfo)
            alCambiarInfo(QString("trazos: %1  |  capa: %2  |  zoom: %3%  |  lienzo: %4×%5")
                              .arg(totalTrazos()).arg(capas[activa].nombre)
                              .arg(qRound(zoom * 100)).arg(docW).arg(docH));
    }

    // ---------- JSON ----------
    static QJsonObject trazoAJson(const Trazo &t) {
        QJsonArray pts;
        for (const Punto &p : t.completo) {
            pts.append(std::round(p.pos.x() * 100) / 100.0);
            pts.append(std::round(p.pos.y() * 100) / 100.0);
            pts.append(std::round(p.presion * 1000) / 1000.0);
        }
        QJsonObject o;
        o["color"] = t.color.name(QColor::HexArgb);
        o["grosor"] = t.grosor;
        o["thin"] = t.thinning;
        o["suav"] = t.streamline;
        o["simular"] = t.simular;
        o["borrar"] = t.borrar;
        o["puntos"] = pts;
        return o;
    }
    static bool trazoDeJson(const QJsonObject &o, Trazo &t) {
        const QJsonArray pts = o["puntos"].toArray();
        if (pts.size() < 3 || pts.size() % 3 != 0) return false;
        t.color = QColor(o["color"].toString());
        t.grosor = o["grosor"].toDouble(16.0);
        t.thinning = o["thin"].toDouble(0.5);
        t.streamline = o["suav"].toDouble(0.5);
        t.simular = o["simular"].toBool();
        t.borrar = o["borrar"].toBool();
        for (int i = 0; i + 2 < pts.size(); i += 3)
            t.completo.push_back({QPointF(pts[i].toDouble(), pts[i + 1].toDouble()),
                                  pts[i + 2].toDouble()});
        t.puntos = t.completo;
        return true;
    }

    static QPainterPath calcularContorno(const Trazo &t, bool terminado) {
        std::vector<pf::InPoint> entrada;
        entrada.reserve(t.puntos.size());
        for (const Punto &p : t.puntos)
            entrada.push_back({{p.pos.x(), p.pos.y()}, t.simular ? -1.0 : p.presion});

        pf::Options o;
        o.size = t.grosor;
        o.thinning = t.thinning;
        o.smoothing = 0.5;
        o.streamline = t.streamline;
        o.simulatePressure = t.simular;
        o.last = terminado;

        const std::vector<pf::Vec> c = pf::getStroke(entrada, o);

        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        const size_t n = c.size();
        if (n == 0) return path;
        if (n < 3) {
            path.moveTo(c[0].x, c[0].y);
            for (size_t i = 1; i < n; ++i) path.lineTo(c[i].x, c[i].y);
            return path;
        }
        auto medio = [](const pf::Vec &a, const pf::Vec &b) {
            return QPointF((a.x + b.x) / 2, (a.y + b.y) / 2);
        };
        path.moveTo(medio(c[0], c[1]));
        for (size_t i = 1; i <= n; ++i) {
            const pf::Vec &a = c[i % n];
            const pf::Vec &b = c[(i + 1) % n];
            path.quadTo(QPointF(a.x, a.y), medio(a, b));
        }
        path.closeSubpath();
        return path;
    }
};