#pragma once
// Guardar y abrir el formato .json de PruebaLapiz (versión 4; también abre la 1, 2 y 3).
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include "core/documento.h"
#include "puente.h"

namespace archivo {

inline QJsonObject trazoAJson(const plz::Trazo &t) {
    QJsonArray pts;
    for (const plz::Punto &p : t.completo) {
        pts.append(std::round(p.x * 100) / 100.0);
        pts.append(std::round(p.y * 100) / 100.0);
        pts.append(std::round(p.presion * 1000) / 1000.0);
    }
    QJsonObject o;
    o["color"] = QColor::fromRgba(t.color).name(QColor::HexArgb);
    o["grosor"] = t.grosor;
    o["thin"] = t.thinning;
    o["suav"] = t.streamline;
    o["simular"] = t.simular;
    o["borrar"] = t.borrar;
    if (t.forma) { o["forma"] = t.forma; o["relleno"] = t.relleno; }
    if (!t.recorte.empty()) {
        QJsonArray r;
        for (const pf::Vec &v : t.recorte) { r.append(v.x); r.append(v.y); }
        o["recorte"] = r;
    }
    o["puntos"] = pts;
    return o;
}

inline bool trazoDeJson(const QJsonObject &o, plz::Trazo &t) {
    const QJsonArray pts = o["puntos"].toArray();
    if (pts.size() < 3 || pts.size() % 3 != 0) return false;
    t.color = QColor(o["color"].toString()).rgba();
    t.grosor = o["grosor"].toDouble(16.0);
    t.thinning = o["thin"].toDouble(0.5);
    t.streamline = o["suav"].toDouble(0.5);
    t.simular = o["simular"].toBool();
    t.borrar = o["borrar"].toBool();
    t.forma = o["forma"].toInt(0);
    t.relleno = o["relleno"].toBool();
    const QJsonArray rc = o["recorte"].toArray();
    for (int i = 0; i + 1 < rc.size(); i += 2) t.recorte.push_back({rc[i].toDouble(), rc[i + 1].toDouble()});
    for (int i = 0; i + 2 < pts.size(); i += 3)
        t.completo.push_back({pts[i].toDouble(), pts[i + 1].toDouble(), pts[i + 2].toDouble()});
    t.puntos = t.completo;
    return true;
}

// Tramos de un relleno como lista plana [y, x0, x1, y, x0, x1, ...]
inline QJsonArray spansAJson(const std::vector<plz::Span> &v) {
    QJsonArray a;
    for (const plz::Span &s : v) { a.append(s.y); a.append(s.x0); a.append(s.x1); }
    return a;
}
inline std::vector<plz::Span> spansDeJson(const QJsonArray &a) {
    std::vector<plz::Span> v;
    for (int i = 0; i + 2 < a.size(); i += 3) v.push_back({a[i].toInt(), a[i + 1].toInt(), a[i + 2].toInt()});
    return v;
}
inline QJsonObject rellenoAJson(const plz::Relleno &r) {
    QJsonObject o;
    o["tipo"] = "relleno";
    if (r.borrar) o["borrar"] = true;
    o["color"] = QColor::fromRgba(r.color).name(QColor::HexArgb);
    o["nucleo"] = spansAJson(r.nucleo);
    o["borde"] = spansAJson(r.borde);
    return o;
}
inline bool rellenoDeJson(const QJsonObject &o, plz::Relleno &r) {
    r.color = QColor(o["color"].toString()).rgba();
    r.borrar = o["borrar"].toBool();
    r.nucleo = spansDeJson(o["nucleo"].toArray());
    r.borde = spansDeJson(o["borde"].toArray());
    return !r.nucleo.empty();
}

inline QJsonObject pegadoAJson(const plz::Pegado &p) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    puente::lectura(p.img).save(&buf, "PNG");
    QJsonObject o;
    o["tipo"] = "pegado";
    o["x"] = p.x;
    o["y"] = p.y;
    o["imagen"] = QString::fromLatin1(bytes.toBase64());
    return o;
}

inline bool guardar(const plz::Documento &doc, const QString &ruta) {
    QJsonArray capasJson;
    for (const plz::Capa &c : doc.capas) {
        QJsonArray arr;
        for (const plz::Operacion &op : c.ops) {
            if (const plz::Trazo *t = std::get_if<plz::Trazo>(&op)) arr.append(trazoAJson(*t));
            else if (const plz::Relleno *r = std::get_if<plz::Relleno>(&op)) arr.append(rellenoAJson(*r));
            else arr.append(pegadoAJson(std::get<plz::Pegado>(op)));
        }
        QJsonObject o;
        o["nombre"] = QString::fromStdString(c.nombre);
        o["visible"] = c.visible;
        o["bloqueada"] = c.bloqueada;
        o["opacidad"] = c.opacidad;
        o["ops"] = arr;
        if (c.esPlantilla) {
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            puente::lectura(c.base.vacia() ? c.img : c.base).save(&buf, "PNG");
            o["plantilla"] = true;
            o["imagen"] = QString::fromLatin1(bytes.toBase64());
        }
        capasJson.append(o);
    }
    QJsonObject lienzo;
    lienzo["ancho"] = doc.ancho;
    lienzo["alto"] = doc.alto;
    QJsonObject raiz;
    raiz["formato"] = "PruebaLapiz";
    raiz["version"] = 4;
    raiz["lienzo"] = lienzo;
    raiz["capas"] = capasJson;

    QSaveFile f(ruta);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(raiz).toJson(QJsonDocument::Compact));
    return f.commit();
}

// Lee el archivo y reemplaza el contenido del documento. Necesita doc.pintor ya configurado.
inline bool abrir(plz::Documento &doc, const QString &ruta) {
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject()) return false;
    const QJsonObject raiz = jd.object();
    if (raiz["formato"].toString() != "PruebaLapiz") return false;

    const QJsonObject l = raiz["lienzo"].toObject();
    int w = l["ancho"].toInt(2000), h = l["alto"].toInt(1500);
    if (w < 100 || w > 6000 || h < 100 || h > 6000) { w = 2000; h = 1500; }

    int id = 1;
    std::vector<plz::Capa> nuevas;
    // Cada elemento es un trazo o un relleno; se aplican en el orden en que se hicieron
    auto cargarOps = [&](const QJsonArray &arr, plz::Capa &c) {
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            if (o["tipo"].toString() == "relleno") {
                plz::Relleno r;
                if (!rellenoDeJson(o, r)) continue;
                plz::aplicarRelleno(c.img, r);
                c.ops.push_back(std::move(r));
                continue;
            }
            if (o["tipo"].toString() == "pegado") {
                QImage im;
                im.loadFromData(QByteArray::fromBase64(o["imagen"].toString().toLatin1()), "PNG");
                if (im.isNull()) continue;
                plz::Pegado p{o["x"].toInt(), o["y"].toInt(), puente::aImagen(im)};
                plz::aplicarPegado(c.img, p);
                c.ops.push_back(std::move(p));
                continue;
            }
            plz::Trazo t;
            if (!trazoDeJson(o, t)) continue;
            t.contornos = t.forma ? plz::contornosDeForma(t) : std::vector<plz::Contorno>{plz::calcularContorno(t, true)};
            if (doc.pintor) doc.pintor(c.img, t);
            c.ops.push_back(std::move(t));
        }
    };
    if (raiz.contains("capas")) {
        for (const QJsonValue &v : raiz["capas"].toArray()) {
            const QJsonObject o = v.toObject();
            plz::Capa c(id, o["nombre"].toString(QString("Capa %1").arg(id)).toStdString(), w, h);
            id++;
            c.visible = o["visible"].toBool(true);
            c.bloqueada = o["bloqueada"].toBool(false);
            c.opacidad = o["opacidad"].toDouble(1.0);
            if (o["plantilla"].toBool()) {          // la base va primero; los trazos se pintan encima
                QImage im;
                im.loadFromData(QByteArray::fromBase64(o["imagen"].toString().toLatin1()), "PNG");
                if (!im.isNull()) {
                    puente::dibujarImagen(c.img, im, QRectF(0, 0, im.width(), im.height()), false);
                    c.base = c.img;
                    c.esPlantilla = true;
                }
            }
            cargarOps(o.contains("ops") ? o["ops"].toArray() : o["trazos"].toArray(), c);
            nuevas.push_back(std::move(c));
        }
    } else {                                        // archivos v1 y v2: una sola capa
        plz::Capa c(id++, "Capa 1", w, h);
        cargarOps(raiz["trazos"].toArray(), c);
        nuevas.push_back(std::move(c));
    }
    doc.cargar(w, h, std::move(nuevas), id);
    return true;
}

}  // namespace archivo
