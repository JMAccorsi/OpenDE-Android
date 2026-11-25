#pragma once

#include "engine/canvas.h"
#include "engine/mesh.h"
#include "engine/resources.h"
#include "render/lighting.h"
#include "render/lightmap.h"
#include "render/render_device.h"
#include "render/span_buffer.h"
#include "render/view_modes.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dx {
    class UMesh;
    struct MeshTriangle;

    UTexture* meshSkin(UObject* actor, UMesh* mesh, size_t index);
    UTexture* meshEnvironmentMap(UObject* actor, UMesh* mesh, UObject* zone, UObject* levelInfo, UTexture* skinOverride);
    void environmentCoords(const Vector& eyeToPoint, const Vector& normal, float& u, float& v);
    UObject* frameSkyZone(UObject* zone);
    UTexture* matrixTexture(UObject* viewport);
    UTexture* spriteAnimOnceFrame(UObject* actor, UTexture* texture);
    bool drawnAsSprite(uint8_t drawType);

    struct RenderStats {
        float lightsMs = 0;
        float occludeMs = 0;
        float skyMs = 0;
        float bspMs = 0;
        float actorsMs = 0;
        float actCandMs = 0;
        float actEmitMs = 0;
        float actFlushMs = 0;
        float actWaitMs = 0;
        float actTransMs = 0;
        float transSurfMs = 0;
        float transUpMs = 0;
        float transDrawMs = 0;
        int transSurfaces = 0;
        int transSurfCalls = 0;
        int transDraws = 0;
        float coronasMs = 0;
        float ssaoMs = 0;
        float fxaaMs = 0;
        int actBatches = 0;
        int actActors = 0;
        int drawCalls = 0;
        int triangles = 0;
        int lightmapUploads = 0;
        int meshFast = 0;
        int meshFull = 0;
        int meshRebuild = 0;
        int meshFog = 0;
        int meshRebGeom = 0;
        int meshRebUncache = 0;
        int meshRebLight = 0;
    };

    struct RenderOptions {
        float anisotropy = 1;
        bool trilinear = false;
        int multisample = 0;
        bool fxaa = false;
        bool smoothLightmaps = false;
        float detailDistance = 380;
        float textureRate = 0;
        OcclusionQuality ambientOcclusion = OcclusionQuality::Off;
    };

    class SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer();
        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        void beginFrame(int width, int height);
        void drawScene(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor);
        void drawSceneRect(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor, int x, int y, int width, int height);
        void drawActorOverlay(UObject* actor, const Vector& viewLocation, const Rotator& viewRotation, float fov, bool clearZ);
        void drawScreenFlash(const Vector& flashScale, const Vector& flashFog);
        Canvas2D& canvas();
        void endFrame();
        void applyBrightness(float brightness);
        void setOptions(const RenderOptions& newOptions);
        void readPixels(std::vector<uint8_t>& rgba, int& width, int& height);
        void flushLevel();
        void setTime(double seconds);
        void setViewportTime(double seconds);
        void setRenderMode(RenderMode mode);
        const RenderStats& stats() const { return frameStats; }

        std::function<bool(UObject* actor, const std::function<void(UObject* item)>& emit)> iteratorHook;

    private:
        struct Batch {
            UTexture* texture = nullptr;
            uint32_t polyFlags = 0;
            int32_t lightmapPage = -1;
            uint8_t zone = 0;
            float uSize = 64;
            float vSize = 64;
            UTexture* detail = nullptr;
            std::vector<std::array<int32_t, 4>> detailPolys;
            std::vector<std::array<int32_t, 6>> surfPolys;
            std::vector<std::array<int32_t, 3>> nodePolys;
            bool fogZone = false;
            bool reversed = false;
            int32_t first = 0;
            int32_t count = 0;
            Vector boundsMin;
            Vector boundsMax;
        };

        struct MirrorSurface {
            Vector normal;
            float distance = 0;
            uint8_t zone = 0;
            uint32_t polyFlags = 0;
            int32_t first = 0;
            int32_t count = 0;
            std::vector<Vector> hull;
        };

        struct NodeEmit {
            int32_t batch = 0;
            int32_t first = 0;
            int32_t count = 0;
        };

        struct ViewVisibility {
            std::vector<int32_t> nodes;
            std::vector<int32_t> nodeSequence;
            std::vector<std::pair<int32_t, int32_t>> leaves;
            std::vector<std::pair<int32_t, int32_t>> culled;
            int32_t endSequence = 0;
            std::vector<int32_t> tileCoverSeq;
            int32_t tileCols = 0;
            int32_t tileRows = 0;
            int32_t tileColumns = 0;
            int32_t tileRowCount = 0;
            std::vector<std::vector<ScreenSpan>> mirrorSpans;
            std::vector<ScreenSpan> backdropSpans;
        };

        struct FogCache {
            bool valid = false;
            bool fogged = false;
            uint64_t signature = 0;
            Vector eye;
            Vector center;
            float extent = -1;
            int32_t count = 0;
        };

        struct OccNode {
            Plane plane;
            uint64_t zoneMask = 0;
            uint64_t emitZoneMask = 0;
            int32_t front = -1;
            int32_t back = -1;
            int32_t iPlane = -1;
            int32_t leaf[2] = { -1, -1 };
            int32_t renderBound = -1;
            uint32_t flags = 0;
            int32_t polyStart = 0;
            uint16_t polyCount = 0;
            int16_t mirror = -1;
        };

        struct LevelData {
            ULevel* level = nullptr;
            UModel* model = nullptr;
            std::vector<OccNode> occNodes;
            std::vector<Vector> occPoints;
            uint32_t vao = 0;
            uint32_t detailVao = 0;
            std::vector<Vector> detailPoints;
            std::vector<Batch> batches;
            bool hasBackdrop = false;
            uint32_t mirrorVao = 0;
            std::vector<MirrorSurface> mirrors;
            std::vector<std::vector<int32_t>> surfNodes;
            std::vector<int32_t> nodeVolumetrics;
            std::vector<int32_t> leafVolumetrics;
            bool anyVolumetric = false;
            std::vector<uint64_t> fogFrame;
            std::vector<FogCache> fogCache;
            std::vector<uint8_t> fogged;
            std::unordered_map<int32_t, std::vector<Vector>> fogPoints;
            std::vector<uint32_t> surfFlags;
            std::vector<int32_t> nodeMirror;
            std::vector<int32_t> nodeEmitStart;
            std::vector<NodeEmit> nodeEmits;
            std::vector<uint64_t> nodeEmitZoneMask;
            std::vector<uint32_t> nodeStamp;
            std::vector<uint32_t> transNodeStamp;
            std::vector<uint32_t> surfStamp;
            std::vector<uint32_t> leafStamp;
            std::vector<uint32_t> cullStamp;
            std::vector<int32_t> cullOrder;
            std::vector<std::vector<std::array<int32_t, 3>>> batchRanges;
            UObject* levelInfo = nullptr;
            std::vector<GLVertex> vertices;
            std::vector<GLVertex> wireVertices;
            std::vector<GLVertex> portalVertices;
            uint32_t flatVaos[2] = {};
        };

        struct TranslucentSurface {
            int32_t order = 0;
            const Batch* batch = nullptr;
            int32_t first = 0;
            int32_t count = 0;
            int32_t node = -1;
        };

        struct GroupExtra {
            uint32_t lightmap = 0;
            uint32_t lightMask = 0;
            bool cull = false;
        };

        struct QueuedGroup {
            uint64_t sortKey = 0;
            uint32_t flags = 0;
            uint32_t texture = 0;
            GroupExtra extra;
            const std::vector<GLVertex>* ref = nullptr;
            uint32_t tempOffset = 0;
            uint32_t tempCount = 0;
        };

        struct BatchQueue {
            std::vector<QueuedGroup> items;
            std::vector<GLVertex> tempVertices;

            void clear()
            {
                items.clear();
                tempVertices.clear();
            }
        };

        struct TranslucentItem {
            int32_t order = 0;
            float distance = 0;
            uint32_t flags = 0;
            uint32_t texture = 0;
            GroupExtra extra;
            std::vector<GLVertex> vertices;
        };

        struct MeshTransform {
            Vector origin;
            Vector x;
            Vector y;
            Vector z;
        };

        struct EmittedGroup {
            uint64_t key = 0;
            uint32_t flags = 0;
            uint32_t texture = 0;
            bool translucent = false;
            uint32_t lightmap = 0;
            uint32_t lightMask = 0;
            bool cull = false;
            Vector low;
            Vector high;
            std::vector<GLVertex> vertices;
        };

        struct LightState {
            std::vector<UObject*> actors;
            std::vector<PreparedLight> prepared;
            std::vector<Vector> colors;
        };

        struct MeshDrawCache {
            bool valid = false;
            bool cacheable = false;
            bool hasEnvironment = false;
            bool hasRawTransform = false;
            bool steadyLights = false;
            Vector rawLocation {};
            Vector rawPrePivot {};
            Rotator rawRotation {};
            float rawDrawScale = 0;
            MeshTransform transform;
            Name sequence;
            float frame = 0;
            Name blendSequences[4];
            float blendFrames[4] = {};
            Vector sphereCenter;
            float sphereRadius = 0;
            float lodTarget = -1;
            int32_t lodVertexSubset = -1;
            int32_t lodNonMorph = -1;
            bool lodMorph = false;
            std::vector<Vector> world;
            std::vector<Vector> worldNormals;
            bool lodTriangles = false;
            std::vector<MeshTriangle> triangles;
            std::vector<const GLTextureInfo*> slotInfo;
            std::vector<uint32_t> slotFlags;
            uint32_t actorFlags = 0;
            bool unlitActor = false;
            float scaleGlow = 0;
            float glowScale = 0;
            Vector ambient;
            Vector eye;
            LightState lights;
            uint64_t lastFullFrame = 0;
            bool hasAnimatedSkin = false;
            uint8_t rawGlow = 0;
            uint8_t rawStyle = 0;
            UObject* rawSkin = nullptr;
            UObject* rawTexture = nullptr;
            std::vector<EmittedGroup> groups;
        };

        struct BrushDrawCache {
            bool valid = false;
            Vector location;
            Rotator rotation;
            Vector scale;
            Vector prePivot;
            UModel* brush = nullptr;
            Vector sphereCenter;
            float sphereRadius = 0;
            std::vector<const GLTextureInfo*> polyInfo;
            std::vector<uint32_t> polyFlags;
            Vector ambient;
            LightState lights;
            std::vector<EmittedGroup> groups;
            std::shared_ptr<UModel> frameModel;
            std::shared_ptr<LightmapAtlas> atlas;
            LightmapTransform lightTransform;
            bool lightTransformValid = false;
            uint64_t litFrame = 0;
            UTexture* textureOverride = nullptr;
        };

        struct OcclusionState {
            uint32_t query = 0;
            bool pending = false;
            bool known = false;
            bool visible = true;
            uint64_t issuedFrame = 0;
            uint64_t resultFrame = 0;
            Vector losPawnLoc {};
            Vector losViewLoc {};
            uint64_t losFrame = 0;
            bool losVisible = false;
        };

        struct View {
            Vector location;
            Rotator rotation;
            Mat4 viewProj {};
            Coords axes;
            float fov = 90;
            float tanHalfWidth = 1;
            float tanHalfHeight = 1;
            int32_t spanWidth = 0;
            int32_t spanHeight = 0;
            uint64_t zoneMask = ~0ull;
            int32_t recursion = 0;
            bool mirrored = false;
            int32_t clipCount = 0;
            std::array<Plane, 8> clipPlanes {};
            std::vector<Plane> cullPlanes;
            std::vector<Vector> portal;
            bool fog = false;
            std::shared_ptr<ViewVisibility> visibility;
        };

        void buildLevel(ULevel* level, const std::vector<UObject*>& actors);
        void drawLevelGeometry(const View& view, bool translucentPass, const std::function<void()>& afterGather = nullptr);
        void drawDetail(const Batch& batch, const View& view, int32_t onlyNode);
        void drawBatch(const Batch& batch, const View& view, const int32_t* firsts, const int32_t* counts, int32_t ranges, int32_t onlyNode, uint32_t& uploadedLights, size_t prebakedByteOffset = 0, int32_t prebakedCount = 0);
        void occludeView(View& view, const std::vector<ScreenSpan>* open);
        void gatherVisible(const View& view);
        bool drawMirrors(const View& view, const std::vector<UObject*>& actors);
        void drawFrameContent(const View& view, const std::vector<UObject*>& actors, UObject* viewActor, bool occlude);
        void applyClip(const View& view);
        void drawSky(const View& frame, const std::vector<UObject*>& actors, UObject* viewActor);
        void markMirror(const View& view, const MirrorSurface& mirror);
        void collectVolumetrics(const View& view);
        bool surfaceFog(int32_t surfIndex, const View& view);
        void drawFog(const Batch& batch, const View& view, int32_t onlyNode);
        Vector meshFog(const Vector& point, const View& view, size_t count) const;
        void drawActors(const std::vector<UObject*>& actors, const View& view, UObject* viewActor, bool occlude, bool preGathered = false);
        void gatherActorCandidates(const std::vector<UObject*>& actors, const View& view, UObject* viewActor);
        bool actorBounds(UObject* actor, Vector& center, float& radius) const;
        void cullOccluded(std::vector<UObject*>& candidates, const View& view);
        void applyView(const View& view);
        void drawActor(UObject* actor, const View& view);
        void drawMesh(UObject* actor, const View& view);
        void drawMeshWith(UObject* actor, UObject* lightActor, UMesh* mesh, UObject* animSource, const MeshTransform& transform, const View& view, MeshDrawCache* preResolvedCache = nullptr);
        void drawSprite(UObject* actor, const View& view);
        void drawBrush(UObject* actor, const View& view);
        void drawMoverSurfaces(UObject* actor, UModel* brush);
        int flushMeshBatches();
        void flushTranslucent(const View& view);
        void drawGroup(uint32_t flags, uint32_t texture, const GroupExtra& extra, const std::vector<GLVertex>& vertices);
        void setActorOrder(UObject* actor, const View& view, int32_t knownLeaf = -1);
        void queueTranslucent(uint32_t flags, uint32_t texture, const GroupExtra& extra, const std::vector<GLVertex>& vertices);
        int flushBatches(BatchQueue& queue);
        View makeView(const Vector& location, const Rotator& rotation, float fov, float nearPlane);
        View makeView(const Vector& location, const Coords& axes, float fov, float nearPlane);
        UObject* zoneActor(uint8_t zone) const;
        UObject* zoneForActor(UObject* actor);
        uint8_t pointZone(const Vector& location) const;
        int32_t pointLeaf(const Vector& location) const;
        int32_t actorLeaf(UObject* actor) const;
        uint8_t actorZone(UObject* actor) const;
        uint64_t visibleZones(uint8_t zone) const;
        bool inFrustum(const View& view, const Vector& center, float radius) const;
        bool sphereReachesVisible(const View& view, const Vector& center, float radius, int32_t knownLeaf = -1);
        bool actorOccludedBySpanTiles(UObject* actor, const View& view, int32_t knownLeaf) const;
        uint32_t litMask(const Vector& low, const Vector& high, bool specialLit) const;
        void addCorona(UObject* light, const Vector& origin, float delta, size_t& free, UObject* viewportActor);
        void drawCoronas(const View& view, int x, int y, int width, int height);
        bool sphereOutsideView(const View& view, const Vector& location, float radius) const;
        bool sphereReachesLeaf(const Vector& center, float radius, int32_t leaf) const;
        float actorRadius(UObject* actor) const;
        Vector actorLight(UObject* actor, const Vector& point, const Vector& normal, const Vector& ambient, float scaleGlow, bool unlit, const Vector* eye);
        void collectLights(const std::vector<UObject*>& actors);
        void collectDynamicLights(const Vector& viewLocation);
        void drawDecal(UObject* actor, const View& view);
        bool sameLights(const LightState& state) const;
        void storeLights(LightState& state) const;
        void appendGroups(const std::vector<EmittedGroup>& groups, BatchQueue& opaque, bool persistent = false);
        void selectLights(const Vector& location, float extent);
        void selectActorLights(UObject* actor);
        void applyBlend(uint32_t polyFlags, int32_t extraFlags);
        const std::vector<MeshTriangle>& meshTriangles(UMesh* mesh);
        uint32_t flatVao();
        void drawWireframe(const View& view, const std::vector<UObject*>& actors, UObject* viewActor);
        void drawPortals(const View& view);

        std::unique_ptr<RenderDevice> device;
        LightmapAtlas lightmaps;
        LevelData levelData;
        std::vector<UObject*> lights;
        std::vector<UObject*> nonStaticLights;
        std::vector<UObject*> scratchAccepted;
        std::vector<PreparedLight> lightPrepared;
        std::vector<Vector> lightColors;
        std::vector<UObject*> actorLights;
        std::vector<DynamicLight> dynamicLights;
        std::vector<DynamicLight> batchLights;
        std::vector<PreparedLight> actorLightPrepared;
        std::vector<Vector> actorLightColors;

        struct LightSender {
            UObject* light = nullptr;
            int32_t factor = 0;
        };

        struct CoronaLight {
            UObject* actor = nullptr;
            float bright = 0;
        };

        std::unordered_map<UObject*, std::vector<LightSender>> actorLightCache;
        CoronaLight coronas[32];
        std::vector<VolumetricLight> volumetrics;
        std::vector<std::array<int64_t, 3>> volumetricPalettes;
        std::vector<Vector> volumetricColors;
        uint64_t volumetricSignature = 0;
        uint64_t fogStamp = 0;
        std::vector<uint8_t> scratchFog;
        std::vector<Vector> scratchFogLit;
        double coronaTime = -1;
        std::vector<UObject*> lightConsider;
        int32_t considerTag = 0;
        int32_t lightDelta = 0;
        int32_t lightFrameCount = 0;
        double lastFrameTime = -1;
        std::vector<Vector> scratchPose;
        std::vector<Vector> scratchNormals;
        std::vector<Vector> scratchWorld;
        std::vector<Vector> scratchWorldNormals;
        std::vector<MeshTriangle> scratchLodTriangles;
        std::vector<float> scratchLodAlphas;
        std::vector<Vector> scratchLit;
        std::vector<uint8_t> scratchLitDone;
        std::vector<UTexture*> scratchSkins;
        std::vector<const GLTextureInfo*> scratchSlotInfo;
        std::vector<uint32_t> scratchSlotFlags;
        std::vector<size_t> scratchSlotGroups;
        std::unordered_map<UMesh*, std::vector<MeshTriangle>> triangleCache;
        struct PoseCacheKey {
            UObject* actor = nullptr;
            UMesh* mesh = nullptr;
            bool operator==(const PoseCacheKey& o) const { return actor == o.actor && mesh == o.mesh; }
        };
        struct PoseCacheKeyHash {
            size_t operator()(const PoseCacheKey& k) const
            {
                size_t h = reinterpret_cast<uintptr_t>(k.actor);
                return h ^ (reinterpret_cast<uintptr_t>(k.mesh) * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h << 6) + (h >> 2));
            }
        };
        std::unordered_map<PoseCacheKey, MeshPoseCaches, PoseCacheKeyHash> poseCaches;
        struct MeshDrawKey {
            UObject* actor = nullptr;
            UMesh* mesh = nullptr;
            UObject* lightActor = nullptr;
            bool mirror = false;
            bool operator==(const MeshDrawKey& o) const
            {
                return actor == o.actor && mesh == o.mesh && lightActor == o.lightActor && mirror == o.mirror;
            }
        };

        struct MeshDrawKeyHash {
            size_t operator()(const MeshDrawKey& k) const
            {
                size_t h = reinterpret_cast<uintptr_t>(k.actor);
                h ^= reinterpret_cast<uintptr_t>(k.mesh) * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= reinterpret_cast<uintptr_t>(k.lightActor) * 0xbf58476d1ce4e5b9ULL + (h << 6) + (h >> 2);
                return h ^ static_cast<size_t>(k.mirror);
            }
        };

        std::unordered_map<MeshDrawKey, MeshDrawCache, MeshDrawKeyHash> meshDrawCaches;
        std::unordered_map<UObject*, BrushDrawCache> brushDrawCaches;
        std::unordered_map<UObject*, OcclusionState> occlusion;
        std::vector<UObject*> scratchCandidates;
        std::vector<UObject*> scratchIterated;
        std::vector<UObject*> scratchDecals;
        std::vector<GLVertex> occlusionBoxes;
        SpanBuffer spans;
        std::vector<int32_t> touchedBatches;
        std::vector<int32_t> scratchNodes;
        std::vector<int32_t> scratchFirsts;
        std::vector<int32_t> scratchCounts;
        struct PreparedBatch {
            int32_t batchIndex = 0;
            int32_t rangeStart = 0;
            int32_t rangeCount = 0;
            size_t indexByteOffset = 0;
            int32_t indexCount = 0;
        };
        std::vector<PreparedBatch> scratchPreparedBatches;
        std::vector<int32_t> scratchAllFirsts;
        std::vector<int32_t> scratchAllCounts;
        std::vector<ScreenSpan> scratchSpans;
        std::vector<std::shared_ptr<ViewVisibility>> visibilityPool;
        std::vector<ScreenPoint> scratchPendingPoints;
        std::vector<int32_t> scratchPendingCounts;
        struct OccStackEntry {
            int32_t index = 0;
            int32_t stage = 0;
            uint32_t planeMask = 0;
            int32_t farChild = 0;
            int32_t farLeaf = -1;
        };
        std::vector<OccStackEntry> scratchOccStack;
        std::vector<EmittedGroup> scratchOldGroups;
        std::vector<GLVertex> scratchSpriteVertices;
        std::vector<GLVertex> scratchDecalVertices;
        std::vector<Vector> scratchDecalPolygon;
        uint32_t visibleStamp = 0;
        uint32_t transStamp = 0;
        int viewportWidth = 1;
        int viewportHeight = 1;
        std::vector<OcclusionState*> occlusionIssued;
        std::unordered_set<UObject*> occlusionUnconfirmed;
        uint64_t sceneFrame = 0;
        BatchQueue meshBatches;
        BatchQueue brushBatches;
        BatchQueue decalBatches;
        std::vector<TranslucentItem> translucentItems;
        size_t translucentCount = 0;
        std::vector<TranslucentSurface> scratchTranslucentSurfaces;
        std::vector<int32_t> nodeOrder;
        std::vector<int32_t> leafOrder;
        UObject* orderActor = nullptr;
        const View* orderView = nullptr;
        int32_t orderLeaf = -1;
        bool orderReady = false;
        int32_t currentOrder = 0;
        int32_t fallbackOrder = 0;
        float currentDistance = 0;
        int frameWidth = 1;
        int frameHeight = 1;
        float viewAspect = 1;
        double time = 0;
        bool timeSet = false;
        double viewportTime = -1;
        double currentTime = 0;
        double startTime = 0;
        float gammaExponent = 1;
        bool offscreen = false;
        RenderOptions options;
        RenderMode renderMode = RenderMode::Textured;
        RenderStats frameStats;

        struct Worker {
            std::thread thread;
            std::mutex mutex;
            std::condition_variable cv;
            std::function<void()> task;
            bool busy = false;
            bool stop = false;

            Worker();
            ~Worker();
            void run(std::function<void()> fn);
            void wait();
        };
        Worker worker;
    };
}
