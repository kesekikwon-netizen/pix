// 도면(용지) 내보내기: 축척·용지 미리보기, 넘침 경고·추천, 표제란(높이 기준 포함), PDF/DXF/PNG/TIFF
// 한 창(Ctrl+P). QtPrintSupport 없이 QPdfWriter(QtGui)로 PDF. 인쇄는 PDF 를 열어서.
#include "mainwindow.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <cstring>

using namespace asec;
namespace fs = std::filesystem;
extern const char *const kVersion;

namespace {
QString qs8(const std::string &s) { return QString::fromUtf8(s.c_str()); }
fs::path toFsS(const QString &q) {
#ifdef _WIN32
  return fs::path(q.toStdWString());
#else
  return fs::u8path(q.toStdString());
#endif
}
QImage toQImageS(const RgbaImage &im) {
  if (im.empty())
    return {};
  return QImage(im.px.data(), im.w, im.h, im.w * 4, QImage::Format_RGBA8888).copy();
}
RgbaImage fromQImageS(const QImage &q0) {
  QImage q = q0.convertToFormat(QImage::Format_RGBA8888);
  RgbaImage r;
  r.w = q.width();
  r.h = q.height();
  r.px.resize(size_t(r.w) * r.h * 4);
  for (int y = 0; y < r.h; ++y)
    std::memcpy(&r.px[size_t(y) * r.w * 4], q.constScanLine(y), size_t(r.w) * 4);
  return r;
}

/// 단면 → 용지 배치(높이는 위아래 4% 여유)
struct SheetGeom {
  SheetLayout L;
  double lenM = 0, heightM = 0, zTopAbs = 0;
};
SheetGeom sheetGeom(const SectionDoc &doc, const SheetSpec &spec) {
  SheetGeom g;
  const double oz = doc.r.srs.origin.z;
  double zr = std::max(0.2, doc.r.zMax - doc.r.zMin);
  double pad = std::max(0.05, zr * 0.04);
  g.lenM = SectionFrame(doc.r.line).L;
  g.heightM = zr + 2 * pad;
  g.zTopAbs = doc.r.zMax + oz + pad;
  g.L = layoutSheet(spec, g.lenM, g.heightM);
  return g;
}

double niceLen(double target) {
  double p = std::pow(10.0, std::floor(std::log10(std::max(1e-9, target))));
  for (double m : {1.0, 2.0, 5.0, 10.0})
    if (m * p >= target * 0.999)
      return m * p;
  return 10 * p;
}

QString paperLabel(const SheetSpec &s) {
  return QStringLiteral("%1 %2").arg(QString::fromLatin1(paperName(s.paper)),
                                     s.landscape ? QStringLiteral("가로") : QStringLiteral("세로"));
}
QString denomText(double d) { return QStringLiteral("1:%1").arg(d, 0, 'f', 0); }
const char *kFormatExt[4] = {"pdf", "dxf", "png", "tif"};
const char *kFormatName[4] = {"PDF", "DXF", "PNG", "TIFF"};
} // namespace

void MainWindow::paintSheet(QPainter &p, const SectionDoc &doc0, const SheetParams &sp,
                            double pxPerMm, int page, const QImage &img0,
                            const SectionImgGeo &geo0) {
  const SheetGeom G = sheetGeom(doc0, sp.spec);
  const SheetLayout &L = G.L;
  const double k = pxPerMm;                // mm → 장치 픽셀
  const double ui = pxPerMm * 25.4 / 96.0; // 96 dpi 기준 글자·선 배율 → 실제 mm
  const double mPerMmPaper = sp.spec.denom / 1000.0;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  p.fillRect(QRectF(0, 0, L.paperW * k, L.paperH * k), Qt::white);
  // 쪽(나눠 붙이기)
  const int cols = sp.split ? L.cols : 1;
  const int col = sp.split ? page % cols : 0, row = sp.split ? page / cols : 0;
  const double plotWm = L.plotW * mPerMmPaper, plotHm = L.plotH * mPerMmPaper;
  SectionXf xf;
  xf.ppm = k * 1000.0 / sp.spec.denom;
  xf.plot = QRectF(L.plotX * k, L.plotY * k, L.plotW * k, L.plotH * k);
  xf.s0 = G.lenM <= plotWm ? -(plotWm - G.lenM) / 2 : col * plotWm;
  xf.zTop = G.heightM <= plotHm ? G.zTopAbs + (plotHm - G.heightM) / 2 : G.zTopAbs - row * plotHm;
  // 문서 스타일: 넣을 것 선택 그대로(그리는 순서 레벨선 → 영상 → 단면선은 paintSectionDoc 이 지킴)
  SectionDoc doc = doc0;
  doc.st.showImage = sp.withImage;
  doc.st.showLine = sp.withLine;
  doc.st.showLevels = sp.withLevels;
  doc.st.imageOpacity = 1.0;
  doc.st.showBaseline = sp.showBaseline;
  doc.st.baselineEl = sp.baselineEl;
  doc.st.plotScaleBar = !sp.withTitle;
  // 영상: 이 쪽에 보이는 거리 범위만 잘라서(PDF 에 쪽마다 전체 영상이 들어가지 않게)
  QImage img = img0;
  SectionImgGeo geo = geo0;
  if (!img.isNull() && geo.res > 0) {
    double sa = xf.s0, sb = xf.s0 + plotWm;
    int x0 = std::max(0, int(std::floor((sa - geo.s0) / geo.res)) - 2),
        x1 = std::min(img.width(), int(std::ceil((sb - geo.s0) / geo.res)) + 2);
    if (x1 > x0 && (x0 > 0 || x1 < img.width())) {
      img = img.copy(x0, 0, x1 - x0, img.height());
      geo.s0 += x0 * geo.res;
    } else if (x1 <= x0)
      img = QImage();
  }
  const QRectF frame(L.frameX * k, L.frameY * k, L.frameW * k, L.frameH * k);
  const QRectF area(frame.left(), frame.top(), frame.width(), (L.titleY - L.frameY) * k);
  paintSectionDoc(p, doc, area, xf, ui, img, true, QString(), &geo, false, false);
  // 테두리
  p.setPen(QPen(theme::Ink, 0.35 * k));
  p.setBrush(Qt::NoBrush);
  p.drawRect(frame);
  if (sp.withTitle) {
    QFont fName = theme::uiFont(std::max(6, int(std::lround(15 * ui))), true);
    QFont fSmall = theme::uiFont(std::max(5, int(std::lround(10.5 * ui))));
    QFont fMono = theme::monoFont(std::max(5, int(std::lround(10 * ui))));
    // 표제란(오른쪽 아래)
    const double tbW = std::min(120.0, L.frameW * 0.45) * k, tbH = (sp.spec.titleHmm - 3) * k;
    QRectF tb(frame.right() - tbW, frame.bottom() - tbH, tbW, tbH);
    p.setPen(QPen(theme::Ink, 0.25 * k));
    p.setBrush(Qt::white);
    p.drawRect(tb);
    const double rowH = tbH / 3.0;
    p.drawLine(QPointF(tb.left(), tb.top() + rowH * 1.3),
               QPointF(tb.right(), tb.top() + rowH * 1.3));
    p.setFont(fName);
    p.setPen(theme::Ink);
    p.drawText(tb.adjusted(3 * k, 0, -3 * k, -(tbH - rowH * 1.3)), Qt::AlignLeft | Qt::AlignVCenter,
               sp.title);
    if (sp.split && L.cols * L.rows > 1) {
      p.setFont(fMono);
      p.drawText(tb.adjusted(3 * k, 0, -3 * k, -(tbH - rowH * 1.3)),
                 Qt::AlignRight | Qt::AlignVCenter,
                 QStringLiteral("%1 / %2").arg(page + 1).arg(L.cols * L.rows));
    }
    p.setFont(fSmall);
    p.setPen(theme::Ink2);
    QString l1 = QStringLiteral("축척 %1 · %2 · %3 · %4")
                     .arg(denomText(sp.spec.denom), paperLabel(sp.spec), sp.facing, sp.date);
    QString l2 = QStringLiteral("%1 · %2").arg(sp.srsLabel, sp.heightLabel);
    QRectF r1(tb.left() + 3 * k, tb.top() + rowH * 1.3, tb.width() - 6 * k, rowH * 0.85);
    QRectF r2(tb.left() + 3 * k, r1.bottom(), tb.width() - 6 * k, rowH * 0.85);
    p.drawText(r1, Qt::AlignLeft | Qt::AlignVCenter, l1);
    p.drawText(r2, Qt::AlignLeft | Qt::AlignVCenter, l2);
    // 축척 막대(왼쪽 아래)
    double lenM = niceLen(40.0 * mPerMmPaper);
    double px = lenM / mPerMmPaper * k;
    QRectF sb(frame.left() + 6 * k, frame.bottom() - 9 * k, px, 1.6 * k);
    p.setPen(QPen(theme::Ink, 0.2 * k));
    p.setBrush(Qt::white);
    p.drawRect(sb);
    p.setBrush(theme::Ink);
    for (int i = 0; i < 4; i += 2)
      p.drawRect(QRectF(sb.x() + px * i / 4, sb.y(), px / 4, sb.height()));
    p.setFont(fMono);
    p.setPen(theme::Ink);
    auto ml = [](double m) {
      return m >= 1 ? QString::number(m, 'g', 4) + " m" : QString::number(m * 100, 'g', 4) + " cm";
    };
    p.drawText(QRectF(sb.left() - 5 * k, sb.bottom() + 0.8 * k, 10 * k, 4 * k),
               Qt::AlignHCenter | Qt::AlignTop, "0");
    p.drawText(QRectF(sb.right() - 10 * k, sb.bottom() + 0.8 * k, 20 * k, 4 * k),
               Qt::AlignHCenter | Qt::AlignTop, ml(lenM));
    p.setFont(fSmall);
    p.setPen(theme::Muted);
    p.drawText(QRectF(sb.left(), sb.top() - 5 * k, 80 * k, 4 * k), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("%1 · 세로:가로 1:1").arg(denomText(sp.spec.denom)));
    p.drawText(QRectF(frame.left() + 6 * k, frame.bottom() - 3.6 * k,
                      tb.left() - frame.left() - 10 * k, 3.4 * k),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("발굴 단면뷰어 %1 · 높이는 모델 좌표계 값 그대로")
                   .arg(QString::fromUtf8(kVersion)));
  }
  p.restore();
}

SheetParams MainWindow::defaultSheetParams() const {
  SheetParams sp;
  QSettings st;
  sp.spec.paper = st.value("sheet/paper", 0).toInt() == 1 ? Paper::A3 : Paper::A4;
  sp.spec.landscape = st.value("sheet/landscape", true).toBool();
  sp.spec.denom = st.value("sheet/denom", 40.0).toDouble();
  sp.format = std::clamp(st.value("sheet/format", 0).toInt(), 0, 3);
  sp.dpi = st.value("sheet/dpi", 300.0).toDouble();
  sp.withImage = st.value("sheet/withImage", true).toBool();
  sp.withLine = st.value("sheet/withLine", true).toBool();
  sp.withLevels = st.value("sheet/withLevels", true).toBool();
  sp.withTitle = st.value("sheet/withTitle", true).toBool();
  sp.title = QStringLiteral("%1 단면도").arg(sectionName());
  if (current_ >= 0 && current_ < int(sections_.size()) &&
      !sections_[size_t(current_)].note.empty())
    sp.title += QStringLiteral(" · ") + qs8(sections_[size_t(current_)].note);
  const SrsDesc &d = srsReport_.desc;
  sp.srsLabel = d.horizontalEpsg
                    ? QStringLiteral("수평 EPSG:%1").arg(d.horizontalEpsg)
                    : (d.known() ? qs8(d.shortAscii()) : QStringLiteral("좌표계 없음"));
  const VDatumInfo &vi = vdatumInfo(d.vdatum);
  int ve = d.verticalEpsg ? d.verticalEpsg : vi.epsg;
  const bool named = d.vdatum == VDatum::EGM96 || d.vdatum == VDatum::EGM2008 ||
                     d.vdatum == VDatum::KVD1964 || d.vdatum == VDatum::KNGeoid;
  QString vn = named && vi.shortName && *vi.shortName ? QString::fromUtf8(vi.shortName)
                                                      : qs8(d.verticalKo());
  sp.heightLabel = QStringLiteral("높이 %1%2%3")
                       .arg(vn, ve ? QStringLiteral(" (EPSG:%1)").arg(ve) : QString(),
                            d.heightDeclared ? QStringLiteral(" · 지정") : QString());
  if (section_->hasResult()) {
    sp.facing = qs8(facingKo(sectionAzimuthDeg(section_->doc().r.line)));
    SectionDoc doc = section_->doc();
    sp.baselineEl = std::round((0.5 * (doc.r.zMin + doc.r.zMax) + doc.r.srs.origin.z) * 2.0) /
                    2.0; // 가운데쯤 0.5 m 단위
  }
  sp.date = QDate::currentDate().toString("yyyy-MM-dd");
  return sp;
}

bool MainWindow::exportSheet(const SectionDoc &doc0, MeshSource &src, const SheetParams &p,
                             const QString &path, QString *msg, const std::atomic<bool> *cancel,
                             const std::function<void(double)> &progress) {
  const SheetGeom G = sheetGeom(doc0, p.spec);
  const int pages = p.split ? G.L.cols * G.L.rows : 1;
  if (p.format == 1) { // DXF: 모델 공간 1:N(용지 배치 없음)
    DxfParams dp;
    dp.world3d = false;
    dp.denom = p.spec.denom;
    dp.image = p.withImage;
    dp.imageDpi = std::min(p.dpi, 200.0);
    SectionDoc doc = doc0;
    doc.st.showLevels = p.withLevels;
    return exportSectionDxf(doc, src, dp, path, msg, cancel);
  }
  // 입면 영상: 출력 해상도로 다시 계산(같은 단면선 → 같은 윤곽은 스냅숏 것을 유지)
  QImage img;
  SectionImgGeo geo{doc0.imgS0, doc0.imgZ1, doc0.imgRes};
  double usedRes = 0;
  if (p.withImage) {
    const double imgDpi = p.format == 0 ? std::min(p.dpi, 300.0) : p.dpi;
    SectionRequest rq;
    rq.line = doc0.r.line;
    rq.depthFade = doc0.st.depthFade ? kDepthFadeStrength : 0.0;
    rq.imageRes = groundResolution(p.spec.denom, imgDpi);
    rq.maxImagePixels = size_t(120) << 20;
    SectionOutput out;
    std::string e;
    if (progress)
      progress(0.05);
    if (!computeSection(src, rq, out, &e, cancel)) {
      if (msg)
        *msg = qs8(e);
      return false;
    }
    img = toQImageS(out.image.img);
    geo = SectionImgGeo{out.image.s0, out.image.z1, out.image.res};
    usedRes = out.image.res;
  }
  if (cancel && cancel->load()) {
    if (msg)
      *msg = QStringLiteral("취소됨");
    return false;
  }
  QStringList files;
  if (p.format == 0) {
    QPdfWriter w(path);
    w.setResolution(int(std::lround(p.dpi)));
    w.setTitle(QStringLiteral("%1 — %2 · %3").arg(p.title, p.srsLabel, p.heightLabel));
    w.setCreator(QStringLiteral("발굴 단면뷰어 %1").arg(QString::fromUtf8(kVersion)));
    QPageSize ps(p.spec.paper == Paper::A3 ? QPageSize::A3 : QPageSize::A4);
    w.setPageLayout(QPageLayout(ps,
                                p.spec.landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter));
    QPainter pt;
    if (!pt.begin(&w)) {
      if (msg)
        *msg = QStringLiteral("PDF 를 만들 수 없습니다: %1").arg(path);
      return false;
    }
    const double pxPerMm = w.resolution() / 25.4;
    for (int i = 0; i < pages; ++i) {
      if (i > 0)
        w.newPage();
      paintSheet(pt, doc0, p, pxPerMm, i, img, geo);
      if (progress)
        progress(0.3 + 0.7 * (i + 1) / pages);
      if (cancel && cancel->load())
        break;
    }
    pt.end();
    files << QFileInfo(path).fileName();
  } else {
    const double pxPerMm = p.dpi / 25.4;
    const QSize sz(int(std::lround(G.L.paperW * pxPerMm)), int(std::lround(G.L.paperH * pxPerMm)));
    QFileInfo fi(path);
    for (int i = 0; i < pages; ++i) {
      QImage canvas(sz, QImage::Format_RGB32);
      canvas.fill(Qt::white);
      {
        QPainter pt(&canvas);
        paintSheet(pt, doc0, p, pxPerMm, i, img, geo);
      }
      const int dpm = int(std::lround(p.dpi / 0.0254));
      canvas.setDotsPerMeterX(dpm);
      canvas.setDotsPerMeterY(dpm);
      QString f = pages > 1 ? fi.absolutePath() + "/" + fi.completeBaseName() +
                                  QStringLiteral("_%1.").arg(i + 1) + fi.suffix()
                            : path;
      if (p.format == 2) {
        if (!canvas.save(f, "PNG")) {
          if (msg)
            *msg = QStringLiteral("PNG 저장 실패: %1").arg(f);
          return false;
        }
      } else {
        TiffOptions o;
        o.dpi = p.dpi;
        o.alpha = false;
        o.description = "Section sheet " + doc0.r.srs.shortLabel();
        std::string err;
        if (!writeTiff(toFsS(f), fromQImageS(canvas), o, &err)) {
          if (msg)
            *msg = qs8(err);
          return false;
        }
      }
      files << QFileInfo(f).fileName();
      if (progress)
        progress(0.3 + 0.7 * (i + 1) / pages);
      if (cancel && cancel->load()) {
        if (msg)
          *msg = QStringLiteral("취소됨");
        return false;
      }
    }
  }
  if (msg) {
    *msg = QStringLiteral("%1\n\n%2 · %3 · %4쪽%5\n%6 · %7")
               .arg(files.join(", "), paperLabel(p.spec), denomText(p.spec.denom))
               .arg(pages)
               .arg(!G.L.fits && !p.split ? QStringLiteral(" (넘친 부분은 잘림)") : QString())
               .arg(p.srsLabel, p.heightLabel);
    if (usedRes > 0)
      *msg += QStringLiteral("\n입면 영상 %1 mm/px").arg(usedRes * 1000, 0, 'f', 2);
  }
  return true;
}

// ---------------------------------------------------------------- 도면 창
namespace {
class SheetPreview : public QWidget {
public:
  std::function<void(QPainter &, const QRect &)> paintFn;
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    if (paintFn)
      paintFn(p, rect());
  }
};
QToolButton *chipBtn(const QString &t, QButtonGroup *g, int id, const char *pos = nullptr) {
  auto *b = new QToolButton;
  b->setObjectName("chip");
  b->setCheckable(true);
  b->setText(t);
  b->setFocusPolicy(Qt::NoFocus);
  b->setMinimumWidth(56);
  if (pos)
    b->setProperty("pos", pos);
  g->addButton(b, id);
  return b;
}
QLabel *sideHead(const QString &t) {
  auto *l = new QLabel(t);
  l->setObjectName("sideHead");
  return l;
}
QLabel *lbl(const QString &t, const char *obj = nullptr) {
  auto *l = new QLabel(t);
  if (obj)
    l->setObjectName(obj);
  return l;
}
} // namespace

QDialog *MainWindow::buildSheetDialog(SheetParams &io, bool &accepted) {
  accepted = false;
  auto *d = new QDialog(this);
  d->setWindowTitle(QStringLiteral("단면도 내보내기"));
  d->setObjectName("sheetDialog");
  auto st = std::make_shared<SheetParams>(io);
  const SectionDoc doc = section_->doc();
  const QImage screenImg = doc.img;
  const SectionImgGeo screenGeo{doc.imgS0, doc.imgZ1, doc.imgRes};
  auto *outer = new QVBoxLayout(d);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  // 머리
  auto *head = new QWidget;
  head->setObjectName("dialogHead");
  head->setAttribute(Qt::WA_StyledBackground);
  {
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(22, 12, 18, 12);
    h->setSpacing(8);
    h->addWidget(lbl(QStringLiteral("단면도 내보내기"), "title"));
    int n = int(sections_.size()), k = current_ + 1;
    h->addWidget(lbl(n > 1 ? QStringLiteral("%1 · %2개 중 %3번째").arg(sectionName()).arg(n).arg(k)
                           : sectionName(),
                     "hint"));
    h->addStretch();
  }
  outer->addWidget(head);
  auto *mid = new QHBoxLayout;
  mid->setContentsMargins(0, 0, 0, 0);
  mid->setSpacing(0);
  // 왼쪽: 미리보기 + 넘침 경고
  auto *left = new QWidget;
  left->setObjectName("dialogMain");
  left->setAttribute(Qt::WA_StyledBackground);
  auto *lv = new QVBoxLayout(left);
  lv->setContentsMargins(22, 12, 22, 14);
  lv->setSpacing(10);
  auto *pvTitle = lbl(QString(), "hint");
  pvTitle->setTextFormat(Qt::RichText);
  lv->addWidget(pvTitle);
  auto *pv = new SheetPreview;
  pv->setMinimumSize(470, 330);
  lv->addWidget(pv, 1);
  auto *warn = new QFrame;
  warn->setObjectName("warnBox");
  warn->setAttribute(Qt::WA_StyledBackground);
  auto *wl = new QHBoxLayout(warn);
  wl->setContentsMargins(12, 9, 12, 9);
  wl->setSpacing(10);
  auto *wIcon = lbl(QString::fromUtf8("▲"), "noticeIcon");
  wl->addWidget(wIcon, 0, Qt::AlignTop);
  auto *wBody = new QVBoxLayout;
  wBody->setSpacing(6);
  auto *wText = lbl(QString());
  wText->setWordWrap(true);
  wText->setTextFormat(Qt::RichText);
  wBody->addWidget(wText);
  auto *wBtns = new QHBoxLayout;
  wBtns->setSpacing(6);
  auto *bSmaller = new QPushButton;
  auto *bA3 = new QPushButton;
  auto *cbSplit = new QCheckBox;
  for (auto *b : {bSmaller, bA3}) {
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet("QPushButton{padding:2px 10px;}");
    wBtns->addWidget(b);
  }
  wBtns->addWidget(cbSplit);
  wBtns->addStretch();
  wBody->addLayout(wBtns);
  wl->addLayout(wBody, 1);
  lv->addWidget(warn);
  auto *okBox = lbl(QString(), "hint");
  okBox->setTextFormat(Qt::RichText);
  lv->addWidget(okBox);
  mid->addWidget(left, 1);
  // 오른쪽: 설정
  auto *side = new QWidget;
  side->setObjectName("dialogSide");
  side->setAttribute(Qt::WA_StyledBackground);
  side->setFixedWidth(330);
  auto *g = new QGridLayout(side);
  g->setContentsMargins(22, 14, 22, 14);
  g->setHorizontalSpacing(10);
  g->setVerticalSpacing(9);
  int r = 0;
  g->addWidget(sideHead(QStringLiteral("도면")), r++, 0, 1, 3);
  auto *name = new QLineEdit(st->title);
  g->addWidget(lbl(QStringLiteral("도면명")), r, 0);
  g->addWidget(name, r++, 1, 1, 2);
  auto *paper = new QComboBox;
  paper->addItem(QStringLiteral("A4 가로"), 0);
  paper->addItem(QStringLiteral("A4 세로"), 1);
  paper->addItem(QStringLiteral("A3 가로"), 2);
  paper->addItem(QStringLiteral("A3 세로"), 3);
  paper->setCurrentIndex((st->spec.paper == Paper::A3 ? 2 : 0) + (st->spec.landscape ? 0 : 1));
  g->addWidget(lbl(QStringLiteral("용지")), r, 0);
  g->addWidget(paper, r++, 1, 1, 2);
  auto *scaleG = new QButtonGroup(d);
  scaleG->setExclusive(true);
  auto *scaleRow = new QWidget;
  {
    auto *h = new QHBoxLayout(scaleRow);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    const int ds[4] = {10, 20, 40, 100};
    for (int i = 0; i < 4; ++i)
      h->addWidget(
          chipBtn(QStringLiteral("1:%1").arg(ds[i]), scaleG, ds[i], i == 0 ? "first" : nullptr));
    h->addWidget(chipBtn(QStringLiteral("맞춤"), scaleG, 0, "last"));
  }
  g->addWidget(lbl(QStringLiteral("축척")), r, 0);
  g->addWidget(scaleRow, r++, 1, 1, 2);
  auto *scaleNote = lbl(QString(), "faint");
  g->addWidget(scaleNote, r++, 1, 1, 2);
  g->addWidget(sideHead(QStringLiteral("표고 · 거리 눈금")), r++, 0, 1, 3);
  auto *lvLine = new QLineEdit;
  auto *lvLab = new QLineEdit;
  for (auto *e : {lvLine, lvLab}) {
    e->setReadOnly(true);
    e->setAlignment(Qt::AlignRight);
    e->setObjectName("mono");
    e->setFixedWidth(80);
    e->setFocusPolicy(Qt::NoFocus);
  }
  lvLine->setToolTip(QStringLiteral("레벨선 간격 — 기본 10 cm, 인쇄에서 0.5 mm 보다 촘촘하면 "
                                    "자동으로 성기게(10 cm 보다 촘촘하게는 안 그음)"));
  lvLab->setToolTip(QStringLiteral("표고 숫자 간격 — 기본 50 cm"));
  {
    auto *h = new QHBoxLayout;
    h->setSpacing(6);
    h->addWidget(lvLine);
    h->addWidget(lbl(QStringLiteral("숫자")));
    h->addWidget(lvLab);
    h->addStretch();
    g->addWidget(lbl(QStringLiteral("레벨선")), r, 0);
    g->addLayout(h, r++, 1, 1, 2);
  }
  auto *cbBase = new QCheckBox;
  cbBase->setChecked(st->showBaseline);
  auto *baseEl = new QDoubleSpinBox;
  baseEl->setRange(-1000, 10000);
  baseEl->setDecimals(2);
  baseEl->setSingleStep(0.1);
  baseEl->setPrefix("EL. ");
  baseEl->setSuffix(" m");
  baseEl->setValue(st->baselineEl);
  {
    auto *h = new QHBoxLayout;
    h->setSpacing(6);
    h->addWidget(cbBase);
    h->addWidget(baseEl, 1);
    g->addWidget(lbl(QStringLiteral("기준선")), r, 0);
    g->addLayout(h, r++, 1, 1, 2);
  }
  g->addWidget(sideHead(QStringLiteral("넣을 것 · 위에서 아래로 그리는 순서")), r++, 0, 1, 3);
  auto *cLine = new QCheckBox(QStringLiteral("단면선  (맨 위 · 빨강 0.35 mm)"));
  cLine->setChecked(st->withLine);
  auto *cImg =
      new QCheckBox(QStringLiteral("입면 영상  (뒤 %1 m)").arg(doc.r.line.back, 0, 'g', 3));
  cImg->setChecked(st->withImage);
  auto *cLev = new QCheckBox(QStringLiteral("레벨선과 숫자  (맨 뒤)"));
  cLev->setChecked(st->withLevels);
  auto *cTitle = new QCheckBox(QStringLiteral("표제란 · 축척 막대 · 방위"));
  cTitle->setChecked(st->withTitle);
  for (auto *c : {cLine, cImg, cLev, cTitle})
    g->addWidget(c, r++, 0, 1, 3);
  g->addWidget(sideHead(QStringLiteral("파일")), r++, 0, 1, 3);
  auto *fmtG = new QButtonGroup(d);
  fmtG->setExclusive(true);
  auto *fmtRow = new QWidget;
  {
    auto *h = new QHBoxLayout(fmtRow);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    for (int i = 0; i < 4; ++i)
      h->addWidget(chipBtn(QString::fromLatin1(kFormatName[i]), fmtG, i,
                           i == 0   ? "first"
                           : i == 3 ? "last"
                                    : nullptr));
  }
  g->addWidget(lbl(QStringLiteral("형식")), r, 0);
  g->addWidget(fmtRow, r++, 1, 1, 2);
  auto *fmtNote = lbl(QString(), "faint");
  fmtNote->setWordWrap(true);
  fmtNote->setTextFormat(Qt::RichText);
  g->addWidget(fmtNote, r++, 0, 1, 3);
  g->setRowStretch(r, 1);
  g->setColumnStretch(1, 1);
  mid->addWidget(side);
  outer->addLayout(mid, 1);
  // 바닥
  auto *foot = new QWidget;
  foot->setObjectName("dialogFoot");
  foot->setAttribute(Qt::WA_StyledBackground);
  auto *fh = new QHBoxLayout(foot);
  fh->setContentsMargins(22, 12, 22, 12);
  fh->setSpacing(8);
  auto *bDefault = new QPushButton(QStringLiteral("이 설정을 기본으로"));
  bDefault->setObjectName("quiet");
  fh->addWidget(bDefault);
  fh->addStretch();
  auto *bSave = new QPushButton;
  bSave->setObjectName("primary");
  bSave->setDefault(true);
  auto *bCancel = new QPushButton(QStringLiteral("취소"));
  fh->addWidget(bSave);
  fh->addWidget(bCancel);
  outer->addWidget(foot);

  // ---- 갱신
  auto refresh = std::make_shared<std::function<void()>>();
  *refresh = [=] {
    SheetGeom G = sheetGeom(doc, st->spec);
    SheetAdvice a = adviseSheet(st->spec, G.lenM, G.heightM);
    bool isStd = false;
    for (auto *b : scaleG->buttons()) {
      int id = scaleG->id(b);
      if (id > 0 && std::fabs(id - st->spec.denom) < 1e-6) {
        QSignalBlocker bl(b);
        b->setChecked(true);
        isStd = true;
      }
    }
    if (!isStd)
      if (auto *b = scaleG->button(0)) {
        QSignalBlocker bl(b);
        b->setChecked(true);
      }
    scaleNote->setText(
        isStd
            ? QString()
            : QStringLiteral("맞춤 = 1:%1 (한 장에 꼭 맞는 축척)").arg(st->spec.denom, 0, 'f', 0));
    if (auto *b = fmtG->button(st->format)) {
      QSignalBlocker bl(b);
      b->setChecked(true);
    }
    const double pxPerMm = 300 / 25.4, ui = pxPerMm * 25.4 / 96.0;
    LevelPlan lp = sectionLevelPlan(pxPerMm * 1000.0 / st->spec.denom, ui, true);
    lvLine->setText(QStringLiteral("%1 m").arg(lp.lineCm / 100.0, 0, 'f', 2));
    lvLab->setText(QStringLiteral("%1 m").arg(lp.labelCm / 100.0, 0, 'f', 2));
    const int pages = st->split ? G.L.cols * G.L.rows : 1;
    pvTitle->setText(
        QStringLiteral(
            "미리보기 · <span style='font-family:monospace'>%1 · %2 · 실제 크기 비율</span>%3")
            .arg(paperLabel(st->spec), denomText(st->spec.denom),
                 pages > 1 ? QStringLiteral(" · %1쪽 중 1쪽").arg(pages) : QString()));
    warn->setVisible(a.overflow);
    okBox->setVisible(!a.overflow);
    okBox->setText(QStringLiteral("<span style='color:#3F6B31'>✓</span> %1 한 장에 들어갑니다 — "
                                  "단면 %2 m × %3 m → 용지 위 %4 × %5 mm")
                       .arg(paperLabel(st->spec))
                       .arg(G.lenM, 0, 'f', 2)
                       .arg(G.heightM, 0, 'f', 2)
                       .arg(G.L.contentW, 0, 'f', 0)
                       .arg(G.L.contentH, 0, 'f', 0));
    if (a.overflow) {
      QString what = G.L.cols > 1 ? QStringLiteral("단면 길이 %1 m는 %2 mm라")
                                        .arg(G.lenM, 0, 'f', 2)
                                        .arg(G.L.contentW, 0, 'f', 0)
                                  : QStringLiteral("단면 높이 %1 m는 %2 mm라")
                                        .arg(G.heightM, 0, 'f', 2)
                                        .arg(G.L.contentH, 0, 'f', 0);
      QString fitPart =
          G.L.cols > 1
              ? QStringLiteral(" 지금은 <b>A 쪽 %1 m만</b> 들어갑니다.").arg(a.fitLenM, 0, 'f', 1)
              : QString();
      QStringList sug;
      if (a.smallerDenom > 0)
        sug << QStringLiteral("%1으로 줄이거나").arg(denomText(a.smallerDenom));
      if (a.a3Fits)
        sug << QStringLiteral("A3 %1 한 장에 넣거나")
                   .arg(st->spec.landscape ? QStringLiteral("가로") : QStringLiteral("세로"));
      if (a.rotateFits)
        sug << QStringLiteral("용지 방향을 바꾸거나");
      sug << QStringLiteral("%1 %2장으로 나눠 붙이기").arg(paperLabel(st->spec)).arg(a.splitSheets);
      wText->setText(
          QStringLiteral("%1에서 %2 %3 한 장에 들어가지 않습니다.%4<br><span "
                         "style='color:#B5573A'>추천</span> %5")
              .arg(denomText(st->spec.denom), what, paperLabel(st->spec), fitPart, sug.join(" ")));
      bSmaller->setVisible(a.smallerDenom > 0);
      bSmaller->setText(QStringLiteral("%1으로").arg(denomText(a.smallerDenom)));
      bA3->setVisible(a.a3Fits);
      bA3->setText(QStringLiteral("A3로"));
      QSignalBlocker bl(cbSplit);
      cbSplit->setChecked(st->split);
      cbSplit->setText(QStringLiteral("나눠서 저장 (%1장)").arg(a.splitSheets));
    }
    bSave->setText(QStringLiteral("%1 저장").arg(QString::fromLatin1(kFormatName[st->format])));
    QString hn = st->format == 1
                     ? QStringLiteral("DXF 는 모델 공간 %1 (용지·표제란 없이, 레벨선·A/A′ 포함). ")
                           .arg(denomText(st->spec.denom))
                     : QString();
    fmtNote->setText(
        hn + QStringLiteral("높이 기준 <b>%1</b>이 표제란(PDF 는 문서 제목에도)에 들어갑니다.")
                 .arg(st->heightLabel.mid(3).toHtmlEscaped()));
    cbBase->setEnabled(true);
    baseEl->setEnabled(st->showBaseline);
    pv->update();
  };
  pv->paintFn = [=](QPainter &p, const QRect &rc) {
    p.fillRect(rc, theme::Wash);
    SheetGeom G = sheetGeom(doc, st->spec);
    double s = std::min((rc.width() - 16.0) / G.L.paperW, (rc.height() - 16.0) / G.L.paperH);
    if (s <= 0.05)
      return;
    QSizeF pz(G.L.paperW * s, G.L.paperH * s);
    QPointF o(rc.left() + (rc.width() - pz.width()) / 2,
              rc.top() + (rc.height() - pz.height()) / 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 28));
    p.drawRect(QRectF(o + QPointF(2, 3), pz));
    p.save();
    p.translate(o);
    p.setClipRect(QRectF(QPointF(0, 0), pz));
    paintSheet(p, doc, *st, s, 0, screenImg, screenGeo);
    p.restore();
  };
  QObject::connect(name, &QLineEdit::textChanged, d, [=](const QString &t) {
    st->title = t;
    pv->update();
  });
  QObject::connect(paper, &QComboBox::currentIndexChanged, d, [=](int i) {
    st->spec.paper = i >= 2 ? Paper::A3 : Paper::A4;
    st->spec.landscape = (i % 2) == 0;
    (*refresh)();
  });
  QObject::connect(scaleG, &QButtonGroup::idClicked, d, [=](int id) {
    if (id > 0)
      st->spec.denom = id;
    else {
      SheetGeom G = sheetGeom(doc, st->spec);
      st->spec.denom = fitDenominator(st->spec, G.lenM, G.heightM);
    }
    (*refresh)();
  });
  QObject::connect(fmtG, &QButtonGroup::idClicked, d, [=](int id) {
    st->format = id;
    (*refresh)();
  });
  QObject::connect(cbBase, &QCheckBox::toggled, d, [=](bool on) {
    st->showBaseline = on;
    (*refresh)();
  });
  QObject::connect(baseEl, &QDoubleSpinBox::valueChanged, d, [=](double v) {
    st->baselineEl = v;
    pv->update();
  });
  QObject::connect(cLine, &QCheckBox::toggled, d, [=](bool on) {
    st->withLine = on;
    pv->update();
  });
  QObject::connect(cImg, &QCheckBox::toggled, d, [=](bool on) {
    st->withImage = on;
    pv->update();
  });
  QObject::connect(cLev, &QCheckBox::toggled, d, [=](bool on) {
    st->withLevels = on;
    pv->update();
  });
  QObject::connect(cTitle, &QCheckBox::toggled, d, [=](bool on) {
    st->withTitle = on;
    pv->update();
  });
  QObject::connect(bSmaller, &QPushButton::clicked, d, [=] {
    SheetGeom G = sheetGeom(doc, st->spec);
    SheetAdvice a = adviseSheet(st->spec, G.lenM, G.heightM);
    if (a.smallerDenom > 0) {
      st->spec.denom = a.smallerDenom;
      (*refresh)();
    }
  });
  QObject::connect(bA3, &QPushButton::clicked, d,
                   [=] { paper->setCurrentIndex(st->spec.landscape ? 2 : 3); });
  QObject::connect(cbSplit, &QCheckBox::toggled, d, [=](bool on) {
    st->split = on;
    (*refresh)();
  });
  QObject::connect(bDefault, &QPushButton::clicked, d, [=] {
    QSettings s;
    s.setValue("sheet/paper", st->spec.paper == Paper::A3 ? 1 : 0);
    s.setValue("sheet/landscape", st->spec.landscape);
    s.setValue("sheet/denom", st->spec.denom);
    s.setValue("sheet/format", st->format);
    s.setValue("sheet/dpi", st->dpi);
    s.setValue("sheet/withImage", st->withImage);
    s.setValue("sheet/withLine", st->withLine);
    s.setValue("sheet/withLevels", st->withLevels);
    s.setValue("sheet/withTitle", st->withTitle);
    bDefault->setText(QStringLiteral("✓ 기본으로 저장함"));
  });
  QObject::connect(bSave, &QPushButton::clicked, d, [=, &io, &accepted] {
    io = *st;
    accepted = true;
    d->accept();
  });
  QObject::connect(bCancel, &QPushButton::clicked, d, &QDialog::reject);
  (*refresh)();
  d->resize(1000, 690);
  return d;
}

void MainWindow::dlgSheet() {
  if (!section_->hasResult() || !src_)
    return;
  if (!ensureFinalSection())
    return;
  syncCurrentSection();
  SheetParams sp = defaultSheetParams();
  bool acc = false;
  std::unique_ptr<QDialog> d(buildSheetDialog(sp, acc));
  d->exec();
  if (!acc)
    return;
  QString base = QFileInfo(path_).completeBaseName() + "_" +
                 sectionName()
                     .replace(QStringLiteral("–"), "-")
                     .replace(QStringLiteral("′"), "'")
                     .remove('\'');
  QString ext = QString::fromLatin1(kFormatExt[sp.format]);
  QString filter =
      QStringLiteral("%1 (*.%2)").arg(QString::fromLatin1(kFormatName[sp.format]), ext);
  QString f = askSavePath(
      "sheet", base + QStringLiteral("_1-%1.").arg(sp.spec.denom, 0, 'f', 0) + ext, filter);
  if (f.isEmpty())
    return;
  if (QFileInfo(f).suffix().isEmpty())
    f += "." + ext;
  SectionDoc doc = section_->doc();
  auto src = src_;
  runTask(
      QStringLiteral("도면 %1 만드는 중").arg(QString::fromLatin1(kFormatName[sp.format])),
      [this, doc, src, sp, f](QString *m) {
        return exportSheet(doc, *src, sp, f, m, &cancelTask_, [this](double x) {
          QMetaObject::invokeMethod(this, [this, x] { setProgress(x); }, Qt::QueuedConnection);
        });
      },
      [this](bool ok, const QString &m) { report(ok, m); });
}
