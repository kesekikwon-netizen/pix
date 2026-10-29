// 1.2 화면 구성: 리본·지금 도구 줄·알림 띠·보기 머리·정보 띠·상태줄·시작 화면·단면 목록, 되돌리기, 최근 파일, 모델별 상태.
#include "mainwindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>

using namespace asec;
extern const char* const kVersion;

static QString qs8(const std::string& s) { return QString::fromUtf8(s.c_str()); }
static QString modelKey(const QString& path) {
    QByteArray h = QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toLower().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("model/") + QString::fromLatin1(h) + "/";
}

namespace {
QFrame* groupSep() { auto* f = new QFrame; f->setObjectName("groupSep"); f->setFrameShape(QFrame::NoFrame); return f; }

enum TileKind { TileNormal, TilePrimary, TileTool, TileToggle };
// 타일 아이콘: 44 px 둥근 면(평소 desk, 켜짐 oat, 손에 든 도구 hand, 주 단추 흙색) + 가운데 24 px 선 그림
QIcon tileIcon(theme::Ico k, TileKind kind) {
    QIcon out;
    auto add = [&](QColor bg, QColor ink, QColor edge, QIcon::State st) {
        for (int sc : {1, 2}) {
            int S = 44 * sc;
            QPixmap pm(S, S); pm.fill(Qt::transparent);
            QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
            p.setPen(edge.isValid() ? QPen(edge, sc) : QPen(Qt::NoPen)); p.setBrush(bg);
            p.drawRoundedRect(QRectF(0.5 * sc, 0.5 * sc, S - sc, S - sc), 8 * sc, 8 * sc);
            QPixmap g = theme::icon(k, 24, ink).pixmap(24 * sc, 24 * sc);
            p.drawPixmap(QRectF((S - 24 * sc) / 2.0, (S - 24 * sc) / 2.0, 24 * sc, 24 * sc), g, QRectF(0, 0, g.width(), g.height()));
            p.end();
            out.addPixmap(pm, QIcon::Normal, st);
        }
    };
    switch (kind) {
    case TilePrimary: add(theme::Action, Qt::white, QColor(), QIcon::Off); break;
    case TileTool: add(theme::Desk, theme::Ink, QColor(), QIcon::Off); add(theme::Hand, theme::Ground, QColor(), QIcon::On); break;
    case TileToggle: add(theme::Desk, theme::Ink, QColor(), QIcon::Off); add(theme::Oat, theme::Ink, theme::Ring, QIcon::On); break;
    default: add(theme::Desk, theme::Ink, QColor(), QIcon::Off); break;
    }
    return out;
}

// 단추 ↔ 동작 묶기(setDefaultAction 은 동작이 바뀔 때마다 글자·아이콘을 동작 것으로 덮으므로 직접 동기화)
void bindButton(QToolButton* b, QAction* a) {
    b->setCheckable(a->isCheckable());
    auto sync = [b, a] {
        b->setEnabled(a->isEnabled());
        if (a->isCheckable()) { QSignalBlocker bl(b); b->setChecked(a->isChecked()); }
        b->setToolTip(a->toolTip());
    };
    sync();
    QObject::connect(a, &QAction::changed, b, sync);
    QObject::connect(a, &QAction::toggled, b, sync);
    if (a->menu()) { b->setMenu(a->menu()); b->setPopupMode(QToolButton::InstantPopup); }
    else QObject::connect(b, &QToolButton::clicked, a, [b, a] {
        if (a->isCheckable()) { QSignalBlocker bl(b); b->setChecked(!b->isChecked()); }   // 동작 쪽에서 토글 후 sync 가 맞춤
        a->trigger();
    });
}

QToolButton* tile(QAction* a, theme::Ico k, TileKind kind = TileNormal, const QString& text = {}) {
    auto* b = new QToolButton;
    b->setObjectName("ribbonTile");
    b->setIcon(tileIcon(k, kind));
    b->setText(text.isEmpty() ? a->iconText() : text);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setIconSize(QSize(44, 44));
    b->setFocusPolicy(Qt::NoFocus);
    if (kind == TilePrimary) b->setProperty("primary", true);
    if (kind == TileTool) b->setProperty("tool", true);
    bindButton(b, a);
    return b;
}

// 작은 줄: 아이콘 + 글자 + 오른쪽 키 칩
QWidget* smallRow(QAction* a, const QString& text, const QString& key) {
    auto* w = new QWidget; auto* h = new QHBoxLayout(w); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(4);
    auto* b = new QToolButton;
    b->setIcon(a->icon());
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setIconSize(QSize(16, 16));
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoRaise(true);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bindButton(b, a);
    h->addWidget(b, 1);
    if (!key.isEmpty()) { auto* k = new QLabel(key); k->setObjectName("kbd"); h->addWidget(k); }
    QObject::connect(a, &QAction::changed, w, [w, a] { w->setEnabled(a->isEnabled()); });
    return w;
}

QToolButton* iconBtn(QAction* a) {
    auto* b = new QToolButton; b->setDefaultAction(a); b->setToolButtonStyle(Qt::ToolButtonIconOnly); b->setIconSize(QSize(16, 16));
    b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QLabel* kbd(const QString& t) { auto* l = new QLabel(t); l->setObjectName("kbd"); return l; }
QLabel* lab(const QString& t, const char* obj = nullptr) { auto* l = new QLabel(t); if (obj) l->setObjectName(obj); return l; }

QWidget* vbox(std::initializer_list<QWidget*> ws, int spacing = 3) {
    auto* w = new QWidget; auto* l = new QVBoxLayout(w); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(spacing);
    for (auto* x : ws) l->addWidget(x);
    l->addStretch();
    return w;
}

// 되돌리기 명령: 바뀌기 전·후 스냅숏. 같은 id(두께 칸)를 1.5 초 안에 연달아 바꾸면 하나로 합침
struct StateCmd : QUndoCommand {
    MainWindow* w; MainWindow::ViewState a, b; int mid; bool first = true; qint64 t;
    StateCmd(MainWindow* w_, const MainWindow::ViewState& a_, const MainWindow::ViewState& b_, const QString& text, int id_)
        : w(w_), a(a_), b(b_), mid(id_), t(QDateTime::currentMSecsSinceEpoch()) { setText(text); }
    void undo() override { w->applyState(a); }
    void redo() override { if (first) { first = false; return; } w->applyState(b); }
    int id() const override { return mid; }
    bool mergeWith(const QUndoCommand* o) override {
        auto* c = static_cast<const StateCmd*>(o);
        if (c->mid != mid || c->t - t > 1500) return false;
        b = c->b; t = c->t;
        return true;
    }
};
}  // namespace

bool MainWindow::ViewState::operator==(const ViewState& o) const {
    auto eqL = [](const SectionLine& x, const SectionLine& y) { return x.a.x == y.a.x && x.a.y == y.a.y && x.b.x == y.b.x && x.b.y == y.b.y; };
    return hasLine == o.hasLine && (!hasLine || eqL(line, o.line)) && front == o.front && back == o.back && image == o.image && profile == o.profile &&
           levels == o.levels && fade == o.fade && smooth == o.smooth && heightDecl == o.heightDecl && current == o.current;
}

QAction* MainWindow::makeAction(const QString& key, const QString& ko, const QString& en, theme::Ico ico, const QString& shortcut, bool checkable) {
    auto* a = new QAction(theme::icon(ico), ko, this);
    a->setCheckable(checkable);
    if (!shortcut.isEmpty()) { a->setShortcut(QKeySequence(shortcut)); a->setShortcutContext(Qt::ApplicationShortcut); }
    a->setToolTip(shortcut.isEmpty() ? QStringLiteral("%1  (%2)").arg(ko, en) : QStringLiteral("%1  (%2)  [%3]").arg(ko, en, shortcut));
    a->setIconText(ko);
    act_[key] = a;
    addAction(a);
    labels_.push_back({a, ko, en, 2});
    return a;
}

void MainWindow::retranslate() {
    for (auto& L : labels_) {
        QString t = bilingual_ ? QStringLiteral("%1 %2").arg(L.ko, L.en) : L.ko;
        if (L.kind >= 100 && L.kind < 200) static_cast<QTabBar*>(L.obj)->setTabText(L.kind - 100, t);
        else if (L.kind == 1) static_cast<QLabel*>(L.obj)->setText(t);
    }
}

// ---------------------------------------------------------------- 리본
QWidget* MainWindow::buildRibbon() {
    using I = theme::Ico;
    auto* rib = new QWidget; rib->setObjectName("ribbon"); rib->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(rib); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    auto* top = new QWidget; top->setObjectName("ribbonTop"); top->setAttribute(Qt::WA_StyledBackground);
    auto* th = new QHBoxLayout(top); th->setContentsMargins(8, 2, 8, 2); th->setSpacing(6);
    tabs_ = new QTabBar; tabs_->setObjectName("ribbonTabs"); tabs_->setDrawBase(false); tabs_->setExpanding(false); tabs_->setFocusPolicy(Qt::NoFocus);
    struct T { const char* ko; const char* en; };
    const T tabNames[] = {{"파일", "File"}, {"홈", "Home"}, {"보기", "View"}, {"측정", "Measure"}, {"내보내기", "Export"}};
    for (auto& t : tabNames) { int i = tabs_->addTab(QString()); labels_.push_back({tabs_, QString::fromUtf8(t.ko), QString::fromUtf8(t.en), 100 + i}); }
    th->addWidget(tabs_);
    th->addStretch();
    for (const char* k : {"undo", "redo"}) {
        auto* b = new QToolButton; b->setObjectName("undoBtn"); b->setDefaultAction(action(k)); b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(16, 16)); b->setFocusPolicy(Qt::NoFocus);
        th->addWidget(b);
    }
    v->addWidget(top);
    pages_ = new QStackedWidget; pages_->setFixedHeight(96);
    v->addWidget(pages_);

    using G = std::pair<std::pair<QString, QString>, QWidget*>;
    auto addPage = [&](std::vector<G> groups) {
        auto* page = new QWidget; auto* h = new QHBoxLayout(page); h->setContentsMargins(10, 4, 10, 4); h->setSpacing(10);
        for (size_t i = 0; i < groups.size(); ++i) {
            auto* g = new QWidget; auto* gl = new QVBoxLayout(g); gl->setContentsMargins(0, 0, 0, 0); gl->setSpacing(2);
            auto* lb = new QLabel; lb->setObjectName("groupLabel"); lb->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            labels_.push_back({lb, groups[i].first.first, groups[i].first.second, 1});
            gl->addWidget(lb);
            gl->addWidget(groups[i].second, 1);
            h->addWidget(g);
            if (i + 1 < groups.size()) h->addWidget(groupSep());
        }
        h->addStretch();
        pages_->addWidget(page);
    };
    auto rowOf = [](std::initializer_list<QWidget*> ws) {
        auto* w = new QWidget; auto* l = new QHBoxLayout(w); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(4);
        for (auto* x : ws) l->addWidget(x, 0, Qt::AlignTop);
        return w;
    };
    auto K = [](const char* s) { return QString::fromUtf8(s); };

    recentMenu_ = new QMenu(this);
    auto recentTile = [&]() {
        auto* b = tile(action("recent"), I::Recent, TileNormal, QStringLiteral("최근"));
        b->setMenu(recentMenu_); b->setPopupMode(QToolButton::InstantPopup);
        return b;
    };
    // ---- 파일
    addPage({{{K("파일"), "File"}, rowOf({tile(action("open"), I::Open), recentTile(), tile(action("close"), I::Close)})},
             {{K("단면 목록"), "Sections"}, rowOf({tile(action("secjson"), I::Csv), tile(action("secimport"), I::Open)})},
             {{K("도움말"), "Help"}, rowOf({tile(action("keys"), I::Keys), tile(action("about"), I::Info)})},
             {{K("끝내기"), "Exit"}, rowOf({tile(action("quit"), I::Close)})}});
    // ---- 홈
    front_ = new QDoubleSpinBox; back_ = new QDoubleSpinBox;
    for (auto* sp : {front_, back_}) { sp->setRange(0, kMaxBandDepth); sp->setDecimals(2); sp->setSingleStep(0.05); sp->setSuffix(" m"); sp->setFixedWidth(80); sp->setAlignment(Qt::AlignRight); }
    front_->setToolTip(QStringLiteral("단면선 앞쪽(보는 사람 쪽) 두께, 0–5 m"));
    back_->setToolTip(QStringLiteral("단면선 뒤쪽(보는 방향) 깊이 — 입면 영상(배경)에 보이는 깊이, 0–5 m, 기본 3 m(입면도용). 숫자키 1–5 = 0.5 / 1 / 2 / 3 / 5 m"));
    auto* chips = new QWidget; { auto* h = new QHBoxLayout(chips); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
        // 칩 = 숫자키 1–5 (0.5 / 1 / 2 / 3 / 5 m). 3 m 가 기본(입면도)
        const double vals[5] = {0.5, 1, 2, 3, 5};
        for (int i = 0; i < 5; ++i) {
            auto* c = new QToolButton; c->setObjectName("chip"); c->setCheckable(true); c->setFocusPolicy(Qt::NoFocus);
            c->setText(i == 4 ? QStringLiteral("5 m") : QString::number(vals[i], 'g', 2));
            c->setToolTip(QStringLiteral("뒤 깊이 %1 m (숫자키 %2)%3").arg(vals[i], 0, 'g', 2).arg(i + 1).arg(i == 3 ? QStringLiteral(" — 기본, 입면도용") : QString()));
            if (i == 0) c->setProperty("pos", "first");
            if (i == 4) c->setProperty("pos", "last");
            double v2 = vals[i];
            QObject::connect(c, &QToolButton::clicked, this, [this, v2] { setBackDepth(v2); });
            depthChip_[i] = c; h->addWidget(c);
        }
        h->addStretch(); }
    auto* thick = new QWidget; { auto* g = new QGridLayout(thick); g->setContentsMargins(0, 0, 0, 0); g->setHorizontalSpacing(4); g->setVerticalSpacing(5);
        g->addWidget(lab(QStringLiteral("앞"), "hint"), 0, 0); g->addWidget(front_, 0, 1); g->addWidget(lab(QStringLiteral("뒤"), "hint"), 0, 2); g->addWidget(back_, 0, 3);
        g->addWidget(chips, 1, 0, 1, 4); }
    lvLine_ = lab(QStringLiteral("0.10 m"), "mono"); lvLabel_ = lab(QStringLiteral("0.50 m"), "mono");
    for (auto* l : {lvLine_, lvLabel_}) { l->setStyleSheet("QLabel{background:#FFFFFF;border:1px solid #C2C0B6;border-radius:4px;padding:1px 6px;}"); l->setMinimumWidth(62); l->setAlignment(Qt::AlignRight | Qt::AlignVCenter); }
    lvLine_->setToolTip(QStringLiteral("레벨선 간격(지금 화면). 작업·인쇄 축척에서 10 cm, 너무 촘촘하면 자동으로 성기게"));
    lvLabel_->setToolTip(QStringLiteral("표고 숫자 간격(지금 화면). 작업·인쇄 축척에서 50 cm"));
    auto* lvBox = new QWidget; { auto* g = new QGridLayout(lvBox); g->setContentsMargins(0, 0, 0, 0); g->setHorizontalSpacing(4); g->setVerticalSpacing(4);
        g->addWidget(lab(QStringLiteral("선"), "hint"), 0, 0); g->addWidget(lvLine_, 0, 1);
        g->addWidget(lab(QStringLiteral("숫자"), "hint"), 1, 0); g->addWidget(lvLabel_, 1, 1); }
    auto* fadeBtn = smallRow(action("fade"), QStringLiteral("깊이 음영"), QString());
    addPage({{{K("파일"), "File"}, rowOf({tile(action("open"), I::Open), recentTile()})},
             {{K("단면"), "Section"}, rowOf({tile(action("draw"), I::Draw, TileTool, QStringLiteral("단면선")),
                                              vbox({smallRow(action("flip"), QStringLiteral("방향 반전"), "R"), smallRow(action("move"), QStringLiteral("평행 이동"), "[ ]"),
                                                    smallRow(action("addsec"), QStringLiteral("단면 추가"), "N")}, 2)})},
             {{K("두께 · 입면 깊이"), "Depth"}, thick},
             {{K("단면 표시"), "Display"}, rowOf({tile(action("image"), I::Image, TileToggle), tile(action("line"), I::Line, TileToggle, QStringLiteral("단면선")),
                                                  tile(action("levels"), I::Levels, TileToggle), vbox({lvBox, fadeBtn}, 4)})},
             {{K("측정"), "Measure"}, rowOf({tile(action("info"), I::Info, TileNormal, QStringLiteral("단면 정보")), tile(action("height"), I::Height, TileNormal, QStringLiteral("높이 기준"))})},
             {{K("내보내기"), "Export"}, rowOf({tile(action("sheet"), I::Sheet, TilePrimary, QStringLiteral("도면")), tile(action("dxf"), I::Dxf, TileNormal, QStringLiteral("DXF")),
                                                tile(action("secimg"), I::Picture, TileNormal, QStringLiteral("영상"))})}});
    // ---- 보기
    opacity_ = new QSlider(Qt::Horizontal); opacity_->setRange(10, 100); opacity_->setValue(100); opacity_->setFixedWidth(110);
    auto* opw = vbox({lab(QStringLiteral("영상 불투명도"), "hint"), opacity_}, 6);
    auto* lw = new QWidget; { auto* h = new QHBoxLayout(lw); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
        const double ws[3] = {1.5, 2.0, 3.0};
        double cur = QSettings().value("view/sectionLineWidth", 2.0).toDouble();
        std::vector<QToolButton*> bs;
        for (int i = 0; i < 3; ++i) {
            auto* c = new QToolButton; c->setObjectName("chip"); c->setCheckable(true); c->setFocusPolicy(Qt::NoFocus);
            c->setText(QStringLiteral("%1 px").arg(ws[i], 0, 'g', 2)); c->setChecked(std::fabs(cur - ws[i]) < 0.01);
            if (i == 0) c->setProperty("pos", "first");
            if (i == 2) c->setProperty("pos", "last");
            bs.push_back(c); h->addWidget(c);
        }
        for (int i = 0; i < 3; ++i) {
            double wv = ws[i];
            QObject::connect(bs[size_t(i)], &QToolButton::clicked, this, [this, bs, i, wv] {
                for (int j = 0; j < 3; ++j) bs[size_t(j)]->setChecked(j == i);
                QSettings().setValue("view/sectionLineWidth", wv); section_->setStyle(secStyle());
            });
        }
        h->addStretch(); }
    auto* lwBox = vbox({lab(QStringLiteral("단면선 굵기 (인쇄 0.35 mm)"), "hint"), lw}, 6);
    addPage({{{K("창"), "Windows"}, rowOf({tile(action("listpanel"), I::Csv, TileToggle), tile(action("view1"), I::Plan, TileToggle), tile(action("view2"), I::Line, TileToggle)})},
             {{K("탐색"), "Navigate"}, rowOf({tile(action("fit"), I::Fit), tile(action("top"), I::Plan), tile(action("zoomin"), I::ZoomIn), tile(action("zoomout"), I::ZoomOut)})},
             {{K("단면 표시"), "Display"}, rowOf({opw, lwBox})},
             {{K("화면"), "Screen"}, rowOf({tile(action("contrast"), I::Band, TileToggle), tile(action("lang"), I::Lang, TileToggle, QStringLiteral("English"))})}});
    // ---- 측정(옛 「분석」)
    addPage({{{K("단면 정리"), "Cleanup"}, rowOf({tile(action("smooth"), I::Smooth, TileToggle)})},
             {{K("정보"), "Info"}, rowOf({tile(action("info"), I::Info, TileNormal, QStringLiteral("단면 정보")), tile(action("height"), I::Height, TileNormal, QStringLiteral("높이 기준"))})},
             {{K("단면"), "Section"}, rowOf({tile(action("flip"), I::Flip), tile(action("clear"), I::Clear, TileNormal, QStringLiteral("지우기"))})}});
    // ---- 내보내기(옛 「추출」 포함)
    addPage({{{K("도면"), "Sheet"}, rowOf({tile(action("sheet"), I::Sheet, TilePrimary, QStringLiteral("도면"))})},
             {{K("단면"), "Section"}, rowOf({tile(action("dxf"), I::Dxf, TileNormal, QStringLiteral("DXF")), tile(action("secimg"), I::Picture, TileNormal, QStringLiteral("영상")), tile(action("csv"), I::Csv, TileNormal, QStringLiteral("CSV"))})},
             {{K("평면"), "Plan"}, rowOf({tile(action("plan"), I::Geo, TileNormal, QStringLiteral("GeoTIFF"))})},
             {{K("점군"), "Point Cloud"}, rowOf({tile(action("xyz"), I::Xyz, TileNormal, QStringLiteral("XYZ")), tile(action("las"), I::Las, TileNormal, QStringLiteral("LAS"))})}});
    QObject::connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
    return rib;
}

// ---------------------------------------------------------------- 지금 도구 줄(그리는 중에만)
QWidget* MainWindow::buildCtxBar() {
    auto* w = new QWidget; w->setObjectName("ctxBar"); w->setAttribute(Qt::WA_StyledBackground); w->setFixedHeight(40);
    auto* h = new QHBoxLayout(w); h->setContentsMargins(10, 0, 10, 0); h->setSpacing(8);
    auto* tool = new QLabel(QStringLiteral("╱  단면선 그리기")); tool->setObjectName("ctxTool");
    h->addWidget(tool);
    ctxHint_ = new QLabel; ctxHint_->setTextFormat(Qt::RichText);
    h->addWidget(ctxHint_);
    h->addSpacing(8);
    h->addWidget(kbd("Shift")); h->addWidget(lab(QStringLiteral("동서·남북 고정"), "hint"));
    h->addWidget(kbd("Enter")); h->addWidget(lab(QStringLiteral("좌표 입력"), "hint"));
    h->addWidget(kbd("Esc")); h->addWidget(lab(QStringLiteral("취소"), "hint"));
    h->addStretch();
    ctxValue_ = new QLabel; ctxValue_->setObjectName("ctxValue"); ctxValue_->setTextFormat(Qt::RichText);
    h->addWidget(ctxValue_);
    auto* close = new QPushButton(QStringLiteral("닫기")); close->setFocusPolicy(Qt::NoFocus);
    close->setStyleSheet("QPushButton{padding:3px 12px;}");
    QObject::connect(close, &QPushButton::clicked, this, [this] { plan_->setDrawMode(false); });
    h->addWidget(close);
    w->setVisible(false);
    return w;
}

void MainWindow::updateCtx(int stage, const SectionLine& l) {
    if (!ctxHint_) return;
    ctxHint_->setText(stage == 0 ? QStringLiteral("평면에서 <b>시작점 A</b>와 <b>끝점 A′</b>를 클릭합니다.")
                                 : QStringLiteral("평면에서 <b>끝점 A′</b>를 클릭합니다. (A 찍음)"));
    double L = SectionFrame(l).L;
    if (stage == 1 && L > 1e-6)
        ctxValue_->setText(QStringLiteral("<span style='color:#5E5D59'>길이</span> %1 m &nbsp; <span style='color:#5E5D59'>방위</span> %2")
                               .arg(L, 0, 'f', 2).arg(qs8(formatAzimuth(sectionAzimuthDeg(l)))));
    else ctxValue_->setText(QStringLiteral("<span style='color:#5E5D59'>길이</span> — &nbsp; <span style='color:#5E5D59'>방위</span> —"));
}

// ---------------------------------------------------------------- 알림 띠(노란 띠 대신)
QFrame* MainWindow::buildNotice() {
    auto* f = new QFrame; f->setObjectName("notice"); f->setProperty("level", "caution"); f->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(f); h->setContentsMargins(12, 5, 8, 5); h->setSpacing(8);
    auto* ic = new QLabel(QString::fromUtf8("▲")); ic->setObjectName("noticeIcon");
    h->addWidget(ic);
    noticeText_ = new QLabel; noticeText_->setTextFormat(Qt::RichText); noticeText_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(noticeText_, 1);
    auto* set = new QPushButton(QStringLiteral("높이 기준 지정…")); set->setStyleSheet("QPushButton{padding:3px 12px;}");
    auto* more = new QPushButton(QStringLiteral("자세히")); more->setObjectName("quiet"); more->setStyleSheet("QPushButton{padding:3px 10px;}");
    auto* x = new QToolButton; x->setIcon(theme::icon(theme::Ico::Close, 16)); x->setAutoRaise(true); x->setToolTip(QStringLiteral("이 알림 닫기(좌표계 표시는 상태줄 오른쪽에 계속)"));
    for (QWidget* b : {static_cast<QWidget*>(set), static_cast<QWidget*>(more), static_cast<QWidget*>(x)}) { b->setFocusPolicy(Qt::NoFocus); h->addWidget(b); }
    QObject::connect(set, &QPushButton::clicked, this, [this] { dlgHeightDatum(); });
    QObject::connect(more, &QPushButton::clicked, this, [this] {
        QString t = QStringLiteral("<b>%1</b><br><br>").arg(qs8(srsReport_.labelKo).toHtmlEscaped());
        for (auto& w : srsReport_.warnings) t += QStringLiteral("▲ ") + qs8(w).toHtmlEscaped() + "<br>";
        t += "<br><small>" + qs8(srsReport_.tooltipKo).toHtmlEscaped().replace("\n", "<br>") + "</small>";
        QMessageBox mb(this); mb.setWindowTitle(QStringLiteral("좌표계 확인")); mb.setTextFormat(Qt::RichText); mb.setText(t); mb.exec();
    });
    QObject::connect(x, &QToolButton::clicked, f, [f] { f->setVisible(false); });
    f->setVisible(false);
    return f;
}

// ---------------------------------------------------------------- 보기 머리
QWidget* MainWindow::buildPlanFrame() {
    auto* fr = new QWidget; fr->setObjectName("viewFrame"); fr->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(fr); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    planTitle_ = new QWidget; planTitle_->setObjectName("viewTitle"); planTitle_->setAttribute(Qt::WA_StyledBackground); planTitle_->setFixedHeight(32);
    auto* h = new QHBoxLayout(planTitle_); h->setContentsMargins(10, 0, 6, 0); h->setSpacing(2);
    h->addWidget(lab(QStringLiteral("평면"), "viewTitleText"));
    h->addSpacing(6); h->addWidget(lab(QStringLiteral("위에서"), "hint"));
    h->addStretch();
    auto mk = [&](theme::Ico ic, const QString& tip, std::function<void()> fn) {
        auto* a = new QAction(theme::icon(ic, 16), tip, this);
        QObject::connect(a, &QAction::triggered, this, fn);
        h->addWidget(iconBtn(a));
    };
    mk(theme::Ico::Fit, QStringLiteral("맞춤 (F)"), [this] { plan_->fitAll(); });
    mk(theme::Ico::ZoomIn, QStringLiteral("확대"), [this] { plan_->zoomBy(1 / 1.4); });
    mk(theme::Ico::ZoomOut, QStringLiteral("축소"), [this] { plan_->zoomBy(1.4); });
    mk(theme::Ico::Max, QStringLiteral("이 보기만 크게 / 나란히"), [this] { bool solo = action("view2")->isChecked(); action("view1")->setChecked(true); action("view2")->setChecked(!solo); });
    v->addWidget(planTitle_);
    v->addWidget(plan_, 1);
    return fr;
}

QWidget* MainWindow::buildSectionFrame() {
    auto* fr = new QWidget; fr->setObjectName("viewFrame"); fr->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(fr); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    secTitleBar_ = new QWidget; secTitleBar_->setObjectName("viewTitle"); secTitleBar_->setAttribute(Qt::WA_StyledBackground); secTitleBar_->setFixedHeight(32);
    auto* h = new QHBoxLayout(secTitleBar_); h->setContentsMargins(10, 0, 6, 0); h->setSpacing(2);
    secTitle_ = lab(QStringLiteral("단면"), "title"); h->addWidget(secTitle_);
    h->addSpacing(8);
    secFacing_ = lab(QString(), "facing"); h->addWidget(secFacing_);
    h->addStretch();
    heightBadge_ = new QToolButton; heightBadge_->setObjectName("heightBadge"); heightBadge_->setFocusPolicy(Qt::NoFocus);
    QObject::connect(heightBadge_, &QToolButton::clicked, this, [this] { dlgHeightDatum(); });
    h->addWidget(heightBadge_); h->addSpacing(6);
    auto mk = [&](theme::Ico ic, const QString& tip, std::function<void()> fn) {
        auto* a = new QAction(theme::icon(ic, 16), tip, this);
        QObject::connect(a, &QAction::triggered, this, fn);
        h->addWidget(iconBtn(a));
    };
    mk(theme::Ico::Fit, QStringLiteral("맞춤"), [this] { section_->fit(); section_->update(); });
    mk(theme::Ico::ZoomIn, QStringLiteral("확대"), [this] { section_->zoomBy(1.4); });
    mk(theme::Ico::ZoomOut, QStringLiteral("축소"), [this] { section_->zoomBy(1 / 1.4); });
    mk(theme::Ico::Max, QStringLiteral("이 보기만 크게 / 나란히"), [this] { bool solo = action("view1")->isChecked(); action("view2")->setChecked(true); action("view1")->setChecked(!solo); });
    v->addWidget(secTitleBar_);
    auto* strip = new QWidget; strip->setObjectName("infoStrip"); strip->setAttribute(Qt::WA_StyledBackground); strip->setFixedHeight(26);
    auto* sh = new QHBoxLayout(strip); sh->setContentsMargins(10, 0, 10, 0); sh->setSpacing(14);
    stripLen_ = new QLabel; stripDepth_ = new QLabel; stripScale_ = new QLabel; stripState_ = new QLabel; stripLevels_ = new QLabel;
    for (auto* l : {stripLen_, stripDepth_}) { l->setTextFormat(Qt::RichText); sh->addWidget(l); }
    // 세로:가로 — 누르면 화면 세로 과장(×1·×2·×5·×10, X 키로 돌아가며). 기복이 작으면 추천을 함께 보여 줌
    vexBtn_ = new QToolButton; vexBtn_->setObjectName("vexBtn"); vexBtn_->setFocusPolicy(Qt::NoFocus);
    vexBtn_->setAutoRaise(true); vexBtn_->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(vexBtn_);
        for (int k : {1, 2, 5, 10}) {
            QAction* a = m->addAction(k == 1 ? QStringLiteral("세로:가로 1:1 (실제 비율)") : QStringLiteral("세로 ×%1 과장 (화면만)").arg(k));
            QObject::connect(a, &QAction::triggered, this, [this, k] { setVex(k); });
        }
        vexBtn_->setMenu(m);
    }
    sh->addWidget(vexBtn_);
    stripScale_->setTextFormat(Qt::RichText); sh->addWidget(stripScale_);
    stripState_->setTextFormat(Qt::RichText); sh->addWidget(stripState_);
    sh->addStretch();
    stripLevels_->setText(QStringLiteral("그리는 순서: 레벨선 → 영상 → 단면선"));
    sh->addWidget(stripLevels_);
    v->addWidget(strip);
    v->addWidget(section_, 1);
    return fr;
}

void MainWindow::setActiveView(int vw) {
    for (auto* t : {planTitle_, secTitleBar_}) {
        if (!t) continue;
        t->setProperty("active", (t == planTitle_) == (vw == 0));
        t->style()->unpolish(t); t->style()->polish(t);
    }
}

QString MainWindow::heightBadgeText(QString* state, QString* tip) const {
    if (!src_) { if (state) *state = "none"; if (tip) *tip = QStringLiteral("모델을 열면 높이 기준이 여기 보입니다"); return QStringLiteral("높이 —"); }
    const SrsDesc& d = srsReport_.desc;
    QString t, st = "ok";
    const VDatumInfo& vi = vdatumInfo(d.vdatum);
    const bool named = d.vdatum == VDatum::EGM96 || d.vdatum == VDatum::EGM2008 || d.vdatum == VDatum::KVD1964 || d.vdatum == VDatum::KNGeoid;
    QString name = named && vi.shortName && *vi.shortName ? QString::fromUtf8(vi.shortName) : qs8(d.verticalKo());
    if (!d.known()) { st = "error"; t = QStringLiteral("● 좌표계 없음"); }
    else if (!d.heightDeclared && (d.vertKind == VertKind::Unspecified || (d.vertKind == VertKind::Ellipsoidal && d.promotedTo3D))) {
        st = "warn"; t = d.vertKind == VertKind::Unspecified ? QStringLiteral("▲ 높이 기준 모름") : QStringLiteral("▲ 높이 타원체고 표기 · 확인");
    } else {
        int ve = d.verticalEpsg ? d.verticalEpsg : vi.epsg;
        t = QStringLiteral("높이 %1%2%3").arg(name, ve ? QStringLiteral("  EPSG:%1").arg(ve) : QString(), d.heightDeclared ? QStringLiteral(" · 지정함") : QString());
    }
    if (state) *state = st;
    if (tip) *tip = QStringLiteral("%1\n%2누르면 「높이 기준 지정」(이름표만 바뀌고 Z 값은 그대로)").arg(qs8(srsReport_.labelKo), heightNote_.isEmpty() ? QString() : QStringLiteral("(%1)\n").arg(heightNote_));
    return t;
}

void MainWindow::setVex(double v) {
    section_->setVerticalExaggeration(v);
    updateVexUi();
    if (v > 1.5) showStatus(QStringLiteral("세로 ×%1 과장 — 화면 보기만 바뀝니다. 도면·DXF·영상 내보내기는 언제나 1:1 (X: 다음 배율)").arg(v, 0, 'g', 3));
    else showStatus(QStringLiteral("세로:가로 1:1 (실제 비율)"));
}

void MainWindow::updateVexUi() {
    if (!vexBtn_) return;
    const double v = section_->verticalExaggeration();
    const bool hasSec = section_->hasResult();
    QString t = v > 1.5 ? QStringLiteral("세로 <b>×%1</b> 과장").arg(v, 0, 'g', 3) : QStringLiteral("세로:가로 <b>1:1</b>");
    QString plain = v > 1.5 ? QStringLiteral("세로 ×%1 과장").arg(v, 0, 'g', 3) : QStringLiteral("세로:가로 1:1");
    QString state = v > 1.5 ? "on" : "off";
    QString tip = QStringLiteral("단면 화면의 세로 과장(×1·×2·×5·×10) — 기복이 작은 면(얕은 수혈 윤곽 등)을 보기 쉽게. 화면만 바뀌고 도면·내보내기는 언제나 1:1 [X]");
    if (hasSec && v < 1.5 && vexSuggest_ > 1) {
        plain += QStringLiteral(" · ×%1 권장").arg(vexSuggest_); state = "hint";
        tip = QStringLiteral("이 단면의 기복은 %1 cm 로 1:1 화면에서는 거의 평평하게 보입니다. ").arg(vexRelief_ * 100, 0, 'f', 0) + tip;
    }
    (void)t;
    vexBtn_->setText(plain);
    vexBtn_->setToolTip(tip);
    vexBtn_->setProperty("state", state);
    vexBtn_->style()->unpolish(vexBtn_); vexBtn_->style()->polish(vexBtn_);
}

QString MainWindow::windowTitleCheck() const {
    const QString t = windowTitle(), dn = QGuiApplication::applicationDisplayName();
    // Qt 는 제목이 표시 이름으로 끝나지 않으면 「 - 표시 이름」을 붙인다(Windows·X11) → 이름이 두 번 보이던 원인
    const bool dup = !dn.isEmpty() && !t.endsWith(dn);
    return QStringLiteral("title=\"%1\" displayName=\"%2\" appended=%3").arg(t, dn).arg(dup ? 1 : 0);
}

void MainWindow::updateHeader() {
    if (!secTitle_) return;
    QString st, tip;
    QString bt = heightBadgeText(&st, &tip);
    for (auto* b : {heightBadge_, heightBadge2_}) {
        if (!b) continue;
        b->setText(bt); b->setToolTip(tip); b->setProperty("state", st);
        b->style()->unpolish(b); b->style()->polish(b);
    }
    updateVexUi();
    const bool has = plan_ && plan_->hasLine();
    const QString monoB = QStringLiteral("<b style='font-family:Consolas,\"DejaVu Sans Mono\",monospace'>%1</b>");
    secTitle_->setText(has ? QStringLiteral("%1 단면").arg(sectionName()) : QStringLiteral("단면"));
    if (has) {
        const SectionLine& l = plan_->line();
        secFacing_->setText(qs8(facingKo(sectionAzimuthDeg(l))));
        stripLen_->setText(QStringLiteral("길이 ") + monoB.arg(QStringLiteral("%1 m").arg(SectionFrame(l).L, 0, 'f', 2)));
    } else { secFacing_->clear(); stripLen_->setText(QStringLiteral("길이 —")); }
    stripDepth_->setText(QStringLiteral("앞 %1 / 뒤 %2").arg(monoB.arg(QString::number(front_->value(), 'f', 2)), monoB.arg(QStringLiteral("%1 m").arg(back_->value(), 0, 'f', 2))));
    if (section_->hasResult()) {
        double d = section_->screenDenom();
        QString ds = (d >= 100 || std::fabs(d - std::round(d)) < 0.05) ? QString::number(std::round(d)) : QString::number(d, 'f', 1);
        stripScale_->setText(QStringLiteral("화면 축척 ") + monoB.arg(QStringLiteral("1:") + ds));
        stripState_->setText(last_.previewLod || !lastFinal_ ? (last_.cutFromLeaf ? QStringLiteral("<span style='color:#7A5A00'>단면선 정확 · 배경 미리보기 → 최종 계산 중…</span>") : QStringLiteral("<span style='color:#7A5A00'>미리보기(거친) → 최종 계산 중…</span>"))
                                                             : QStringLiteral("<span style='color:#3F6B31'>✓</span> 최종(잎)"));
        LevelPlan lp = sectionLevelPlan(section_->xf().ppmZ(), 1.0, false);
        lvLine_->setText(QStringLiteral("%1 m").arg(lp.lineCm / 100.0, 0, 'f', 2));
        lvLabel_->setText(QStringLiteral("%1 m").arg(lp.labelCm / 100.0, 0, 'f', 2));
        if (scaleCombo_) { QSignalBlocker b(scaleCombo_); scaleCombo_->setEditText(QStringLiteral("1:") + ds); }
    } else {
        stripScale_->setText(QStringLiteral("화면 축척 —")); stripState_->clear();
        if (scaleCombo_) { QSignalBlocker b(scaleCombo_); scaleCombo_->setEditText(QStringLiteral("—")); }
    }
    if (secCount_) secCount_->setText(QString::number(sections_.size()));
}

// ---------------------------------------------------------------- 상태줄
QWidget* MainWindow::buildCoordBar() {
    auto* w = new QWidget; w->setObjectName("coordBar"); w->setAttribute(Qt::WA_StyledBackground); w->setFixedHeight(30);
    auto* h = new QHBoxLayout(w); h->setContentsMargins(4, 2, 8, 2); h->setSpacing(2);
    msg_ = new QLabel; msg_->setObjectName("statusMsg");
    msg_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(msg_, 1);
    auto field = [&](const char* k, int wpx) {
        auto* l = new QLabel(QString::fromUtf8(k)); l->setObjectName("coordKey"); h->addWidget(l);
        auto* e = new QLineEdit; e->setObjectName("coord"); e->setReadOnly(true); e->setFixedWidth(wpx); e->setFocusPolicy(Qt::ClickFocus); e->setAlignment(Qt::AlignRight);
        h->addWidget(e);
        return e;
    };
    cx_ = field("X", 104); cy_ = field("Y", 104); cz_ = field("Z", 70);
    h->addWidget(lab(QStringLiteral("m"), "hint"));
    zSrc_ = new QLabel(QStringLiteral("—")); zSrc_->setObjectName("zSrc"); zSrc_->setMinimumWidth(64);
    h->addWidget(zSrc_);
    h->addSpacing(8);
    h->addWidget(lab(QStringLiteral("축척"), "coordKey"));
    scaleCombo_ = new QComboBox; scaleCombo_->setEditable(true); scaleCombo_->setFixedWidth(84); scaleCombo_->setFocusPolicy(Qt::ClickFocus);
    for (int d : {10, 20, 40, 50, 100, 200}) scaleCombo_->addItem(QStringLiteral("1:%1").arg(d), d);
    scaleCombo_->addItem(QStringLiteral("맞춤"), 0);
    scaleCombo_->setToolTip(QStringLiteral("단면 보기를 이 축척(화면 96 dpi 기준)으로 맞춤. 1:20 / 1:40 에서 레벨선 10 cm · 숫자 50 cm"));
    QObject::connect(scaleCombo_, &QComboBox::activated, this, [this](int i) {
        int d = scaleCombo_->itemData(i).toInt();
        if (d <= 0) { section_->fit(); section_->update(); } else section_->setScreenDenom(d);
        updateHeader();
    });
    h->addWidget(scaleCombo_);
    h->addSpacing(8);
    info_ = new QLabel; info_->setObjectName("statusInfo"); info_->setVisible(false);
    h->addWidget(info_);
    progress_ = new QProgressBar; progress_->setTextVisible(false); progress_->setRange(0, 1000); progress_->setVisible(false); progress_->setFixedWidth(120); h->addWidget(progress_);
    srsLabel_ = new QLabel(QStringLiteral("좌표계 —")); srsLabel_->setObjectName("srsLabel"); h->addWidget(srsLabel_);
    heightBadge2_ = new QToolButton; heightBadge2_->setObjectName("heightBadge"); heightBadge2_->setFocusPolicy(Qt::NoFocus);
    QObject::connect(heightBadge2_, &QToolButton::clicked, this, [this] { dlgHeightDatum(); });
    h->addWidget(heightBadge2_);
    return w;
}

// ---------------------------------------------------------------- 단면 목록(P1-1)
QWidget* MainWindow::buildSidePanel() {
    auto* w = new QWidget; w->setObjectName("sidePanel"); w->setAttribute(Qt::WA_StyledBackground);
    w->setMinimumWidth(200); w->setMaximumWidth(320);
    auto* v = new QVBoxLayout(w); v->setContentsMargins(10, 8, 10, 8); v->setSpacing(6);
    auto* head = new QHBoxLayout; head->setSpacing(6);
    head->addWidget(lab(QStringLiteral("단면 목록"), "sectionHead"));
    secCount_ = lab(QStringLiteral("0"), "monoFaint"); head->addWidget(secCount_);
    head->addStretch();
    auto* add = new QToolButton; add->setText(QStringLiteral("＋ 새 단면")); add->setFocusPolicy(Qt::NoFocus); add->setToolTip(QStringLiteral("새 단면 추가 (N) — 다음 글자 이름으로 그리기 시작"));
    QObject::connect(add, &QToolButton::clicked, this, [this] { addSection(); });
    head->addWidget(add);
    v->addLayout(head);
    secList_ = new QListWidget; secList_->setObjectName("sectionList"); secList_->setFocusPolicy(Qt::NoFocus);
    secList_->setContextMenuPolicy(Qt::CustomContextMenu); secList_->setSpacing(2);
    QObject::connect(secList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { selectSection(secList_->row(it)); });
    QObject::connect(secList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { renameSection(secList_->row(it)); });
    QObject::connect(secList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        auto* it = secList_->itemAt(pt); if (!it) return;
        int i = secList_->row(it);
        QMenu m(this);
        m.addAction(QStringLiteral("이 단면 보기"), this, [this, i] { selectSection(i); });
        m.addAction(QStringLiteral("이름 바꾸기 / 메모…"), this, [this, i] { renameSection(i); });
        m.addSeparator();
        m.addAction(QStringLiteral("목록에서 지우기"), this, [this, i] { deleteSection(i); });
        m.exec(secList_->viewport()->mapToGlobal(pt));
    });
    v->addWidget(secList_, 1);
    auto* foot = lab(QStringLiteral("누르면 그 단면으로 · 두 번 누르면 이름 바꾸기\n목록은 이 PC 설정에 모델별로 저장(파일 → 단면 목록 내보내기)"), "faint");
    foot->setWordWrap(true);
    v->addWidget(foot);
    return w;
}

QString MainWindow::sectionName() const {
    if (current_ >= 0 && current_ < int(sections_.size())) return qs8(sections_[size_t(current_)].name);
    return QStringLiteral("A–A′");
}

QImage MainWindow::makeThumb() const {
    if (!section_->hasResult()) return {};
    SectionDoc d = section_->doc();
    QImage t(128, 60, QImage::Format_ARGB32_Premultiplied); t.fill(QColor("#FFFFFF"));
    QPainter p(&t); p.setRenderHint(QPainter::Antialiasing);
    const auto& r = d.r;
    double s0 = 0, s1 = SectionFrame(r.line).L, z0 = r.zMin, z1 = r.zMax;
    if (z1 <= z0) { z0 = 0; z1 = 1; }
    double pad = 4, sc = std::min((t.width() - 2 * pad) / std::max(1e-6, s1 - s0), (t.height() - 2 * pad) / std::max(1e-6, z1 - z0));
    double ox = (t.width() - (s1 - s0) * sc) / 2, oy = (t.height() - (z1 - z0) * sc) / 2;
    if (!d.img.isNull() && d.imgRes > 0) {
        QRectF dst(ox + (d.imgS0 - s0) * sc, oy + (z1 - d.imgZ1) * sc, d.img.width() * d.imgRes * sc, d.img.height() * d.imgRes * sc);
        p.drawImage(dst, d.img);
    }
    p.setPen(QPen(QColor(255, 0, 0), 2.0));
    for (auto& pl : r.profile) {
        QPolygonF poly;
        for (auto& q : pl) poly << QPointF(ox + (q.x - s0) * sc, oy + (z1 - q.y) * sc);
        p.drawPolyline(poly);
    }
    p.end();
    return t;
}

void MainWindow::refreshSectionList() {
    if (!secList_) return;
    QSignalBlocker b(secList_);
    secList_->clear();
    for (size_t i = 0; i < sections_.size(); ++i) {
        const auto& s = sections_[i];
        auto* it = new QListWidgetItem(secList_);
        auto* row = new QWidget; row->setObjectName("secRow");
        auto* h = new QHBoxLayout(row); h->setContentsMargins(6, 5, 6, 5); h->setSpacing(8);
        auto* th = new QLabel; th->setFixedSize(64, 30); th->setStyleSheet("QLabel{background:#FFFFFF;border:1px solid #DEDCD1;border-radius:3px;}");
        if (auto f = thumbs_.find(int(i)); f != thumbs_.end() && !f->second.isNull())
            th->setPixmap(QPixmap::fromImage(f->second.scaled(62, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        h->addWidget(th);
        double L = std::hypot(s.bx - s.ax, s.by - s.ay);
        auto* txt = vbox({lab(qs8(s.name), "secName"),
                          lab(QStringLiteral("%1 m · 뒤 %2 m").arg(L, 0, 'f', 2).arg(s.back, 0, 'f', 1), "mono")}, 1);
        if (!s.note.empty()) static_cast<QVBoxLayout*>(txt->layout())->insertWidget(2, lab(qs8(s.note), "faint"));
        h->addWidget(txt, 1);
        row->setProperty("current", int(i) == current_);
        it->setSizeHint(QSize(10, s.note.empty() ? 46 : 60));
        secList_->setItemWidget(it, row);
        if (int(i) == current_) it->setSelected(true);
    }
    if (secCount_) secCount_->setText(QString::number(sections_.size()));
}

void MainWindow::syncCurrentSection() {
    if (!src_ || !plan_->hasLine() || applyingState_) return;
    const SectionLine& l = plan_->line();
    const Vec3 o = srs().origin;
    if (current_ < 0 || current_ >= int(sections_.size())) {
        SavedSection s; s.name = sectionLetterName(int(sections_.size()));
        sections_.push_back(s); current_ = int(sections_.size()) - 1;
    }
    SavedSection& s = sections_[size_t(current_)];
    s.ax = l.a.x + o.x; s.ay = l.a.y + o.y; s.bx = l.b.x + o.x; s.by = l.b.y + o.y;
    s.front = front_->value(); s.back = back_->value(); s.backUserSet = backUserSet_;
    if (!thumbs_.count(current_) && section_->hasResult() && lastFinal_) thumbs_[current_] = makeThumb();
    refreshSectionList();
}

void MainWindow::selectSection(int i) {
    if (i < 0 || i >= int(sections_.size()) || !src_) return;
    if (i == current_ && plan_->hasLine()) return;
    current_ = i;
    const SavedSection& s = sections_[size_t(i)];
    const Vec3 o = srs().origin;
    SectionLine l; l.a = {s.ax - o.x, s.ay - o.y}; l.b = {s.bx - o.x, s.by - o.y}; l.front = s.front; l.back = s.back;
    applyingState_ = true;
    { QSignalBlocker b1(front_), b2(back_); front_->setValue(s.front); back_->setValue(s.back); }
    backUserSet_ = s.backUserSet;
    plan_->setBand(s.front, s.back);
    plan_->setLine(l, true);
    applyingState_ = false;
    fitNextResult_ = true;
    requestSection(true);
    updateDepthChips();
    refreshSectionList();
    commitState(QStringLiteral("단면 %1 보기").arg(qs8(s.name)));
    updateHeader();
}

void MainWindow::addSection() {
    if (!src_) return;
    syncCurrentSection();
    std::vector<std::string> used; for (auto& s : sections_) used.push_back(s.name);
    int k = 0; while (std::find(used.begin(), used.end(), sectionLetterName(k)) != used.end()) ++k;
    SavedSection s; s.name = sectionLetterName(k); s.front = front_->value();
    s.back = backUserSet_ ? back_->value() : kDefaultBackDepth; s.backUserSet = backUserSet_;   // 새 단면: 기본 3 m(사용자가 고른 값이 있으면 그 값)
    sections_.push_back(s); current_ = int(sections_.size()) - 1;
    applyingState_ = true;
    plan_->setLine(SectionLine(), false); section_->clear(); last_ = SectionOutput();
    applyingState_ = false;
    refreshSectionList();
    plan_->setDrawMode(true);
    showStatus(QStringLiteral("새 단면 %1: 평면에서 시작점을 클릭하세요").arg(qs8(s.name)));
    updateHeader(); updateEnabled();
}

void MainWindow::renameSection(int i) {
    if (i < 0 || i >= int(sections_.size())) return;
    SavedSection& s = sections_[size_t(i)];
    QDialog d(this); d.setWindowTitle(QStringLiteral("단면 이름 · 메모"));
    auto* f = new QFormLayout(&d);
    auto* name = new QLineEdit(qs8(s.name)); auto* note = new QLineEdit(qs8(s.note));
    note->setPlaceholderText(QStringLiteral("예: 2호 주거지 북벽"));
    f->addRow(QStringLiteral("이름"), name); f->addRow(QStringLiteral("메모"), note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    f->addRow(bb);
    if (d.exec() != QDialog::Accepted) return;
    if (!name->text().trimmed().isEmpty()) s.name = name->text().trimmed().toStdString();
    s.note = note->text().trimmed().toStdString();
    refreshSectionList(); updateHeader(); saveModelState();
}

void MainWindow::deleteSection(int i) {
    if (i < 0 || i >= int(sections_.size())) return;
    if (QMessageBox::question(this, QStringLiteral("단면 지우기"), QStringLiteral("목록에서 %1 을(를) 지울까요? (모델 파일은 그대로)").arg(qs8(sections_[size_t(i)].name))) != QMessageBox::Yes) return;
    sections_.erase(sections_.begin() + i);
    std::map<int, QImage> nt;
    for (auto& [k, im] : thumbs_) if (k < i) nt[k] = im; else if (k > i) nt[k - 1] = im;
    thumbs_ = nt;
    if (current_ == i) current_ = -1; else if (current_ > i) --current_;
    refreshSectionList();
    if (current_ < 0 && !sections_.empty()) selectSection(0);
    saveModelState(); updateHeader();
}

bool MainWindow::exportSectionsJson(const QString& path, QString* msg) {
    syncCurrentSection();
    std::string js = sectionsToJson(sections_, QFileInfo(path_).fileName().toStdString(), srsReport_.desc.shortAscii(), current_);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (msg) *msg = QStringLiteral("저장할 수 없습니다: ") + path; return false; }
    f.write(QByteArray::fromStdString(js));
    if (msg) *msg = QStringLiteral("단면 %1개 저장: %2").arg(sections_.size()).arg(QDir::toNativeSeparators(path));
    return true;
}

void MainWindow::importSectionsJson() {
    if (!src_) return;
    QString start = QFileInfo(path_).absolutePath();
    QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("단면 목록 가져오기"), start, QStringLiteral("단면 목록 (*.sections.json *.json)"));
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, windowTitle(), QStringLiteral("열 수 없습니다")); return; }
    std::vector<SavedSection> v; int cur = 0; std::string err;
    if (!sectionsFromJson(f.readAll().toStdString(), v, &cur, &err)) { QMessageBox::warning(this, windowTitle(), QStringLiteral("단면 목록을 읽지 못했습니다: ") + qs8(err)); return; }
    sections_ = v; thumbs_.clear(); current_ = -1;
    refreshSectionList();
    if (!sections_.empty()) selectSection(std::clamp(cur, 0, int(sections_.size()) - 1));
    saveModelState();
    showStatus(QStringLiteral("단면 %1개 가져옴").arg(sections_.size()));
}

// ---------------------------------------------------------------- 되돌리기
MainWindow::ViewState MainWindow::captureState() const {
    ViewState s;
    s.hasLine = plan_->hasLine(); if (s.hasLine) s.line = plan_->line();
    s.front = front_->value(); s.back = back_->value();
    s.image = action("image")->isChecked(); s.profile = action("line")->isChecked(); s.levels = action("levels")->isChecked();
    s.fade = action("fade")->isChecked(); s.smooth = action("smooth")->isChecked();
    s.heightDecl = src_ ? int(src_->srs.heightDeclared) : 0;
    s.current = current_;
    return s;
}

void MainWindow::applyState(const ViewState& s) {
    applyingState_ = true;
    {
        QSignalBlocker b1(front_), b2(back_);
        front_->setValue(s.front); back_->setValue(s.back);
    }
    plan_->setBand(s.front, s.back);
    const std::pair<const char*, bool> toggles[] = {{"image", s.image}, {"line", s.profile}, {"levels", s.levels}, {"fade", s.fade}, {"smooth", s.smooth}};
    for (auto& [k, v] : toggles) action(k)->setChecked(v);   // applyingState_ 가 되돌리기 기록을 막음(단추 동기화는 신호로)
    section_->setStyle(secStyle());
    if (src_ && int(src_->srs.heightDeclared) != s.heightDecl) setHeightDeclaration(VDatum(s.heightDecl), true);
    current_ = (s.current >= 0 && s.current < int(sections_.size())) ? s.current : current_;
    if (s.hasLine) { SectionLine l = s.line; l.front = s.front; l.back = s.back; plan_->setLine(l, true); }
    else { plan_->setLine(SectionLine(), false); section_->clear(); last_ = SectionOutput(); }
    lastState_ = s;
    applyingState_ = false;
    if (s.hasLine) { requestSection(true); syncCurrentSection(); }
    updateDepthChips(); updateEnabled(); updateHeader(); refreshSectionList();
}

void MainWindow::commitState(const QString& text, int mergeId) {
    if (applyingState_ || !undo_ || !src_) return;
    ViewState now = captureState();
    if (now == lastState_) return;
    undo_->push(new StateCmd(this, lastState_, now, text, mergeId));
    lastState_ = now;
    saveModelState();
}

bool MainWindow::undoTest(QString* log) {
    auto say = [&](const QString& s) { if (log) *log += s + "\n"; };
    if (!src_ || !plan_->hasLine()) { say("undo-test: no line"); return false; }
    syncCurrentSection();
    undo_->clear(); lastState_ = captureState();
    ViewState s0 = captureState();
    back_->setValue(back_->value() > 2 ? 1.0 : 3.0);      // 1 두께(병합 id 1)
    action("flip")->trigger();                              // 2 반전
    action("levels")->trigger();                            // 3 레벨선 끔/켬
    ViewState s3 = captureState();
    int n = undo_->count();
    say(QStringLiteral("undo-test: commands=%1 (%2)").arg(n).arg(n == 3 ? "ok" : "FAIL"));
    for (int i = 0; i < 3; ++i) undo_->undo();
    bool u = captureState() == s0;
    for (int i = 0; i < 3; ++i) undo_->redo();
    bool r = captureState() == s3;
    for (int i = 0; i < 3; ++i) undo_->undo();
    bool u2 = captureState() == s0;
    say(QStringLiteral("undo-test: undo3=%1 redo3=%2 undo3again=%3").arg(u ? "ok" : "FAIL", r ? "ok" : "FAIL", u2 ? "ok" : "FAIL"));
    return n == 3 && u && r && u2;
}

// ---------------------------------------------------------------- 최근 파일 · 모델별 상태
QStringList MainWindow::recentFiles() const { return QSettings().value("recent/files").toStringList(); }

void MainWindow::addRecent(const QString& path) {
    std::vector<std::string> v; for (auto& s : recentFiles()) v.push_back(s.toStdString());
    v = pushRecent(v, QFileInfo(path).absoluteFilePath().toStdString(), 8);
    QStringList out; for (auto& s : v) out << qs8(s);
    QSettings().setValue("recent/files", out);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList rf = recentFiles();
    if (rf.isEmpty()) { recentMenu_->addAction(QStringLiteral("(최근 연 파일 없음)"))->setEnabled(false); return; }
    int i = 0;
    for (const QString& f : rf) {
        bool ok = QFileInfo::exists(f);
        QString t = QStringLiteral("&%1  %2%3").arg(++i).arg(QFileInfo(f).fileName(), ok ? QString() : QStringLiteral("   ● 원본 없음"));
        auto* a = recentMenu_->addAction(t, this, [this, f] { openFile(f); });
        a->setToolTip(QDir::toNativeSeparators(f)); a->setEnabled(ok);
    }
    recentMenu_->addSeparator();
    recentMenu_->addAction(QStringLiteral("목록 비우기"), this, [this] { QSettings().remove("recent/files"); rebuildRecentMenu(); if (body_ && body_->currentIndex() == 0) rebuildStartPage(); });
}

void MainWindow::saveModelState() {
    if (!src_ || path_.isEmpty()) return;
    syncCurrentSection();
    QSettings st; const QString k = modelKey(path_);
    const Vec3 o = srs().origin;
    st.setValue(k + "path", QFileInfo(path_).absoluteFilePath());
    st.setValue(k + "front", front_->value()); st.setValue(k + "back", back_->value()); st.setValue(k + "backUserSet", backUserSet_);
    st.setValue(k + "hasLine", plan_->hasLine());
    if (plan_->hasLine()) {
        const SectionLine& l = plan_->line();
        st.setValue(k + "line", QStringLiteral("%1 %2 %3 %4").arg(l.a.x + o.x, 0, 'f', 4).arg(l.a.y + o.y, 0, 'f', 4).arg(l.b.x + o.x, 0, 'f', 4).arg(l.b.y + o.y, 0, 'f', 4));
    }
    st.setValue(k + "camera", QStringLiteral("%1 %2 %3").arg(plan_->cameraX() + o.x, 0, 'f', 3).arg(plan_->cameraY() + o.y, 0, 'f', 3).arg(plan_->metersPerPixel(), 0, 'g', 6));
    st.setValue(k + "sections", qs8(sectionsToJson(sections_, QFileInfo(path_).fileName().toStdString(), srsReport_.desc.shortAscii(), current_)));
    st.setValue(k + "count", int(sections_.size()));
    QStringList names; for (auto& s : sections_) names << qs8(s.name);
    st.setValue(k + "names", names.join(", "));
    st.setValue(k + "srs", srsReport_.desc.horizontalEpsg ? QStringLiteral("EPSG:%1").arg(srsReport_.desc.horizontalEpsg) : qs8(srsReport_.desc.shortAscii()));
    QString hs, tip; QString ht = heightBadgeText(&hs, &tip);
    st.setValue(k + "height", ht); st.setValue(k + "heightState", hs);
    st.setValue(k + "lastOpened", QDateTime::currentDateTime().toString(Qt::ISODate));
}

void MainWindow::restoreModelState() {
    sections_.clear(); thumbs_.clear(); current_ = -1;
    QSettings st; const QString k = modelKey(path_);
    const Vec3 o = srs().origin;
    if (st.contains(k + "sections")) {
        int cur = -1; std::string err;
        sectionsFromJson(st.value(k + "sections").toString().toStdString(), sections_, &cur, &err);
        current_ = (cur >= 0 && cur < int(sections_.size())) ? cur : (sections_.empty() ? -1 : 0);
    }
    if (st.contains(k + "front")) {
        QSignalBlocker b1(front_), b2(back_);
        const bool us = st.value(k + "backUserSet", false).toBool();
        front_->setValue(st.value(k + "front").toDouble());
        back_->setValue(resolveBackDepth(true, st.value(k + "back").toDouble(), us));   // 옛 0.5(기본값) → 3 m
        backUserSet_ = us;
        plan_->setBand(front_->value(), back_->value());
    }
    if (st.value(k + "hasLine").toBool()) {
        QStringList p = st.value(k + "line").toString().split(' ', Qt::SkipEmptyParts);
        if (p.size() == 4) {
            SectionLine l; l.a = {p[0].toDouble() - o.x, p[1].toDouble() - o.y}; l.b = {p[2].toDouble() - o.x, p[3].toDouble() - o.y};
            l.front = front_->value(); l.back = back_->value();
            applyingState_ = true; plan_->setLine(l, true); applyingState_ = false;
            fitNextResult_ = true;
            requestSection(true);
        }
    }
    QStringList cam = st.value(k + "camera").toString().split(' ', Qt::SkipEmptyParts);
    if (cam.size() == 3 && cam[2].toDouble() > 0) {
        double cx = cam[0].toDouble() - o.x, cy = cam[1].toDouble() - o.y, mpp = cam[2].toDouble();
        QTimer::singleShot(50, this, [this, cx, cy, mpp] { if (src_) plan_->setCamera(cx, cy, mpp); });
    }
    if (undo_) undo_->clear();
    lastState_ = captureState();
    updateDepthChips(); refreshSectionList(); updateHeader();
}

// ---------------------------------------------------------------- 시작 화면
QWidget* MainWindow::buildStartPage() {
    auto* w = new QWidget; w->setObjectName("startPage"); w->setAttribute(Qt::WA_StyledBackground);
    new QVBoxLayout(w);
    return w;
}

void MainWindow::rebuildStartPage() {
    if (!startPage_) return;
    auto* lay = static_cast<QVBoxLayout*>(startPage_->layout());
    while (auto* it = lay->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
    lay->setContentsMargins(48, 32, 48, 24); lay->setSpacing(16);
    auto* hdr = new QWidget; { auto* v = new QVBoxLayout(hdr); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(4);
        v->addWidget(lab(QStringLiteral("발굴 단면뷰어"), "bigTitle"));
        v->addWidget(lab(QStringLiteral("3MX·OBJ 모델에서 단면도를 뽑습니다. 원본 파일은 바꾸지 않습니다."), "hint")); }
    lay->addWidget(hdr);
    auto* cols = new QHBoxLayout; cols->setSpacing(20);
    auto* left = new QVBoxLayout; left->setSpacing(14);
    const QStringList rf = recentFiles();
    QSettings st;
    auto meta = [&](const QString& f, const char* key) { return st.value(modelKey(f) + key); };
    auto ago = [](const QString& iso) {
        QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
        if (!t.isValid()) return QStringLiteral("—");
        qint64 s = t.secsTo(QDateTime::currentDateTime());
        if (s < 3600) return QStringLiteral("%1분 전").arg(std::max<qint64>(1, s / 60));
        if (s < 86400) return QStringLiteral("오늘 %1").arg(t.toString("HH:mm"));
        if (s < 2 * 86400) return QStringLiteral("어제 %1").arg(t.toString("HH:mm"));
        return t.toString("yyyy-MM-dd");
    };
    if (!rf.isEmpty()) {
        const QString f = rf.first();
        bool ok = QFileInfo::exists(f);
        auto* card = new QFrame; card->setObjectName("card"); card->setAttribute(Qt::WA_StyledBackground);
        auto* g = new QGridLayout(card); g->setContentsMargins(20, 16, 20, 16); g->setHorizontalSpacing(24); g->setVerticalSpacing(6);
        g->addWidget(lab(QStringLiteral("이어서 하기"), "faint"), 0, 0, 1, 4);
        g->addWidget(lab(QFileInfo(f).completeBaseName(), "heroName"), 1, 0, 1, 4);
        auto* pathL = lab(QDir::toNativeSeparators(f), "monoFaint"); pathL->setTextInteractionFlags(Qt::TextSelectableByMouse);
        g->addWidget(pathL, 2, 0, 1, 4);
        const QString ks[4] = {QStringLiteral("마지막으로 연 때"), QStringLiteral("단면"), QStringLiteral("좌표계"), QStringLiteral("높이 기준")};
        QString names = meta(f, "names").toString();
        int cnt = meta(f, "count").toInt();
        const QString vs[4] = {ago(meta(f, "lastOpened").toString()), cnt ? QStringLiteral("%1개 · %2").arg(cnt).arg(names) : QStringLiteral("없음"),
                               meta(f, "srs").toString().isEmpty() ? QStringLiteral("—") : meta(f, "srs").toString(),
                               meta(f, "height").toString().isEmpty() ? QStringLiteral("—") : meta(f, "height").toString()};
        for (int i = 0; i < 4; ++i) {
            g->addWidget(lab(ks[i], "faint"), 3, i);
            auto* val = lab(vs[i]); val->setWordWrap(true);
            if (i == 3) { QString hs = meta(f, "heightState").toString(); if (hs == "warn") val->setStyleSheet("color:#7A5A00;font-weight:600"); else if (hs == "error") val->setStyleSheet("color:#B53333;font-weight:600"); }
            g->addWidget(val, 4, i);
        }
        auto* go = new QPushButton(ok ? QStringLiteral("이어서 열기  →") : QStringLiteral("원본 없음")); go->setObjectName("primary"); go->setEnabled(ok);
        go->setToolTip(QStringLiteral("마지막 단면선·두께·화면을 그대로 되살립니다 (Ctrl+Shift+O)"));
        QObject::connect(go, &QPushButton::clicked, this, [this, f] { openFile(f); });
        g->addWidget(go, 1, 4, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
        g->setColumnStretch(3, 1);
        left->addWidget(card);
    }
    auto* recentHead = lab(QStringLiteral("최근 연 모델"), "sectionHead");
    left->addWidget(recentHead);
    auto* tbl = new QTableWidget(std::max<int>(1, int(rf.size())), 4); tbl->setObjectName("recentTable");
    tbl->setHorizontalHeaderLabels({QStringLiteral("모델"), QStringLiteral("좌표계 · 높이"), QStringLiteral("단면"), QStringLiteral("마지막으로 연 때")});
    tbl->verticalHeader()->setVisible(false); tbl->setEditTriggers(QAbstractItemView::NoEditTriggers); tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->setSelectionMode(QAbstractItemView::SingleSelection); tbl->setShowGrid(false); tbl->setFocusPolicy(Qt::NoFocus);
    tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) tbl->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    tbl->verticalHeader()->setDefaultSectionSize(44);
    if (rf.isEmpty()) {
        tbl->setItem(0, 0, new QTableWidgetItem(QStringLiteral("아직 연 모델이 없습니다 — 아래에서 파일을 여세요")));
        tbl->setSpan(0, 0, 1, 4);
    }
    for (int i = 0; i < rf.size(); ++i) {
        const QString& f = rf[i];
        bool ok = QFileInfo::exists(f);
        auto* it0 = new QTableWidgetItem;
        it0->setToolTip(QDir::toNativeSeparators(f) + (ok ? QString() : QStringLiteral("\n원본 파일이 없습니다(이동·삭제·드라이브 분리)")));
        tbl->setItem(i, 0, it0);
        {   // 두 줄: 굵은 이름 + 회색 경로
            auto* cell = new QWidget; cell->setAttribute(Qt::WA_TransparentForMouseEvents);
            auto* cv = new QVBoxLayout(cell); cv->setContentsMargins(8, 3, 8, 3); cv->setSpacing(0);
            auto* nm = lab((ok ? QString() : QStringLiteral("<span style='color:#B53333'>●</span> ")) + QFileInfo(f).fileName().toHtmlEscaped(), "secName");
            nm->setTextFormat(Qt::RichText);
            auto* pth = lab(QDir::toNativeSeparators(f), "monoFaint");
            pth->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            cv->addWidget(nm); cv->addWidget(pth);
            tbl->setCellWidget(i, 0, cell);
        }
        QString sr = meta(f, "srs").toString(), ht = meta(f, "height").toString();
        tbl->setItem(i, 1, new QTableWidgetItem(sr.isEmpty() ? QStringLiteral("—") : sr + (ht.isEmpty() ? QString() : " · " + ht)));
        tbl->setItem(i, 2, new QTableWidgetItem(QString::number(meta(f, "count").toInt())));
        tbl->setItem(i, 3, new QTableWidgetItem(ago(meta(f, "lastOpened").toString())));
    }
    QObject::connect(tbl, &QTableWidget::cellClicked, this, [this, rf](int r, int) { if (r < rf.size() && QFileInfo::exists(rf[r])) openFile(rf[r]); });
    tbl->setFixedHeight(34 + 44 * std::min<int>(6, std::max<int>(1, int(rf.size()))));
    tbl->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    left->addWidget(tbl);
    auto* drop = new QFrame; drop->setObjectName("dropZone"); drop->setAttribute(Qt::WA_StyledBackground);
    { auto* h = new QHBoxLayout(drop); h->setContentsMargins(20, 16, 20, 16); h->setSpacing(14);
        auto* ic = new QLabel; ic->setPixmap(theme::icon(theme::Ico::Open, 28, theme::Ink2).pixmap(28, 28)); h->addWidget(ic);
        h->addWidget(vbox({lab(QStringLiteral("모델 파일을 여기로 끌어다 놓으세요"), "secName"), lab(QStringLiteral("Production_*.3mx (권장) · *.obj — 폴더가 읽기 전용이어도 됩니다"), "hint")}, 2), 1);
        auto* ob = new QPushButton(QStringLiteral("파일 열기   Ctrl+O")); QObject::connect(ob, &QPushButton::clicked, this, [this] { chooseOpen(); });
        h->addWidget(ob); }
    left->addWidget(drop);
    left->addStretch(1);
    cols->addLayout(left, 3);
    // 오른쪽: 세 걸음 + 자주 쓰는 키
    auto* right = new QVBoxLayout; right->setSpacing(14);
    auto* steps = new QFrame; steps->setObjectName("cardSide"); steps->setAttribute(Qt::WA_StyledBackground);
    { auto* v = new QVBoxLayout(steps); v->setContentsMargins(18, 14, 18, 14); v->setSpacing(10);
        v->addWidget(lab(QStringLiteral("세 걸음으로 단면도"), "sectionHead"));
        const char* st3[3][2] = {{"모델 열기", "3MX 는 대략 모양이 먼저 뜨고 디테일이 이어서 옵니다"}, {"단면선 긋기  S", "평면에서 시작점 A, 끝점 A′ 를 클릭. 뒤 깊이 기본 3 m(숫자키 1–5)"}, {"도면 만들기  Ctrl+P", "축척·용지를 고르면 넘치는지 미리 보여 줍니다"}};
        for (int i = 0; i < 3; ++i) {
            auto* row = new QHBoxLayout; row->setSpacing(10);
            auto* n = lab(QString::number(i + 1), "stepNum"); n->setFixedSize(24, 24); n->setAlignment(Qt::AlignCenter);
            row->addWidget(n, 0, Qt::AlignTop);
            auto* t = vbox({lab(QString::fromUtf8(st3[i][0]), "secName"), lab(QString::fromUtf8(st3[i][1]), "hint")}, 1);
            static_cast<QLabel*>(t->layout()->itemAt(1)->widget())->setWordWrap(true);
            row->addWidget(t, 1);
            v->addLayout(row);
        } }
    right->addWidget(steps);
    auto* keys = new QFrame; keys->setObjectName("cardSide"); keys->setAttribute(Qt::WA_StyledBackground);
    { auto* g = new QGridLayout(keys); g->setContentsMargins(18, 14, 18, 14); g->setHorizontalSpacing(12); g->setVerticalSpacing(6);
        g->addWidget(lab(QStringLiteral("자주 쓰는 키"), "sectionHead"), 0, 0, 1, 2);
        const char* kk[][2] = {{"Ctrl+O", "열기"}, {"Ctrl+Shift+O", "최근 모델 다시 열기"}, {"S", "단면선 그리기"}, {"1 – 5", "뒤 깊이 0.5 / 1 / 2 / 3(기본) / 5 m"},
                               {"[  ]", "평행 이동 0.1 m"}, {"R", "방향 반전"}, {"Ctrl+Z / Ctrl+Y", "되돌리기 / 다시"}, {"Ctrl+P", "도면"}, {"F1", "모든 단축키"}};
        int r = 1;
        for (auto& k : kk) { g->addWidget(kbd(QString::fromUtf8(k[0])), r, 0, Qt::AlignLeft); g->addWidget(lab(QString::fromUtf8(k[1])), r, 1); ++r; }
        g->setColumnStretch(1, 1); }
    right->addWidget(keys);
    right->addStretch();
    cols->addLayout(right, 2);
    lay->addLayout(cols, 1);
    auto* foot = lab(QStringLiteral("발굴 단면뷰어 %1 · 높이는 모델 좌표계 그대로(높이 기준 지정은 이름표만) · 단면선 빨강 #FF0000").arg(QString::fromUtf8(kVersion)), "faint");
    lay->addWidget(foot);
}

void MainWindow::showStart(bool on) {
    if (!body_) return;
    if (on) rebuildStartPage();
    body_->setCurrentIndex(on ? 0 : 1);
    if (ctxBar_ && on) ctxBar_->setVisible(false);
    showStatus(on ? QStringLiteral("모델을 여세요 — 최근 모델을 누르거나 파일을 끌어다 놓아도 됩니다") : QString());
}

// ---------------------------------------------------------------- 작은 동작
void MainWindow::setBackDepth(double v) {
    if (!back_) return;
    backUserSet_ = true;
    QSettings().setValue("section/backUserSet", true);
    back_->setValue(std::clamp(v, 0.0, kMaxBandDepth));
    updateDepthChips();
    showStatus(QStringLiteral("뒤 깊이 %1 m — 입면 영상에 단면선 뒤 %1 m 까지 보입니다").arg(back_->value(), 0, 'g', 3));
}

void MainWindow::updateDepthChips() {
    const double vals[5] = {0.5, 1, 2, 3, 5};
    for (int i = 0; i < 5; ++i) if (depthChip_[i]) depthChip_[i]->setChecked(std::fabs(back_->value() - vals[i]) < 1e-6);
}

void MainWindow::shiftLine(double d) {
    if (!src_ || !plan_->hasLine()) return;
    SectionLine l = plan_->line();
    SectionFrame fr(l);
    Vec2 off = fr.n * d;
    l.a = l.a + off; l.b = l.b + off;
    plan_->setLine(l, true);
    requestSection(true);
    syncCurrentSection();
    commitState(QStringLiteral("평행 이동"), 2);
    showStatus(QStringLiteral("단면선을 %1 %2 m 옮김 ([ ] 0.1 m · { } 1 m)").arg(d > 0 ? QStringLiteral("보는 쪽으로") : QStringLiteral("반대쪽으로")).arg(std::fabs(d), 0, 'g', 3));
    updateHeader();
}

void MainWindow::applyHighContrast(bool on) {
    highContrast_ = on;
    QSettings().setValue("ui/highContrast", on);
    qApp->setStyleSheet(theme::styleSheet(on));
    section_->setStyle(secStyle());
    updateHeader();
}

void MainWindow::dlgKeys() {
    QDialog d(this); d.setWindowTitle(QStringLiteral("단축키"));
    auto* v = new QVBoxLayout(&d);
    auto* t = new QTableWidget(0, 2); t->setHorizontalHeaderLabels({QStringLiteral("키"), QStringLiteral("하는 일")});
    t->verticalHeader()->setVisible(false); t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->setShowGrid(false);
    t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    const char* rows[][2] = {
        {"Ctrl+O", "모델 열기"}, {"Ctrl+Shift+O", "최근 모델 다시 열기(마지막 단면선·화면 그대로)"}, {"S", "단면선 그리기 — A 클릭, A′ 클릭"},
        {"Shift (그리는 중)", "동서·남북으로 고정"}, {"Enter (그리는 중)", "A/A′ 좌표 직접 입력"}, {"Esc", "그리기 취소"},
        {"1 2 3 4 5", "뒤 깊이 0.5 / 1 / 2 / 3 / 5 m (기본 3 m — 입면도용 배경)"}, {"[  ]", "평행 이동 −0.1 / +0.1 m (보는 쪽 +)"}, {"{  }", "평행 이동 −1 / +1 m"},
        {"X", "세로 과장 ×1 → ×2 → ×5 → ×10 (화면만 · 도면은 1:1)"},
        {"R", "방향 반전(A↔A′)"}, {"N", "새 단면 추가"}, {"I · L · V", "입면 영상 · 단면선 · 레벨선 켜기/끄기"},
        {"Ctrl+Z / Ctrl+Y", "되돌리기 / 다시(단면선·두께·표시·높이 기준)"}, {"Ctrl+P", "도면(축척·용지 미리보기 → PDF/DXF/PNG/TIFF)"},
        {"Ctrl+D · Ctrl+E", "단면 DXF · 단면 영상(GeoTIFF 포함)"}, {"F · T · + · −", "맞춤 · 위에서 · 확대 · 축소"}, {"Ctrl+1 · Ctrl+2", "평면 · 단면 보기 켜기/끄기"},
        {"F1", "이 표"}};
    for (auto& r : rows) {
        int i = t->rowCount(); t->insertRow(i);
        auto* k = new QTableWidgetItem(QString::fromUtf8(r[0])); k->setFont(theme::monoFont(12));
        t->setItem(i, 0, k); t->setItem(i, 1, new QTableWidgetItem(QString::fromUtf8(r[1])));
    }
    t->resizeColumnToContents(0);
    v->addWidget(t);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.resize(560, 620);
    d.exec();
}

void MainWindow::dlgCoordEntry() {
    if (!src_) return;
    const Vec3 o = srs().origin;
    SectionLine cur = plan_->line();
    bool haveA = plan_->drawStage() == 1;
    QDialog d(this); d.setWindowTitle(QStringLiteral("단면선 좌표 입력 (실좌표 m)"));
    auto* g = new QGridLayout(&d);
    auto mk = [&](double v) { auto* s = new QDoubleSpinBox; s->setRange(-1e8, 1e8); s->setDecimals(3); s->setValue(v); s->setFixedWidth(140); return s; };
    double ax = cur.a.x + o.x, ay = cur.a.y + o.y, bx = cur.b.x + o.x, by = cur.b.y + o.y;
    if (!plan_->hasLine() && !haveA) { ax = plan_->cameraX() + o.x; ay = plan_->cameraY() + o.y; bx = ax + 10; by = ay; }
    auto *sax = mk(ax), *say = mk(ay), *sbx = mk(bx), *sby = mk(by);
    g->addWidget(lab(QString()), 0, 0); g->addWidget(lab(QStringLiteral("X (동)"), "hint"), 0, 1); g->addWidget(lab(QStringLiteral("Y (북)"), "hint"), 0, 2);
    g->addWidget(lab(QStringLiteral("<b>A</b>")), 1, 0); g->addWidget(sax, 1, 1); g->addWidget(say, 1, 2);
    g->addWidget(lab(QStringLiteral("<b>A′</b>")), 2, 0); g->addWidget(sbx, 2, 1); g->addWidget(sby, 2, 2);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    g->addWidget(bb, 3, 0, 1, 3);
    if (d.exec() != QDialog::Accepted) return;
    SectionLine l; l.a = {sax->value() - o.x, say->value() - o.y}; l.b = {sbx->value() - o.x, sby->value() - o.y};
    l.front = front_->value(); l.back = back_->value();
    if (SectionFrame(l).L <= 1e-3) { QMessageBox::warning(this, windowTitle(), QStringLiteral("A 와 A′ 가 같습니다")); return; }
    if (plan_->drawStage() >= 0) plan_->finishDrawAt(l);
    else { plan_->setLine(l, true); fitNextResult_ = true; requestSection(true); syncCurrentSection(); commitState(QStringLiteral("단면선 좌표 입력")); }
}

// ---------------------------------------------------------------- 생성자
MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("발굴 단면뷰어 %1").arg(QString::fromUtf8(kVersion)));
    setAcceptDrops(true);
    bilingual_ = QSettings().value("ui/bilingual", false).toBool();
    highContrast_ = QSettings().value("ui/highContrast", false).toBool();
    using I = theme::Ico;
    undo_ = new QUndoStack(this);
    undo_->setUndoLimit(200);
    makeAction("open", QStringLiteral("열기"), "Open", I::Open, "Ctrl+O");
    makeAction("recent", QStringLiteral("최근"), "Recent", I::Recent, "Ctrl+Shift+O");
    makeAction("close", QStringLiteral("닫기"), "Close", I::Close);
    makeAction("about", QStringLiteral("프로그램 정보"), "About", I::Info);
    makeAction("keys", QStringLiteral("단축키"), "Shortcuts", I::Keys, "F1");
    makeAction("quit", QStringLiteral("끝내기"), "Exit", I::Close, "Ctrl+Q");
    makeAction("draw", QStringLiteral("단면선 그리기"), "Draw Section", I::Draw, "S", true);
    makeAction("flip", QStringLiteral("방향 반전"), "Flip Direction", I::Flip, "R");
    makeAction("move", QStringLiteral("평행 이동"), "Offset", I::Move);
    makeAction("addsec", QStringLiteral("단면 추가"), "Add Section", I::Add, "N");
    makeAction("clear", QStringLiteral("단면선 지우기"), "Clear Line", I::Clear);
    makeAction("fit", QStringLiteral("맞춤"), "Fit View", I::Fit, "F");
    makeAction("top", QStringLiteral("위에서"), "Top", I::Plan, "T");
    makeAction("zoomin", QStringLiteral("확대"), "Zoom In", I::ZoomIn, "+");
    makeAction("zoomout", QStringLiteral("축소"), "Zoom Out", I::ZoomOut, "-");
    makeAction("image", QStringLiteral("입면 영상"), "Image", I::Image, "I", true)->setChecked(true);
    makeAction("line", QStringLiteral("단면선"), "Profile Line", I::Line, "L", true)->setChecked(true);
    makeAction("levels", QStringLiteral("레벨선"), "Level Lines", I::Levels, "V", true)->setChecked(true);
    makeAction("smooth", QStringLiteral("평활"), "Smooth", I::Smooth, QString(), true);
    makeAction("fade", QStringLiteral("깊이 음영"), "Depth Shading", I::Band, QString(), true);
    action("fade")->setToolTip(QStringLiteral("깊이 음영 (Depth Shading) — 입면 영상에서 단면선보다 뒤에 있는 면일수록 옅게 그려 빨간 단면선이 잘 보이게 합니다(내보내기도 같음)"));
    makeAction("info", QStringLiteral("단면 정보"), "Section Info", I::Info);
    makeAction("height", QStringLiteral("높이 기준 지정"), "Height Datum", I::Height);
    makeAction("view1", QStringLiteral("평면"), "Plan", I::View1, "Ctrl+1", true)->setChecked(true);
    makeAction("view2", QStringLiteral("단면"), "Section", I::View2, "Ctrl+2", true)->setChecked(true);
    makeAction("listpanel", QStringLiteral("단면 목록"), "Section List", I::Csv, QString(), true)->setChecked(QSettings().value("ui/sectionList", true).toBool());
    makeAction("sheet", QStringLiteral("도면"), "Sheet", I::Sheet, "Ctrl+P");
    makeAction("dxf", QStringLiteral("단면 DXF"), "Section DXF", I::Dxf, "Ctrl+D");
    makeAction("secimg", QStringLiteral("단면 영상"), "PNG/TIFF/GeoTIFF", I::Picture, "Ctrl+E");
    makeAction("plan", QStringLiteral("평면 GeoTIFF"), "Plan GeoTIFF", I::Geo);
    makeAction("xyz", QStringLiteral("점군 XYZ"), "Points XYZ", I::Xyz);
    makeAction("las", QStringLiteral("점군 LAS"), "Points LAS", I::Las);
    makeAction("csv", QStringLiteral("단면선 CSV"), "Profile CSV", I::Csv);
    makeAction("secjson", QStringLiteral("단면 목록 내보내기"), "Export Sections", I::Csv);
    makeAction("secimport", QStringLiteral("단면 목록 가져오기"), "Import Sections", I::Open);
    makeAction("contrast", QStringLiteral("고대비"), "High Contrast", I::Band, QString(), true)->setChecked(highContrast_);
    makeAction("lang", QStringLiteral("영어 병기"), "English", I::Lang, QString(), true)->setChecked(bilingual_);
    makeAction("coord", QStringLiteral("좌표 입력"), "Enter Coordinates", I::Draw, "Return")->setEnabled(false);
    makeAction("esc", QStringLiteral("그리기 취소"), "Cancel", I::Close, "Esc")->setEnabled(false);
    makeAction("undo", QStringLiteral("되돌리기"), "Undo", I::Undo, "Ctrl+Z")->setEnabled(false);
    makeAction("redo", QStringLiteral("다시"), "Redo", I::Redo, "Ctrl+Y")->setEnabled(false);
    action("redo")->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
    action("smooth")->setChecked(QSettings().value("section/smooth", false).toBool());
    action("fade")->setChecked(QSettings().value("section/depthFade", true).toBool());
    {
        auto* m = new QMenu(this);
        m->addAction(QStringLiteral("보는 쪽으로 0.1 m   ]"), this, [this] { shiftLine(0.1); });
        m->addAction(QStringLiteral("반대쪽으로 0.1 m   ["), this, [this] { shiftLine(-0.1); });
        m->addAction(QStringLiteral("보는 쪽으로 1 m   }"), this, [this] { shiftLine(1.0); });
        m->addAction(QStringLiteral("반대쪽으로 1 m   {"), this, [this] { shiftLine(-1.0); });
        action("move")->setMenu(m);
    }

    plan_ = new PlanView; section_ = new SectionView;
    auto* central = new QWidget; central->setObjectName("central");
    auto* v = new QVBoxLayout(central); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    v->addWidget(buildRibbon());
    ctxBar_ = buildCtxBar();
    v->addWidget(ctxBar_);
    body_ = new QStackedWidget;
    startPage_ = buildStartPage();
    body_->addWidget(startPage_);
    workArea_ = new QWidget; workArea_->setObjectName("workArea");
    { auto* wl = new QVBoxLayout(workArea_); wl->setContentsMargins(0, 0, 0, 0); wl->setSpacing(0);
        notice_ = buildNotice(); wl->addWidget(notice_);
        split_ = new QSplitter(Qt::Horizontal); split_->setHandleWidth(1);
        sidePanel_ = buildSidePanel();
        planFrame_ = buildPlanFrame(); sectionFrame_ = buildSectionFrame();
        split_->addWidget(sidePanel_); split_->addWidget(planFrame_); split_->addWidget(sectionFrame_);
        split_->setStretchFactor(0, 0); split_->setStretchFactor(1, 5); split_->setStretchFactor(2, 7);
        split_->setChildrenCollapsible(false);
        split_->setSizes({232, 560, 760});
        sidePanel_->setVisible(action("listpanel")->isChecked());
        wl->addWidget(split_, 1); }
    body_->addWidget(workArea_);
    v->addWidget(body_, 1);
    v->addWidget(buildCoordBar());
    setCentralWidget(central);
    retranslate();
    tabs_->setCurrentIndex(1);
    rebuildRecentMenu();
    setActiveView(0);

    front_->setValue(QSettings().value("section/front", 0.0).toDouble());
    {   // 뒤 깊이: 기본 3 m. 1.2.0 까지 저장된 0.5(옛 기본값)는 사용자가 바꾼 표시가 없으면 새 기본값으로
        QSettings st;
        backUserSet_ = st.value("section/backUserSet", false).toBool();
        back_->setValue(resolveBackDepth(st.contains("section/back"), st.value("section/back").toDouble(), backUserSet_));
    }
    plan_->setBand(front_->value(), back_->value());
    if (auto g = QSettings().value("ui/geometry").toByteArray(); !g.isEmpty()) restoreGeometry(g);
    else resize(1600, 950);

    // ---- 숫자키·평행 이동(작업 영역에서만: 입력 칸 타이핑과 겹치지 않게)
    {
        const double depths[5] = {0.5, 1, 2, 3, 5};
        for (int i = 0; i < 5; ++i) {
            auto* sc = new QShortcut(QKeySequence(QString::number(i + 1)), workArea_);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            double dv = depths[i];
            QObject::connect(sc, &QShortcut::activated, this, [this, dv] { if (src_) setBackDepth(dv); });
        }
        {   // X: 세로 과장 1 → 2 → 5 → 10 → 1 (화면만)
            auto* sc = new QShortcut(QKeySequence(QStringLiteral("X")), workArea_);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            QObject::connect(sc, &QShortcut::activated, this, [this] {
                const double v = section_->verticalExaggeration();
                setVex(v < 2 ? 2 : v < 5 ? 5 : v < 10 ? 10 : 1);
            });
        }
        const std::pair<const char*, double> shifts[] = {{"]", 0.1}, {"[", -0.1}, {"}", 1.0}, {"{", -1.0}};
        for (auto& [k, d] : shifts) {
            auto* sc = new QShortcut(QKeySequence(QString::fromLatin1(k)), workArea_);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            double dd = d;
            QObject::connect(sc, &QShortcut::activated, this, [this, dd] { shiftLine(dd); });
        }
    }

    // ---- 연결 ----
    QObject::connect(undo_, &QUndoStack::canUndoChanged, this, [this](bool b) { action("undo")->setEnabled(b); });
    QObject::connect(undo_, &QUndoStack::canRedoChanged, this, [this](bool b) { action("redo")->setEnabled(b); });
    QObject::connect(undo_, &QUndoStack::undoTextChanged, this, [this](const QString& t) { action("undo")->setToolTip(t.isEmpty() ? QStringLiteral("되돌리기 [Ctrl+Z]") : QStringLiteral("되돌리기: %1 [Ctrl+Z]").arg(t)); });
    QObject::connect(undo_, &QUndoStack::redoTextChanged, this, [this](const QString& t) { action("redo")->setToolTip(t.isEmpty() ? QStringLiteral("다시 [Ctrl+Y]") : QStringLiteral("다시: %1 [Ctrl+Y]").arg(t)); });
    QObject::connect(action("undo"), &QAction::triggered, this, [this] { if (!plan_->drawMode()) undo_->undo(); });
    QObject::connect(action("redo"), &QAction::triggered, this, [this] { if (!plan_->drawMode()) undo_->redo(); });
    QObject::connect(action("open"), &QAction::triggered, this, [this] { chooseOpen(); });
    QObject::connect(action("recent"), &QAction::triggered, this, [this] {
        const QStringList rf = recentFiles();
        for (const QString& f : rf) if (QFileInfo::exists(f) && f != path_) { openFile(f); return; }
        if (!rf.isEmpty() && QFileInfo::exists(rf.first())) openFile(rf.first());
    });
    QObject::connect(action("close"), &QAction::triggered, this, [this] { closeScene(); });
    QObject::connect(action("about"), &QAction::triggered, this, [this] { dlgAbout(); });
    QObject::connect(action("keys"), &QAction::triggered, this, [this] { dlgKeys(); });
    QObject::connect(action("quit"), &QAction::triggered, this, [this] { close(); });
    QObject::connect(action("draw"), &QAction::toggled, this, [this](bool on) { if (plan_->drawMode() != on) plan_->setDrawMode(on); });
    plan_->onDrawModeChanged = [this](bool on) {
        action("draw")->setChecked(on);
        ctxBar_->setVisible(on);
        action("coord")->setEnabled(on); action("esc")->setEnabled(on);
        if (on) { setActiveView(0); showStatus(QStringLiteral("평면에서 시작점 A 를 클릭하세요 — Shift 동서·남북 고정 · Enter 좌표 입력 · Esc 취소")); }
        else {
            drawEnded_ = plan_->hasLine();
            QTimer::singleShot(0, this, [this] { drawEnded_ = false; });
            showStatus(plan_->hasLine() ? QStringLiteral("단면을 계산합니다 — 다음: 뒤 깊이(숫자키 1–5)를 고르거나 Ctrl+P 로 도면") : QString());
        }
    };
    plan_->onDrawProgress = [this](int stage, const SectionLine& l) {
        updateCtx(stage, l);
        if (stage == 1) showStatus(QStringLiteral("끝점 A′ 를 클릭하세요 — Shift 를 누르면 동서·남북으로 고정"));
    };
    QObject::connect(action("coord"), &QAction::triggered, this, [this] { if (plan_->drawMode()) dlgCoordEntry(); });
    QObject::connect(action("esc"), &QAction::triggered, this, [this] { if (plan_->drawMode()) plan_->setDrawMode(false); });
    QObject::connect(action("flip"), &QAction::triggered, this, [this] {
        if (!plan_->hasLine()) return;
        SectionLine l = plan_->line(); std::swap(l.a, l.b);
        plan_->setLine(l, true); requestSection(true);
        syncCurrentSection(); commitState(QStringLiteral("방향 반전")); updateHeader();
    });
    QObject::connect(action("move"), &QAction::triggered, this, [this] { shiftLine(0.1); });
    QObject::connect(action("addsec"), &QAction::triggered, this, [this] { addSection(); });
    QObject::connect(action("clear"), &QAction::triggered, this, [this] {
        plan_->setLine(SectionLine(), false); section_->clear(); last_ = SectionOutput(); updateEnabled();
        commitState(QStringLiteral("단면선 지우기")); updateHeader();
    });
    QObject::connect(action("fit"), &QAction::triggered, this, [this] { plan_->fitAll(); section_->fit(); section_->update(); });
    QObject::connect(action("top"), &QAction::triggered, this, [this] { plan_->topView(); });
    QObject::connect(action("zoomin"), &QAction::triggered, this, [this] { plan_->zoomBy(1 / 1.4); section_->zoomBy(1.4); });
    QObject::connect(action("zoomout"), &QAction::triggered, this, [this] { plan_->zoomBy(1.4); section_->zoomBy(1 / 1.4); });
    const std::pair<const char*, const char*> tg[] = {{"image", "입면 영상"}, {"line", "단면선 표시"}, {"levels", "레벨선"}};
    for (auto& [k, name] : tg) {
        QString nm = QString::fromUtf8(name);
        QObject::connect(action(k), &QAction::toggled, this, [this, nm](bool) { section_->setStyle(secStyle()); commitState(nm); });
    }
    QObject::connect(opacity_, &QSlider::valueChanged, this, [this](int) { section_->setStyle(secStyle()); });
    QObject::connect(action("smooth"), &QAction::toggled, this, [this](bool on) { QSettings().setValue("section/smooth", on); requestSection(true); commitState(QStringLiteral("평활")); });
    QObject::connect(action("fade"), &QAction::toggled, this, [this](bool on) { QSettings().setValue("section/depthFade", on); section_->setStyle(secStyle()); requestSection(true); commitState(QStringLiteral("깊이 음영")); });
    QObject::connect(action("info"), &QAction::triggered, this, [this] { dlgInfo(); });
    QObject::connect(action("height"), &QAction::triggered, this, [this] { dlgHeightDatum(); });
    QObject::connect(action("view1"), &QAction::toggled, this, [this](bool on) {
        if (!on && !action("view2")->isChecked()) { action("view1")->setChecked(true); return; }
        planFrame_->setVisible(on);
    });
    QObject::connect(action("view2"), &QAction::toggled, this, [this](bool on) {
        if (!on && !action("view1")->isChecked()) { action("view2")->setChecked(true); return; }
        sectionFrame_->setVisible(on);
    });
    QObject::connect(action("listpanel"), &QAction::toggled, this, [this](bool on) { sidePanel_->setVisible(on); QSettings().setValue("ui/sectionList", on); });
    QObject::connect(action("sheet"), &QAction::triggered, this, [this] { dlgSheet(); });
    QObject::connect(action("dxf"), &QAction::triggered, this, [this] { dlgSectionDxf(); });
    QObject::connect(action("secimg"), &QAction::triggered, this, [this] { dlgSectionImage(); });
    QObject::connect(action("plan"), &QAction::triggered, this, [this] { dlgPlan(); });
    QObject::connect(action("xyz"), &QAction::triggered, this, [this] { dlgPoints(0); });
    QObject::connect(action("las"), &QAction::triggered, this, [this] { dlgPoints(1); });
    QObject::connect(action("csv"), &QAction::triggered, this, [this] { dlgProfileCsv(); });
    QObject::connect(action("secjson"), &QAction::triggered, this, [this] {
        if (!src_) return;
        QFileInfo mi(path_);
        QString dir = QFileInfo(mi.absolutePath()).isWritable() ? mi.absolutePath() : QDir::homePath();
        QString fn = QFileDialog::getSaveFileName(this, QStringLiteral("단면 목록 내보내기"), dir + "/" + mi.completeBaseName() + ".sections.json", QStringLiteral("단면 목록 (*.sections.json)"));
        if (fn.isEmpty()) return;
        QString m; bool ok = exportSectionsJson(fn, &m); report(ok, m);
    });
    QObject::connect(action("secimport"), &QAction::triggered, this, [this] { importSectionsJson(); });
    QObject::connect(action("contrast"), &QAction::toggled, this, [this](bool on) { applyHighContrast(on); });
    QObject::connect(action("lang"), &QAction::toggled, this, [this](bool on) { bilingual_ = on; QSettings().setValue("ui/bilingual", on); retranslate(); });
    auto bandChanged = [this] {
        plan_->setBand(front_->value(), back_->value());
        if (!applyingState_) { backUserSet_ = true; QSettings().setValue("section/backUserSet", true); }   // 사용자가 바꿈
        QSettings().setValue("section/front", front_->value()); QSettings().setValue("section/back", back_->value());
        if (plan_->hasLine()) { SectionLine l = plan_->line(); l.front = front_->value(); l.back = back_->value(); plan_->setLine(l, true); requestSection(true); }
        updateDepthChips();
        syncCurrentSection();
        commitState(QStringLiteral("두께·입면 깊이"), 1);
        updateHeader();
    };
    QObject::connect(front_, &QDoubleSpinBox::valueChanged, this, bandChanged);
    QObject::connect(back_, &QDoubleSpinBox::valueChanged, this, bandChanged);

    plan_->onLineChanged = [this](const SectionLine&, bool final) {
        if (final && drawEnded_) { drawEnded_ = false; fitNextResult_ = true; setActiveView(1); }
        requestSection(final); updateEnabled();
        if (final && !applyingState_) { syncCurrentSection(); commitState(QStringLiteral("단면선")); updateHeader(); }
    };
    plan_->onOpenRequest = [this] { chooseOpen(); };
    section_->onViewChanged = [this] { updateHeader(); };
    auto fmt = [](double v, int dec) { return QString::number(v, 'f', dec); };
    plan_->onCursor = [this, fmt](double X, double Y, double Z, bool hasZ, bool valid) {
        if (!valid) { cx_->clear(); cy_->clear(); cz_->clear(); pickFloorGen_ = pickWorker_->cancelAll(); showZSource(ZSource::None, QString()); return; }
        cx_->setText(fmt(X, 3)); cy_->setText(fmt(Y, 3)); cz_->setText(hasZ ? fmt(Z, 3) : QStringLiteral("—"));
        showZSource(hasZ ? ZSource::Coarse : ZSource::None, QStringLiteral("정밀 피킹 계산 중…"));
        requestPick(plan_->lastMousePos());
    };
    section_->onCursor = [this, fmt](double s, double zAbs, double X, double Y, bool valid) {
        if (!valid) { cx_->clear(); cy_->clear(); cz_->clear(); return; }
        cx_->setText(fmt(X, 3)); cy_->setText(fmt(Y, 3)); cz_->setText(fmt(zAbs, 3));
        showZSource(ZSource::SectionCursor, QStringLiteral("단면 화면의 커서 위치(s = %1 m, z)입니다. 표면을 피킹한 값이 아닙니다.").arg(fmt(s, 3)));
    };
    secWorker_ = std::make_unique<CoalescingWorker>();
    pickWorker_ = std::make_unique<CoalescingWorker>();
    showStart(true);
    updateEnabled();
    updateHeader();
}

int MainWindow::addSectionAt(const SectionLine& l) {
    if (!src_) return -1;
    syncCurrentSection();
    const Vec3 o = srs().origin;
    std::vector<std::string> used; for (auto& s : sections_) used.push_back(s.name);
    int k = 0; while (std::find(used.begin(), used.end(), sectionLetterName(k)) != used.end()) ++k;
    SavedSection s; s.name = sectionLetterName(k);
    s.ax = l.a.x + o.x; s.ay = l.a.y + o.y; s.bx = l.b.x + o.x; s.by = l.b.y + o.y; s.front = front_->value(); s.back = back_->value();
    sections_.push_back(s);
    refreshSectionList();
    return int(sections_.size()) - 1;
}
