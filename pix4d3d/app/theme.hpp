// Strata 「조용한 도판」 색·글꼴(research/ui-ref/UI-SPEC.md §2, strata.qss). 바탕 ground + 먹색 글자, 흙색은 주 단추 하나에만.
// 데이터 색(단면선 #FF0000, 레벨선 회색)은 그대로 둔다.
#pragma once
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace theme {
// ---- 토큰
inline const QColor Ground{0xFA, 0xF9, 0xF5}, Card{0xFF, 0xFF, 0xFF}, Wash{0xF5, 0xF4, 0xED}, Desk{0xF0, 0xEE, 0xE6}, Press{0xE8, 0xE6, 0xDC}, Oat{0xE3, 0xDA, 0xCC};
inline const QColor Ink{0x14, 0x14, 0x13}, Hand{0x30, 0x30, 0x2E}, Ink2{0x3D, 0x3D, 0x3A}, Muted{0x5E, 0x5D, 0x59}, Faint{0x73, 0x72, 0x6C};
inline const QColor Line{0xDE, 0xDC, 0xD1}, Edge{0xC2, 0xC0, 0xB6}, Ring{0xB0, 0xAE, 0xA5}, CheckEdge{0x87, 0x86, 0x7F};
inline const QColor Action{0xB5, 0x57, 0x3A}, Block{0xA3, 0x3B, 0x3B}, BlockWash{0xF3, 0xDE, 0xDA}, Caution{0x7A, 0x5A, 0x00}, Done{0x3F, 0x6B, 0x31};
inline const QColor LevelMinor{0xDE, 0xDC, 0xD1}, LevelMajor{0x9C, 0x9A, 0x92}, LevelText{0x3D, 0x3D, 0x3A};
// 옛 이름(다른 파일 호환)
inline const QColor Bg = Ground, InkSub = Muted, Chip = Desk, Idle = Faint, Outline = CheckEdge, Brand = Action, BrandSoft = Wash, Ok = Done;
inline const QColor SectionRed{0xFF, 0x00, 0x00};  // 평면의 단면선·괄호선: 순수 빨강(DXF 색 1) — 바꾸지 않음
inline const QColor ProfileRed{0xFF, 0x00, 0x00};  // 단면 보기의 절단 윤곽선: 순수 빨강

inline QStringList fontFamilies() { return {QStringLiteral("Malgun Gothic"), QStringLiteral("Noto Sans CJK KR"), QStringLiteral("Noto Sans KR"), QStringLiteral("Apple SD Gothic Neo")}; }
inline QString fontFamily() {
#ifdef Q_OS_WIN
    return QStringLiteral("Malgun Gothic");
#else
    return QStringLiteral("Noto Sans CJK KR");
#endif
}
inline QFont uiFont(int px, bool bold = false) { QFont f; f.setFamilies(fontFamilies()); f.setPixelSize(px); f.setBold(bold); return f; }
inline QFont monoFont(int px) {
    QFont f; f.setFamilies({QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Noto Sans Mono"), QStringLiteral("monospace")});
    f.setStyleHint(QFont::Monospace); f.setPixelSize(px); return f;
}

// 체크 표시 그림: rcc 없이 실행 중 임시 PNG 로 만들어 QSS url() 로 씀
inline QString checkImagePath(const QColor& c, const QString& name) {
    QString dir = QDir::tempPath() + "/ExcavSection-ui";
    QDir().mkpath(dir);
    QString path = dir + "/" + name + ".png";
    if (!QFile::exists(path)) {
        QPixmap pm(28, 28); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(c, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath t; t.moveTo(6, 14.5); t.lineTo(11.5, 20); t.lineTo(22, 8); p.drawPath(t);
        p.end(); pm.save(path, "PNG");
    }
    return QDir::fromNativeSeparators(path);
}

inline QString arrowImagePath(const QColor& c, const QString& name) {
    QString dir = QDir::tempPath() + "/ExcavSection-ui";
    QDir().mkpath(dir);
    QString path = dir + "/" + name + ".png";
    if (!QFile::exists(path)) {
        QPixmap pm(20, 20); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath t; t.moveTo(5, 8); t.lineTo(10, 13); t.lineTo(15, 8); p.drawPath(t);
        p.end(); pm.save(path, "PNG");
    }
    return QDir::fromNativeSeparators(path);
}

inline QString styleSheet(bool highContrast = false) {
    QString s = QStringLiteral(R"(
QMainWindow, QWidget#central, QWidget#workArea { background: #FAF9F5; }
QWidget { color: #141413; font-size: 13px; }
QLabel#hint, QLabel#statusInfo { color: #5E5D59; font-size: 12px; }
QLabel#faint { color: #73726C; font-size: 11px; }
QLabel#title { font-size: 17px; font-weight: 700; color: #141413; }
QLabel#bigTitle { font-size: 26px; font-weight: 700; color: #141413; }
QLabel#heroName { font-size: 30px; font-weight: 700; color: #141413; }
QLabel#mono, QLineEdit#mono { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 12px; }
QLabel#secName { font-size: 13px; font-weight: 700; color: #141413; }
QWidget#dialogMain { background: #FAF9F5; }
QLabel#monoFaint { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 11px; color: #73726C; }
QLabel#sectionHead { font-size: 13px; font-weight: 700; color: #141413; }
QLabel#facing { color: #5E5D59; font-size: 12px; }

QWidget#ribbon { background: #FFFFFF; border-bottom: 1px solid #DEDCD1; }
QWidget#ribbonTop { background: #FFFFFF; border-bottom: 1px solid #DEDCD1; }
QTabBar#ribbonTabs { background: #FFFFFF; }
QTabBar#ribbonTabs::tab { background: transparent; border: none; border-bottom: 2px solid transparent; padding: 6px 16px 6px 16px; margin: 0; color: #3D3D3A; font-size: 13px; }
QTabBar#ribbonTabs::tab:hover { color: #141413; }
QTabBar#ribbonTabs::tab:selected { color: #141413; font-weight: 700; border-bottom: 2px solid #141413; }
QLabel#brand { color: #73726C; font-size: 11px; padding-right: 12px; }
QLabel#groupLabel { color: #3D3D3A; font-size: 11px; font-weight: 700; }
QFrame#groupSep { background: #DEDCD1; max-width: 1px; min-width: 1px; }

QToolButton#ribbonTile { min-width: 58px; padding: 1px 2px 2px 2px; border: none; border-radius: 8px; background: transparent; color: #141413; font-size: 12px; }
QToolButton#ribbonTile:hover { background: #F5F4ED; }
QToolButton#ribbonTile:pressed { background: #F0EEE6; }
QToolButton#ribbonTile:checked { background: transparent; }
QToolButton#ribbonTile:checked:hover { background: #F5F4ED; }
QToolButton#ribbonTile[tool="true"]:checked { font-weight: 700; }
QToolButton#ribbonTile[primary="true"] { font-weight: 700; }
QToolButton#ribbonTile::menu-indicator { image: none; width: 0px; }
QToolButton#ribbonTile:disabled { color: #9C9A92; }

QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 2px 6px; background: transparent; color: #141413; }
QToolButton:hover { background: #F5F4ED; border-color: #C2C0B6; }
QToolButton:pressed { background: #E8E6DC; }
QToolButton:checked { background: #E3DACC; border-color: #B0AEA5; }
QToolButton:disabled { color: #9C9A92; }
QToolButton::menu-indicator { image: none; width: 0px; }
QToolButton#smallRow { text-align: left; padding: 1px 6px; border: 1px solid #DEDCD1; background: #FFFFFF; }
QToolButton#smallRow:hover { background: #F5F4ED; border-color: #C2C0B6; }
QToolButton#undoBtn { border: 1px solid #C2C0B6; border-radius: 6px; background: #FFFFFF; padding: 2px 8px; }
QToolButton#undoBtn:hover { background: #F5F4ED; }

QToolButton#chip { min-height: 20px; padding: 0 7px; border: 1px solid #C2C0B6; border-radius: 0; background: #FFFFFF; font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 12px; }
QToolButton#chip:hover { background: #F5F4ED; }
QToolButton#chip:checked { background: #E3DACC; border-color: #141413; }
QToolButton#chip[pos="first"] { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }
QToolButton#chip[pos="last"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }

QWidget#ctxBar { background: #F5F4ED; border-bottom: 1px solid #DEDCD1; }
QWidget#ctxBar QLabel { font-size: 12px; }
QLabel#ctxTool { background: #30302E; color: #FAF9F5; border-radius: 6px; padding: 3px 10px; font-weight: 700; font-size: 12px; }
QLabel#kbd { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 11px; color: #3D3D3A; background: #FFFFFF; border: 1px solid #C2C0B6; border-bottom-width: 2px; border-radius: 4px; padding: 0 5px; }
QLabel#ctxValue { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 12px; color: #141413; }

QWidget#viewFrame { background: #FFFFFF; border: 1px solid #DEDCD1; }
QWidget#viewTitle { background: #FFFFFF; border-bottom: 1px solid #DEDCD1; }
QWidget#viewTitle[active="true"] { border-bottom: 2px solid #141413; }
QLabel#viewTitleText { font-weight: 700; color: #141413; padding-left: 2px; }
QWidget#infoStrip { background: #F5F4ED; border-bottom: 1px solid #DEDCD1; }
QWidget#infoStrip QLabel { font-size: 12px; color: #3D3D3A; }
QWidget#viewBar { background: #F5F4ED; border-bottom: 1px solid #DEDCD1; }
QWidget#viewTitle QToolButton { padding: 2px; border-radius: 4px; }

QToolButton#heightBadge { background: #F5F4ED; border: 1px solid #C2C0B6; border-radius: 4px; padding: 1px 8px; font-size: 12px; color: #141413; }
QToolButton#heightBadge:hover { border-color: #141413; }
QToolButton#heightBadge[state="warn"] { background: #FFFFFF; border-color: #7A5A00; color: #7A5A00; }
QToolButton#heightBadge[state="error"] { background: #F3DEDA; border-color: #A33B3B; color: #A33B3B; }
QToolButton#heightBadge[state="none"] { background: #F0EEE6; border-color: #DEDCD1; color: #73726C; }
QToolButton#vexBtn { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 0px 6px; font-size: 12px; color: #3D3D3A; }
QToolButton#vexBtn:hover { border-color: #C2C0B6; }
QToolButton#vexBtn::menu-indicator { image: none; width: 0px; }
QToolButton#vexBtn[state="on"] { background: #FFF4D6; border-color: #C98A1B; color: #7A5A00; font-weight: bold; }
QToolButton#vexBtn[state="hint"] { border-color: #C98A1B; color: #7A5A00; }

QToolButton#viewNum { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; padding: 0; border: 1px solid #C2C0B6; border-radius: 4px; background: #FFFFFF; font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 11px; }
QToolButton#viewNum:checked { background: #30302E; color: #FAF9F5; border-color: #30302E; }
QToolButton#viewNum:disabled { background: #F0EEE6; color: #73726C; border-color: #DEDCD1; }

QListWidget#sectionList { background: #FFFFFF; border: none; outline: 0; }
QListWidget#sectionList::item { border-bottom: 1px solid #DEDCD1; color: #141413; padding: 0; }
QListWidget#sectionList::item:hover { background: #F5F4ED; }
QListWidget#sectionList::item:selected { background: #E3DACC; color: #141413; border-left: 3px solid #141413; }
QWidget#sidePanel { background: #FFFFFF; border-right: 1px solid #DEDCD1; }

QWidget#coordBar { background: #FFFFFF; border-top: 1px solid #DEDCD1; }
QLabel#coordKey { color: #5E5D59; font-size: 12px; padding: 0 2px 0 10px; }
QLineEdit#coord { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 12px; background: transparent; border: none; padding: 0 4px; min-height: 20px; }
QLabel#statusMsg { color: #141413; font-size: 12px; padding: 0 10px; }
QLabel#zSrc { color: #5E5D59; font-size: 12px; padding: 0 4px; }
QLabel#zSrc[coarse="true"] { color: #7A5A00; }
QLabel#srsLabel { color: #141413; font-size: 12px; padding: 0 8px; }
QLabel#srsLabel[state="warn"] { color: #7A5A00; }
QLabel#srsLabel[state="error"] { color: #A33B3B; }

QDoubleSpinBox, QSpinBox, QComboBox, QLineEdit { background: #FFFFFF; border: 1px solid #C2C0B6; border-radius: 4px; padding: 1px 6px; min-height: 20px; selection-background-color: #E3DACC; selection-color: #141413; }
QDoubleSpinBox, QSpinBox { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 12px; }
QDoubleSpinBox:hover, QSpinBox:hover, QComboBox:hover, QLineEdit:hover { border-color: #B0AEA5; }
QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus, QLineEdit:focus { border: 1px solid #141413; }
QDoubleSpinBox:disabled, QSpinBox:disabled, QComboBox:disabled, QLineEdit:disabled { background: #F0EEE6; color: #73726C; }
QComboBox::drop-down { width: 20px; border: none; }
QComboBox::down-arrow { image: url(%ARROW%); width: 10px; height: 10px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; border: none; }
QAbstractSpinBox { padding-right: 6px; }
QComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #C2C0B6; selection-background-color: #E3DACC; selection-color: #141413; outline: 0; }

QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #87867F; border-radius: 2px; background: #FFFFFF; }
QCheckBox::indicator:checked { background: #141413; border-color: #141413; image: url(%CHECK%); }
QCheckBox::indicator:disabled { background: #F0EEE6; border-color: #DEDCD1; }
QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid #87867F; border-radius: 7px; background: #FFFFFF; }
QRadioButton::indicator:checked { border: 4px solid #141413; background: #FFFFFF; width: 8px; height: 8px; }

QSlider::groove:horizontal { height: 4px; background: #E8E6DC; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #141413; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; margin: -6px 0; border-radius: 7px; background: #FFFFFF; border: 1px solid #87867F; }

QSplitter::handle { background: #DEDCD1; }
QSplitter::handle:horizontal { width: 1px; }

QPushButton { background: #FFFFFF; border: 1px solid #C2C0B6; border-radius: 8px; padding: 6px 16px; min-height: 18px; color: #141413; }
QPushButton:hover { background: #F5F4ED; border-color: #B0AEA5; }
QPushButton:pressed { background: #E8E6DC; }
QPushButton:disabled { color: #73726C; background: #F0EEE6; border-color: #DEDCD1; }
QPushButton#primary { background: #B5573A; color: #FFFFFF; border-color: #B5573A; font-weight: 700; }
QPushButton#primary:hover { background: #A44D32; border-color: #A44D32; }
QPushButton#primary:pressed { background: #8F4229; }
QPushButton#quiet { background: transparent; border-color: transparent; }
QPushButton#quiet:hover { background: #F5F4ED; }
QPushButton#danger { background: #FFFFFF; color: #A33B3B; border-color: #A33B3B; }

QDialog { background: #FAF9F5; }
QWidget#dialogHead { background: #FFFFFF; border-bottom: 1px solid #DEDCD1; }
QWidget#dialogSide { background: #F5F4ED; border-left: 1px solid #DEDCD1; }
QWidget#dialogFoot { background: #FFFFFF; border-top: 1px solid #DEDCD1; }
QLabel#sideHead { color: #5E5D59; font-size: 11px; }
QGroupBox { border: none; border-top: 1px solid #DEDCD1; margin-top: 18px; padding-top: 10px; background: transparent; }
QGroupBox::title { subcontrol-origin: margin; left: 0; padding: 0 8px 0 0; color: #5E5D59; font-size: 11px; }
QLabel#calc { background: #FFFFFF; border: none; border-top: 1px solid #141413; border-bottom: 1px solid #DEDCD1; padding: 6px 2px; color: #141413; }
QFrame#warnBox { background: #FFFFFF; border: 1px solid #C2C0B6; border-radius: 8px; }
QFrame#warnBox QLabel { font-size: 12px; }

QFrame#notice { background: #FFFFFF; border: none; border-bottom: 1px solid #DEDCD1; }
QFrame#notice[level="block"] { background: #F3DEDA; }
QFrame#notice[level="block"] QLabel { color: #A33B3B; }
QLabel#noticeIcon { color: #7A5A00; font-size: 13px; }

QProgressBar { border: none; background: #E8E6DC; border-radius: 2px; max-height: 4px; min-height: 4px; }
QProgressBar::chunk { background: #141413; border-radius: 2px; }

QMenu { background: #FFFFFF; border: 1px solid #C2C0B6; padding: 4px; }
QMenu::item { min-height: 22px; padding: 3px 28px 3px 12px; border-radius: 4px; }
QMenu::item:selected { background: #F5F4ED; color: #141413; }
QMenu::item:disabled { color: #73726C; }
QMenu::separator { height: 1px; background: #DEDCD1; margin: 4px 8px; }
QToolTip { background: #30302E; color: #FAF9F5; border: none; padding: 4px 8px; font-size: 12px; }

QWidget#startPage { background: #FAF9F5; }
QFrame#card { background: #FFFFFF; border: 1px solid #C2C0B6; border-radius: 8px; }
QFrame#cardSide { background: #FFFFFF; border: none; border-left: 1px solid #DEDCD1; }
QFrame#dropZone { background: #F5F4ED; border: 1px dashed #B0AEA5; border-radius: 8px; }
QTableWidget#recentTable { background: #FFFFFF; border: none; border-top: 2px solid #141413; border-bottom: 2px solid #141413; gridline-color: #DEDCD1; outline: 0; }
QTableWidget#recentTable::item { border-bottom: 1px solid #DEDCD1; padding: 4px 6px; }
QTableWidget#recentTable::item:selected { background: #E3DACC; color: #141413; }
QHeaderView::section { background: #FFFFFF; color: #5E5D59; border: none; border-bottom: 1px solid #DEDCD1; padding: 4px 6px; font-size: 11px; }
QLabel#stepNum { border: 1px solid #141413; border-radius: 11px; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; font-size: 11px; qproperty-alignment: AlignCenter; }

QScrollBar:vertical { width: 10px; background: transparent; margin: 0; }
QScrollBar::handle:vertical { background: #C2C0B6; border-radius: 4px; min-height: 24px; margin: 2px; }
QScrollBar:horizontal { height: 10px; background: transparent; margin: 0; }
QScrollBar::handle:horizontal { background: #C2C0B6; border-radius: 4px; min-width: 24px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");
    if (highContrast)
        s += QStringLiteral(R"(
QWidget#ribbon, QWidget#viewTitle, QWidget#coordBar, QWidget#ribbonTop { border-color: #87867F; }
QDoubleSpinBox, QSpinBox, QComboBox, QLineEdit, QPushButton { border-color: #73726C; }
QLabel#hint, QLabel#statusInfo, QLabel#groupLabel, QLabel#coordKey, QLabel#faint { color: #3D3D3A; }
QToolButton#ribbonTile:checked, QToolButton:checked, QToolButton#chip:checked { border-color: #141413; }
)");
    s.replace(QStringLiteral("%CHECK%"), checkImagePath(QColor(0xFF, 0xFF, 0xFF), QStringLiteral("check-white")));
    s.replace(QStringLiteral("%ARROW%"), arrowImagePath(QColor(0x5E, 0x5D, 0x59), QStringLiteral("arrow-down")));
    return s;
}

// ---- 아이콘: 먹색 선(데이터 색 아이콘만 빨강·회색 획 유지)
enum class Ico { Open, Draw, Flip, Plan, Fit, Orbit, Image, Line, Levels, Dxf, Png, Smooth, Tif, Geo, Xyz, Las, Csv, ZoomIn, ZoomOut, Close, Info, Clear, View1, View2, Lang, Band,
                 Recent, Sheet, Undo, Redo, Measure, Height, Move, Add, Max, Picture, Keys };

inline QIcon icon(Ico k, int S = 32, QColor inkColor = Ink) {
    QIcon ic;
    for (int scale : {1, 2}) {
        int s = S * scale;
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(s / 32.0, s / 32.0);
        const QColor I = inkColor;
        QPen ink(I, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        QPen red(SectionRed, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin), thin(QColor(0x9C, 0x9A, 0x92), 1.3);
        p.setPen(ink); p.setBrush(Qt::NoBrush);
        switch (k) {
        case Ico::Open: {
            QPainterPath f; f.moveTo(4, 9); f.lineTo(12, 9); f.lineTo(14.5, 12); f.lineTo(28, 12); f.lineTo(28, 25); f.lineTo(4, 25); f.closeSubpath();
            p.drawPath(f);
            break; }
        case Ico::Recent:
            p.drawEllipse(QPointF(16, 16), 11, 11);
            p.drawLine(QPointF(16, 9.5), QPointF(16, 16)); p.drawLine(QPointF(16, 16), QPointF(20.5, 19));
            break;
        case Ico::Draw:
            p.drawLine(QPointF(8, 24), QPointF(24, 8));
            p.setBrush(I == Ink ? QColor(Qt::white) : QColor(Qt::transparent)); p.drawEllipse(QPointF(7.5, 24.5), 3, 3); p.drawEllipse(QPointF(24.5, 7.5), 3, 3);
            break;
        case Ico::Flip:
            p.drawLine(QPointF(5, 11), QPointF(25, 11)); p.drawLine(QPointF(21, 7), QPointF(25, 11)); p.drawLine(QPointF(21, 15), QPointF(25, 11));
            p.drawLine(QPointF(27, 21), QPointF(7, 21)); p.drawLine(QPointF(11, 17), QPointF(7, 21)); p.drawLine(QPointF(11, 25), QPointF(7, 21));
            break;
        case Ico::Move:
            p.drawLine(QPointF(5, 16), QPointF(27, 16));
            p.drawLine(QPointF(9, 12), QPointF(5, 16)); p.drawLine(QPointF(9, 20), QPointF(5, 16));
            p.drawLine(QPointF(23, 12), QPointF(27, 16)); p.drawLine(QPointF(23, 20), QPointF(27, 16));
            break;
        case Ico::Add:
            p.drawLine(QPointF(16, 7), QPointF(16, 25)); p.drawLine(QPointF(7, 16), QPointF(25, 16));
            break;
        case Ico::Plan:
            p.drawRoundedRect(QRectF(5, 5, 22, 22), 3, 3);
            p.drawLine(QPointF(16, 10), QPointF(16, 20)); p.drawLine(QPointF(12.5, 13.5), QPointF(16, 10)); p.drawLine(QPointF(19.5, 13.5), QPointF(16, 10));
            break;
        case Ico::Fit:
            for (auto c : {QPointF(6, 6), QPointF(26, 6), QPointF(6, 26), QPointF(26, 26)}) {
                double sx = c.x() < 16 ? 1 : -1, sy = c.y() < 16 ? 1 : -1;
                p.drawLine(c, c + QPointF(6 * sx, 0)); p.drawLine(c, c + QPointF(0, 6 * sy));
            }
            break;
        case Ico::Max:
            p.drawRect(QRectF(7, 7, 18, 18));
            break;
        case Ico::Orbit:
            p.drawEllipse(QRectF(5, 10, 22, 12));
            p.drawArc(QRectF(8, 4, 16, 24), 30 * 16, 120 * 16);
            break;
        case Ico::Image: case Ico::Picture: {
            p.drawRoundedRect(QRectF(5, 7, 22, 18), 2, 2);
            QPainterPath m; m.moveTo(7, 23); m.lineTo(13, 15); m.lineTo(17, 19); m.lineTo(21, 13); m.lineTo(25, 23);
            p.drawPath(m);
            p.drawEllipse(QPointF(11, 12), 1.6, 1.6);
            break; }
        case Ico::Line: {
            QPainterPath pp; pp.moveTo(4, 14); pp.lineTo(10, 13); pp.lineTo(13, 21); pp.lineTo(20, 21); pp.lineTo(23, 12); pp.lineTo(28, 11);
            p.setPen(QPen(SectionRed, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(pp);
            break; }
        case Ico::Levels:
            p.setPen(QPen(QColor(0x9C, 0x9A, 0x92), 1.3)); for (int y = 8; y <= 24; y += 4) p.drawLine(6, y, 26, y);
            p.setPen(QPen(I, 2.0)); p.drawLine(6, 16, 26, 16);
            break;
        case Ico::Sheet: {
            QPainterPath d; d.moveTo(8, 4); d.lineTo(20, 4); d.lineTo(25, 9); d.lineTo(25, 28); d.lineTo(8, 28); d.closeSubpath();
            p.drawPath(d);
            p.drawLine(QPointF(12, 14), QPointF(21, 14)); p.drawLine(QPointF(12, 18.5), QPointF(21, 18.5)); p.drawLine(QPointF(12, 23), QPointF(18, 23));
            break; }
        case Ico::Dxf: case Ico::Png: case Ico::Tif: case Ico::Geo: case Ico::Xyz: case Ico::Las: case Ico::Csv: {
            QPainterPath d; d.moveTo(8, 4); d.lineTo(20, 4); d.lineTo(25, 9); d.lineTo(25, 28); d.lineTo(8, 28); d.closeSubpath();
            p.drawPath(d);
            const char* t = k == Ico::Dxf ? "DXF" : k == Ico::Png ? "PNG" : k == Ico::Tif ? "TIF" : k == Ico::Geo ? "GEO" : k == Ico::Xyz ? "XYZ" : k == Ico::Las ? "LAS" : "CSV";
            QFont f; f.setPixelSize(7); f.setBold(true); p.setFont(f); p.setPen(I);
            p.drawText(QRectF(6, 14, 21, 10), Qt::AlignCenter, t);
            break; }
        case Ico::ZoomIn: case Ico::ZoomOut:
            p.drawEllipse(QPointF(14, 14), 8, 8);
            p.drawLine(QPointF(20, 20), QPointF(27, 27));
            p.drawLine(QPointF(10, 14), QPointF(18, 14)); if (k == Ico::ZoomIn) p.drawLine(QPointF(14, 10), QPointF(14, 18));
            break;
        case Ico::Close:
            p.drawLine(9, 9, 23, 23); p.drawLine(23, 9, 9, 23);
            break;
        case Ico::Info:
            p.drawEllipse(QPointF(16, 16), 11, 11);
            p.drawLine(QPointF(16, 14.5), QPointF(16, 22)); p.setPen(QPen(I, 2.8, Qt::SolidLine, Qt::RoundCap)); p.drawPoint(QPointF(16, 10));
            break;
        case Ico::Clear:
            p.drawLine(QPointF(9, 10), QPointF(23, 10)); p.drawLine(QPointF(13, 10), QPointF(13, 7)); p.drawLine(QPointF(13, 7), QPointF(19, 7)); p.drawLine(QPointF(19, 7), QPointF(19, 10));
            p.drawLine(QPointF(10.5, 10), QPointF(12, 26)); p.drawLine(QPointF(12, 26), QPointF(20, 26)); p.drawLine(QPointF(20, 26), QPointF(21.5, 10));
            break;
        case Ico::View1: case Ico::View2: {
            p.drawRect(QRectF(4, 6, 24, 20));
            QFont f; f.setPixelSize(12); f.setBold(true); p.setFont(f);
            p.drawText(QRectF(4, 6, 24, 20), Qt::AlignCenter, k == Ico::View1 ? "1" : "2");
            break; }
        case Ico::Lang: {
            p.drawRoundedRect(QRectF(3, 6, 18, 14), 2, 2);
            QFont f; f.setPixelSize(10); f.setBold(true); p.setFont(f); p.drawText(QRectF(3, 6, 18, 14), Qt::AlignCenter, QString::fromUtf8("가"));
            p.drawRoundedRect(QRectF(12, 14, 18, 14), 2, 2);
            p.drawText(QRectF(12, 14, 18, 14), Qt::AlignCenter, "A");
            break; }
        case Ico::Band:
            p.setPen(Qt::NoPen); p.setBrush(QColor(0x9C, 0x9A, 0x92, 70)); p.drawRect(QRectF(5, 12, 22, 10));
            p.setPen(ink); p.setBrush(Qt::NoBrush); p.drawLine(5, 12, 27, 12);
            p.setPen(QPen(I, 1.2)); p.drawLine(5, 22, 27, 22);
            break;
        case Ico::Smooth: {
            QPainterPath s2; s2.moveTo(4, 20); s2.cubicTo(10, 6, 20, 28, 28, 12);
            p.drawPath(s2);
            break; }
        case Ico::Undo: case Ico::Redo: {
            p.save();
            if (k == Ico::Redo) { p.translate(32, 0); p.scale(-1, 1); }
            QPainterPath u; u.moveTo(9, 13); u.lineTo(20, 13); u.cubicTo(26, 13, 26, 24, 20, 24); u.lineTo(13, 24);
            p.drawPath(u); p.drawLine(QPointF(13, 9), QPointF(9, 13)); p.drawLine(QPointF(13, 17), QPointF(9, 13));
            p.restore();
            break; }
        case Ico::Measure:
            p.save(); p.translate(16, 16); p.rotate(-45);
            p.drawRect(QRectF(-13, -5, 26, 10));
            for (int x = -9; x <= 9; x += 4) p.drawLine(QPointF(x, -5), QPointF(x, x % 8 == 3 || x % 8 == -5 ? -1 : 0.5));
            p.restore();
            break;
        case Ico::Height:
            p.drawLine(QPointF(5, 26), QPointF(27, 26));
            p.drawLine(QPointF(16, 22), QPointF(16, 6)); p.drawLine(QPointF(11, 11), QPointF(16, 6)); p.drawLine(QPointF(21, 11), QPointF(16, 6));
            break;
        case Ico::Keys:
            p.drawRoundedRect(QRectF(4, 9, 24, 15), 3, 3);
            for (int x = 8; x <= 24; x += 4) p.drawPoint(QPointF(x, 14));
            p.drawLine(QPointF(10, 19.5), QPointF(22, 19.5));
            break;
        }
        p.end();
        ic.addPixmap(pm);
    }
    return ic;
}
}  // namespace theme
