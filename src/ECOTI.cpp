// ECOTI Thermal Fusion - GML plugin (com.remarrow.ecoti).
//
// Two kinds of thermal night vision, as new entries in the gear menu's Night Vision list:
//
//   ECOTI     the AN/PAS-29B Enhanced Clip-On Thermal Imager on any of the game's GPNVG-18s and PVS-31s
//             (every phosphor): "<goggle> + ECOTI". The clip-on sits on the goggle's bridge and folds with
//             it. With the goggle down and powered, anything warm-blooded (people, alive or cooling dead)
//             is fused into the picture as heat: white-hot, over the tubes' own image.
//   Fusion    a panoramic fusion goggle, the GPNVG-18's successor: the thermal core is built into the
//             bridge, and it pairs with ATAK to float a HUD in the picture - a compass tape with the
//             heading, team markers (every other player, name and range) and navigation waypoints
//             (extraction zones, the mission objective, and your own: hold the left thumbstick in while
//             looking at a spot to drop one, at the sky to clear it).
//
// Underneath, each entry is the game's own NVG entry (its actor, flip lever, power and post-process),
// built at runtime and never saved as itself: while worn the outfit holds the stock goggle with the
// colour name "ECOTI" or "FusionHUD", which every save, every machine and the network understand. Without
// this plugin such a save simply loads the stock goggle.
//
// Everything here is drawn by this machine for this machine (the heat, the HUD); other players with the
// plugin see the clip-on / fusion core on your goggle. The models are built from code (no art files).
//
// Settings: GML\config\com.remarrow.ecoti.cfg. Build: build.bat [install].
#define NOMINMAX
#include <GML/GML.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "version.h"
#pragma comment(lib, "gdi32.lib")

using namespace gml;

GML_PLUGIN("com.remarrow.ecoti", "ECOTI Thermal Fusion", ECOTI_VERSION);

// ------------------------------------------------------------------ math (UE conventions: cm, X fwd, Y right, Z up)

struct V3 { double x = 0, y = 0, z = 0; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double Len(V3 a) { return std::sqrt(Dot(a, a)); }
struct V2 { double x = 0, y = 0; };
struct Rot { double pitch = 0, yaw = 0, roll = 0; };
struct Quat { double x = 0, y = 0, z = 0, w = 1; };
struct Color { float r = 0, g = 0, b = 0, a = 1; };
static constexpr double kPi = 3.14159265358979323846, kDeg = kPi / 180;

static double Clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static double Wrap360(double a) { a = std::fmod(a, 360); return a < 0 ? a + 360 : a; }
static double WrapDeg(double a) { a = Wrap360(a + 180); return a - 180; }

static Quat ToQuat(Rot r) {  // FRotator::Quaternion
    double sp = std::sin(r.pitch * kDeg / 2), cp = std::cos(r.pitch * kDeg / 2);
    double sy = std::sin(r.yaw * kDeg / 2), cy = std::cos(r.yaw * kDeg / 2);
    double sr = std::sin(r.roll * kDeg / 2), cr = std::cos(r.roll * kDeg / 2);
    return {cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy};
}
static V3 Rotate(Quat q, V3 v) {  // FQuat::RotateVector
    V3 u{q.x, q.y, q.z};
    V3 t = Cross(u, v) * 2;
    return v + t * q.w + Cross(u, t);
}
static V3 Unrotate(Quat q, V3 v) { return Rotate({-q.x, -q.y, -q.z, q.w}, v); }

// ------------------------------------------------------------------ engine helpers

static double Now() { return GetTickCount64() / 1000.0; }

// GML's FindClass scans every object while a class isn't loaded (and several of ours only load on some maps):
// a miss is asked again only every 5 s.
static GUClass* Cls(const char* name) {
    struct E { GUClass* c = nullptr; double next = 0; };
    static std::map<std::string, E> cache;
    E& e = cache[name];
    if (e.c && API->IsValid((GUObject*)e.c)) return e.c;
    if (Now() < e.next) return nullptr;
    e.c = FindClass(name);
    if (!e.c) e.next = Now() + 5;
    return e.c;
}
static bool Is(Object o, const char* cls) { GUClass* c = Cls(cls); return c && o.IsA(c); }

static std::vector<uint8_t> Transform(V3 loc, Rot rot, V3 scale = {1, 1, 1}) {
    static int size = 0;
    Params p(Lib("KismetMathLibrary"), "MakeTransform");
    p.Set("Location", loc).Set("Rotation", rot).Set("Scale", scale).Call();
    if (!size) {
        GML_PropInfo ri;
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &ri);
        size = ri.size;
    }
    auto* s = (uint8_t*)p.Ptr("ReturnValue");
    return {s, s + size};
}

static void Attach(Object c, Object parent, uint8_t rule) {  // rule: 0 KeepRelative, 1 KeepWorld, 2 SnapToTarget
    Params at(c, "K2_AttachToComponent");
    at.SetObj("Parent", parent).Set("SocketName", uint64_t(0)).Set("LocationRule", rule).Set("RotationRule", rule)
        .Set("ScaleRule", rule).Set("bWeldSimulatedBodies", false).Call();
}
static void SetRel(Object c, V3 loc, Rot rot = {}) {
    Params p(c, "K2_SetRelativeLocationAndRotation");
    p.Set("NewLocation", loc).Set("NewRotation", rot).Set("bSweep", false).Set("bTeleport", true).Call();
}
static void SetScale(Object c, double s) {
    Params p(c, "SetRelativeScale3D");
    p.Set("NewScale3D", V3{s, s, s}).Call();
}
static void SetVisible(Object c, bool on) {
    if (!c || c.GetBool("bVisible") == on) return;
    Params v(c, "SetVisibility");
    v.Set("bNewVisibility", on).Set("bPropagateToChildren", false).Call();
}
static V3 CompLoc(Object c) { Params p(c, "K2_GetComponentLocation"); p.Call(); return p.Return<V3>(); }
static Rot CompRot(Object c) { Params p(c, "K2_GetComponentRotation"); p.Call(); return p.Return<Rot>(); }
static V3 ActorLoc(Object a) { Params p(a, "K2_GetActorLocation"); p.Call(); return p.Return<V3>(); }
static Object GetComp(Object actor, const char* cls) {
    GUClass* c = Cls(cls);
    if (!actor || !c) return {};
    Params p(actor, "GetComponentByClass");
    p.Set("ComponentClass", c).Call();
    return p.Return<GUObject*>();
}
static std::vector<Object> GetComps(Object actor, const char* cls) {
    std::vector<Object> out;
    GUClass* c = Cls(cls);
    if (!actor || !c) return out;
    Params p(actor, "K2_GetComponentsByClass");
    p.Set("ComponentClass", c).Call();
    auto* arr = (GML_TArray*)p.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) out.push_back(((GUObject**)arr->Data)[i]);
    return out;
}
static void Destroy(Object c) {
    if (!c) return;
    Params d(c, "K2_DestroyComponent");
    d.SetObj("Object", c).Call();
}
static bool Alive(GUObject* o) { return o && API->IsValid(o); }

static Object PlayerPawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static Object VRCamera(Object pawn) { return pawn ? pawn.GetObj("VRReplicatedCamera") : Object(); }
// This machine is a client in someone else's game (the host owns every actor there).
static bool NetClient() {
    Params s(Lib("KismetSystemLibrary"), "IsServer");
    s.SetObj("WorldContextObject", Object(API->WorldContext())).Call();
    return !s.Return<bool>();
}
// You and everything on you (helmet, NVG, pouches, what your hands hold): what the HUD's and waypoint traces
// look past. Refreshed every few seconds while the fusion goggle is worn (the engine allocates the list, and a
// plugin can't free it).
static std::vector<GUObject*> s_mine;
static void ScanMine() {
    s_mine.clear();
    Object me = PlayerPawn();
    if (!me) return;
    s_mine.push_back(me.ptr);
    Params p(me, "GetAttachedActors");
    if (!p) return;
    p.Set("bResetArray", true).Set("bRecursivelyIncludeAttachedActors", true).Call();
    if (auto* arr = (GML_TArray*)p.Ptr("OutActors"))
        for (int i = 0; i < arr->Num; i++) s_mine.push_back(((GUObject**)arr->Data)[i]);
}
static Object GearManager(Object pawn) { return GetComp(pawn, "BP_GearManagerComponent_C"); }
static uint64_t NameOf(const char* s) { return API->MakeName(s); }

// What this plugin looks at in the world, found in one pass over the objects twice a second.
struct World {
    std::vector<GUObject*> players;  // BP_PlayerCharacter_C
    std::vector<GUObject*> people;   // every Character (players, AI) and player ragdolls
    std::vector<GUObject*> exfil;    // BP_ExtractionZone_C
    GUObject* mission = nullptr;     // BP_GameMode_MissionBase_C (the host's)
};
static World s_world;
static void ScanWorld() {
    World w;
    GUClass* actor = Cls("Actor");
    GUClass* player = Cls("BP_PlayerCharacter_C");
    GUClass* character = Cls("Character");
    GUClass* ragdoll = Cls("BP_PlayerRagdoll_C");
    GUClass* spectator = Cls("BP_SpectatorCharacter_C");
    GUClass* exfil = Cls("BP_ExtractionZone_C");
    GUClass* mission = Cls("BP_GameMode_MissionBase_C");
    int n = API->ObjectCount();
    for (int i = 0; actor && i < n; i++) {
        GUObject* o = API->ObjectAt(i);
        if (!o || !API->IsA(o, actor) || !API->IsValid(o)) continue;
        bool isChar = character && API->IsA(o, character);
        bool isRag = ragdoll && API->IsA(o, ragdoll);
        bool isExfil = exfil && API->IsA(o, exfil);
        bool isMission = mission && API->IsA(o, mission);
        if (!isChar && !isRag && !isExfil && !isMission) continue;
        if (Object(o).Name().rfind("Default__", 0) == 0) continue;  // a class default
        if (isChar && spectator && API->IsA(o, spectator)) continue;
        if (isChar || isRag) w.people.push_back(o);
        if (isChar && player && API->IsA(o, player)) w.players.push_back(o);
        if (isExfil) w.exfil.push_back(o);
        if (isMission) w.mission = o;
    }
    s_world = std::move(w);
}

static std::string FStringUtf8(const GML_FString& s) {
    if (!s.Data || s.Num <= 0) return "";
    char buf[256];
    int n = API->FStringToUtf8(&s, buf, sizeof buf);
    return std::string(buf, std::min(n, (int)sizeof buf - 1));
}

// A line trace on the Visibility channel. Returns the hit distance along the ray (or -1).
static double Trace(V3 from, V3 to, const std::vector<GUObject*>& ignore, V3* at = nullptr) {
    Params t(Lib("KismetSystemLibrary"), "LineTraceSingle");
    t.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Start", from).Set("End", to)
        .Set("TraceChannel", (uint8_t)0).Set("bTraceComplex", false).Set("DrawDebugType", (uint8_t)0).Set("bIgnoreSelf", true);
    *(GML_TArray*)t.Ptr("ActorsToIgnore") = GML_TArray{(void*)ignore.data(), (int32_t)ignore.size(), (int32_t)ignore.size()};
    t.Call();
    if (!t.Return<bool>()) return -1;
    static GML_PropInfo loc{};
    static bool init = false;
    if (!init) {
        GML_PropInfo hit;
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetSystemLibrary").Struct(), "LineTraceSingle"), "OutHit"), &hit);
        API->GetPropertyInfo(API->FindProperty(hit.structType, "ImpactPoint"), &loc);
        init = true;
    }
    V3 p;
    std::memcpy(&p, (uint8_t*)t.Ptr("OutHit") + loc.offset, sizeof p);
    if (at) *at = p;
    return Len(p - from);
}

// The value for `key` in a TMap<EGearItem, V> property of `obj`, or nullptr.
static uint8_t* MapValue(Object obj, const char* prop, uint8_t key) {
    GFProperty* mp = obj ? API->FindProperty(obj.Struct(), prop) : nullptr;
    void* map = obj ? obj.PropPtr(prop) : nullptr;
    if (!mp || !map) return nullptr;
    struct Ctx { uint8_t key; uint8_t* found; } ctx{key, nullptr};
    API->MapForEach(map, mp, [](void* u, void* k, void* v) -> int {
        auto* c = (Ctx*)u;
        if (*(uint8_t*)k != c->key) return 0;
        c->found = (uint8_t*)v;
        return 1;
    }, &ctx);
    return ctx.found;
}

// ------------------------------------------------------------------ settings

static ConfigEntry<bool> s_cfgEcoti, s_cfgFusion, s_cfgThermal, s_cfgSelfHeat, s_cfgHud, s_cfgRequireAtak, s_cfgTeam, s_cfgWaypoints;
static ConfigEntry<float> s_cfgHeat, s_cfgHeatAlpha, s_cfgCoolMinutes, s_cfgHudFov, s_cfgHudDistance, s_cfgHudBright, s_cfgNorthYaw,
    s_cfgTapeSpan, s_cfgWpHold, s_cfgMarkerRange;
static ConfigEntry<std::string> s_cfgFusionPhosphor, s_cfgWpButton;

// ------------------------------------------------------------------ the models (built from code)
//
// Both are boxes and cylinders in Unreal axes (cm, X forward, Z up), origin at the bottom of their mount,
// back centre. Materials: 0 polymer body, 1 germanium lens, 2 rubber / battery cap.

struct ObjBuilder {
    std::ostringstream v, n;
    std::vector<std::string> faces[3];
    int nv = 0, nn = 0;
    int Vert(V3 p) { v << "v " << p.x << " " << p.y << " " << p.z << "\n"; return ++nv; }
    int Norm(V3 d) { n << "vn " << d.x << " " << d.y << " " << d.z << "\n"; return ++nn; }
    void Tri(int mat, int a, int b, int c, int na, int nb, int nc) {
        std::ostringstream f;
        f << "f " << a << "/1/" << na << " " << b << "/1/" << nb << " " << c << "/1/" << nc;
        faces[mat].push_back(f.str());
    }
    // A quad with its own vertices (flat): corners in order around the face, normal `d`.
    void Quad(int mat, V3 a, V3 b, V3 c, V3 e, V3 d) {
        int ia = Vert(a), ib = Vert(b), ic = Vert(c), ie = Vert(e), in = Norm(d);
        Tri(mat, ia, ib, ic, in, in, in);
        Tri(mat, ia, ic, ie, in, in, in);
    }
    void Box(int mat, V3 lo, V3 hi) {
        V3 p[8];
        for (int i = 0; i < 8; i++) p[i] = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
        Quad(mat, p[1], p[3], p[7], p[5], {1, 0, 0});   // front
        Quad(mat, p[0], p[4], p[6], p[2], {-1, 0, 0});  // back
        Quad(mat, p[2], p[6], p[7], p[3], {0, 1, 0});   // right
        Quad(mat, p[0], p[1], p[5], p[4], {0, -1, 0});  // left
        Quad(mat, p[4], p[5], p[7], p[6], {0, 0, 1});   // top
        Quad(mat, p[0], p[2], p[3], p[1], {0, 0, -1});  // bottom
    }
    // A cylinder along `axis` (0 X, 1 Y) from a to b, centred on c (the other two coordinates), both ends capped.
    void Cyl(int mat, int axis, double a, double b, V3 c, double r, int seg = 28) {
        auto at = [&](double t, double ang) {
            double u = std::cos(ang) * r, w = std::sin(ang) * r;
            return axis == 0 ? V3{t, c.y + u, c.z + w} : V3{c.x + u, t, c.z + w};
        };
        auto radial = [&](double ang) { return axis == 0 ? V3{0, std::cos(ang), std::sin(ang)} : V3{std::cos(ang), 0, std::sin(ang)}; };
        for (int i = 0; i < seg; i++) {
            double a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
            int p0 = Vert(at(a, a0)), p1 = Vert(at(b, a0)), p2 = Vert(at(b, a1)), p3 = Vert(at(a, a1));
            int n0 = Norm(radial(a0)), n1 = Norm(radial(a1));
            Tri(mat, p0, p1, p2, n0, n0, n1);
            Tri(mat, p0, p2, p3, n0, n1, n1);
        }
        for (int end = 0; end < 2; end++) {
            double t = end ? b : a;
            V3 dir = axis == 0 ? V3{1, 0, 0} : V3{0, 1, 0};
            if ((end == 1) != (b > a)) dir = dir * -1;
            V3 centre = axis == 0 ? V3{t, c.y, c.z} : V3{c.x, t, c.z};
            int nc = Norm(dir);
            for (int i = 0; i < seg; i++) {
                int ic = Vert(centre), i0 = Vert(at(t, 2 * kPi * i / seg)), i1 = Vert(at(t, 2 * kPi * (i + 1) / seg));
                Tri(mat, ic, i0, i1, nc, nc, nc);
            }
        }
    }
    std::string Text() const {
        static const char* names[3] = {"Body", "Lens", "Accent"};
        std::ostringstream s;
        s << v.str() << "vt 0 0\n" << n.str();
        for (int m = 0; m < 3; m++) {
            if (faces[m].empty()) continue;
            s << "usemtl " << names[m] << "\n";
            for (auto& f : faces[m]) s << f << "\n";
        }
        return s.str();
    }
};

// AN/PAS-29B ECOTI: a compact housing on a mounting shoe, the germanium objective in front, two buttons on
// top and the CR123 battery cap on the right.
static std::string EcotiObj() {
    ObjBuilder b;
    b.Box(2, {0, -1.2, 0}, {4.5, 1.2, 0.6});         // mounting shoe
    b.Box(0, {0.2, -2.1, 0.6}, {5.6, 2.1, 3.6});     // housing
    b.Box(0, {0.8, -1.6, 3.6}, {4.8, 1.6, 3.9});     // top ridge
    b.Box(0, {-0.6, -1.4, 1.0}, {0.2, 1.4, 3.2});    // rear (injection) block
    b.Cyl(0, 0, 5.6, 6.9, {0, 0, 2.1}, 1.45);        // lens barrel
    b.Cyl(1, 0, 6.6, 6.95, {0, 0, 2.1}, 1.15);       // germanium lens
    b.Box(2, {1.2, -0.55, 3.9}, {1.8, -0.05, 4.2});  // buttons
    b.Box(2, {2.2, -0.55, 3.9}, {2.8, -0.05, 4.2});
    b.Cyl(2, 1, 2.1, 2.7, {3.0, 0, 2.1}, 0.9);       // battery cap
    return b.Text();
}
// The fusion goggle's own thermal core, low on the bridge: a flatter housing with a small objective.
static std::string FusionObj() {
    ObjBuilder b;
    b.Box(0, {0, -1.9, 0}, {4.6, 1.9, 2.4});          // core housing
    b.Box(0, {0.4, -1.5, 2.4}, {4.0, 1.5, 2.7});      // cover
    b.Cyl(0, 0, 4.6, 5.3, {0, 0, 1.2}, 1.15);         // bezel
    b.Cyl(1, 0, 5.1, 5.35, {0, 0, 1.2}, 0.9);         // germanium lens
    b.Box(2, {0.6, 1.9, 0.8}, {1.4, 2.05, 1.6});      // side pairing button
    b.Box(2, {0.6, -2.05, 0.8}, {1.4, -1.9, 1.6});
    return b.Text();
}
// The HUD: one quad 100 cm ahead of the eye spanning `hfov` degrees (aspect W:H), UV (0,0) top left.
static std::string HudObj(double hfov, double aspect) {
    double w = 100 * std::tan(hfov * kDeg / 2), h = w / aspect;
    std::ostringstream s;
    s << "v 100 " << -w << " " << h << "\nv 100 " << w << " " << h << "\nv 100 " << w << " " << -h << "\nv 100 " << -w << " " << -h << "\n";
    s << "vt 0 1\nvt 1 1\nvt 1 0\nvt 0 0\nvn -1 0 0\nusemtl Body\n";  // OBJ's v runs up (the importer flips it)
    s << "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";
    return s.str();
}

static GUObject* s_mats[3] = {};  // body, lens, accent
static GUObject* s_meshEcoti = nullptr;
static GUObject* s_meshFusion = nullptr;
static GUObject* s_meshHud = nullptr;
static GUObject* s_heatMat = nullptr;  // the overlay on everything warm (alive)
static GUObject* s_hudMat = nullptr;
static GUObject* s_hudRT = nullptr;
static GUObject* s_blend = nullptr;    // the engine's translucent unlit glTF material
static bool s_assetsTried = false, s_assetsOk = false;
static constexpr int kHudW = 1280, kHudH = 800;

// The heat colour. The engine's glTF material multiplies it by the mesh's vertex colours, and the game paints
// those (they mask its camo and gear tints): a plain white would come out red, purple, white in patches. The
// goggle's picture is monochrome (its post-process takes brightness only), so the heat is made far brighter than
// white: every tinted patch then saturates, and a body reads as one white-hot shape.
static float HeatLevel() { return (float)Clamp(s_cfgHeat.Value(), 0, 4) * 6.0f; }

static GUObject* ImportObj(const char* name, const std::string& text, GUObject* const* mats, int count) {
    static const char* groups[] = {"Body", "Lens", "Accent"};
    GML_MeshImport o{};
    o.size = sizeof o;
    o.axis = GML_AXIS_UNREAL;
    o.flipWinding = -1;
    o.materialCount = count;
    o.materialNames = groups;
    o.materials = mats;
    GUObject* m = API->ImportDynamicMeshFromMemory(text.data(), text.size(), name, &o);
    if (!m) Error("could not build mesh {}", name);
    else API->KeepAlive(m);
    return m;
}

static GUObject* Mid(GUObject* parent, Color base, float metallic = -1, float rough = -1) {
    GUObject* m = parent ? API->CreateMaterialInstance(parent) : nullptr;
    if (!m) return nullptr;
    API->KeepAlive(m);
    float c[4] = {base.r, base.g, base.b, base.a};
    API->SetMaterialVector(m, "BaseColorFactor", c);
    if (metallic >= 0) API->SetMaterialScalar(m, "MetallicFactor", metallic);
    if (rough >= 0) API->SetMaterialScalar(m, "RoughnessFactor", rough);
    return m;
}

// The HUD's font: Windows' Consolas Bold, drawn once into an atlas (ASCII 32..127, 16 x 6 cells) with GDI and
// handed to the engine as a TGA. Canvas text can't be used on the HUD: drawn into a see-through render target it
// leaves the alpha at 0 (invisible). Glyphs drawn from this atlas with the Masked blend write their alpha.
static GUObject* s_font = nullptr;
static constexpr int kGlyphCols = 16, kGlyphRows = 6, kCellW = 40, kCellH = 64;
static double s_glyphAdvance = 29;  // px in the atlas, measured

static std::vector<uint8_t> FontAtlasTga() {
    const int W = kGlyphCols * kCellW, H = kGlyphRows * kCellH;
    std::vector<uint8_t> tga;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    HFONT font = CreateFontW(-50, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    if (bmp && bits && font) {
        HGDIOBJ oldBmp = SelectObject(dc, bmp), oldFont = SelectObject(dc, font);
        std::memset(bits, 0, (size_t)W * H * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        SIZE adv{};
        if (GetTextExtentPoint32W(dc, L"M", 1, &adv) && adv.cx > 0) s_glyphAdvance = adv.cx;
        for (int c = 32; c < 128; c++) {
            int i = c - 32;
            wchar_t ch = (wchar_t)c;
            TextOutW(dc, (i % kGlyphCols) * kCellW + (kCellW - (int)s_glyphAdvance) / 2, (i / kGlyphCols) * kCellH + 4, &ch, 1);
        }
        GdiFlush();
        uint8_t hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)(W & 255), (uint8_t)(W >> 8), (uint8_t)(H & 255), (uint8_t)(H >> 8), 32, 0x28};
        tga.assign(hdr, hdr + 18);
        tga.reserve(18 + (size_t)W * H * 4);
        auto* px = (uint8_t*)bits;
        for (int p = 0; p < W * H; p++) {
            uint8_t a = std::max(px[p * 4], std::max(px[p * 4 + 1], px[p * 4 + 2]));  // white on black: coverage
            tga.push_back(255); tga.push_back(255); tga.push_back(255); tga.push_back(a);
        }
        SelectObject(dc, oldFont);
        SelectObject(dc, oldBmp);
    }
    if (font) DeleteObject(font);
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    return tga;
}

static GUObject* HudRT() {
    if (s_hudRT && API->IsValid(s_hudRT)) return s_hudRT;
    Params p(Lib("KismetRenderingLibrary"), "CreateRenderTarget2D");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Width", (int32_t)kHudW).Set("Height", (int32_t)kHudH)
        .Set("Format", (uint8_t)6).Set("ClearColor", Color{0, 0, 0, 0}).Set("bAutoGenerateMipMaps", false).Set("bSupportUAVs", false).Call();
    s_hudRT = p.Return<GUObject*>();
    if (s_hudRT) {
        API->KeepAlive(s_hudRT);
        if (s_hudMat) API->SetMaterialTexture(s_hudMat, "BaseColorTexture", s_hudRT);
    }
    return s_hudRT;
}

static bool BuildAssets() {
    if (s_assetsTried) return s_assetsOk;
    s_assetsTried = true;
    if (API->size < offsetof(GML_API, ImportDynamicMeshFromMemory) + sizeof(void*)) {
        Error("this plugin needs GML 2.2 or newer");
        return false;
    }
    GUObject* opaque = LoadObject("/InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS.MI_Default_Opaque_DS").ptr;
    if (!opaque) opaque = LoadObject("/InterchangeAssets/gltf/M_Default.M_Default").ptr;
    s_blend = LoadObject("/InterchangeAssets/gltf/MaterialInstances/MI_Unlit_Blend_DS.MI_Unlit_Blend_DS").ptr;
    if (!s_blend) s_blend = LoadObject("/InterchangeAssets/gltf/MaterialInstances/MI_Unlit_Blend.MI_Unlit_Blend").ptr;
    if (!opaque || !s_blend) { Error("the engine's glTF materials are missing"); return false; }
    s_mats[0] = Mid(opaque, {0.011f, 0.011f, 0.010f, 1}, 0.0f, 0.72f);   // black polymer
    s_mats[1] = Mid(opaque, {0.05f, 0.042f, 0.065f, 1}, 0.9f, 0.12f);    // germanium: dark, glossy, a violet sheen
    s_mats[2] = Mid(opaque, {0.006f, 0.006f, 0.006f, 1}, 0.0f, 0.9f);    // rubber
    s_meshEcoti = ImportObj("ecoti", EcotiObj(), s_mats, 3);
    s_meshFusion = ImportObj("fusion_core", FusionObj(), s_mats, 3);
    float h = HeatLevel(), a = s_cfgHeatAlpha.Value();
    s_heatMat = Mid(s_blend, {h, h, h, a});
    s_hudMat = Mid(s_blend, {1, 1, 1, 1});
    if (s_hudMat) {
        GUObject* hudParts[1] = {s_hudMat};
        s_meshHud = ImportObj("fusion_hud", HudObj(s_cfgHudFov.Value(), (double)kHudW / kHudH), hudParts, 1);
        HudRT();
        std::vector<uint8_t> atlas = FontAtlasTga();
        if (!atlas.empty()) s_font = API->ImportTextureFromMemory(atlas.data(), atlas.size(), "ecoti_hud_font");
        if (s_font) API->KeepAlive(s_font);
        else Warn("the HUD font could not be made - the HUD shows no text");
    }
    s_assetsOk = s_meshEcoti && s_meshFusion && s_heatMat;
    Log("assets {}", s_assetsOk ? "ready" : "INCOMPLETE");
    return s_assetsOk;
}

// ------------------------------------------------------------------ the gear menu entries

static constexpr uint8_t kCatNightVision = 19;  // EGearItem
enum Kind { KindNone = 0, KindEcoti = 1, KindFusion = 2 };
static const char* kTagEcoti = "ECOTI";      // SelectedColor of the stock entry that stands for ours in a save
static const char* kTagFusion = "FusionHUD";

struct Donor { const char* path; bool gpnvg; const char* phosphor; };
static const Donor kDonors[] = {
    {"/Game/Characters/Data/DA_NightVision_GPNVG18_Green.DA_NightVision_GPNVG18_Green", true, "Green"},
    {"/Game/Characters/Data/DA_NightVision_GPNVG18_WhitePhos.DA_NightVision_GPNVG18_WhitePhos", true, "WhitePhos"},
    {"/Game/Characters/Data/DA_NightVision_GPNVG18_WhiteFilmless.DA_NightVision_GPNVG18_WhiteFilmless", true, "WhiteFilmless"},
    {"/Game/Characters/Data/DA_NightVision_PVS31_Green.DA_NightVision_PVS31_Green", false, "Green"},
    {"/Game/Characters/Data/DA_NightVision_PVS31_WhitePhos.DA_NightVision_PVS31_WhitePhos", false, "WhitePhos"},
    {"/Game/Characters/Data/DA_NightVision_PVS31_WhiteFilmless.DA_NightVision_PVS31_WhiteFilmless", false, "WhiteFilmless"},
};
static constexpr int kDonorCount = sizeof kDonors / sizeof kDonors[0];

static Object DonorObj(int i) {
    static GUObject* d[kDonorCount] = {};
    if (i < 0 || i >= kDonorCount) return {};
    if (!d[i] || !API->IsValid(d[i])) d[i] = LoadObject(kDonors[i].path).ptr;
    return d[i];
}
static int DonorIndex(GUObject* data) {
    for (int i = 0; data && i < kDonorCount; i++)
        if (DonorObj(i).ptr == data) return i;
    return -1;
}

struct Entry { Kind kind; int donor; GUObject* item; };
static std::vector<Entry> s_entries;
static const Entry* EntryOf(GUObject* item) {
    for (auto& e : s_entries)
        if (item && e.item == item) return &e;
    return nullptr;
}

// What a worn (item, colour) pair is: one of ours (the runtime entry itself, or the stock entry with our tag).
static Kind KindOf(GUObject* data, uint64_t color, int* donor = nullptr) {
    if (const Entry* e = EntryOf(data)) { if (donor) *donor = e->donor; return e->kind; }
    static uint64_t tagE = NameOf(kTagEcoti), tagF = NameOf(kTagFusion);
    int d = DonorIndex(data);
    if (donor) *donor = d;
    if (d < 0) return KindNone;
    if (color == tagE) return KindEcoti;
    if (color == tagF && kDonors[d].gpnvg) return KindFusion;
    return KindNone;
}

// FGearItemStruct: {UOutfitItem_C* Data; FName SelectedColor}, Blueprint-mangled names.
static int GearOff(const char* field, int fallback) {
    GUStruct* s = (GUStruct*)API->FindObject("/Game/Gameplay/Customization/GearItemStruct.GearItemStruct");
    GML_PropInfo i;
    return s && API->GetPropertyInfo(API->FindProperty(s, field), &i) ? i.offset : fallback;
}
static int GearDataOff() { static int off = GearOff("Data", 0); return off; }
static int GearColorOff() { static int off = GearOff("SelectedColor", 8); return off; }

static GML_TArray* NightVisionList(Object gi) {
    uint8_t* v = MapValue(gi, "OutfitItems", kCatNightVision);
    if (!v) return nullptr;
    GUStruct* s = (GUStruct*)API->FindObject("/Game/Gameplay/Customization/OutfitItemList.OutfitItemList");
    GML_PropInfo i;
    if (!s || !API->GetPropertyInfo(API->FindProperty(s, "Items"), &i)) return nullptr;
    return (GML_TArray*)(v + i.offset);
}

static std::string TextOf(Object o, const char* prop) {
    char b[160] = "";
    if (void* t = o.PropPtr(prop)) API->TextToString(t, b, sizeof b);
    return b;
}

static GUObject* MakeEntry(Object gi, int donor, const std::string& name, const char* subtitle) {
    Object d = DonorObj(donor);
    GUClass* cls = FindClass("OutfitItem_NightVision_C");
    if (!d || !cls) { Error("{} or OutfitItem_NightVision_C missing", kDonors[donor].path); return nullptr; }
    Object item = NewObject(cls, gi);
    if (!item) { Error("could not construct gear entry '{}'", name); return nullptr; }
    for (const char* p : {"Mesh", "EnabledMesh", "NVGClass", "PPMaterial", "CustomIcon"}) item.Set(p, d.Get<GUObject*>(p));
    item.Set("Category", kCatNightVision);
    item.Set("CustomIconSize", d.Get<double>("CustomIconSize"));
    item.Set("bUseEyesOnlyFace", d.Get<bool>("bUseEyesOnlyFace"));
    uint8_t text[16];
    if (API->MakeText(name.c_str(), text)) std::memcpy(item.PropPtr("DisplayName"), text, 16);
    if (API->MakeText(subtitle, text)) std::memcpy(item.PropPtr("Subtitle"), text, 16);
    API->KeepAlive(item.ptr);
    return item.ptr;
}

static int FusionDonor() {
    std::string ph = s_cfgFusionPhosphor.Value();
    for (int i = 0; i < kDonorCount; i++)
        if (kDonors[i].gpnvg && _stricmp(kDonors[i].phosphor, ph.c_str()) == 0) return i;
    return 1;  // white phosphor
}

// After the game builds its gear lists (GetItems; polled, as that call is Blueprint-to-Blueprint): ours join
// the Night Vision list, each right after its stock goggle, the fusion goggle after the GPNVG-18s.
static void AddEntries() {
    Object gi(API->WorldContext());
    GML_TArray* raw = gi ? NightVisionList(gi) : nullptr;
    if (!raw) return;
    TArrayView<GUObject*> list{raw};
    if (s_entries.empty()) {
        if (s_cfgEcoti.Value())
            for (int i = 0; i < kDonorCount; i++) {
                Object d = DonorObj(i);
                if (!d) continue;
                std::string stock = TextOf(d, "DisplayName"), tubes = TextOf(d, "Subtitle");  // "GPNVG 18" / "Green"
                if (stock.empty()) stock = kDonors[i].gpnvg ? "GPNVG-18" : "PVS-31";
                if (tubes.empty()) tubes = kDonors[i].phosphor;
                if (GUObject* it = MakeEntry(gi, i, stock + " + ECOTI", (tubes + ", ECOTI thermal").c_str()))
                    s_entries.push_back({KindEcoti, i, it});
            }
        if (s_cfgFusion.Value()) {
            std::string tubes = TextOf(DonorObj(FusionDonor()), "Subtitle");
            if (GUObject* it = MakeEntry(gi, FusionDonor(), "Panoramic Fusion", ((tubes.empty() ? "" : tubes + ", ") + "thermal + ATAK HUD").c_str()))
                s_entries.push_back({KindFusion, FusionDonor(), it});
        }
    }
    int before = list.Num(), added = 0;
    for (auto& e : s_entries) {
        if (!Alive(e.item) || list.Contains(e.item)) continue;
        if (list.Add(e.item)) added++;
    }
    if (!added) return;
    // Each one next to its stock goggle: move it up from the end to just after the donor's last ECOTI sibling.
    auto* data = (GUObject**)raw->Data;
    for (auto& e : s_entries) {
        int at = -1, from = -1;
        for (int i = 0; i < raw->Num; i++) {
            if (data[i] == e.item) from = i;
            const Entry* o = EntryOf(data[i]);
            if (data[i] == DonorObj(e.donor).ptr || (o && o != &e && o->donor == e.donor && from < 0)) at = i;
            if (e.kind == KindFusion && !o && DonorIndex(data[i]) >= 0 && kDonors[DonorIndex(data[i])].gpnvg) at = std::max(at, i);
        }
        if (from < 0 || at < 0 || at + 1 >= from) continue;
        GUObject* it = data[from];
        for (int i = from; i > at + 1; i--) data[i] = data[i - 1];
        data[at + 1] = it;
    }
    Log("added {} entries to the Night Vision gear ({} -> {})", added, before, list.Num());
}

// ---- picking one in the gear menu
//
// A tile's press goes through ProcessEvent (a bound event). For ours the tile briefly carries the stock goggle,
// so the menu puts that on through the game's own path (on a client: to the host, which can only take real
// assets); straight after, the stock goggle goes on with our tag, which is what makes it ours everywhere.

struct PendingPick { GUObject* tile = nullptr; GUObject* item = nullptr; };
static PendingPick s_pick;

static void WearPair(Object gm, int donor, Kind kind) {
    Object d = DonorObj(donor);
    if (!gm || !d) return;
    uint8_t buf[16] = {};
    *(GUObject**)(buf + GearDataOff()) = d.ptr;
    *(uint64_t*)(buf + GearColorOff()) = NameOf(kind == KindFusion ? kTagFusion : kTagEcoti);
    Params p(gm, "SetOutfitItem");
    std::memcpy(p.Ptr("GearItem"), buf, 16);
    p.Call();
    Log("on: {} as {} '{}'", kind == KindFusion ? "the fusion goggle" : "the ECOTI", d.Name(), kind == KindFusion ? kTagFusion : kTagEcoti);
}

// The local outfit holding one of our runtime entries (put on some other way than a tile): swap it for the pair.
static void ConvertWorn() {
    Object gm = GearManager(PlayerPawn());
    uint8_t* v = gm ? MapValue(gm, "GearByCategory", kCatNightVision) : nullptr;
    if (!v) return;
    if (const Entry* e = EntryOf(*(GUObject**)(v + GearDataOff()))) WearPair(gm, e->donor, e->kind);
}

// ------------------------------------------------------------------ on the goggle

struct Dressed {
    GUObject* actor = nullptr;  // the NVG actor the game hung on the VR camera
    Kind kind = KindNone;
    bool local = false;
    std::vector<Object> comps;
};
static std::map<GUObject*, Dressed> s_dressed;  // by NVG actor (ours and other players')

// Where each module sits on the goggle's lever (the part that flips), from the housing's own bounds: the clip-on
// on top of the bridge, its lens flush with the objectives; the fusion core in front of the bridge. cm, lever axes.
// The lever's box tops out at the mount arm (Z 0.8..0.9); the tubes' tops, where the bridge is, are ~5.6 below.
static V3 s_ecotiFit[2] = {{-6.4, 0, -5.6}, {-6.4, 0, -5.6}};  // [GPNVG-18, PVS-31]: from (max X, centre Y, max Z)
static V3 s_fusionFit = {-5.4, 0, -5.7};

struct Box { V3 lo, hi; bool ok = false; };
static Box MeshBox(Object comp) {
    Box b;
    Object m = comp ? comp.GetObj("StaticMesh") : Object();
    if (!m) return b;
    Params bb(m, "GetBoundingBox");
    bb.Call();
    b.lo = bb.Get<V3>("ReturnValue");
    b.hi = *(V3*)((uint8_t*)bb.Ptr("ReturnValue") + 24);
    b.ok = true;
    return b;
}

static bool IsGpnvg(Object nvg) { return nvg && Is(nvg, "BP_GPNVG18_C"); }

static V3 FitFor(Object nvg, Kind kind) {
    Object lever = nvg.GetObj("NVG");
    Box b = MeshBox(lever);
    if (!b.ok) b = MeshBox(nvg.GetObj("LocalMesh"));
    if (!b.ok) b = {{-4.8, -10.5, -10.1}, {8.4, 10.4, 0.9}, true};  // the GPNVG-18's
    V3 anchor{b.hi.x, (b.lo.y + b.hi.y) / 2, b.hi.z};
    return anchor + (kind == KindFusion ? s_fusionFit : s_ecotiFit[IsGpnvg(nvg) ? 0 : 1]);
}

static void Undress(GUObject* actor) {
    auto it = s_dressed.find(actor);
    if (it == s_dressed.end()) return;
    if (Alive(actor))
        for (Object c : it->second.comps) if (c) Destroy(c);
    s_dressed.erase(it);
}

static void Dress(Object nvg, Kind kind, bool local) {
    Undress(nvg.ptr);
    if (!BuildAssets()) return;
    Object lever = nvg.GetObj("NVG");
    if (!lever) { Warn("the NVG actor has no lever - nothing fitted"); return; }
    Dressed d;
    d.actor = nvg.ptr;
    d.kind = kind;
    d.local = local;
    GUObject* mesh = kind == KindFusion ? s_meshFusion : s_meshEcoti;
    Object c = API->AddDynamicMeshComponent(nvg.ptr, mesh, s_mats, 3);
    if (c) {
        Attach(c, lever, 2);
        V3 at = FitFor(nvg, kind);
        SetRel(c, at);
        Params sh(c, "SetCastShadow"); sh.Set("NewCastShadow", false).Call();
        c.SetBool("bHiddenInSceneCapture", false);
        d.comps.push_back(c);
        Log("{} fitted on {} ({}) at ({:.2f}, {:.2f}, {:.2f}) on its lever", kind == KindFusion ? "fusion core" : "ECOTI", nvg.Name(),
            local ? "yours" : "another player's", at.x, at.y, at.z);
    }
    s_dressed[nvg.ptr] = d;
}

// Whether a player wears one of ours, and which: their gear manager's own map (set on this machine), else the
// replicated gear (what the host sent: a client's own outfit, or another player's).
static Kind WornKind(Object gm, int* donor = nullptr) {
    if (!gm) return KindNone;
    if (uint8_t* v = MapValue(gm, "GearByCategory", kCatNightVision))
        return KindOf(*(GUObject**)(v + GearDataOff()), *(uint64_t*)(v + GearColorOff()), donor);
    GFProperty* prop = API->FindProperty(gm.Struct(), "ReplicatedGear");
    auto* arr = (GML_TArray*)gm.PropPtr("ReplicatedGear");
    GML_PropInfo ai{}, ei{};
    if (!prop || !arr || !API->GetPropertyInfo(prop, &ai) || !ai.inner || !API->GetPropertyInfo(ai.inner, &ei)) return KindNone;
    for (int i = 0; i < arr->Num; i++) {
        uint8_t* e = (uint8_t*)arr->Data + (size_t)i * ei.size;
        if (Kind k = KindOf(*(GUObject**)(e + GearDataOff()), *(uint64_t*)(e + GearColorOff()), donor)) return k;
    }
    return KindNone;
}

static Object s_nvg;       // the local player's NVG actor, when it carries one of ours
static Kind s_kind = KindNone;

// Every player's NVG actor gets what they wear (called 4 times a second).
static void CheckGoggles() {
    Object me = PlayerPawn();
    std::set<GUObject*> seen;
    s_nvg = {};
    s_kind = KindNone;
    for (GUObject* p : s_world.players) {
        Object pawn(p);
        if (!pawn) continue;
        Object gm = GearManager(pawn);
        Object nvg = gm ? gm.GetObj("NVG") : Object();
        if (!nvg) continue;
        int donor = -1;
        Kind k = WornKind(gm, &donor);
        bool local = pawn == me;
        // Loading an outfit swaps the goggle but can leave the old one's actor on the helmet (a PVS-31 actor for a
        // GPNVG-18): the host lets the game build the right one.
        if (k && local && donor >= 0 && !NetClient()) {
            GUClass* want = DonorObj(donor).Get<GUClass*>("NVGClass");
            if (want && nvg.Class() != want) {
                Log("the helmet carries {} for a {} - the game builds the right goggle", nvg.Name(), DonorObj(donor).Name());
                Undress(nvg.ptr);
                Params d(nvg, "K2_DestroyActor"); d.Call();
                gm.Set("NVG", (GUObject*)nullptr);
                Params c(gm, "CheckNVG"); c.Call();
                continue;
            }
        }
        if (k) {
            seen.insert(nvg.ptr);
            auto it = s_dressed.find(nvg.ptr);
            if (it == s_dressed.end() || it->second.kind != k) Dress(nvg, k, local);
            if (local) { s_nvg = nvg; s_kind = k; }
        }
    }
    for (auto it = s_dressed.begin(); it != s_dressed.end();) {
        GUObject* a = it->first;
        ++it;
        if (!seen.count(a)) { Log("thermal module off {}", Alive(a) ? Object(a).Name() : "a gone NVG actor"); Undress(a); }
    }
}

// How far up the lever is turned: 0 flipped down (in front of the eyes), 1 up.
static double LeverUp(Object lever) {
    double a = std::fabs(lever.Get<float>("CurrentLeverAngle"));
    double lim = std::max(std::fabs(lever.Get<float>("LeverLimitNegative")), std::fabs(lever.Get<float>("LeverLimitPositive")));
    return lim > 1 ? Clamp(1 - a / lim, 0, 1) : 0;
}
static bool Powered(Object nvg) {
    Object dev = nvg ? nvg.GetObj("NVGDevice") : Object();
    if (!dev) return false;
    Params p(dev, "IsPowered");
    p.Call();
    return p.Return<bool>();
}
// The goggle is down and on: thermal (and the HUD) show.
static bool Active() { return s_nvg && s_kind && Powered(s_nvg); }

// ------------------------------------------------------------------ thermal: people are hot
//
// Through an ECOTI the clip-on's thermal picture is laid over the tubes'. Here anything warm-blooded (every
// character: other players, AI, you) gets a white-hot overlay material, which the NVG's own post-process then
// turns into its phosphor's colour like everything else in view. Walls still hide them (thermal doesn't see
// through walls). The dead cool off over [Thermal] CoolMinutes.

struct HeatTarget {
    std::vector<std::pair<Object, GUObject*>> comps;  // mesh, its overlay before ours
    std::vector<Object> meshes;                       // its skeletal meshes, looked up every few seconds (the
    double meshesAt = -1;                             // engine allocates the list, and a plugin can't free it)
    double diedAt = -1;
    GUObject* mid = nullptr;                          // a cooling body's own overlay
};
static std::map<GUObject*, HeatTarget> s_heat;
static bool s_heatOn = false;

static bool IsDead(Object actor) {
    Object h = GetComp(actor, "DarkHealthComponent");
    return h && h.GetBool("bIsDead");
}

// Which of a character's meshes glow: the ones that are drawn. An overlay draws even over a mesh whose own
// materials show nothing, and the game's AI animate an invisible body (CharacterMesh0) under the outfit you see
// (OverlayMesh, their SelectedSkeletalMesh): heat on that body would show a soldier's outline round an insurgent.
static bool Drawn(Object actor, Object comp) {
    if (!comp.GetBool("bVisible") || comp.GetBool("bHiddenInGame") || !comp.GetBool("bRenderInMainPass")) return false;
    if (Object outfit = actor.GetObj("OverlayMesh"); outfit && outfit != comp && outfit.GetObj("SkinnedAsset") && comp == actor.GetObj("Mesh"))
        return false;
    return true;
}

static void SetOverlay(Object comp, GUObject* mat) {
    Params p(comp, "SetOverlayMaterial");
    p.SetObj("NewOverlayMaterial", Object(mat)).Call();
}

static void HeatOff() {
    for (auto& [a, t] : s_heat)
        if (Alive(a))
            for (auto& [c, before] : t.comps) if (c) SetOverlay(c, before);
    s_heat.clear();
    s_heatOn = false;
}

static void UpdateHeat() {
    bool want = s_cfgThermal.Value() && Active() && s_heatMat;
    if (!want) { if (s_heatOn) HeatOff(); return; }
    s_heatOn = true;
    Object me = PlayerPawn();
    std::set<GUObject*> seen;
    double cool = std::max(0.1, (double)s_cfgCoolMinutes.Value()) * 60, now = Now();
    for (GUObject* p : s_world.people) {
        Object a(p);
        if (!a || (a == me && !s_cfgSelfHeat.Value())) continue;
        seen.insert(a.ptr);
        HeatTarget& t = s_heat[a.ptr];
        bool dead = Is(a, "BP_PlayerRagdoll_C") || IsDead(a);
        if (dead && t.diedAt < 0) t.diedAt = now;
        GUObject* mat = s_heatMat;
        if (t.diedAt >= 0) {
            double left = 1 - (now - t.diedAt) / cool;
            if (!t.mid) t.mid = API->CreateMaterialInstance(s_blend);
            if (t.mid) {
                float h = HeatLevel() * (float)(0.05 + 0.95 * std::max(0.0, left) * std::max(0.0, left)),
                      al = s_cfgHeatAlpha.Value() * (float)std::max(0.0, left);
                float c[4] = {h, h, h, al};
                API->SetMaterialVector(t.mid, "BaseColorFactor", c);
                mat = left > 0 ? t.mid : nullptr;
            }
        }
        if (t.meshesAt < 0 || now - t.meshesAt > 5) { t.meshes = GetComps(a, "SkeletalMeshComponent"); t.meshesAt = now; }
        for (Object c : t.meshes) {
            if (!c || !Drawn(a, c)) continue;
            auto known = std::find_if(t.comps.begin(), t.comps.end(), [&](auto& p) { return p.first == c; });
            if (known == t.comps.end()) {
                Params g(c, "GetOverlayMaterial");
                g.Call();
                t.comps.push_back({c, g.Return<GUObject*>()});
                known = t.comps.end() - 1;
            }
            GUObject* cur = c.Get<GUObject*>("OverlayMaterial");
            GUObject* target = mat ? mat : known->second;
            if (cur != target) SetOverlay(c, target);
        }
    }
    for (auto it = s_heat.begin(); it != s_heat.end();) {
        if (seen.count(it->first)) { ++it; continue; }
        it = s_heat.erase(it);  // gone from the world
    }
}

// ------------------------------------------------------------------ the fusion goggle's ATAK HUD
//
// One head-locked plane in front of the eyes carries everything, drawn each frame into a render target: the
// compass tape (heading, cardinal points, bearings of team and waypoints), team markers and waypoints where
// they are in view (an arrow at the edge when not), range, time. It is pushed out to [HUD] Distance but pulled
// in before anything closer in its way, at the same angular size, so it never sinks into a wall; the NVG's
// post-process turns it into the phosphor's colour.

struct Hud {
    Object comp;
    GUObject* actor = nullptr;
    double dist = 150;
};
static Hud s_hud;

struct Waypoint { V3 at; std::string label; int shape; };  // shape 0 team, 1 waypoint, 2 extraction, 3 objective
static bool s_userWpSet = false;
static V3 s_userWp;

struct Canvas {
    Object c;
    V2 size;
    float bright = 1;
    Color Col(float a = 1) const { return {bright, bright, bright, a}; }
    void Line(V2 a, V2 b, double t = 2, float alpha = 1) {
        Params p(c, "K2_DrawLine");
        p.Set("ScreenPositionA", a).Set("ScreenPositionB", b).Set("Thickness", (float)t).Set("RenderColor", Col(alpha)).Call();
    }
    // Text from the HUD font, `scale` 1 = 26 px tall glyph cells; centred on `at` unless !centre (then its top left).
    void Text(const std::string& s, V2 at, double scale = 1, bool centre = true) {
        if (!s_font || s.empty()) return;
        double k = scale * 26.0 / kCellH * 1.6;
        double gw = kCellW * k, gh = kCellH * k, adv = s_glyphAdvance * k;
        double x = centre ? at.x - adv * s.size() / 2 - (gw - adv) / 2 : at.x, y = centre ? at.y - gh * 0.45 : at.y;
        for (size_t i = 0; i < s.size(); i++) {
            int c = (unsigned char)s[i];
            if (c <= 32 || c >= 128) continue;
            int g = c - 32;
            Params p(this->c, "K2_DrawTexture");
            p.SetObj("RenderTexture", Object(s_font)).Set("ScreenPosition", V2{x + i * adv, y}).Set("ScreenSize", V2{gw, gh})
                .Set("CoordinatePosition", V2{(double)(g % kGlyphCols) / kGlyphCols, (double)(g / kGlyphCols) / kGlyphRows})
                .Set("CoordinateSize", V2{1.0 / kGlyphCols, 1.0 / kGlyphRows}).Set("RenderColor", Col()).Set("BlendMode", (uint8_t)1)
                .Set("Rotation", 0.0f).Set("PivotPoint", V2{0.5, 0.5}).Call();
        }
    }
    void Diamond(V2 at, double r, double t = 2.5) {
        V2 n{at.x, at.y - r}, e{at.x + r, at.y}, s{at.x, at.y + r}, w{at.x - r, at.y};
        Line(n, e, t); Line(e, s, t); Line(s, w, t); Line(w, n, t);
    }
    void Chevron(V2 at, double r, double t = 2.5) {  // an inverted triangle with a stem: a waypoint
        V2 a{at.x - r, at.y - r * 1.4}, b{at.x + r, at.y - r * 1.4}, tip{at.x, at.y};
        Line(a, b, t); Line(b, tip, t); Line(tip, a, t);
    }
    void Square(V2 at, double r, double t = 2.5) {
        V2 a{at.x - r, at.y - r}, b{at.x + r, at.y - r}, cc{at.x + r, at.y + r}, d{at.x - r, at.y + r};
        Line(a, b, t); Line(b, cc, t); Line(cc, d, t); Line(d, a, t);
    }
    void Arrow(V2 at, double ang, double r) {  // pointing along `ang` (screen radians, 0 = right, y down)
        V2 tip{at.x + std::cos(ang) * r, at.y + std::sin(ang) * r};
        V2 l{at.x + std::cos(ang + 2.5) * r, at.y + std::sin(ang + 2.5) * r}, rr{at.x + std::cos(ang - 2.5) * r, at.y + std::sin(ang - 2.5) * r};
        Line(tip, l, 3); Line(l, rr, 3); Line(rr, tip, 3);
    }
};

static std::string Range(double cm) {
    double m = cm / 100;
    char b[32];
    if (m >= 1000) snprintf(b, sizeof b, "%.1fkm", m / 1000);
    else snprintf(b, sizeof b, "%dm", (int)std::lround(m));
    return b;
}
static const char* Cardinal(int deg) {
    switch (((deg % 360) + 360) % 360) {
        case 0: return "N"; case 45: return "NE"; case 90: return "E"; case 135: return "SE";
        case 180: return "S"; case 225: return "SW"; case 270: return "W"; case 315: return "NW";
    }
    return nullptr;
}

static std::vector<Waypoint> Points(Object me) {
    std::vector<Waypoint> pts;
    if (s_cfgTeam.Value())
        for (GUObject* pp : s_world.players) {
            Object p(pp);
            if (!p || p == me || IsDead(p)) continue;
            // their name, asked for once (the engine allocates the string, and a plugin can't free it)
            static std::map<GUObject*, std::string> names;
            std::string& name = names[p.ptr];
            if (name.empty()) {
                name = "TEAM";
                if (Object ps = p.GetObj("PlayerState")) {
                    Params n(ps, "GetPlayerName");
                    if (n) { n.Call(); std::string s = FStringUtf8(n.Return<GML_FString>()); if (!s.empty()) name = s; }
                }
                for (auto& ch : name) ch = (char)std::toupper((unsigned char)ch);
                if (name.size() > 14) name.resize(14);
            }
            pts.push_back({ActorLoc(p) + V3{0, 0, 95}, name, 0});
        }
    if (s_cfgWaypoints.Value()) {
        for (GUObject* z : s_world.exfil) if (Alive(z)) pts.push_back({ActorLoc(z), "EXFIL", 2});
        if (Object gm = Alive(s_world.mission) ? Object(s_world.mission) : Object())
            if (Object obj = gm.GetObj("Objective")) pts.push_back({ActorLoc(obj), "OBJ", 3});
        if (s_userWpSet) pts.push_back({s_userWp, "WP1", 1});
    }
    return pts;
}

static void DrawHud(Object cam, Object me, double hfov) {
    GUObject* rt = HudRT();
    if (!rt) return;
    Params clr(Lib("KismetRenderingLibrary"), "ClearRenderTarget2D");
    clr.SetObj("WorldContextObject", Object(API->WorldContext())).SetObj("TextureRenderTarget", Object(rt)).Set("ClearColor", Color{0, 0, 0, 0}).Call();
    Params b(Lib("KismetRenderingLibrary"), "BeginDrawCanvasToRenderTarget");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).SetObj("TextureRenderTarget", Object(rt)).Call();
    Canvas cv;
    cv.c = b.Get<GUObject*>("Canvas");
    cv.size = b.Get<V2>("Size");
    cv.bright = s_cfgHudBright.Value();
    GML_PropInfo ci;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetRenderingLibrary").Struct(), "BeginDrawCanvasToRenderTarget"), "Context"), &ci);
    std::vector<uint8_t> ctx((uint8_t*)b.Ptr("Context"), (uint8_t*)b.Ptr("Context") + ci.size);
    if (cv.c) {
        double W = cv.size.x, H = cv.size.y;
        double tw = std::tan(hfov * kDeg / 2), th = tw * H / W;  // the plane's half size at distance 1
        V3 eye = CompLoc(cam);
        Quat q = ToQuat(CompRot(cam));
        Rot cr = CompRot(cam);
        double heading = Wrap360(cr.yaw - s_cfgNorthYaw.Value());
        bool atak = !s_cfgRequireAtak.Value() || (GearManager(me) && GearManager(me).GetObj("ATAK"));

        // The compass tape across the top: `span` degrees wide, ticks every 5, numbers every 15.
        double span = Clamp(s_cfgTapeSpan.Value(), 30, 180), tapeY = H * 0.09, tapeW = W * 0.62, x0 = (W - tapeW) / 2;
        auto tapeX = [&](double bearing) { return W / 2 + WrapDeg(bearing - heading) / span * tapeW; };
        cv.Line({x0, tapeY + 18}, {x0 + tapeW, tapeY + 18}, 1.5, 0.6f);
        for (int d = (int)std::floor((heading - span / 2) / 5) * 5; d <= heading + span / 2; d += 5) {
            double x = tapeX(d);
            if (x < x0 || x > x0 + tapeW) continue;
            int dd = ((d % 360) + 360) % 360;
            bool major = dd % 15 == 0;
            cv.Line({x, tapeY + 18}, {x, tapeY + (major ? 4 : 10)}, major ? 2.5 : 1.5);
            if (const char* c = Cardinal(dd)) cv.Text(c, {x, tapeY - 14}, dd % 90 == 0 ? 1.5 : 1.15);
            else if (major) { char t[8]; snprintf(t, sizeof t, "%03d", dd); cv.Text(t, {x, tapeY - 12}, 0.95); }
        }
        // the lubber line and the heading
        cv.Line({W / 2, tapeY + 22}, {W / 2 - 9, tapeY + 36}, 2.5);
        cv.Line({W / 2, tapeY + 22}, {W / 2 + 9, tapeY + 36}, 2.5);
        char hd[16];
        snprintf(hd, sizeof hd, "%03d", (int)std::lround(heading) % 360);
        cv.Square({W / 2, tapeY + 58}, 22, 2);
        cv.Text(hd, {W / 2, tapeY + 58}, 1.35);

        // Team and waypoints: on the tape by bearing, in the picture where they are.
        std::vector<Waypoint> pts = atak ? Points(me) : std::vector<Waypoint>{};
        double range = std::max(10.0, (double)s_cfgMarkerRange.Value()) * 100;
        for (auto& p : pts) {
            V3 d = p.at - eye;
            double dist = Len(d);
            if (p.shape == 0 && dist > range) continue;
            double bearing = Wrap360(std::atan2(d.y, d.x) / kDeg - s_cfgNorthYaw.Value());
            double tx = tapeX(bearing);
            if (tx >= x0 && tx <= x0 + tapeW) {
                if (p.shape == 0) cv.Diamond({tx, tapeY + 30}, 6, 2);
                else cv.Chevron({tx, tapeY + 34}, 6, 2);
            }
            V3 l = Unrotate(q, d);  // camera axes
            double u = l.x > 1 ? l.y / l.x / tw : 1e9, v = l.x > 1 ? l.z / l.x / th : 1e9;
            bool inView = l.x > 1 && std::fabs(u) < 0.92 && std::fabs(v) < 0.86;
            if (inView) {
                V2 s{W / 2 + u * W / 2, H / 2 - v * H / 2};
                double r = p.shape == 0 ? 12 : 11;
                if (p.shape == 0) cv.Diamond(s, r);
                else if (p.shape == 3) cv.Square(s, r * 0.8);
                else cv.Chevron(s, r);
                cv.Text(p.label, {s.x, s.y - r - 18}, 0.95);
                cv.Text(Range(dist), {s.x, s.y + r + 14}, 0.85);
            } else {
                // an arrow at the edge, pointing the way to turn
                double ang = std::atan2(-l.z, l.y);
                if (l.x <= 1 && std::fabs(l.y) < 1e-3 && std::fabs(l.z) < 1e-3) ang = 0;
                V2 e{W / 2 + std::cos(ang) * W * 0.42, H / 2 + std::sin(ang) * H * 0.40};
                cv.Arrow(e, ang, 14);
                cv.Text(p.label + " " + Range(dist), {e.x - std::cos(ang) * 46, e.y - std::sin(ang) * 30}, 0.8);
            }
        }

        // Along the bottom: the link, the time, the thermal core.
        std::time_t tt = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &tt);
        char clock[16];
        snprintf(clock, sizeof clock, "%02d:%02d", lt.tm_hour, lt.tm_min);
        cv.Text(clock, {W * 0.30, H * 0.93}, 1.0);
        cv.Text(atak ? "ATAK LINK" : "NO ATAK", {W / 2, H * 0.93}, 1.0);
        cv.Text(s_cfgThermal.Value() ? "WHT HOT" : "THERM OFF", {W * 0.70, H * 0.93}, 1.0);
        if (s_userWpSet && atak) {
            double d = Len(s_userWp - eye);
            double brg = Wrap360(std::atan2(s_userWp.y - eye.y, s_userWp.x - eye.x) / kDeg - s_cfgNorthYaw.Value());
            char t[48];
            snprintf(t, sizeof t, "WP1 %03d  %s", (int)std::lround(brg) % 360, Range(d).c_str());
            cv.Text(t, {W / 2, H * 0.865}, 1.0);
        }
    }
    Params e(Lib("KismetRenderingLibrary"), "EndDrawCanvasToRenderTarget");
    e.SetObj("WorldContextObject", Object(API->WorldContext()));
    std::memcpy(e.Ptr("Context"), ctx.data(), ctx.size());
    e.Call();
}

static void HudOff() {
    if (s_hud.comp) SetVisible(s_hud.comp, false);
}

static void UpdateHud(double dt) {
    bool want = s_cfgHud.Value() && s_kind == KindFusion && Active() && s_meshHud;
    Object me = PlayerPawn();
    Object cam = VRCamera(me);
    if (!want || !cam) { HudOff(); return; }
    if (!s_hud.comp || s_hud.actor != s_nvg.ptr || !Alive(s_hud.comp.ptr)) {
        if (s_hud.comp && Alive(s_hud.comp.ptr)) Destroy(s_hud.comp);
        GUObject* parts[1] = {s_hudMat};
        s_hud = Hud{};
        s_hud.comp = API->AddDynamicMeshComponent(s_nvg.ptr, s_meshHud, parts, 1);
        s_hud.actor = s_nvg.ptr;
        if (!s_hud.comp) return;
        Attach(s_hud.comp, cam, 2);
        Params o(s_hud.comp, "SetOnlyOwnerSee"); o.Set("bNewOnlyOwnerSee", true).Call();
        Params sh(s_hud.comp, "SetCastShadow"); sh.Set("NewCastShadow", false).Call();
        Params tp(s_hud.comp, "SetTranslucentSortPriority"); tp.Set("NewTranslucentSortPriority", (int32_t)1000).Call();
        s_hud.comp.SetBool("bHiddenInSceneCapture", true);
        Log("fusion HUD on, {:.0f} deg wide", s_cfgHudFov.Value());
    }
    SetVisible(s_hud.comp, true);
    // Its distance: as far as [HUD] Distance, nearer when something is in the way (the centre and four corners).
    double farCm = Clamp(s_cfgHudDistance.Value(), 30, 1000);
    V3 eye = CompLoc(cam);
    Quat q = ToQuat(CompRot(cam));
    double tw = std::tan(s_cfgHudFov.Value() * kDeg / 2), th = tw * kHudH / kHudW;
    std::vector<GUObject*> ignore = s_mine;
    ignore.push_back(s_nvg.ptr);
    double nearest = farCm;
    for (V2 c : {V2{0, 0}, V2{-tw, th}, V2{tw, th}, V2{-tw, -th}, V2{tw, -th}}) {
        V3 dir = Rotate(q, V3{1, c.x, c.y});
        double l = Len(dir);
        double hit = Trace(eye + dir * 10, eye + dir * farCm, ignore);  // from 10 cm out: past the goggle itself
        if (hit >= 0) nearest = std::min(nearest, (hit + 10 * l) / l);  // along the view axis
    }
    double target = Clamp(nearest * 0.85, 25, farCm);
    s_hud.dist = target < s_hud.dist ? target : s_hud.dist + (target - s_hud.dist) * std::min(1.0, dt * 3);
    SetRel(s_hud.comp, {});
    SetScale(s_hud.comp, s_hud.dist / 100);
    DrawHud(cam, me, s_cfgHudFov.Value());
}

// ---- dropping a waypoint: hold the left thumbstick in (looking at the spot; at the sky to clear it)

static bool s_wpDown = false, s_wpFired = false;
static double s_wpHeld = 0;

static void DropWaypoint() {
    Object me = PlayerPawn();
    Object cam = VRCamera(me);
    if (!cam) return;
    V3 eye = CompLoc(cam);
    V3 dir = Rotate(ToQuat(CompRot(cam)), V3{1, 0, 0});
    V3 at;
    std::vector<GUObject*> ignore = s_mine;
    if (s_nvg) ignore.push_back(s_nvg.ptr);
    if (Trace(eye, eye + dir * 200000, ignore, &at) >= 0) {
        s_userWp = at;
        s_userWpSet = true;
        Log("waypoint WP1 dropped at ({:.0f}, {:.0f}, {:.0f}), {:.0f} m away", at.x, at.y, at.z, Len(at - eye) / 100);
    } else if (s_userWpSet) {
        s_userWpSet = false;
        Log("waypoint WP1 cleared");
    }
}

// ------------------------------------------------------------------ tick

static void Tick(double dt) {
    dt = Clamp(dt, 0, 0.1);
    static double check = 0, heat = 0, entries = 0, scan = 0;
    if ((entries -= dt) <= 0) { entries = 1; AddEntries(); }
    static double mine = 0;
    if ((scan -= dt) <= 0) { scan = 0.5; ScanWorld(); }
    if (s_kind == KindFusion && (mine -= dt) <= 0) { mine = 3; ScanMine(); }  // what the HUD's traces look past
    if ((check -= dt) <= 0) { check = 0.25; ConvertWorn(); CheckGoggles(); }
    if ((heat -= dt) <= 0 || (s_heatOn != (s_cfgThermal.Value() && Active()))) { heat = 0.5; UpdateHeat(); }
    if (s_kind == KindFusion) {
        if (s_wpDown && !s_wpFired && (s_wpHeld += dt) >= s_cfgWpHold.Value()) { s_wpFired = true; DropWaypoint(); }
        UpdateHud(dt);
    } else HudOff();
}

// ------------------------------------------------------------------ dev (tests\ECOTIDev)

extern "C" __declspec(dllexport) GUObject* ECOTI_Entry(int i) { return i >= 0 && i < (int)s_entries.size() ? s_entries[i].item : nullptr; }

extern "C" __declspec(dllexport) void ECOTI_Dev(const char* cmd) {
    std::string c = cmd ? cmd : "";
    Object gm = GearManager(PlayerPawn());
    if (c == "state") {
        Object nvg = gm ? gm.GetObj("NVG") : Object();
        Object lever = nvg ? nvg.GetObj("NVG") : Object();
        uint8_t* v = gm ? MapValue(gm, "GearByCategory", kCatNightVision) : nullptr;
        char col[64] = "";
        if (v) API->NameToString(*(uint64_t*)(v + GearColorOff()), col, sizeof col);
        Log("ECOTI_Dev state: worn {} '{}' kind {} | nvg {} lever {:.1f} up {:.2f} powered {} | active {} heat {} ({} people) | hud {} dist {:.0f} | wp {}",
            v && *(GUObject**)(v + GearDataOff()) ? Object(*(GUObject**)(v + GearDataOff())).Name() : "none", col, (int)s_kind,
            nvg ? nvg.Name() : "none", lever ? lever.Get<float>("CurrentLeverAngle") : 0.f, lever ? LeverUp(lever) : 0.0, nvg ? Powered(nvg) : false,
            Active(), s_heatOn, s_heat.size(), s_hud.comp && s_hud.comp.GetBool("bVisible"), s_hud.dist, s_userWpSet);
    } else if (c.rfind("wear ", 0) == 0) {  // "wear <entry index>": as if its tile were pressed
        size_t i = (size_t)atoi(c.c_str() + 5);
        if (i < s_entries.size()) WearPair(gm, s_entries[i].donor, s_entries[i].kind);
    } else if (c == "entries") {
        for (size_t i = 0; i < s_entries.size(); i++)
            Log("entry {}: {} '{}' on {}", i, s_entries[i].kind == KindFusion ? "fusion" : "ecoti", TextOf(Object(s_entries[i].item), "DisplayName"),
                DonorObj(s_entries[i].donor).Name());
    } else if (c == "down" || c == "up") {
        Object nvg = gm ? gm.GetObj("NVG") : Object();
        Object lever = nvg ? nvg.GetObj("NVG") : Object();
        if (!lever) { Warn("ECOTI_Dev {}: no NVG", c); return; }
        float lim = lever.Get<float>("LeverLimitNegative");
        float sign = lim > 0 ? -1.f : 1.f;
        Params p(lever, "SetLeverAngle");
        p.Set("NewAngle", c == "down" ? sign * std::fabs(lim) : 0.0f).Set("DualAxisForwardVector", V3{1, 0, 0}).Set("bAllowThrowingEvents", true).Call();
    } else if (c.rfind("fit ", 0) == 0) {  // "fit <ecoti|fusion> x y z": the offset from the housing's corner, live
        char kind[16] = "";
        V3 v;
        if (sscanf_s(c.c_str() + 4, "%15s %lf %lf %lf", kind, (unsigned)sizeof kind, &v.x, &v.y, &v.z) == 4) {
            Object nvg = gm ? gm.GetObj("NVG") : Object();
            if (std::string(kind) == "fusion") s_fusionFit = v;
            else s_ecotiFit[IsGpnvg(nvg) ? 0 : 1] = v;
            for (auto& [a, d] : s_dressed)
                if (Alive(a) && !d.comps.empty()) SetRel(d.comps[0], FitFor(Object(a), d.kind));
        }
    } else if (c == "wp") DropWaypoint();
    else if (c.rfind("wpat ", 0) == 0) {
        V3 v;
        if (sscanf_s(c.c_str() + 5, "%lf %lf %lf", &v.x, &v.y, &v.z) == 3) { s_userWp = v; s_userWpSet = true; }
    } else if (c == "heat") UpdateHeat();
    else Warn("ECOTI_Dev: unknown command '{}'", c);
}

// ------------------------------------------------------------------ plugin

GML_AWAKE() {
    s_cfgEcoti = Config.Bind("Gear", "ECOTI", true, "'<goggle> + ECOTI' entries for every GPNVG-18 and PVS-31 in the Night Vision gear (next launch).");
    s_cfgFusion = Config.Bind("Gear", "Fusion", true, "The 'Panoramic Fusion (ATAK)' goggle in the Night Vision gear (next launch).");
    s_cfgFusionPhosphor = Config.Bind("Gear", "FusionPhosphor", "WhitePhos", "The fusion goggle's tubes: Green, WhitePhos or WhiteFilmless (next launch).");
    s_cfgThermal = Config.Bind("Thermal", "Enabled", true, "People show white-hot through either goggle while it is down and on.");
    s_cfgHeat = Config.Bind("Thermal", "Heat", 1.0f, "How hot people look (0..4; 1 saturates the goggle's picture). Next launch.");
    s_cfgHeatAlpha = Config.Bind("Thermal", "Fusion", 0.7f, "How much of the thermal picture is fused over the tubes' image on a warm body (0..1). Next launch.");
    s_cfgCoolMinutes = Config.Bind("Thermal", "CoolMinutes", 4.0f, "Minutes a dead body takes to cool off to the background.");
    s_cfgSelfHeat = Config.Bind("Thermal", "SelfHeat", true, "Your own hands and arms are warm too.");
    s_cfgHud = Config.Bind("HUD", "Enabled", true, "The fusion goggle's ATAK HUD.");
    s_cfgHudFov = Config.Bind("HUD", "FOV", 64.0f, "How wide the HUD is, degrees across (the GPNVG-18 sees 97 x 40). Next launch.");
    s_cfgHudDistance = Config.Bind("HUD", "Distance", 300.0f, "How far away the HUD seems, cm (it comes nearer when something closer is in the way).");
    s_cfgHudBright = Config.Bind("HUD", "Brightness", 0.55f, "HUD brightness (the NVG's gain multiplies it).");
    s_cfgNorthYaw = Config.Bind("HUD", "NorthYaw", 0.0f, "Which world direction is north, as a yaw (0: the map's up, +X, as on the ATAK map).");
    s_cfgTapeSpan = Config.Bind("HUD", "CompassSpan", 120.0f, "Degrees of heading the compass tape shows.");
    s_cfgRequireAtak = Config.Bind("HUD", "RequireATAK", false, "Team markers and waypoints only while you carry an ATAK (the compass always works).");
    s_cfgTeam = Config.Bind("HUD", "TeamMarkers", true, "A marker on every other player, with name and range.");
    s_cfgMarkerRange = Config.Bind("HUD", "TeamRange", 1500.0f, "Team markers out to this range, m.");
    s_cfgWaypoints = Config.Bind("HUD", "Waypoints", true, "Extraction zones, the mission objective (when hosting) and your own waypoint.");
    s_cfgWpHold = Config.Bind("HUD", "WaypointHold", 0.6f, "Seconds to hold the left thumbstick in to drop a waypoint where you look (at the sky: clear it).");

    // A gear tile pressed: ours puts the stock goggle on through the menu, then the tagged pair (see "picking").
    const char* press = "WB_CustomizationItem_C:BndEvt__WB_CustomizationItem_Button_46_K2Node_ComponentBoundEvent_1_OnButtonPressedEvent__DelegateSignature";
    Hook(press,
         [](Object tile, GUFunction*, void*) {
             s_pick = {};
             const Entry* e = EntryOf(tile.Get<GUObject*>("OutfitItem"));
             if (!e) return false;
             s_pick = {tile.ptr, e->item};
             tile.Set("OutfitItem", DonorObj(e->donor).ptr);
             return false;
         },
         [](Object tile, GUFunction*, void*) {
             if (!s_pick.tile || s_pick.tile != tile.ptr) return;
             const Entry* e = EntryOf(s_pick.item);
             tile.Set("OutfitItem", s_pick.item);
             s_pick = {};
             if (e) WearPair(GearManager(PlayerPawn()), e->donor, e->kind);
         });
    // Waypoints: the left thumbstick held in (pressed / released).
    auto stick = [](bool down) {
        return [down](Object self, GUFunction*, void*) {
            if (self.ptr != PlayerPawn().ptr) return;
            s_wpDown = down;
            if (down) { s_wpHeld = 0; s_wpFired = false; }
        };
    };
    HookAfter("BP_PlayerCharacter_C:InpActEvt_LeftThumbStick_K2Node_InputActionEvent_2", stick(true));
    HookAfter("BP_PlayerCharacter_C:InpActEvt_LeftThumbStick_K2Node_InputActionEvent_3", stick(false));

    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) {
        BuildAssets();
        s_dressed.clear();
        s_heat.clear();
        s_heatOn = false;
        s_hud = Hud{};
        s_userWpSet = false;
        AddEntries();
    });
    On(GML_EVENT_TICK, [](void* data) { Tick(data ? *(float*)data : 1.0 / 90); });
    Message("ECOTI Thermal Fusion {} - ECOTI clip-ons and the Panoramic Fusion goggle in the Night Vision gear", ECOTI_VERSION);
    return 0;
}
