#pragma once
// Port a C++ de perfect-freehand (MIT License, (c) Steve Ruiz)
// https://github.com/steveruizok/perfect-freehand
// Mantén este aviso de licencia si distribuyes el código.
#include <vector>
#include <cmath>
#include <algorithm>

namespace pf {

struct Vec { double x = 0, y = 0; };
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
inline Vec operator*(Vec a, double s) { return {a.x * s, a.y * s}; }
inline double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
inline double dist2(Vec a, Vec b) { Vec d = a - b; return dot(d, d); }
inline double dist(Vec a, Vec b) { return std::sqrt(dist2(a, b)); }
inline Vec uni(Vec a) { double l = std::hypot(a.x, a.y); return l > 0 ? Vec{a.x / l, a.y / l} : Vec{0, 0}; }
inline Vec per(Vec a) { return {a.y, -a.x}; }
inline Vec lrp(Vec a, Vec b, double t) { return a + (b - a) * t; }
inline Vec rotAround(Vec a, Vec c, double r) {
    const double s = std::sin(r), co = std::cos(r);
    const double px = a.x - c.x, py = a.y - c.y;
    return {px * co - py * s + c.x, px * s + py * co + c.y};
}

struct InPoint { Vec p; double pressure = -1; };   // pressure < 0 = desconocida

struct Options {
    double size = 16;
    double thinning = 0.5;
    double smoothing = 0.5;
    double streamline = 0.5;
    bool simulatePressure = true;
    bool capStart = true, capEnd = true;
    double taperStart = 0, taperEnd = 0;   // 0 = sin adelgazado
    bool last = false;                     // true cuando el trazo ya terminó
};

struct StrokePoint {
    Vec point; double pressure; Vec vector; double distance; double runningLength;
};

inline std::vector<StrokePoint> getStrokePoints(std::vector<InPoint> pts, const Options &o) {
    std::vector<StrokePoint> out;
    if (pts.empty()) return out;
    const double t = 0.15 + (1 - o.streamline) * 0.85;

    if (pts.size() == 2) {
        InPoint last = pts[1];
        pts.pop_back();
        for (int i = 1; i < 5; ++i) {
            InPoint n;
            n.p = lrp(pts[0].p, last.p, i / 4.0);
            n.pressure = (pts[0].pressure >= 0 && last.pressure >= 0)
                             ? pts[0].pressure + (last.pressure - pts[0].pressure) * (i / 4.0) : -1;
            pts.push_back(n);
        }
    }
    if (pts.size() == 1) { InPoint n; n.p = pts[0].p + Vec{1, 1}; pts.push_back(n); }

    out.push_back({pts[0].p, pts[0].pressure >= 0 ? pts[0].pressure : 0.25, {1, 1}, 0, 0});
    bool reached = false;
    double running = 0;
    StrokePoint prev = out[0];
    const size_t max = pts.size() - 1;

    for (size_t i = 1; i < pts.size(); ++i) {
        Vec point = (o.last && i == max) ? pts[i].p : lrp(prev.point, pts[i].p, t);
        if (prev.point.x == point.x && prev.point.y == point.y) continue;
        const double d = dist(point, prev.point);
        running += d;
        if (i < max && !reached) {
            if (running < o.size) continue;
            reached = true;
        }
        prev = {point, pts[i].pressure >= 0 ? pts[i].pressure : 0.5,
                uni(prev.point - point), d, running};
        out.push_back(prev);
    }
    out[0].vector = out.size() > 1 ? out[1].vector : Vec{0, 0};
    return out;
}

inline double strokeRadius(double size, double thinning, double pressure) {
    return size * (0.5 - thinning * (0.5 - pressure));
}

// Devuelve el contorno (polígono) del trazo
inline std::vector<Vec> getStrokeOutlinePoints(const std::vector<StrokePoint> &points, const Options &o) {
    const double RATE = 0.275;
    const double FIXED_PI = 3.14159265358979323846 + 0.0001;
    std::vector<Vec> leftPts, rightPts;
    if (points.empty() || o.size <= 0) return leftPts;

    const double totalLength = points.back().runningLength;
    const double minDistance = std::pow(o.size * o.smoothing, 2);
    const size_t n = points.size();

    double prevPressure = points[0].pressure;
    for (size_t i = 0; i < std::min<size_t>(10, n); ++i) {
        double pressure = points[i].pressure;
        if (o.simulatePressure) {
            const double sp = std::min(1.0, points[i].distance / o.size);
            const double rp = std::min(1.0, 1 - sp);
            pressure = std::min(1.0, prevPressure + (rp - prevPressure) * (sp * RATE));
        }
        prevPressure = (prevPressure + pressure) / 2;
    }

    double radius = strokeRadius(o.size, o.thinning, points.back().pressure);
    bool hasFirst = false; double firstRadius = 0;
    Vec prevVector = points[0].vector;
    Vec pl = points[0].point, pr = pl, tl = pl, tr = pr;
    bool prevSharp = false;

    for (size_t i = 0; i < n; ++i) {
        double pressure = points[i].pressure;
        const Vec point = points[i].point, vector = points[i].vector;
        const double distance = points[i].distance, runLen = points[i].runningLength;

        if (i < n - 1 && totalLength - runLen < 3) continue;

        if (o.thinning != 0) {
            if (o.simulatePressure) {
                const double sp = std::min(1.0, distance / o.size);
                const double rp = std::min(1.0, 1 - sp);
                pressure = std::min(1.0, prevPressure + (rp - prevPressure) * (sp * RATE));
            }
            radius = strokeRadius(o.size, o.thinning, pressure);
        } else {
            radius = o.size / 2;
        }
        if (!hasFirst) { firstRadius = radius; hasFirst = true; }

        double tsStrength = 1, teStrength = 1;
        if (o.taperStart > 0 && runLen < o.taperStart) {
            const double x = runLen / o.taperStart; tsStrength = x * (2 - x);
        }
        if (o.taperEnd > 0 && totalLength - runLen < o.taperEnd) {
            const double x = (totalLength - runLen) / o.taperEnd - 1; teStrength = x * x * x + 1;
        }
        radius = std::max(0.01, radius * std::min(tsStrength, teStrength));

        const Vec nextVector = (i < n - 1) ? points[i + 1].vector : points[i].vector;
        const double nextDpr = (i < n - 1) ? dot(vector, nextVector) : 1.0;
        const double prevDpr = dot(vector, prevVector);

        const bool sharp = prevDpr < 0 && !prevSharp;
        const bool nextSharp = nextDpr < 0;

        if (sharp || nextSharp) {
            const Vec offset = per(prevVector) * radius;
            for (double step = 1.0 / 13, t = 0; t <= 1; t += step) {
                tl = rotAround(point - offset, point, FIXED_PI * t);
                leftPts.push_back(tl);
                tr = rotAround(point + offset, point, FIXED_PI * -t);
                rightPts.push_back(tr);
            }
            pl = tl; pr = tr;
            if (nextSharp) prevSharp = true;
            continue;
        }
        prevSharp = false;

        if (i == n - 1) {
            const Vec offset = per(vector) * radius;
            leftPts.push_back(point - offset);
            rightPts.push_back(point + offset);
            continue;
        }

        const Vec offset = per(lrp(nextVector, vector, nextDpr)) * radius;
        tl = point - offset;
        if (i <= 1 || dist2(pl, tl) > minDistance) { leftPts.push_back(tl); pl = tl; }
        tr = point + offset;
        if (i <= 1 || dist2(pr, tr) > minDistance) { rightPts.push_back(tr); pr = tr; }

        prevPressure = pressure;
        prevVector = vector;
    }

    const Vec firstPoint = points[0].point;
    const Vec lastPoint = n > 1 ? points.back().point : points[0].point + Vec{1, 1};

    std::vector<Vec> startCap, endCap;

    if (n == 1) {
        if (!(o.taperStart > 0 || o.taperEnd > 0) || o.last) {
            const Vec start = firstPoint + uni(per(firstPoint - lastPoint)) * (-(hasFirst ? firstRadius : radius));
            std::vector<Vec> dot_;
            for (double step = 1.0 / 13, t = step; t <= 1; t += step)
                dot_.push_back(rotAround(start, firstPoint, FIXED_PI * 2 * t));
            return dot_;
        }
    } else {
        if (o.taperStart > 0) {
            // inicio adelgazado: sin tapa
        } else if (o.capStart && !rightPts.empty()) {
            for (double step = 1.0 / 13, t = step; t <= 1; t += step)
                startCap.push_back(rotAround(rightPts[0], firstPoint, FIXED_PI * t));
        } else if (!leftPts.empty() && !rightPts.empty()) {
            const Vec cv = leftPts[0] - rightPts[0];
            const Vec a = cv * 0.5, b = cv * 0.51;
            startCap = {firstPoint - a, firstPoint - b, firstPoint + b, firstPoint + a};
        }

        const Vec direction = per(points.back().vector * -1.0);
        if (o.taperEnd > 0) {
            endCap.push_back(lastPoint);
        } else if (o.capEnd) {
            const Vec start = lastPoint + direction * radius;
            for (double step = 1.0 / 29, t = step; t < 1; t += step)
                endCap.push_back(rotAround(start, lastPoint, FIXED_PI * 3 * t));
        } else {
            endCap = {lastPoint + direction * radius, lastPoint + direction * (radius * 0.99),
                      lastPoint - direction * (radius * 0.99), lastPoint - direction * radius};
        }
    }

    std::vector<Vec> res = leftPts;
    res.insert(res.end(), endCap.begin(), endCap.end());
    res.insert(res.end(), rightPts.rbegin(), rightPts.rend());
    res.insert(res.end(), startCap.begin(), startCap.end());
    return res;
}

inline std::vector<Vec> getStroke(const std::vector<InPoint> &pts, const Options &o) {
    return getStrokeOutlinePoints(getStrokePoints(pts, o), o);
}

} // namespace pf