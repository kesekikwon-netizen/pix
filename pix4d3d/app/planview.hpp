// 평면 보기: 위에서 본 정사(orthographic) 텍스처 메시 + 빨간 단면선·두께 띠(괄호선) 편집
#pragma once
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <atomic>
#include <functional>
#include <memory>
#include "asec/section.hpp"
#include "asec/tmx.hpp"
#include "asec/stream.hpp"
#include <unordered_map>

/// GPU 에 올릴 메시(장면 중심 기준 float 좌표)
struct DisplayMesh {
    std::vector<float> pos, uv, nrm;
    std::vector<uint32_t> idx;
    QImage tex;  // RGBA8888, 행0 = 위
};

/// 화면 메시의 XY 격자 색인: 커서 아래 Z(가장 높은 면)
class HeightIndex {
public:
    void build(const std::vector<std::shared_ptr<DisplayMesh>>& ms, double cell);
    bool z(double x, double y, double& out) const;  // 중심 기준 좌표
private:
    struct Tri { float x[3], y[3], z[3]; };
    std::vector<Tri> tris_;
    std::vector<std::vector<uint32_t>> cells_;
    double x0_ = 0, y0_ = 0, cell_ = 1;
    int nx_ = 0, ny_ = 0;
};

class PlanView : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit PlanView(QWidget* parent = nullptr);
    ~PlanView() override;

    /// center = 로컬 좌표의 장면 중심(메시는 이미 이 값을 뺀 상태), bounds = 로컬 상자
    void setScene(std::vector<std::shared_ptr<DisplayMesh>> meshes, const asec::Vec3& center, const asec::Box3& bounds, const asec::SrsInfo& srs);
    /// 3MX: 시점 의존 LOD 스트리밍(roots = 각 layer 의 루트 .3mxb). heightMeshes = 커서 Z 용 거친 메시(선택)
    void setStreamingScene(const std::vector<asec::fs::path>& roots, const asec::Vec3& center, const asec::Box3& bounds, const asec::SrsInfo& srs,
                           std::vector<std::shared_ptr<DisplayMesh>> heightMeshes = {});
    bool hasScene() const { return hasScene_; }
    void clearScene();

    // ---- 스트리밍 상태(자동화·성능 기록용)
    struct FrameInfo { double ms = 0; size_t draw = 0, uploaded = 0, evicted = 0, wanted = 0, queued = 0, loading = 0; size_t residentBytes = 0, gpuBytes = 0; int maxDepth = -1; bool idle = true;
                       size_t uploadBytes = 0, staging = 0; double uploadMs = 0;
                       size_t burstDone = 0, burstTotal = 0; };   // burst*: 이번 불러오기 묶음(LOD 카드 「12/40」)
    /// GPU 올리기 예산(프레임당 바이트). 설정 view/uploadBudgetKB(기본 3072), 0 이면 옛 방식(노드 통째)
    static size_t uploadBudgetBytes();
    const FrameInfo& lastFrame() const { return lastFrame_; }
    bool streamIdle() const { return !streamer_ || lastFrame_.idle; }
    bool streaming() const { return bool(streamer_); }
    void setCamera(double cx, double cy, double mpp);  // 로컬 XY 중심 + m/px(위에서 보기)
    double cameraX() const { return double(target_.x()) + center_.x; }
    double cameraY() const { return double(target_.y()) + center_.y; }
    static size_t gpuBudgetBytes();
    QByteArray glRenderer() const { return glRenderer_; }

    void setDrawMode(bool on);
    bool drawMode() const { return drawStage_ >= 0; }
    void setLine(const asec::SectionLine& l, bool has);
    const asec::SectionLine& line() const { return line_; }
    bool hasLine() const { return hasLine_; }
    void setBand(double front, double back);

    void fitAll();
    void topView();
    void zoomBy(double f);
    /// 현재 화면에 보이는 로컬 XY 범위(평면 기준)
    bool viewRectLocal(asec::Box3& out) const;
    const asec::Box3& bounds() const { return bounds_; }
    double metersPerPixel() const { return mpp_; }
    /// 휠 확대/축소(커서 위치 고정). smooth=true 면 약 0.1 초에 걸쳐 부드럽게(설정 view/smoothZoom). 자동화·시험용으로도 씀
    void wheelZoom(const QPointF& at, double notches);
    bool zoomAnimating() const { return zoomPending_ != 0.0; }
    bool screenToLocalXYPublic(const QPointF& sp, asec::Vec2& out) const { return screenToLocalXY(sp, out); }
    /// 화면 점 → 광선(로컬 double 좌표: 원점, 방향). 정밀 피킹(asec::pickRay)용
    bool screenRayLocal(const QPointF& sp, asec::Vec3& o, asec::Vec3& d) const;
    QPointF lastMousePos() const { return QPointF(lastMouse_); }

    std::function<void(const asec::SectionLine&, bool final)> onLineChanged;
    std::function<void(double X, double Y, double Z, bool hasZ, bool valid)> onCursor;  // 실좌표
    std::function<void(bool)> onDrawModeChanged;
    std::function<void()> onOpenRequest;
    std::function<void(int stage, const asec::SectionLine&)> onDrawProgress;   // 그리는 중(0 A 대기, 1 A′ 대기): 지금 도구 줄 길이·방위
    /// 좌표 입력(Enter)으로 A·A′ 를 정해 그리기 끝내기(로컬 XY)
    void finishDrawAt(const asec::SectionLine& l);
    int drawStage() const { return drawStage_; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct Gpu { GLuint vbo = 0, ibo = 0, tex = 0; GLsizei count = 0; bool hasTex = false; size_t bytes = 0; int texW = 0, texH = 0, texLevels = 0; };
    // 내려간 노드의 텍스처를 같은 크기 다음 노드가 다시 씀(새로 잡는 비용 = 소프트웨어 GL 에서 수~수십 ms 를 피함)
    struct PoolTex { GLuint tex; int w, h, levels; size_t bytes; };
    std::vector<PoolTex> texPool_;
    size_t texPoolBytes_ = 0;
    void freeTexPool();
    std::vector<std::shared_ptr<DisplayMesh>> meshes_;
    std::vector<Gpu> gpu_;
    bool hasScene_ = false;
    // 스트리밍(3MX)
    std::unique_ptr<asec::LodStreamer> streamer_;
    std::unordered_map<asec::LodStreamer::Key, std::vector<Gpu>> gpuNodes_;
    size_t gpuNodeBytes_ = 0;
    std::atomic<bool> repaintQueued_{false};
    FrameInfo lastFrame_;
    QByteArray glRenderer_;
    double lodBias_ = 1.0;  // >1 이면 더 세밀하게(설정 view/lodBias)
    void paintStreaming(const QMatrix4x4& mvp);
    // 나눠 올리기: 준비된 노드를 peek 으로 빌려 버퍼·텍스처(밉맵 단계·행 띠)를 프레임마다 예산만큼 올리고, 다 올리면 take 로 상주 확정
    struct Staging {
        std::shared_ptr<void> prep;   // NodePrep
        std::vector<Gpu> parts;
        size_t part = 0; int stage = 0; size_t offset = 0; int level = 0; int row = 0;
        uint64_t lastSeen = 0;
    };
    std::unordered_map<asec::LodStreamer::Key, Staging> staging_;
    uint64_t frameNo_ = 0;
    size_t burstDone_ = 0, burstPeak_ = 0;
    bool stepStaging(Staging& st, size_t& budget, const class QElapsedTimer& t, double maxMs);   // true = 다 올림
    void freeStaging();
    void freeGpuNode(std::vector<Gpu>& v);
    void drawGpu(const Gpu& g);
    bool needUpload_ = false;
    std::unique_ptr<QOpenGLShaderProgram> prog_;
    HeightIndex hidx_;
    asec::Vec3 center_;  // 로컬 장면 중심
    asec::Box3 bounds_;  // 로컬
    asec::SrsInfo srs_;
    // 카메라(중심 기준 좌표)
    QVector3D target_;
    double mpp_ = 0.01, yaw_ = 0, pitch_ = 90;
    double zoomPending_ = 0;            // 남은 확대량(log, 음수 = 확대)
    QPointF zoomAnchor_;
    class QTimer* zoomTimer_ = nullptr;
    void applyZoomAt(const QPointF& at, double factor);
    QMatrix4x4 proj_, view_;
    // 단면선(로컬 XY)
    asec::SectionLine line_;
    bool hasLine_ = false;
    int drawStage_ = -1;  // -1 편집, 0 A 대기, 1 B 대기
    int dragHandle_ = -1; // 0 A, 1 B, 2 가운데(전체 이동)
    QPointF dragStartMouse_; asec::SectionLine dragStartLine_;
    bool panning_ = false, orbiting_ = false;
    QPoint lastMouse_;

    void freeGpu();
    void upload();
    void updateMatrices();
    bool screenToLocalXY(const QPointF& sp, asec::Vec2& out) const;
    QPointF localToScreen(double x, double y, double z) const;
    double refZ() const;
    void paintOverlay(QPainter& p);
    void paintEmpty(QPainter& p);
    void paintLodCard(QPainter& p);
    asec::Vec2 lockAxis(const asec::Vec2& w, Qt::KeyboardModifiers m) const;
    void emitLine(bool final) { if (onLineChanged && hasLine_) onLineChanged(line_, final); }
};
