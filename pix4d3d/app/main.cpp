// 발굴 단면뷰어 진입점. 명령줄 자동화(화면 캡처·내보내기 시험용):
//   SectionViewer [파일.3mx|.obj] [--line AX AY BX BY] [--local] [--front m] [--back m] [--size WxH] [--tab N]
//                 [--shot out.png] [--export-png f] [--export-tiff f] [--export-geotiff f] [--export-dxf f] [--dxf3d]
//                 [--export-plan f] [--plan-view] [--export-xyz f] [--export-las f] [--area whole|band|view] [--spacing m] [--norgb]
//                 [--scale N] [--dpi N] [--log f] [--perf-log f.csv] [--pick X Y]... [--hover] [--height-datum KEY] [--quit]
//   --wheel-test: 평면·단면 휠 확대/축소(커서 고정 오차 px, 애니메이션 시간, 프레임) + 레벨선 간격 기록
//   --depth-fade on|off: 입면 깊이 음영(기본 설정값)
//   --height-datum KEY: 높이 기준 지정(이름표만, 값 변환·저장 없음) srs|ellipsoidal|egm96|egm2008|kvd1964|kngeoid
//   --pick X Y: 그 실좌표(--local 이면 로컬)에서 잎 메시 연직 정밀 피킹 → 로그(Z, 출처, 시간)
//   --hover: 평면 보기 가운데로 마우스 이동을 흉내 → 좌표줄 Z 와 Z 출처(대략 → 잎 표면)를 로그
//   --perf-log: 평면 보기 카메라 경로(맞춤→확대→이동→축소)를 재생하며 프레임마다 시간·LOD 상태를 CSV 로 기록
//   1.2: --settings DIR(설정을 DIR 의 INI 로 — 시험용) --export-pdf f / --export-sheet-png f / --export-sheet-tiff f / --export-sheet-dxf f
//        [--paper A4L|A4P|A3L|A3P] [--sheet-scale N] [--split] --export-dialog-shot f --undo-test --ctx-shot f
//        --extra-line AX AY BX BY(단면 목록에 더함, 반복 가능) --start-shot f(파일 없이: 시작 화면)
//   --cut-check: 잘린 면 정확도(단면선 윗면 vs 잎 연직 피킹, 미리보기 vs 최종) + 단면선이 배경 위에 순수 빨강으로 그려졌는지(화면·내보내기)
//   --vex N: 단면 화면 세로 과장(1·2·5·10, 화면만) / 창 제목 중복 검사는 항상 로그(window-title=…)
//   --plan-cam X Y mpp: 캡처 전에 평면 카메라를 실좌표 중심·m/px 로(평면-단면 정합 확인)
#include <QApplication>
#include <QPainter>
#include <algorithm>
#include <QDialog>
#include <QSettings>
#include <array>
#include <memory>
#include <QFile>
#include <QScreen>
#include <QFontDatabase>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include "asec/pick.hpp"
#include "mainwindow.hpp"
#include "theme.hpp"

using namespace asec;
extern const char* const kVersion;

static void processFor(int ms) {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) { QApplication::processEvents(QEventLoop::AllEvents, 20); }
}

// --cut-check: 잘린 면이 정확한지(잎 피킹 대비 mm), 미리보기(거친 배경)에서도 단면선은 잎인지, 단면선이 배경 위에 그려지는지
template <class LogFn>
static void runCutCheck(MainWindow& w, LogFn log) {
    auto src = w.source();
    const SectionDoc d = w.sectionDoc();
    const SectionResult& r = d.r;
    const Vec3 o = r.srs.origin;
    const SectionFrame fr(r.line);
    // 1) 1 cm 간격 s 마다: 단면선(모든 선분 보간)의 가장 높은 점 vs 그 XY 의 잎 메시 연직 피킹(윗면). 닫힌 고리(덤불 수관) 포함
    std::vector<double> diff; size_t steep = 0;
    for (double sx = 0.005; sx < fr.L; sx += 0.01) {
        double zTop = -1e300, slope = 0;
        for (auto& pl : r.profile)
            for (size_t i = 1; i < pl.size(); ++i) {
                const Vec2 a = pl[i - 1], b = pl[i];
                const double lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
                if (sx < lo || sx > hi || hi - lo < 1e-9) continue;
                const double z = a.y + (b.y - a.y) * (sx - a.x) / (b.x - a.x);
                if (z > zTop) { zTop = z; slope = std::fabs((b.y - a.y) / (b.x - a.x)); }
            }
        if (zTop < -1e299) continue;
        if (slope > 3) { ++steep; continue; }   // 거의 수직인 벽: 연직 피킹으로 비교할 수 없음
        const Vec2 xy = fr.planXY(sx, 0);
        PickResult pr; std::string e;
        if (pickVertical(*src, xy.x, xy.y, pr, &e) && pr.hit) {
            const double dd = std::fabs(pr.local.z - zTop);
            diff.push_back(dd);
            if (dd > 0.005) log(QStringLiteral("cut-accuracy outlier: s=%1 profile-top=%2 pick=%3 (Δ %4 mm)").arg(sx, 0, 'f', 3).arg(zTop + o.z, 0, 'f', 3).arg(pr.world.z, 0, 'f', 3).arg(dd * 1000, 0, 'f', 1));
        }
    }
    std::sort(diff.begin(), diff.end());
    auto q = [&](double f) { return diff.empty() ? 0.0 : diff[size_t(f * (diff.size() - 1))]; };
    size_t n5 = 0, n1 = 0; for (double x : diff) { n5 += x <= 0.005; n1 += x <= 0.001; }
    std::vector<double> in5(diff.begin(), diff.begin() + n5);   // 5 mm 안 표본(이탈은 위에 하나씩 기록)
    auto q5 = [&](double f) { return in5.empty() ? 0.0 : in5[size_t(f * (in5.size() - 1))]; };
    log(QStringLiteral("cut-accuracy: back=%1m samples=%2@1cm (walls>3:1 skipped %3) |profile-top − leaf-pick| median=%4mm p95=%5mm p99=%6mm max=%7mm")
            .arg(r.line.back, 0, 'f', 2).arg(diff.size()).arg(steep).arg(q(0.5) * 1000, 0, 'f', 2).arg(q(0.95) * 1000, 0, 'f', 2).arg(q(0.99) * 1000, 0, 'f', 2).arg(q(1.0) * 1000, 0, 'f', 2));
    log(QStringLiteral("cut-accuracy: within1mm=%1/%2 within5mm=%3/%2 outliers>5mm=%4 max-within5mm=%5mm")
            .arg(n1).arg(diff.size()).arg(n5).arg(diff.size() - n5).arg(q5(1.0) * 1000, 0, 'f', 2));
    // 2) 미리보기(배경 거친 LOD) 단면선 = 최종(잎) 단면선?
    {
        SectionRequest rq; rq.line = r.line; rq.meshRes = std::max(0.008, fr.L / 700.0); rq.imageRes = rq.meshRes; rq.wantImage = true;
        SectionOutput pre; std::string e;
        rq.meshRes = rq.imageRes;
        if (computeSection(*src, rq, pre, &e)) {
            size_t nA = 0, nB = 0; double dmax = 0;
            for (auto& pl : r.profile) nA += pl.size();
            for (auto& pl : pre.result.profile) nB += pl.size();
            if (nA == nB) { auto ia = r.profile.begin(); for (auto& pl : pre.result.profile) { for (size_t i = 0; i < pl.size(); ++i) dmax = std::max(dmax, (pl[i] - (*ia)[i]).len()); ++ia; } }
            log(QStringLiteral("cut-preview: image-lod-depth=%1 cut-from-leaf=%2 cut-depth=%3 vertices preview=%4 final=%5 max-diff=%6mm")
                    .arg(pre.stats.maxDepth).arg(pre.cutFromLeaf ? 1 : 0).arg(pre.cutStats.maxDepth).arg(nB).arg(nA).arg(nA == nB ? QString::number(dmax * 1000, 'f', 3) : QStringLiteral("n/a")));
        }
    }
    // 3) 단면선이 배경 위(순수 빨강)인지: 화면 그림과 내보내기 그림에서 단면선 꼭짓점 자리 픽셀
    auto onTop = [&](const QImage& im, const SectionXf& xf, bool exp, int& n) {
        int red = 0; n = 0;
        const double ppmZ = exp ? xf.ppm : xf.ppmZ();
        for (auto& pl : r.profile)
            for (size_t i = 1; i + 1 < pl.size(); ++i) {
                const double x = xf.plot.left() + (pl[i].x - xf.s0) * xf.ppm, y = xf.plot.top() + (xf.zTop - (pl[i].y + o.z)) * ppmZ;
                if (!xf.plot.adjusted(2, 2, -2, -2).contains(QPointF(x, y))) continue;
                ++n; bool hit = false;
                for (int dy = -1; dy <= 1 && !hit; ++dy) for (int dx = -1; dx <= 1 && !hit; ++dx) {
                    QRgb c = im.pixel(int(x) + dx, int(y) + dy);
                    hit = qRed(c) >= 250 && qGreen(c) <= 8 && qBlue(c) <= 8;
                }
                red += hit;
            }
        return red;
    };
    {
        SectionView* sv = w.sectionView();
        QImage im = sv->grab().toImage().convertToFormat(QImage::Format_RGB32);
        const double dpr = im.devicePixelRatio();
        SectionXf xf = sv->xf();
        if (dpr != 1.0) { xf.plot = QRectF(xf.plot.topLeft() * dpr, xf.plot.size() * dpr); xf.ppm *= dpr; }
        int n = 0; int red = onTop(im, xf, false, n);
        log(QStringLiteral("cut-on-top screen: vertices=%1 pure-red=%2 (%3%)").arg(n).arg(red).arg(n ? 100.0 * red / n : 0, 0, 'f', 1));
        SectionXf ex; const double ppm = 1000.0 / 40 / 25.4 * 150;   // 1:40, 150 dpi
        QSize sz = sectionExportLayout(d, ppm, 150 / 96.0, ex);
        QImage img(sz, QImage::Format_RGB32); img.fill(Qt::white);
        { QPainter p(&img); SectionImgGeo g{d.imgS0, d.imgZ1, d.imgRes}; paintSectionDoc(p, d, QRectF(QPointF(0, 0), QSizeF(sz)), ex, 150 / 96.0, d.img, true, QString(), &g); }
        int n2 = 0; int red2 = onTop(img, ex, true, n2);
        log(QStringLiteral("cut-on-top export(1:40,150dpi): vertices=%1 pure-red=%2 (%3%) image=%4x%5").arg(n2).arg(red2).arg(n2 ? 100.0 * red2 / n2 : 0, 0, 'f', 1).arg(sz.width()).arg(sz.height()));
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--settings") {   // 시험용: 사용자 설정을 건드리지 않게
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromLocal8Bit(argv[i + 1]));
        }
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QSurfaceFormat fmt; fmt.setSamples(4); fmt.setDepthBufferSize(24); QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("ExcavSection");
    QApplication::setApplicationName("SectionViewer");
    // 표시 이름 = 창 제목 끝과 같게(「… — 발굴 단면뷰어 x.y.z」). Qt(Windows·X11)는 제목이 표시 이름으로 끝나지 않으면
    // 「 - 표시 이름」을 덧붙여 「발굴 단면뷰어」가 두 번 보였음(1.2.0 신고)
    QApplication::setApplicationDisplayName(QStringLiteral("발굴 단면뷰어 %1").arg(QString::fromUtf8(kVersion)));
    QFont f = theme::uiFont(13); f.setStyleStrategy(QFont::PreferAntialias);   // 한글 본문 13 px
    QApplication::setFont(f);
    app.setStyle("Fusion");
    app.setStyleSheet(theme::styleSheet(QSettings().value("ui/highContrast", false).toBool()));
    QIcon ic; for (const char* r : {":/app_256.png"}) ic.addFile(r);
    ic.addFile(QApplication::applicationDirPath() + "/app.ico");
    app.setWindowIcon(ic);

    QStringList a = app.arguments();
    QString file, shot, logPath, perfPath, heightDatum, depthFade, dialogShot, ctxShot, paperArg, startShot;
    bool undoTest = false, split = false; double sheetScale = 0, sectionScale = 0;
    QString lodShot;
    double camX = 0, camY = 0, camMpp = 0, vexArg = 0;
    bool cutCheck = false;
    std::vector<std::array<double, 4>> extraLines;
    bool haveLine = false, local = false, quit = false;
    double ax = 0, ay = 0, bx = 0, by = 0, front = -1, back = -1, denom = 20, dpi = 300, spacing = 0;
    int W = 0, H = 0, tab = -1;
    bool dxf3d = false, rgb = true, planView = false, hover = false, wheelTest = false;
    std::vector<std::pair<double, double>> picks;
    QString area = "whole";
    QList<QPair<QString, QString>> exports;
    for (int i = 1; i < a.size(); ++i) {
        QString s = a[i];
        auto nx = [&]() { return i + 1 < a.size() ? a[++i] : QString(); };
        if (s == "--line" && i + 4 < a.size()) { ax = a[i + 1].toDouble(); ay = a[i + 2].toDouble(); bx = a[i + 3].toDouble(); by = a[i + 4].toDouble(); i += 4; haveLine = true; }
        else if (s == "--local") local = true;
        else if (s == "--height-datum") heightDatum = nx();
        else if (s == "--depth-fade") depthFade = nx();
        else if (s == "--wheel-test") wheelTest = true;
        else if (s == "--front") front = nx().toDouble();
        else if (s == "--back") back = nx().toDouble();
        else if (s == "--shot") shot = nx();
        else if (s == "--size") { QStringList p = nx().split('x'); if (p.size() == 2) { W = p[0].toInt(); H = p[1].toInt(); } }
        else if (s == "--tab") tab = nx().toInt();
        else if (s == "--scale") denom = nx().toDouble();
        else if (s == "--dpi") dpi = nx().toDouble();
        else if (s == "--spacing") spacing = nx().toDouble();
        else if (s == "--area") area = nx();
        else if (s == "--dxf3d") dxf3d = true;
        else if (s == "--norgb") rgb = false;
        else if (s == "--plan-view") planView = true;
        else if (s == "--log") logPath = nx();
        else if (s == "--perf-log") perfPath = nx();
        else if (s == "--quit") quit = true;
        else if (s == "--pick" && i + 2 < a.size()) { picks.push_back({a[i + 1].toDouble(), a[i + 2].toDouble()}); i += 2; }
        else if (s == "--hover") hover = true;
        else if (s == "--vex") vexArg = nx().toDouble();
        else if (s == "--cut-check") cutCheck = true;
        else if (s == "--plan-cam" && i + 3 < a.size()) { camX = a[i + 1].toDouble(); camY = a[i + 2].toDouble(); camMpp = a[i + 3].toDouble(); i += 3; }
        else if (s == "--settings") nx();
        else if (s == "--export-dialog-shot") dialogShot = nx();
        else if (s == "--ctx-shot") ctxShot = nx();
        else if (s == "--lod-shot") lodShot = nx();
        else if (s == "--section-scale") sectionScale = nx().toDouble();
        else if (s == "--start-shot") startShot = nx();
        else if (s == "--undo-test") undoTest = true;
        else if (s == "--paper") paperArg = nx().toUpper();
        else if (s == "--sheet-scale") sheetScale = nx().toDouble();
        else if (s == "--split") split = true;
        else if (s == "--extra-line" && i + 4 < a.size()) { extraLines.push_back({a[i + 1].toDouble(), a[i + 2].toDouble(), a[i + 3].toDouble(), a[i + 4].toDouble()}); i += 4; }
        else if (s.startsWith("--export-")) exports.append({s.mid(9), nx()});
        else if (!s.startsWith("--")) file = s;
    }

    MainWindow w;
    if (W > 0 && H > 0) w.resize(W, H);
    w.show();
    if (file.isEmpty() && !startShot.isEmpty()) {   // 시작 화면 캡처
        processFor(800);
        QPixmap pm = w.grab();
        bool ok = pm.save(startShot);
        fprintf(stdout, "start-shot %s: %s (%dx%d)\n", ok ? "ok" : "FAILED", startShot.toUtf8().constData(), pm.width(), pm.height());
        return ok ? 0 : 6;
    }
    if (file.isEmpty()) return app.exec();

    int rc = 0;
    QFile logF(logPath);
    bool logOk = !logPath.isEmpty() && logF.open(QIODevice::WriteOnly | QIODevice::Text);
    auto log = [&](const QString& m) {
        QByteArray b = (m + "\n").toUtf8();
        if (logOk) { logF.write(b); logF.flush(); }
        fputs(b.constData(), stdout); fflush(stdout);
    };
    bool automated = haveLine || !shot.isEmpty() || !exports.isEmpty() || quit || !perfPath.isEmpty() || !startShot.isEmpty();
    if (!automated) { QTimer::singleShot(0, &w, [&] { w.openFile(file); }); return app.exec(); }

    processFor(200);
    OpenedScene sc; QString err;
    auto t0 = std::chrono::steady_clock::now();
    if (!MainWindow::loadScene(file, sc, &err)) { log("open-error: " + err); return 2; }
    log(QStringLiteral("open-ok: %1 kind=%2 displayTris=%3 srs=%4 origin=%5,%6,%7 ms=%8").arg(file, sc.kind).arg(sc.displayTris)
            .arg(QString::fromStdString(sc.src->srs.shortLabel())).arg(sc.src->srs.origin.x, 0, 'f', 3).arg(sc.src->srs.origin.y, 0, 'f', 3).arg(sc.src->srs.origin.z, 0, 'f', 3)
            .arg(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), 0, 'f', 0));
    log(QStringLiteral("layers=%1 open-warnings=%2").arg(sc.layers).arg(sc.warnings.size()));
    auto tApply = std::chrono::steady_clock::now();
    auto msSince = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); };
    w.applyScene(std::move(sc));
    if (!heightDatum.isEmpty()) {   // 높이 기준 지정(이름표만, 저장 안 함). 키: srs|ellipsoidal|egm96|egm2008|kvd1964|kngeoid
        VDatum d = heightDatum == "srs" ? VDatum::None : vdatumFromKey(heightDatum.toStdString());
        if (d == VDatum::Unknown) log("height-datum: unknown key " + heightDatum);
        else w.setHeightDeclaration(d, false);
    }
    if (front >= 0 || back >= 0) w.setThickness(front >= 0 ? front : w.frontDepth(), back >= 0 ? back : w.backDepth());  // 성능 기록(단면 끌기)도 이 두께로
    if (!depthFade.isEmpty()) w.setDepthFade(depthFade != "off" && depthFade != "0");
    {
        const SrsReport& r = w.srsReport();
        QString ws;
        for (auto& x : r.warnings) ws += " | " + QString::fromStdString(x);
        log(QStringLiteral("srs-report: %1 latlon=%2,%3 maxLocalXY=%4 f32step-mm=%5 warnings=%6%7").arg(QString::fromStdString(r.labelKo))
                .arg(r.lat, 0, 'f', 6).arg(r.lon, 0, 'f', 6).arg(r.maxLocalXY, 0, 'f', 1).arg(r.float32StepMm, 0, 'f', 3).arg(r.warnings.size()).arg(ws));
    }
    {   // LOD 스트리밍: 거친 모델이 처음 보일 때까지 / 현재 시점 세부가 다 찰 때까지
        double first = -1;
        while (msSince(tApply) < 60000) {
            QApplication::processEvents(QEventLoop::AllEvents, 10);
            if (first < 0 && w.plan()->lastFrame().draw > 0) first = msSince(tApply);
            if (w.plan()->streamIdle() && w.plan()->lastFrame().draw > 0) break;
        }
        if (w.plan()->streaming())
            log(QStringLiteral("stream: first-draw-ms=%1 idle-ms=%2 nodes=%3 depth=%4 gpuMB=%5").arg(first, 0, 'f', 0).arg(msSince(tApply), 0, 'f', 0)
                    .arg(w.plan()->lastFrame().draw).arg(w.plan()->lastFrame().maxDepth).arg(w.plan()->lastFrame().gpuBytes / 1048576.0, 0, 'f', 1));
    }
    if (!perfPath.isEmpty()) {
        // 카메라 경로 재생: 맞춤(전체) → 2배씩 확대 6단계(각 20프레임) → 확대 상태로 가로 이동 60프레임 → 축소 40프레임
        QFile pf(perfPath);
        if (!pf.open(QIODevice::WriteOnly | QIODevice::Text)) { log("perf-log open failed"); rc = 5; }
        else {
            QTextStream ts(&pf);
            ts << "frame,t_ms,phase,center_x,center_y,mpp,frame_ms,paint_ms,draw_nodes,uploaded,evicted,wanted,queued,loading,gpu_mb,max_depth,idle,upload_kb,upload_ms,staging\n";
            PlanView* pv = w.plan();
            Box3 b = pv->bounds();
            double cx = b.center().x, cy = b.center().y, mpp0 = pv->metersPerPixel();
            struct Step { const char* phase; double x, y, mpp; };
            std::vector<Step> path;
            for (int i = 0; i < 20; ++i) path.push_back({"fit", cx, cy, mpp0});
            for (int z = 1; z <= 6; ++z)
                for (int i = 0; i < 20; ++i) path.push_back({"zoom", cx, cy, mpp0 / std::pow(2.0, z - 1 + (i + 1) / 20.0)});
            double mz = mpp0 / 64.0, span = (b.mx.x - b.mn.x) * 0.3;
            for (int i = 0; i < 60; ++i) path.push_back({"pan", cx - span / 2 + span * i / 59.0, cy, mz});
            for (int i = 0; i < 40; ++i) path.push_back({"zoomout", cx + span / 2, cy, mz * std::pow(64.0, (i + 1) / 40.0)});
            auto t0 = std::chrono::steady_clock::now();
            double worst = 0, sum = 0;
            for (size_t i = 0; i < path.size(); ++i) {
                auto tf = std::chrono::steady_clock::now();
                pv->setCamera(path[i].x, path[i].y, path[i].mpp);
                pv->repaint();
                QApplication::processEvents(QEventLoop::AllEvents, 5);
                double fms = msSince(tf);
                worst = std::max(worst, fms); sum += fms;
                const auto& F = pv->lastFrame();
                ts << i << "," << QString::number(msSince(t0), 'f', 2) << "," << path[i].phase << "," << QString::number(path[i].x, 'f', 3) << ","
                   << QString::number(path[i].y, 'f', 3) << "," << QString::number(path[i].mpp, 'g', 6) << "," << QString::number(fms, 'f', 3) << ","
                   << QString::number(F.ms, 'f', 3) << "," << F.draw << "," << F.uploaded << "," << F.evicted << "," << F.wanted << "," << F.queued << ","
                   << F.loading << "," << QString::number(F.gpuBytes / 1048576.0, 'f', 2) << "," << F.maxDepth << "," << (F.idle ? 1 : 0) << "," << F.uploadBytes / 1024 << "," << QString::number(F.uploadMs, 'f', 2) << "," << F.staging << "\n";
            }
            // 단면선 끌기: 60 프레임(약 16 ms 간격) 동안 선을 옮기며 미리보기 요청 → 놓음(최종). 화면 프레임 시간과 미리보기 갱신 수
            {
                double L = std::min(b.mx.x - b.mn.x, 60.0) * 0.8;
                double y0 = cy - (b.mx.y - b.mn.y) * 0.25, y1 = cy + (b.mx.y - b.mn.y) * 0.25;
                pv->setCamera(cx, cy, mpp0);
                processFor(100);
                auto c0 = w.sectionCounters();
                auto td = std::chrono::steady_clock::now();
                double dworst = 0, dsum = 0;
                for (int i = 0; i < 60; ++i) {
                    auto tf = std::chrono::steady_clock::now();
                    SectionLine l; l.a = Vec2(cx - L / 2, y0 + (y1 - y0) * i / 59.0); l.b = Vec2(cx + L / 2, y0 + (y1 - y0) * i / 59.0);
                    w.dragLine(l, i == 59);
                    pv->repaint();
                    QApplication::processEvents(QEventLoop::AllEvents, 5);
                    while (msSince(tf) < 16.0) QApplication::processEvents(QEventLoop::AllEvents, 2);
                    double fms = msSince(tf);
                    const auto& F = pv->lastFrame();
                    dworst = std::max(dworst, F.ms); dsum += F.ms;
                    ts << (path.size() + i) << "," << QString::number(msSince(t0), 'f', 2) << ",secdrag," << QString::number(cx, 'f', 3) << ","
                       << QString::number((y0 + (y1 - y0) * i / 59.0), 'f', 3) << "," << QString::number(mpp0, 'g', 6) << "," << QString::number(fms, 'f', 3) << ","
                       << QString::number(F.ms, 'f', 3) << "," << F.draw << "," << F.uploaded << "," << F.evicted << "," << F.wanted << "," << F.queued << ","
                       << F.loading << "," << QString::number(F.gpuBytes / 1048576.0, 'f', 2) << "," << F.maxDepth << "," << (F.idle ? 1 : 0) << "," << F.uploadBytes / 1024 << "," << QString::number(F.uploadMs, 'f', 2) << "," << F.staging << "\n";
                }
                double dragMs = msSince(td);
                int pv1 = w.sectionCounters().previewsShown - c0.previewsShown;
                auto tfin = std::chrono::steady_clock::now();
                while (msSince(tfin) < 30000 && w.sectionCounters().finalsShown == c0.finalsShown) QApplication::processEvents(QEventLoop::AllEvents, 5);
                log(QStringLiteral("section-drag: frames=60 drag-ms=%1 previews-shown=%2 (%3/s) paint-avg-ms=%4 paint-worst-ms=%5 final-after-release-ms=%6 final-tris=%7 final-compute-ms=%8")
                        .arg(dragMs, 0, 'f', 0).arg(pv1).arg(pv1 * 1000.0 / dragMs, 0, 'f', 1).arg(dsum / 60, 0, 'f', 2).arg(dworst, 0, 'f', 2)
                        .arg(msSince(tfin), 0, 'f', 0).arg(w.sectionCounters().lastTris).arg(w.sectionCounters().lastMs, 0, 'f', 0));
            }
            ts.flush();
            log(QStringLiteral("perf: frames=%1 avg-ms=%2 worst-ms=%3 csv=%4 (GL=%5)").arg(path.size()).arg(sum / path.size(), 0, 'f', 2).arg(worst, 0, 'f', 2)
                    .arg(perfPath, QString::fromLatin1(reinterpret_cast<const char*>(pv->glRenderer().constData()))));
        }
    }
    for (auto& pk : picks) {
        auto src = w.source();
        Vec3 o = local ? Vec3() : w.srs().origin;
        double lx = pk.first - o.x, ly = pk.second - o.y;
        PickResult r; std::string e;
        bool ok = pickVertical(*src, lx, ly, r, &e);
        // 비교: 거친 LOD(0.5 m/px 에 충분한 단계)로 읽은 높이
        double coarseZ = std::nan("");
        {
            std::vector<MeshPtr> ms; LeafStats st;
            SectionLine sl; sl.a = Vec2(lx - 0.02, ly); sl.b = Vec2(lx + 0.02, ly); sl.front = sl.back = 0.02;
            if (src->bandMeshes(sectionBand(sl), 0.5, ms, &st, &e, nullptr)) {
                double t = 1e300;
                if (rayMeshes(ms, Vec3(lx, ly, src->bounds.mx.z + 10), Vec3(0, 0, -1), 0, t)) coarseZ = src->bounds.mx.z + 10 - t + src->srs.origin.z;
            }
        }
        if (!ok) log("pick-error: " + QString::fromStdString(e));
        else if (!r.hit) log(QStringLiteral("pick %1 %2: no surface").arg(pk.first, 0, 'f', 3).arg(pk.second, 0, 'f', 3));
        else log(QStringLiteral("pick %1 %2: Z=%3 source=%4 leafTiles=%5 depth=%6 tris=%7 ms=%8 coarse0.5m-Z=%9 (diff %10 mm)")
                     .arg(r.world.x, 0, 'f', 3).arg(r.world.y, 0, 'f', 3).arg(r.world.z, 0, 'f', 4).arg(QString::fromUtf8(zSourceKo(r.source)))
                     .arg(r.stats.leafNodes).arg(r.stats.maxDepth).arg(r.trianglesTested).arg(r.ms, 0, 'f', 1).arg(coarseZ, 0, 'f', 4)
                     .arg((coarseZ - r.world.z) * 1000, 0, 'f', 1));
    }
    if (hover) {
        PlanView* pv = w.plan();
        QPointF c(pv->width() / 2.0, pv->height() / 2.0);
        QMouseEvent ev(QEvent::MouseMove, c, pv->mapToGlobal(c), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pv, &ev);
        log("hover-immediate: " + w.cursorText());
        auto th = std::chrono::steady_clock::now();
        while (msSince(th) < 10000 && !w.cursorText().contains(QStringLiteral("잎")) && !w.cursorText().contains(QStringLiteral("없습니다"))) processFor(20);
        log(QStringLiteral("hover-final(%1 ms): ").arg(msSince(th), 0, 'f', 0) + w.cursorText());
    }
    if (front >= 0 || back >= 0) w.setThickness(front >= 0 ? front : 0.0, back >= 0 ? back : 0.5);
    if (tab >= 0) w.selectRibbonTab(tab);
    processFor(300);
    log("window-" + w.windowTitleCheck());
    log(QStringLiteral("depth: front=%1m back=%2m backUserSet=%3").arg(w.frontDepth(), 0, 'f', 2).arg(w.backDepth(), 0, 'f', 2).arg(w.backUserSet() ? 1 : 0));
    if (haveLine) {
        Vec3 o = local ? Vec3() : w.srs().origin;
        SectionLine l; l.a = Vec2(ax - o.x, ay - o.y); l.b = Vec2(bx - o.x, by - o.y);
        if (!w.computeNow(l, &err)) { log("section-error: " + err); rc = 3; }
        else {
            auto d = w.sectionDoc();
            size_t nv = 0; for (auto& pl : d.r.profile) nv += pl.size();
            log(QStringLiteral("section-ok: polylines=%1 vertices=%2 z=[%3,%4] image=%5x%6").arg(d.r.profile.size()).arg(nv)
                    .arg(d.r.zMin + d.r.srs.origin.z, 0, 'f', 2).arg(d.r.zMax + d.r.srs.origin.z, 0, 'f', 2).arg(d.img.width()).arg(d.img.height()));
            log(QStringLiteral("vex-suggest: relief=%1cm visible1to1=%2m suggest=x%3").arg(w.vexRelief() * 100, 0, 'f', 1)
                    .arg(w.sectionView()->fitVisibleHeight(), 0, 'f', 2).arg(w.vexSuggestion()));
            if (vexArg > 0) { w.setVex(vexArg); log(QStringLiteral("vex: x%1 (화면만)").arg(w.sectionView()->verticalExaggeration(), 0, 'g', 3)); }
            if (cutCheck) { processFor(300); runCutCheck(w, log); }
        }
    }
    processFor(400);
    if (haveLine && w.hasSection()) {   // 화면 레벨선 간격(맞춤 상태) 기록
        SectionView* sv = w.sectionView();
        LevelPlan lp = sectionLevelPlan(sv->xf().ppm, 1.0, false);
        log(QStringLiteral("levels-screen: ppm=%1 line=%2cm label=%3cm").arg(sv->xf().ppm, 0, 'f', 2).arg(lp.lineCm).arg(lp.labelCm));
        for (double denom2 : {20.0, 40.0, 100.0}) {
            double ppm = 1000.0 / denom2 / 25.4 * dpi;
            LevelPlan e = sectionLevelPlan(ppm, dpi / 96.0, true);
            log(QStringLiteral("levels-export 1:%1 %2dpi: line=%3cm label=%4cm").arg(denom2, 0, 'f', 0).arg(dpi, 0, 'f', 0).arg(e.lineCm).arg(e.labelCm));
        }
    }
    if (haveLine && w.hasSection() && !extraLines.empty()) {   // 단면 목록: 더 넣고 각각 계산(썸네일) → 첫 단면으로
        Vec3 o = local ? Vec3() : w.srs().origin;
        for (auto& e : extraLines) {
            SectionLine l; l.a = Vec2(e[0] - o.x, e[1] - o.y); l.b = Vec2(e[2] - o.x, e[3] - o.y);
            int idx = w.addSectionAt(l);
            auto c0 = w.sectionCounters();
            w.selectSectionAt(idx);
            auto te = std::chrono::steady_clock::now();
            while (msSince(te) < 60000 && w.sectionCounters().finalsShown == c0.finalsShown) QApplication::processEvents(QEventLoop::AllEvents, 10);
            log(QStringLiteral("extra-section %1: final in %2 ms").arg(idx).arg(msSince(te), 0, 'f', 0));
        }
        auto c0 = w.sectionCounters();
        w.selectSectionAt(0);
        auto te = std::chrono::steady_clock::now();
        while (msSince(te) < 60000 && w.sectionCounters().finalsShown == c0.finalsShown) QApplication::processEvents(QEventLoop::AllEvents, 10);
        processFor(300);
        log(QStringLiteral("sections: %1").arg(w.sectionCount()));
    }
    if (undoTest) {
        QString ul; bool ok = w.undoTest(&ul);
        log(ul.trimmed()); log(QStringLiteral("undo-test %1").arg(ok ? "ok" : "FAILED"));
        if (!ok) rc = 7;
        processFor(300);
        auto c0 = w.sectionCounters();
        auto te = std::chrono::steady_clock::now();
        while (msSince(te) < 30000 && w.sectionCounters().finalsShown == c0.finalsShown) QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (wheelTest) {
        // 휠 확대/축소: 커서 아래 지점이 고정되는지(px 오차)와 애니메이션 중 프레임 시간
        PlanView* pv = w.plan();
        auto sendWheel = [](QWidget* wd, QPointF at, int sign) {   // 실제 휠 이벤트 경로로 시험(120 = 한 칸)
            QWheelEvent ev(at, wd->mapToGlobal(at), QPoint(), QPoint(0, 120 * sign), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(wd, &ev);
        };
        auto runPlan = [&](double notches, const char* name) {
            QPointF at(pv->width() * 0.3, pv->height() * 0.4);
            Vec2 b0, b1; pv->screenToLocalXYPublic(at, b0);
            double mpp0 = pv->metersPerPixel(), worst = 0; int frames = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < int(std::fabs(notches)); ++i) { sendWheel(pv, at, notches > 0 ? 1 : -1); processFor(40); worst = std::max(worst, pv->lastFrame().ms); }
            while (pv->zoomAnimating() && msSince(t0) < 5000) { QApplication::processEvents(QEventLoop::AllEvents, 5); worst = std::max(worst, pv->lastFrame().ms); }
            double tAnim = msSince(t0);
            for (int i = 0; i < 30; ++i) { processFor(16); worst = std::max(worst, pv->lastFrame().ms); ++frames; }
            pv->screenToLocalXYPublic(at, b1);
            double drift = (b1 - b0).len() / pv->metersPerPixel();
            log(QStringLiteral("wheel-plan %1: notches=%2 mpp %3→%4 anchor-drift-px=%5 anim-ms=%6 frame-worst-ms(애니+정착)=%7 depth=%8 idle=%9")
                    .arg(name).arg(notches).arg(mpp0, 0, 'g', 4).arg(pv->metersPerPixel(), 0, 'g', 4).arg(drift, 0, 'f', 3).arg(tAnim, 0, 'f', 0)
                    .arg(worst, 0, 'f', 1).arg(pv->lastFrame().maxDepth).arg(pv->streamIdle() ? 1 : 0));
        };
        runPlan(6, "in"); runPlan(-6, "out");
        SectionView* sv = w.sectionView();
        if (sv->hasResult()) {
            auto runSec = [&](double notches, const char* name) {
                QPointF at(sv->width() * 0.35, sv->height() * 0.55);
                double s0, z0, s1, z1; sv->screenToSZ(at, s0, z0);
                double ppm0 = sv->xf().ppm;
                auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < int(std::fabs(notches)); ++i) { sendWheel(sv, at, notches > 0 ? 1 : -1); processFor(40); }
                while (sv->zoomAnimating() && msSince(t0) < 5000) QApplication::processEvents(QEventLoop::AllEvents, 5);
                double tAnim = msSince(t0);
                sv->screenToSZ(at, s1, z1);
                double drift = std::hypot(s1 - s0, z1 - z0) * sv->xf().ppm;
                LevelPlan lp = sectionLevelPlan(sv->xf().ppm, 1.0, false);
                log(QStringLiteral("wheel-section %1: notches=%2 ppm %3→%4 anchor-drift-px=%5 anim-ms=%6 levels line=%7cm label=%8cm")
                        .arg(name).arg(notches).arg(ppm0, 0, 'f', 2).arg(sv->xf().ppm, 0, 'f', 2).arg(drift, 0, 'f', 3).arg(tAnim, 0, 'f', 0).arg(lp.lineCm).arg(lp.labelCm));
            };
            runSec(8, "in");
            if (!shot.isEmpty()) {   // 확대 상태 단면(레벨선 10 cm / 숫자 50 cm 확인용)
                QString zp = shot; zp.insert(zp.lastIndexOf('.') < 0 ? zp.size() : zp.lastIndexOf('.'), "_section_wheelzoom");
                sv->grab().save(zp); log("wheel-shot: " + zp);
            }
            runSec(-14, "out"); sv->fit(); sv->update();
        }
        pv->fitAll();
    }
    for (auto& e : exports) {
        QString kind = e.first, path = e.second, m;
        bool ok = false;
        auto src = w.source();
        if (kind == "png" || kind == "tiff" || kind == "geotiff") {
            SectionExportParams p; p.format = kind == "png" ? 0 : kind == "tiff" ? 1 : 2; p.denom = denom; p.dpi = dpi;
            ok = w.hasSection() && MainWindow::exportSectionImage(w.sectionDoc(), *src, p, path, &m);
        } else if (kind == "dxf") {
            DxfParams p; p.world3d = dxf3d; p.denom = denom; p.imageDpi = std::min(dpi, 200.0);
            ok = w.hasSection() && MainWindow::exportSectionDxf(w.sectionDoc(), *src, p, path, &m);
        } else if (kind == "plan") {
            PlanParams p; p.denom = denom; p.dpi = dpi; p.wholeModel = !planView;
            if (planView && !w.plan()->viewRectLocal(p.area)) p.wholeModel = true;
            SectionLine l = w.plan()->line();
            ok = MainWindow::exportPlan(*src, p, w.plan()->hasLine() ? &l : nullptr, path, &m);
        } else if (kind == "xyz" || kind == "las") {
            PointParams p; p.format = kind == "las" ? PointFormat::LAS : PointFormat::XYZ; p.spacing = spacing; p.rgb = rgb;
            p.area = area == "band" ? 1 : area == "view" ? 2 : 0;
            if (p.area == 2 && !w.plan()->viewRectLocal(p.box)) p.area = 0;
            ok = MainWindow::exportPointCloud(*src, p, w.plan()->line(), path, &m);
        } else if (kind == "csv") {
            ok = MainWindow::exportProfileCsv(w.sectionDoc(), path, &m);
        } else if (kind == "pdf" || kind == "sheet-png" || kind == "sheet-tiff" || kind == "sheet-dxf") {
            SheetParams sp = w.defaultSheetParams();
            sp.format = kind == "pdf" ? 0 : kind == "sheet-dxf" ? 1 : kind == "sheet-png" ? 2 : 3;
            sp.spec.denom = sheetScale > 0 ? sheetScale : denom; sp.dpi = dpi; sp.split = split;
            if (!paperArg.isEmpty()) { sp.spec.paper = paperArg.startsWith("A3") ? Paper::A3 : Paper::A4; sp.spec.landscape = !paperArg.endsWith("P"); }
            SectionDoc d = w.sectionDoc();
            SheetLayout L = layoutSheet(sp.spec, SectionFrame(d.r.line).L, d.r.zMax - d.r.zMin);
            ok = w.hasSection() && MainWindow::exportSheet(d, *src, sp, path, &m);
            m += QStringLiteral(" | sheet %1x%2 fits=%3").arg(L.cols).arg(L.rows).arg(L.fits ? 1 : 0);
        } else m = "unknown export kind";
        log(QStringLiteral("export-%1 %2: %3").arg(kind, ok ? "ok" : "FAILED", QString(m).replace('\n', " | ")));
        if (!ok) rc = 4;
    }
    if (!dialogShot.isEmpty() && w.hasSection()) {   // 도면 창 캡처(넘침 경고가 보이게 기본 1:40 A4 가로)
        SheetParams sp = w.defaultSheetParams();
        if (sheetScale > 0) sp.spec.denom = sheetScale;
        if (!paperArg.isEmpty()) { sp.spec.paper = paperArg.startsWith("A3") ? Paper::A3 : Paper::A4; sp.spec.landscape = !paperArg.endsWith("P"); }
        sp.showBaseline = true;
        bool acc = false;
        std::unique_ptr<QDialog> dlg(w.buildSheetDialog(sp, acc));
        dlg->show();
        processFor(700);
        bool ok = dlg->grab().save(dialogShot);
        log(QStringLiteral("dialog-shot %1: %2").arg(ok ? "ok" : "FAILED", dialogShot));
        dlg->close();
    }
    if (sectionScale > 0 && w.hasSection()) { w.sectionView()->setScreenDenom(sectionScale); processFor(200); }
    if (camMpp > 0) {   // 평면 카메라를 실좌표(X,Y)·m/px 로(정합 확인용 캡처)
        PlanView* pv = w.plan();
        const Vec3 o = local ? Vec3() : w.srs().origin;
        pv->setCamera(camX - o.x, camY - o.y, camMpp);
        auto tc = std::chrono::steady_clock::now();
        auto msC = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tc).count(); };
        processFor(300);
        while (msC() < 30000 && !pv->streamIdle()) QApplication::processEvents(QEventLoop::AllEvents, 10);
        processFor(300);
        log(QStringLiteral("plan-cam: X=%1 Y=%2 mpp=%3 depth=%4 idle=%5").arg(camX, 0, 'f', 3).arg(camY, 0, 'f', 3).arg(camMpp).arg(pv->lastFrame().maxDepth).arg(pv->streamIdle() ? 1 : 0));
    }
    if (!shot.isEmpty()) {
        processFor(500);
        // QOpenGLWidget 위 QPainter 덧그림까지 포함하려면 창 시스템에서 직접 캡처(실패 시 위젯 grab)
        QPixmap pm = w.screen() ? w.screen()->grabWindow(w.winId()) : QPixmap();
        if (pm.isNull() || pm.width() < 10) pm = w.grab();
        bool ok = pm.save(shot);
        log(QStringLiteral("shot %1: %2 (%3x%4)").arg(ok ? "ok" : "FAILED", shot).arg(pm.width()).arg(pm.height()));
    }
    if (!ctxShot.isEmpty()) {   // 그리기 중(A 찍고 A′ 로 움직이는 중) 지금 도구 줄
        PlanView* pv = w.plan();
        const SectionLine keep = pv->line(); const bool hadLine = pv->hasLine();
        pv->setDrawMode(true);
        processFor(200);
        QPointF pa(pv->width() * 0.30, pv->height() * 0.55), pb(pv->width() * 0.72, pv->height() * 0.50);
        QMouseEvent pr(QEvent::MouseButtonPress, pa, pv->mapToGlobal(pa), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pv, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, pa, pv->mapToGlobal(pa), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pv, &rl);
        for (int i = 1; i <= 10; ++i) {
            QPointF q = pa + (pb - pa) * (i / 10.0);
            QMouseEvent mv(QEvent::MouseMove, q, pv->mapToGlobal(q), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(pv, &mv);
            processFor(30);
        }
        processFor(500);
        QPixmap pm = w.screen() ? w.screen()->grabWindow(w.winId()) : QPixmap();
        if (pm.isNull() || pm.width() < 10) pm = w.grab();
        bool ok = pm.save(ctxShot);
        log(QStringLiteral("ctx-shot %1: %2 ctxbar=%3").arg(ok ? "ok" : "FAILED", ctxShot).arg(w.ctxBar()->isVisible() ? 1 : 0));
        pv->setDrawMode(false);
        pv->setLine(keep, hadLine);   // 그리기 전 단면선 되살림
        processFor(100);
    }
    if (!lodShot.isEmpty()) {   // 디테일 불러오는 중 카드(평면 확대 직후)
        PlanView* pv = w.plan();
        pv->zoomBy(1 / 6.0);
        auto tl = std::chrono::steady_clock::now();
        while (msSince(tl) < 3000 && pv->streamIdle()) QApplication::processEvents(QEventLoop::AllEvents, 5);
        processFor(120);
        QPixmap pm = w.screen() ? w.screen()->grabWindow(w.winId()) : QPixmap();
        if (pm.isNull() || pm.width() < 10) pm = w.grab();
        bool ok = pm.save(lodShot);
        log(QStringLiteral("lod-shot %1: %2 idle=%3 queued=%4 loading=%5").arg(ok ? "ok" : "FAILED", lodShot).arg(pv->streamIdle() ? 1 : 0).arg(pv->lastFrame().queued).arg(pv->lastFrame().loading));
        auto t2 = std::chrono::steady_clock::now();
        while (msSince(t2) < 30000 && !pv->streamIdle()) QApplication::processEvents(QEventLoop::AllEvents, 10);
        pv->fitAll(); processFor(200);
    }
    if (quit) return rc;
    return app.exec();
}

#ifdef _WIN32
// GUI 하위 시스템 진입점(Qt EntryPoint 라이브러리 대신). 인수는 QApplication 이 GetCommandLineW 로 유니코드로 다시 읽는다.
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
