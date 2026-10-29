#include "sectionview.hpp"

#include <QMouseEvent>
#include <QSettings>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <cmath>
#include "theme.hpp"

using namespace asec;

SectionView::SectionView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SectionView::clear() { has_ = false; img_ = QImage(); update(); }

void SectionView::setResult(const SectionResult& r, const QImage& img, double s0, double z1, double res, bool keepView) {
    bool first = !has_;
    double oldL = has_ ? SectionFrame(r_.line).L : 0;
    r_ = r; img_ = img; imgS0_ = s0; imgZ1_ = z1; imgRes_ = res; has_ = true;
    double L = SectionFrame(r.line).L;
    if (first || !keepView || std::fabs(L - oldL) > 0.02 * std::max(L, oldL)) fit();
    update();
}

QRectF SectionView::plotRect(const QRectF& a, double ui) const {
    return a.adjusted(60 * ui, 30 * ui, -54 * ui, -40 * ui);   // 위: A/A′ 글자만(제목은 보기 머리에)
}

void SectionView::fit() {
    zoomPending_ = 0; if (zoomTimer_) zoomTimer_->stop();
    if (!has_) return;
    QRectF pr = plotRect(rect(), 1.0);
    double L = SectionFrame(r_.line).L, zr = std::max(0.2, r_.zMax - r_.zMin);
    const double vex = std::max(1.0, xf_.vex);
    xf_.ppm = std::max(1e-3, std::min(pr.width() / (L * 1.03), pr.height() / (zr * vex * 1.06)));
    double oz = r_.srs.origin.z;
    xf_.s0 = L / 2 - pr.width() / 2 / xf_.ppm;
    xf_.zTop = (r_.zMin + r_.zMax) / 2 + oz + pr.height() / 2 / xf_.ppmZ();
    xf_.plot = pr;
}

void SectionView::resizeEvent(QResizeEvent*) {
    QRectF pr = plotRect(rect(), 1.0);
    if (has_ && xf_.plot.isValid()) {
        // 가운데 유지
        double sc = xf_.s0 + xf_.plot.width() / 2 / xf_.ppm, zc = xf_.zTop - xf_.plot.height() / 2 / xf_.ppmZ();
        xf_.plot = pr;
        xf_.s0 = sc - pr.width() / 2 / xf_.ppm;
        xf_.zTop = zc + pr.height() / 2 / xf_.ppmZ();
    }
    xf_.plot = pr;
}

static QString fmtDist(double s, double step) {
    int dec = step >= 1 ? 0 : step >= 0.1 ? 1 : 2;
    return QString::number(s, 'f', dec);
}

SectionDoc SectionView::doc() const {
    SectionDoc d; d.r = r_; d.st = st_; d.img = img_; d.imgS0 = imgS0_; d.imgZ1 = imgZ1_; d.imgRes = imgRes_;
    return d;
}

LevelPlan sectionLevelPlan(double ppm, double ui, bool forExport) {
    QFont small(theme::fontFamily()); small.setPixelSize(std::max(8, int(std::lround(11.5 * ui))));
    const double minLinePx = forExport ? 0.5 / 25.4 * 96.0 * ui : 4.0 * ui;   // 인쇄 0.5 mm / 화면 4 px
    return planLevels(ppm, minLinePx, QFontMetricsF(small).height() * 1.35, 10);
}

void paintSectionDoc(QPainter& p, const SectionDoc& d, const QRectF& area, const SectionXf& xf, double ui, const QImage& img, bool forExport,
                     const QString& footer, const SectionImgGeo* geo, bool busy_, bool titleRow) {
    const SectionResult& r_ = d.r;
    const SectionStyle& st_ = d.st;
    const SectionImgGeo ig = geo ? *geo : SectionImgGeo{d.imgS0, d.imgZ1, d.imgRes};
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(area, Qt::white);
    const QRectF pr = xf.plot;
    const double oz = r_.srs.origin.z;
    const SectionFrame f(r_.line);
    auto X = [&](double s) { return pr.left() + (s - xf.s0) * xf.ppm; };
    const double vex = forExport ? 1.0 : std::max(1.0, xf.vex);   // 내보내기는 언제나 1:1
    const double ppmZ = xf.ppm * vex;
    auto Y = [&](double zAbs) { return pr.top() + (xf.zTop - zAbs) * ppmZ; };
    const double sVis0 = xf.s0, sVis1 = xf.s0 + pr.width() / xf.ppm;
    const double zVis1 = xf.zTop, zVis0 = xf.zTop - pr.height() / ppmZ;
    QFont small = theme::uiFont(std::max(8, int(std::lround(11.5 * ui))));
    QFont mono = theme::monoFont(std::max(8, int(std::lround(11 * ui))));
    QFont monoBold = mono; monoBold.setBold(true);
    QFont title = theme::uiFont(std::max(6, int(std::lround(14 * ui))), true);
    QFontMetricsF fm(small);

    // 1) 세로 격자(거리) — 아주 옅게
    double dStep = niceStep(std::max(1e-3, sVis1 - sVis0), std::max(2, int(pr.width() / (90 * ui))));
    p.save();
    p.setClipRect(pr);
    {
        QPen g(QColor(0xF0, 0xEE, 0xE6), 1.0 * ui);
        p.setPen(g);
        for (long k = long(std::ceil(sVis0 / dStep)); k * dStep <= sVis1; ++k) p.drawLine(QPointF(X(k * dStep), pr.top()), QPointF(X(k * dStep), pr.bottom()));
    }
    // 2) 레벨선(맨 아래 층 — 영상·단면선 밑): 10 cm 얇고 옅게, 50 cm·1 m 조금 진하게. 너무 촘촘하면 생략
    //    간격 규칙(planLevels): 선 10 cm·숫자 50 cm 기본. 화면은 선 사이 4 px, 인쇄(내보내기)는 0.5 mm 보다 좁으면 50 cm·1 m·5 m 로 솎음.
    //    확대해도 10 cm 보다 촘촘하게 긋지 않음.
    std::vector<LevelLine> levels;
    LevelPlan lp = sectionLevelPlan(ppmZ, ui, forExport);   // 레벨선 간격은 세로 픽셀/m 기준
    // 세로 과장 화면: 10 cm 선마다 숫자가 겹치지 않으면 10 cm 숫자(얕은 기복 읽기). 내보내기·1:1 은 규칙 그대로(50 cm)
    if (vex > 1.0 && lp.lineCm == 10 && 0.1 * ppmZ >= fm.height() * 1.35) lp.labelCm = 10;
    if (st_.showLevels) {
        levels = levelLines(zVis0, zVis1, lp.lineCm);
        for (auto& lv : levels) {
            double y = Y(lv.z);
            QColor c; double w;
            // Strata 레벨선: 10 cm #DEDCD1 0.75 px, 50 cm #9C9A92 1 px, 1 m 조금 굵게
            if (lv.cls == LevelClass::Master) { c = theme::LevelMajor; w = 1.25; }
            else if (lv.cls == LevelClass::Major) { c = theme::LevelMajor; w = 1.0; }
            else { c = theme::LevelMinor; w = 0.75; }
            p.setPen(QPen(c, w * ui));
            p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        }
    }
    // 3) 입면 영상(레벨선 위)
    if (st_.showImage && !img.isNull()) {
        QRectF tr(X(ig.s0), Y(ig.z1Local + oz), img.width() * ig.res * xf.ppm, img.height() * ig.res * ppmZ);
        p.setOpacity(st_.imageOpacity);
        p.drawImage(tr, img);
        p.setOpacity(1.0);
    }
    // 4) 단면 한계(A, A′) 세로 점선
    {
        QPen lim(QColor(255, 0, 0, 90), 1.0 * ui, Qt::DashLine);
        p.setPen(lim);
        p.drawLine(QPointF(X(0), pr.top()), QPointF(X(0), pr.bottom()));
        p.drawLine(QPointF(X(f.L), pr.top()), QPointF(X(f.L), pr.bottom()));
    }
    // 5b) 기준선 EL(점선, 맨 위)
    if (st_.showBaseline && st_.baselineEl > zVis0 && st_.baselineEl < zVis1) {
        double y = Y(st_.baselineEl);
        p.setPen(QPen(theme::Ink2, 1.0 * ui, Qt::DashLine));
        p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        p.setFont(small);
        QString bt = QStringLiteral("기준선 EL. %1 m").arg(st_.baselineEl, 0, 'f', 2);
        QRectF br(pr.left() + 6 * ui, y - fm.height() - 2 * ui, fm.horizontalAdvance(bt) + 8 * ui, fm.height());
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 220)); p.drawRect(br);
        p.setPen(theme::Ink2); p.drawText(br, Qt::AlignCenter, bt);
    }
    // 5c) 세로 과장 표시(화면만): 도면이 아님을 분명히
    if (vex > 1.0) {
        p.setFont(small);
        QString vt = QStringLiteral("세로 ×%1 과장 — 화면 보기만 · 도면·내보내기는 1:1").arg(vex, 0, 'g', 3);
        QRectF vr(pr.left() + 8 * ui, pr.top() + 8 * ui, fm.horizontalAdvance(vt) + 16 * ui, fm.height() + 8 * ui);
        p.setPen(QPen(QColor(0xC9, 0x8A, 0x1B), 1.0 * ui)); p.setBrush(QColor(0xFF, 0xF4, 0xD6, 235)); p.drawRoundedRect(vr, 5 * ui, 5 * ui);
        p.setPen(QColor(0x7A, 0x5A, 0x00)); p.drawText(vr, Qt::AlignCenter, vt);
    }
    // 10) 축척 막대(그림 안 오른쪽 아래) — 단면선 밑에 그림
    if (st_.plotScaleBar) {
        double len = niceStep(110 * ui / xf.ppm, 1);
        double px = len * xf.ppm;
        QRectF sb(pr.right() - px - 14 * ui, pr.bottom() - 22 * ui, px, 5 * ui);
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 220));
        p.drawRoundedRect(sb.adjusted(-8 * ui, -16 * ui, 8 * ui, 8 * ui), 4 * ui, 4 * ui);
        p.setPen(QPen(theme::Ink, 1.0 * ui)); p.setBrush(Qt::white); p.drawRect(sb);
        p.setBrush(theme::Ink);
        for (int i = 0; i < 4; i += 2) p.drawRect(QRectF(sb.x() + px * i / 4, sb.y(), px / 4, sb.height()));
        p.setFont(small); p.setPen(theme::Ink);
        QString lab = len >= 1 ? QString::number(len, 'g', 4) + " m" : QString::number(len * 100, 'g', 4) + " cm";
        p.drawText(QRectF(sb.left(), sb.top() - 15 * ui, px, 13 * ui), Qt::AlignHCenter | Qt::AlignBottom, lab);
    }
    if (!forExport && busy_) {   // 「최종 계산 중」 상자도 단면선 밑
        p.setFont(small);
        QRectF b(pr.right() - 110 * ui, pr.top() + 8 * ui, 100 * ui, 22 * ui);
        p.setPen(QPen(theme::Edge, 1)); p.setBrush(Qt::white); p.drawRoundedRect(b, 6 * ui, 6 * ui);
        p.setPen(theme::Ink); p.drawText(b, Qt::AlignCenter, QStringLiteral("최종 계산 중…"));
    }
    // 5) 단면선(잘린 면): 그림 칸 안에서 맨 마지막 — 입면 배경·기준선·축척 막대·안내 상자가 절대 가리지 않음.
    //    순수 빨강 약 2 px(인쇄 0.35 mm), 안티에일리어싱, 둥근 이음. 닫힌 고리(나무·돌 덩어리)는 닫아서 그림
    if (st_.showLine) {
        p.setBrush(Qt::NoBrush);
        // 화면: 설정 굵기(기본 2 px) / 인쇄: 0.35 mm
        const double lw = forExport ? 0.35 / 25.4 * 96.0 * ui : std::clamp(st_.lineWidthPx, 1.0, 4.0) * ui;
        p.setPen(QPen(theme::ProfileRed, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        // 화면만: 0.1 px 보다 작은 굴곡은 그려도 보이지 않으므로 화면 좌표 0.1 px 허용으로 점을 솎아 그림(모든 꼭짓점이 선 중심에서 0.1 px 안 —
        // 화소 단위로 같은 그림, 끌기 중 다시 그리기가 빨라짐). 내보내기(PDF·DXF·영상)는 꼭짓점 전부 그대로
        const double screenTolM = forExport ? 0.0 : kScreenProfileTolPx / std::max(xf.ppm, ppmZ);
        for (const auto& pl0 : r_.profile) {
            if (pl0.size() < 2) continue;
            const bool closed = pl0.size() > 3 && (pl0.front() - pl0.back()).len() < 1e-9;
            const Polyline simp = screenTolM > 0 ? simplifyDP(pl0, screenTolM) : Polyline();
            const Polyline& pl = screenTolM > 0 && simp.size() >= 2 ? simp : pl0;
            QPainterPath path;
            path.moveTo(X(pl[0].x), Y(pl[0].y + oz));
            const size_t n = closed && pl.size() > 3 ? pl.size() - 1 : pl.size();
            for (size_t i = 1; i < n; ++i) path.lineTo(X(pl[i].x), Y(pl[i].y + oz));
            if (closed) path.closeSubpath();
            p.drawPath(path);
        }
    }
    p.restore();  // clip

    // 6) 테두리
    p.setPen(QPen(theme::Edge, 1.0 * ui));
    p.setBrush(Qt::NoBrush);
    p.drawRect(pr);

    // 7) 표고 라벨(왼쪽·오른쪽), 겹치지 않는 간격
    if (st_.showLevels) {
        const int lab = lp.labelCm;
        for (auto& lv : levels) {
            if (lv.cm % lab != 0) continue;
            double y = Y(lv.z);
            if (y < pr.top() + fm.height() * 0.3 || y > pr.bottom() - fm.height() * 0.3) continue;
            p.setFont(lv.cls == LevelClass::Master ? monoBold : mono);
            p.setPen(theme::LevelText);
            QString t = QString::fromStdString(formatElevation(lv.z));
            p.drawText(QRectF(area.left(), y - fm.height() / 2, pr.left() - area.left() - 8 * ui, fm.height()), Qt::AlignRight | Qt::AlignVCenter, t);
            p.setPen(QPen(theme::LevelMajor, 1.0 * ui));
            p.drawLine(QPointF(pr.left() - 5 * ui, y), QPointF(pr.left(), y));
            p.drawLine(QPointF(pr.right(), y), QPointF(pr.right() + 5 * ui, y));
            p.setPen(theme::LevelText);
            p.drawText(QRectF(pr.right() + 8 * ui, y - fm.height() / 2, 60 * ui, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, t);
        }
        p.setFont(small); p.setPen(theme::Idle);
        p.drawText(QRectF(area.left() + 4 * ui, pr.top() - fm.height() - 4 * ui, pr.left() - area.left(), fm.height()), Qt::AlignLeft, QStringLiteral("표고(m)"));
    }
    // 8) 거리축
    p.setFont(mono);
    for (long k = long(std::ceil(sVis0 / dStep)); k * dStep <= sVis1 + 1e-9; ++k) {
        double s = k * dStep, x = X(s);
        if (x < pr.left() - 0.5 || x > pr.right() + 0.5) continue;
        p.setPen(QPen(theme::Outline, 1.0 * ui));
        p.drawLine(QPointF(x, pr.bottom()), QPointF(x, pr.bottom() + 5 * ui));
        p.setPen(theme::LevelText);
        p.drawText(QRectF(x - 40 * ui, pr.bottom() + 6 * ui, 80 * ui, fm.height()), Qt::AlignHCenter | Qt::AlignTop, fmtDist(s, dStep));
    }
    p.setFont(small);
    p.setPen(theme::Idle);
    p.drawText(QRectF(pr.right() - 160 * ui, pr.bottom() + 6 * ui + fm.height(), 160 * ui, fm.height()), Qt::AlignRight, QStringLiteral("A 로부터 거리(m)"));
    // 9) A / A′ 표시
    p.setFont(title);
    for (int k = 0; k < 2; ++k) {
        double x = X(k ? f.L : 0);
        if (x < pr.left() - 20 || x > pr.right() + 20) continue;
        QRectF tr(x - 18 * ui, pr.top() - 24 * ui, 36 * ui, 20 * ui);
        p.setPen(theme::SectionRed);
        p.drawText(tr, Qt::AlignCenter, k ? QStringLiteral("A′") : QStringLiteral("A"));
    }
    // 11) 제목(내보내기 영상만 — 화면은 보기 머리·정보 띠가 대신, 도면은 표제란이 대신)
    if (titleRow) {
    p.setFont(title); p.setPen(theme::Ink);
    QString t1 = QStringLiteral("단면 A–A′");
    p.drawText(QRectF(area.left() + 12 * ui, area.top() + 8 * ui, 200 * ui, 20 * ui), Qt::AlignLeft | Qt::AlignVCenter, t1);
    p.setFont(small); p.setPen(theme::InkSub);
    auto cmText = [](int cm) { return cm < 100 ? QStringLiteral("%1 cm").arg(cm) : QStringLiteral("%1 m").arg(cm / 100); };
    QString lvText = st_.showLevels ? QStringLiteral("  ·  레벨선 %1 / 숫자 %2").arg(cmText(lp.lineCm), cmText(lp.labelCm)) : QString();
    QString t2 = QStringLiteral("길이 %1 m  ·  두께 앞 %2 / 뒤 %3 m  ·  %4%5  ·  가로:세로 1:1")
                     .arg(f.L, 0, 'f', 2).arg(r_.line.front, 0, 'f', 2).arg(r_.line.back, 0, 'f', 2)
                     .arg(QString::fromStdString(r_.srs.describe().labelKo()), lvText);
    p.drawText(QRectF(area.left() + 12 * ui + QFontMetricsF(title).horizontalAdvance(t1) + 12 * ui, area.top() + 8 * ui, area.width(), 20 * ui), Qt::AlignLeft | Qt::AlignVCenter, t2);
    }
    if (!footer.isEmpty()) {
        p.setPen(theme::Idle);
        p.drawText(QRectF(area.left() + 12 * ui, area.bottom() - fm.height() - 4 * ui, area.width() - 24 * ui, fm.height()), Qt::AlignRight, footer);
    }
    p.restore();
}

QSize sectionExportLayout(const SectionDoc& d, double ppm, double ui, SectionXf& xf) {
    const SectionResult& r_ = d.r;
    double L = SectionFrame(r_.line).L, zr = r_.zMax - r_.zMin;
    // 영상 픽셀이 출력 픽셀과 1:1 로 겹치도록 여백·원점을 정수 픽셀에 맞춤(재표본 흐림 없음)
    double padS = std::round(std::max(0.02 * L, 12 * ui / ppm) * ppm) / ppm;
    double w = std::ceil((L + 2 * padS) * ppm), h = std::ceil(zr * ppm);
    double left = std::round(74 * ui), top = std::round(46 * ui);
    QSize sz(int(left + w + std::ceil(54 * ui)), int(top + h + std::ceil((44 + 18) * ui)));
    xf.ppm = ppm;
    xf.plot = QRectF(left, top, w, h);
    xf.s0 = -padS;
    xf.zTop = r_.zMax + r_.srs.origin.z;
    return sz;
}

void SectionView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (!has_) {
        p.fillRect(rect(), Qt::white);
        p.setRenderHint(QPainter::Antialiasing);
        // 빈 격자 + 안내
        p.setPen(QPen(theme::Desk, 1));
        for (int x = 0; x < width(); x += 24) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 24) p.drawLine(0, y, width(), y);
        QRectF r(0, 0, std::min(400, width() - 40), 110);
        r.moveCenter(QPointF(width() / 2.0, height() / 2.0));
        p.setPen(QPen(theme::Edge, 1)); p.setBrush(theme::Card); p.drawRoundedRect(r, 8, 8);
        p.setFont(theme::uiFont(15, true)); p.setPen(theme::Ink);
        p.drawText(r.adjusted(16, 18, -16, -50), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("단면선을 그리세요"));
        p.setFont(theme::uiFont(12)); p.setPen(theme::Muted);
        p.drawText(r.adjusted(16, 52, -16, -8), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   QStringLiteral("「단면선 그리기」(S) 후 평면에서 A, A′ 두 점을 클릭\n끝점을 끌면 단면이 바로 바뀝니다"));
        return;
    }
    SectionDoc d; d.r = r_; d.st = st_; d.imgS0 = imgS0_; d.imgZ1 = imgZ1_; d.imgRes = imgRes_;
    paintSectionDoc(p, d, rect(), xf_, 1.0, img_, false, QString(), nullptr, busy_, false);
    static thread_local double lastPpm = -1;
    if (onViewChanged && xf_.ppm != lastPpm) { lastPpm = xf_.ppm; QTimer::singleShot(0, this, [this] { if (onViewChanged) onViewChanged(); }); }
}

void SectionView::wheelEvent(QWheelEvent* e) {
    if (!has_) return;
    double notches = e->angleDelta().y() / 120.0;
    if (notches == 0 && !e->pixelDelta().isNull()) notches = e->pixelDelta().y() / 60.0;
    wheelZoom(e->position(), notches);
    e->accept();
}

bool SectionView::screenToSZ(const QPointF& m, double& s, double& z) const {
    if (!has_ || xf_.ppm <= 0) return false;
    s = xf_.s0 + (m.x() - xf_.plot.left()) / xf_.ppm;
    z = xf_.zTop - (m.y() - xf_.plot.top()) / xf_.ppmZ();
    return true;
}

void SectionView::applyZoomAt(const QPointF& m, double f) {
    double s, z;
    if (!screenToSZ(m, s, z)) return;
    xf_.ppm = std::clamp(xf_.ppm * f, 0.5, 50000.0);
    xf_.s0 = s - (m.x() - xf_.plot.left()) / xf_.ppm;
    xf_.zTop = z + (m.y() - xf_.plot.top()) / xf_.ppmZ();
    update();
}

void SectionView::wheelZoom(const QPointF& at, double notches) {
    if (!has_ || notches == 0) return;
    const double step = std::log(1.18) * notches;
    if (!QSettings().value("view/smoothZoom", true).toBool()) { zoomPending_ = 0; applyZoomAt(at, std::exp(step)); return; }
    zoomAnchor_ = at;
    zoomPending_ += step;
    if (!zoomTimer_) {
        zoomTimer_ = new QTimer(this);
        zoomTimer_->setInterval(16);
        QObject::connect(zoomTimer_, &QTimer::timeout, this, [this] {
            double d = std::fabs(zoomPending_) < 0.004 ? zoomPending_ : zoomPending_ * 0.4;
            zoomPending_ -= d;
            if (std::fabs(zoomPending_) < 1e-9) { zoomPending_ = 0; zoomTimer_->stop(); }
            applyZoomAt(zoomAnchor_, std::exp(d));
        });
    }
    if (!zoomTimer_->isActive()) zoomTimer_->start();
}

void SectionView::mousePressEvent(QMouseEvent* e) { panning_ = true; last_ = e->pos(); setCursor(Qt::ClosedHandCursor); }
void SectionView::mouseReleaseEvent(QMouseEvent*) { panning_ = false; setCursor(Qt::ArrowCursor); }
void SectionView::mouseDoubleClickEvent(QMouseEvent*) { fit(); update(); }
void SectionView::leaveEvent(QEvent*) { if (onCursor) onCursor(0, 0, 0, 0, false); }

void SectionView::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        QPoint d = e->pos() - last_; last_ = e->pos();
        xf_.s0 -= d.x() / xf_.ppm; xf_.zTop += d.y() / xf_.ppmZ();
        update();
    }
    if (has_ && onCursor) {
        double s = xf_.s0 + (e->position().x() - xf_.plot.left()) / xf_.ppm, z = xf_.zTop - (e->position().y() - xf_.plot.top()) / xf_.ppmZ();
        Vec3 w = sectionToWorld(r_, s, z - r_.srs.origin.z);
        onCursor(s, z, w.x, w.y, xf_.plot.contains(e->position()));
    }
}

double SectionView::screenDenom() const {
    return xf_.ppm > 0 ? logicalDpiX() / (0.0254 * xf_.ppm) : 0;
}

void SectionView::setScreenDenom(double d) {
    if (!has_ || d <= 0) return;
    zoomPending_ = 0; if (zoomTimer_) zoomTimer_->stop();
    zoomBy((logicalDpiX() / (0.0254 * d)) / xf_.ppm);
}

void SectionView::zoomBy(double f) {
    if (!has_) return;
    zoomPending_ = 0; if (zoomTimer_) zoomTimer_->stop();
    QPointF m = xf_.plot.center();
    double s = xf_.s0 + (m.x() - xf_.plot.left()) / xf_.ppm, z = xf_.zTop - (m.y() - xf_.plot.top()) / xf_.ppmZ();
    xf_.ppm = std::clamp(xf_.ppm * f, 0.5, 50000.0);
    xf_.s0 = s - (m.x() - xf_.plot.left()) / xf_.ppm;
    xf_.zTop = z + (m.y() - xf_.plot.top()) / xf_.ppmZ();
    update();
}

void SectionView::setVerticalExaggeration(double v) {
    v = std::clamp(v, 1.0, 20.0);
    if (v == xf_.vex) return;
    zoomPending_ = 0; if (zoomTimer_) zoomTimer_->stop();
    if (!has_ || !xf_.plot.isValid()) { xf_.vex = v; update(); return; }
    // 세로 가운데: 보이는 가로 범위 안 단면선 높이의 가운데(없으면 지금 화면 가운데)
    double zc = xf_.zTop - xf_.plot.height() / 2 / xf_.ppmZ();
    {
        const double sa = xf_.s0, sb = xf_.s0 + xf_.plot.width() / xf_.ppm;
        double lo = 1e18, hi = -1e18;
        for (auto& pl : r_.profile) for (auto& q : pl) if (q.x >= sa && q.x <= sb) { lo = std::min(lo, q.y); hi = std::max(hi, q.y); }
        if (hi >= lo) zc = (lo + hi) / 2 + r_.srs.origin.z;
    }
    xf_.vex = v;
    xf_.zTop = zc + xf_.plot.height() / 2 / xf_.ppmZ();
    update();
    if (onViewChanged) onViewChanged();
}

double SectionView::fitVisibleHeight() const {
    if (!has_) return 0;
    QRectF pr = plotRect(rect(), 1.0);
    double L = SectionFrame(r_.line).L, zr = std::max(0.2, r_.zMax - r_.zMin);
    double ppm = std::max(1e-3, std::min(pr.width() / (L * 1.03), pr.height() / (zr * 1.06)));
    return pr.height() / ppm;
}
