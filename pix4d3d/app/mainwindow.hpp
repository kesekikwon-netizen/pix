// 주 창(1.2 Strata): 리본(파일/홈/보기/측정/내보내기) + 지금 도구 줄 + 알림 띠 + [단면 목록 | 평면
// | 단면] + 상태줄, 시작 화면. moc 없이(람다·std::function) 작성. 화면 구성 코드는
// mainwindow_ui.cpp, 도면(용지) 내보내기는 sheetexport.cpp.
#pragma once
#include "asec/engine.hpp"
#include "asec/las.hpp"
#include "asec/pick.hpp"
#include "asec/pointcloud.hpp"
#include "asec/schedule.hpp"
#include "asec/sheet.hpp"
#include "asec/tiff.hpp"
#include "planview.hpp"
#include "sectionview.hpp"
#include "theme.hpp"
#include <QAction>
#include <QMainWindow>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

class QLabel;
class QLineEdit;
class QProgressBar;
class QDoubleSpinBox;
class QSlider;
class QTabBar;
class QStackedWidget;
class QToolButton;
class QSplitter;
class QCheckBox;
class QUndoStack;
class QListWidget;
class QFrame;
class QComboBox;
class QMenu;
class QTableWidget;

inline constexpr double kDepthFadeStrength = 0.55; // 깊이 음영 세기(가장 먼 면을 흰색 쪽으로 55%)
inline constexpr double kMaxBandDepth = 5.0; // 두께 띠 앞/뒤 최대(m) — 입면 영상 깊이 최대 5 m
class QDialog;
class QPainter;

/// 열기 결과(작업 스레드에서 만들고 GUI 스레드에서 적용)
struct OpenedScene {
  QString path;
  std::shared_ptr<asec::MeshSource> src;
  std::shared_ptr<asec::PointCloud> pc; // LAS: 원본 점군(단면·낮볼내기는 이것으로 계산)
  std::vector<std::shared_ptr<DisplayMesh>>
      display; // OBJ: 화면 메시 전체 / 3MX: 커서 Z 용 거친 메시(텍스처 없음)
  std::vector<asec::fs::path> streamRoots; // 3MX: LOD 스트리밍 루트(비면 정적 표시)
  asec::Vec3 center;
  asec::Box3 bounds;
  size_t displayTris = 0, displayMeshes = 0;
  QString kind;                      // "3MX" / "OBJ"
  size_t layers = 1;                 // 3MX meshPyramid 레이어 수(병합 3MX > 1)
  std::vector<std::string> warnings; // 열기 경고(레이어 제외 등)
};

struct SectionExportParams {
  int format = 0;
  double denom = 20, dpi = 300;
}; // format 0 PNG 1 TIFF 2 GeoTIFF
struct DxfParams {
  bool world3d = false;
  double denom = 20;
  bool image = true;
  double imageDpi = 200;
};
struct PlanParams {
  bool wholeModel = true;
  double denom = 100, dpi = 200;
  bool overlayLine = true;
  asec::Box3 area;
};
/// 도면(용지) 내보내기: 형식 0 PDF 1 DXF 2 PNG 3 TIFF
struct SheetParams {
  asec::SheetSpec spec;
  int format = 0;
  double dpi = 300;   // PNG/TIFF(및 PDF 영상) 해상도
  bool split = false; // 넘치면 여러 장으로 나눠(PDF 여러 쪽 / 영상 여러 파일)
  QString title;      // 도면명
  bool withImage = true, withLine = true, withLevels = true, withTitle = true;
  bool showBaseline = false;
  double baselineEl = 0;
  QString heightLabel, srsLabel, facing, date;
};
struct PointParams {
  asec::PointFormat format = asec::PointFormat::XYZ;
  int area = 0;
  double spacing = 0;
  bool rgb = true;
  asec::Box3 box;
};

class MainWindow : public QMainWindow {
public:
  MainWindow();
  ~MainWindow() override;

  // ---- 작업(동기). 대화상자는 작업 스레드에서, 명령줄 자동화는 GUI 스레드에서 직접 호출 ----
  static bool loadScene(const QString &path, OpenedScene &out, QString *err,
                        const std::function<void(double)> &progress = {});
  void applyScene(OpenedScene &&s);
  bool computeNow(const asec::SectionLine &l, QString *err); // 동기 단면(자동화용)
  bool ensureFinalSection();                                 // 화면 결과가 미리보기면 최종(잎) 계산
  // 내보내기: 입력을 모두 인자로 받음(스레드 안전). src 는 공유 포인터 사본, doc 은 GUI 스레드에서
  // 뜬 스냅숏
  static bool exportSectionImage(const SectionDoc &doc, asec::MeshSource &src,
                                 const SectionExportParams &p, const QString &path, QString *msg,
                                 const std::atomic<bool> *cancel = nullptr);
  static bool exportSectionDxf(const SectionDoc &doc, asec::MeshSource &src, const DxfParams &p,
                               const QString &path, QString *msg,
                               const std::atomic<bool> *cancel = nullptr);
  static bool exportPlan(asec::MeshSource &src, const PlanParams &p, const asec::SectionLine *line,
                         const QString &path, QString *msg,
                         const std::atomic<bool> *cancel = nullptr);
  static bool exportPointCloud(asec::MeshSource &src, const PointParams &p,
                               const asec::SectionLine &line, const QString &path, QString *msg,
                               const std::atomic<bool> *cancel = nullptr,
                               const std::function<void(size_t)> &progress = {});
  static bool exportProfileCsv(const SectionDoc &doc, const QString &path, QString *msg);
  /// 도면(용지): PDF(벡터 선 + 영상) / PNG / TIFF 는 용지 그대로, DXF 는 모델 공간 1:N.
  /// progress(0..1)
  static bool exportSheet(const SectionDoc &doc, asec::MeshSource &src, const SheetParams &p,
                          const QString &path, QString *msg,
                          const std::atomic<bool> *cancel = nullptr,
                          const std::function<void(double)> &progress = {});
  /// 용지 한 장 그리기(미리보기·내보내기 공용). pxPerMm = 장치 픽셀/mm, page = 나눠 붙이기 쪽
  /// 번호(0..)
  static void paintSheet(QPainter &p, const SectionDoc &doc, const SheetParams &sp, double pxPerMm,
                         int page, const QImage &img, const SectionImgGeo &geo);
  SheetParams defaultSheetParams() const;
  void dlgSheet();                                            // Ctrl+P 「도면」 창
  QDialog *buildSheetDialog(SheetParams &io, bool &accepted); // 자동화(창 캡처)에서도 씀
  QWidget *startPage() const { return startPage_; }
  void showStart(bool on);
  bool undoTest(QString *log); // 자동화: 되돌리기/다시 점검
  QWidget *ctxBar() const { return ctxBar_; }
  std::shared_ptr<asec::MeshSource> source() const { return src_; }
  const asec::PointCloud *points() const { return points_.get(); } // LAS 원본 점군(없으면 nullptr)
  SectionDoc sectionDoc() const { return section_->doc(); }
  bool hasScene() const { return bool(src_); }
  bool hasSection() const;
  void setLineLocal(const asec::SectionLine &l);
  void setThickness(double front, double back);
  double frontDepth() const;
  double backDepth() const;
  bool backUserSet() const { return backUserSet_; }
  void setDepthFade(bool on);
  const asec::SrsInfo &srs() const;
  const asec::SrsReport &srsReport() const { return srsReport_; }
  QString cursorText() const; // 자동화 로그: 좌표줄 X/Y/Z + Z 출처
  struct SectionCounters {
    int previewsShown = 0, finalsShown = 0;
    double lastMs = 0;
    size_t lastTris = 0;
  };
  const SectionCounters &sectionCounters() const { return secCounters_; }
  /// 단면선 끌기 흉내(자동화): final=false 는 끄는 중, true 는 놓음
  void dragLine(const asec::SectionLine &l, bool final) {
    plan_->setLine(l, true);
    requestSection(final);
  }
  PlanView *plan() const { return plan_; }
  SectionView *sectionView() const { return section_; }
  /// 화면 세로 과장(1·2·5·10). 화면 보기만 — 도면·내보내기는 1:1
  void setVex(double v);
  int vexSuggestion() const { return vexSuggest_; }
  double vexRelief() const { return vexRelief_; }
  QString windowTitleCheck() const; // 시험용: 창 제목 + 표시 이름 중복 여부
  void selectRibbonTab(int i);
  void openFile(const QString &path);
  int addSectionAt(const asec::SectionLine &l); // 자동화: 단면 목록에 더함(로컬 좌표) → 번호
  void selectSectionAt(int i) { selectSection(i); }
  int sectionCount() const { return int(sections_.size()); }
  void restoreModelState(); // 모델별 마지막 단면선·두께·화면·단면 목록 되살리기
  void saveModelState();

protected:
  void dragEnterEvent(QDragEnterEvent *) override;
  void dropEvent(QDropEvent *) override;
  void closeEvent(QCloseEvent *) override;

private:
  PlanView *plan_ = nullptr;
  SectionView *section_ = nullptr;
  QWidget *planFrame_ = nullptr;
  QWidget *sectionFrame_ = nullptr;
  QSplitter *split_ = nullptr;
  QTabBar *tabs_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  QStackedWidget *body_ = nullptr; // 0 시작 화면, 1 작업
  QWidget *startPage_ = nullptr;
  QWidget *workArea_ = nullptr;
  QWidget *ctxBar_ = nullptr;
  QLabel *ctxHint_ = nullptr, *ctxValue_ = nullptr;
  QFrame *notice_ = nullptr;
  QLabel *noticeText_ = nullptr;
  QLabel *secTitle_ = nullptr, *secFacing_ = nullptr;
  QToolButton *heightBadge_ = nullptr, *heightBadge2_ = nullptr;
  QToolButton *vexBtn_ = nullptr;
  int vexSuggest_ = 1;
  double vexRelief_ = 0;
  void updateVexUi();
  QLabel *stripLen_ = nullptr, *stripDepth_ = nullptr, *stripScale_ = nullptr,
         *stripState_ = nullptr, *stripLevels_ = nullptr;
  QWidget *planTitle_ = nullptr, *secTitleBar_ = nullptr;
  QComboBox *scaleCombo_ = nullptr;
  QLabel *lvLine_ = nullptr, *lvLabel_ = nullptr;
  QToolButton *depthChip_[5] = {};
  bool backUserSet_ = false; // 뒤 깊이를 사용자가 직접 고름(아니면 기본 3 m 를 따름)
  QMenu *recentMenu_ = nullptr;
  QUndoStack *undo_ = nullptr;
  QWidget *sidePanel_ = nullptr;
  QListWidget *secList_ = nullptr;
  QLabel *secCount_ = nullptr;
  QLineEdit *cx_ = nullptr, *cy_ = nullptr, *cz_ = nullptr;
  QLabel *msg_ = nullptr, *info_ = nullptr, *srsLabel_ = nullptr;
  asec::SrsReport srsReport_;              // 열린 모델의 좌표계 점검(표시·경고)
  asec::Box3 sceneBounds_;                 // 열린 모델 로컬 상자(좌표계 재점검용)
  std::vector<std::string> sceneWarnings_; // 열 때 경고(병합 레이어 등)
  QString heightNote_;                     // 높이 기준 지정 출처 안내(모델별 저장 / 마지막 지정값)
  QProgressBar *progress_ = nullptr;
  QDoubleSpinBox *front_ = nullptr, *back_ = nullptr;
  QSlider *opacity_ = nullptr;
  bool drawEnded_ = false; // 방금 그리기를 마침(다음 단면 결과를 맞춤으로)
  bool highContrast_ = false;
  bool fitNextResult_ = false;
  // ---- 되돌리기: 단면선·두께·표시·높이 기준·현재 단면 스냅숏
public:
  struct ViewState {
    asec::SectionLine line;
    bool hasLine = false;
    double front = 0, back = 0.5;
    bool image = true, profile = true, levels = true, fade = true, smooth = false;
    int heightDecl = 0;
    int current = 0;
    bool operator==(const ViewState &o) const;
  };
  ViewState captureState() const;
  void applyState(const ViewState &s);
  void commitState(const QString &text, int mergeId = -1);
  QUndoStack *undoStack() const { return undo_; }

private:
  ViewState lastState_;
  bool applyingState_ = false;
  // ---- 단면 목록(P1-1): 실좌표, 설정(모델 경로 키)에 저장
  std::vector<asec::SavedSection> sections_;
  int current_ = -1;
  std::map<int, QImage> thumbs_;
  void refreshSectionList();
  void syncCurrentSection(); // 지금 단면선 → sections_[current_]
  void selectSection(int i);
  void addSection();
  void renameSection(int i);
  void deleteSection(int i);
  bool exportSectionsJson(const QString &path, QString *msg);
  void importSectionsJson();
  QString sectionName() const; // 지금 단면 이름("A–A′")
  QImage makeThumb() const;    // 목록 썸네일(64×30)
  // ---- 최근 파일
  QStringList recentFiles() const;
  void addRecent(const QString &path);
  void rebuildRecentMenu();
  void rebuildStartPage();
  // ---- 화면 부품
  QWidget *buildStartPage();
  QWidget *buildCtxBar();
  QFrame *buildNotice();
  QWidget *buildSidePanel();
  QWidget *buildPlanFrame();
  QWidget *buildSectionFrame();
  void updateHeader(); // 단면 머리·정보 띠·높이 배지
  void updateCtx(int stage, const asec::SectionLine &l);
  void setActiveView(int v); // 0 평면 1 단면
  void shiftLine(double d);  // 평행 이동(m, + = 보는 쪽)
  void dlgCoordEntry();
  void dlgKeys();
  void setBackDepth(double v);
  void updateDepthChips();
  void applyHighContrast(bool on);
  QString heightBadgeText(QString *state, QString *tip) const;
  std::map<QString, QAction *> act_;
  struct Label {
    QObject *obj;
    QString ko, en;
    int kind;
  }; // kind 0 탭 1 그룹라벨 2 동작 3 제목
  std::vector<Label> labels_;
  bool bilingual_ = false;

  std::shared_ptr<asec::MeshSource> src_;
  std::shared_ptr<asec::PointCloud> points_; // LAS 장면의 원본 점군
  QString path_, kind_;
  size_t displayTris_ = 0;
  bool streaming_ = false;

  // 단면 작업(asec::CoalescingWorker): 끄는 동안 미리보기는 합치고(거친 LOD), 놓으면 잎으로 최종
  // 계산
  std::unique_ptr<asec::CoalescingWorker> secWorker_;
  // 커서 정밀 Z: 잎 메시 CPU 피킹(마지막 요청만 계산)
  std::unique_ptr<asec::CoalescingWorker> pickWorker_;
  asec::ResultGate pickGate_;
  uint64_t pickFloorGen_ = 0;
  QLabel *zSrc_ = nullptr;
  void requestPick(const QPointF &screen);
  void showZSource(asec::ZSource s, const QString &detail);
  asec::ResultGate secGate_; // GUI 스레드 소유: 이미 보여준 것보다 새 세대만 받음
  uint64_t secFloorGen_ = 0; // 장면 바꿈/닫기 이후 세대만 받음
  SectionCounters secCounters_;
  asec::SectionOutput last_; // GUI 스레드 소유(마지막 완료 결과)
  bool lastFinal_ = false;

  // 일반 작업(열기·내보내기)
  std::thread task_;
  std::atomic<bool> taskBusy_{false};
  std::atomic<bool> cancelTask_{false};

  QAction *action(const QString &key) const { return act_.at(key); }
  QAction *makeAction(const QString &key, const QString &ko, const QString &en, theme::Ico ico,
                      const QString &shortcut = {}, bool checkable = false);
  QWidget *buildRibbon();
  QWidget *buildCoordBar();
  void retranslate();
  void requestSection(bool final);
  void onSectionDone(asec::SectionOutput &&out, uint64_t gen, bool final, const QString &err);
  void runTask(const QString &what, std::function<bool(QString *)> work,
               std::function<void(bool, const QString &)> done);
  void setProgress(double f);
  void updateEnabled();
  void showStatus(const QString &s);
  void chooseOpen();
  void closeScene();
  void dlgSectionImage();
  void dlgSectionDxf();
  void dlgPlan();
  void dlgPoints(int presetFormat);
  void dlgProfileCsv();
  void dlgInfo();
  void dlgAbout();
  QString askSavePath(const QString &key, const QString &suggested, const QString &filter);
  SectionStyle secStyle() const;
  void report(bool ok, const QString &m);
  void applySrsReport();
  void dlgHeightDatum();

public:
  /// 높이 기준 지정(이름표만, 값 변환 없음). persist = 모델별 설정 + 마지막 지정값 저장
  void setHeightDeclaration(asec::VDatum v, bool persist);

private:
  void refreshSrs();
};
