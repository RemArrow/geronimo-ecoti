// ECOTIDev - in-game test harness for the ECOTI Thermal Fusion plugin. Not shipped.
// Loads TrainingGrounds and checks, in order: the gear entries, the ECOTI on a GPNVG-18 and a PVS-31 (where it
// sits, from the side and the front; the PVS-31 one picked from a gear menu tile), the fusion goggle (a runtime
// entry put on directly: converted to the tagged stock goggle), then at night (lights and auto exposure down) an
// AI in front with the goggle down and on (heat, HUD, a waypoint), that AI killed (cooling), and an outfit saved
// and loaded with the fusion goggle. Without a headset the player's camera sits behind the goggle's tubes: for
// the wearer's view the goggle's model is hidden for the shot (which also drops the lens mask, so the picture
// isn't phosphor-coloured there). "SNAP <name>" = a game-rendered screenshot.
// The runner snapshots and restores SaveGames: the save test writes an outfit slot.
#define NOMINMAX
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>
#include <functional>

using namespace gml;

GML_PLUGIN("com.remarrow.ecotidev", "ECOTIDev", "0.1.0", {{"com.remarrow.ecoti", "0.1.0", GML_DEPENDENCY_SOFT}});

struct V3 { double x = 0, y = 0, z = 0; };
struct Rot { double pitch = 0, yaw = 0, roll = 0; };
static const char* kMap = "/Game/Maps/Training/TrainingGrounds";
static constexpr double kD2R = 3.14159265358979 / 180;

static void Dev(const char* cmd) {
    static auto fn = (void (*)(const char*))GetProcAddress(GetModuleHandleW(L"ECOTI.dll"), "ECOTI_Dev");
    if (fn) fn(cmd);
    else Error("ECOTI.dll's ECOTI_Dev not found");
}
static GUObject* Entry(int i) {
    static auto fn = (GUObject * (*)(int)) GetProcAddress(GetModuleHandleW(L"ECOTI.dll"), "ECOTI_Entry");
    return fn ? fn(i) : nullptr;
}
static void Snap(const char* n) { Log("SNAP {}", n); API->ExecConsoleCommand("shot"); }
static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static Object PC() {
    Params p(Lib("GameplayStatics"), "GetPlayerController");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static Object Gear() {
    Object pawn = Pawn();
    if (!pawn) return {};
    Params p(pawn, "GetComponentByClass");
    p.Set("ComponentClass", FindClass("BP_GearManagerComponent_C")).Call();
    return p.Return<GUObject*>();
}
static V3 CompLoc(Object c) { Params p(c, "K2_GetComponentLocation"); p.Call(); return p.Return<V3>(); }
static Rot CompRot(Object c) { Params p(c, "K2_GetComponentRotation"); p.Call(); return p.Return<Rot>(); }

static void SetItem(GUObject* item) {
    Object gm = Gear();
    if (!gm || !item) { Error("no gear manager / item"); return; }
    uint8_t buf[16] = {};
    *(GUObject**)buf = item;
    Params p(gm, "SetOutfitItem");
    std::memcpy(p.Ptr("GearItem"), buf, 16);
    p.Call();
}
static uint8_t* MapValue(Object obj, const char* prop, uint8_t key) {
    GFProperty* mp = API->FindProperty(obj.Struct(), prop);
    void* map = obj.PropPtr(prop);
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
static std::string Text(Object o, const char* prop) {
    char b[128] = "";
    if (void* t = o.PropPtr(prop)) API->TextToString(t, b, sizeof b);
    return b;
}
static GUObject* Worn(uint8_t cat) {
    Object gm = Gear();
    uint8_t* v = gm ? MapValue(gm, "GearByCategory", cat) : nullptr;
    return v ? *(GUObject**)v : nullptr;
}
static std::string WornColor(uint8_t cat) {
    Object gm = Gear();
    uint8_t* v = gm ? MapValue(gm, "GearByCategory", cat) : nullptr;
    char b[64] = "";
    if (v) API->NameToString(*(uint64_t*)(v + 8), b, sizeof b);
    return b;
}

static void ListCategory(uint8_t cat) {
    Object gi(API->WorldContext());
    uint8_t* v = MapValue(gi, "OutfitItems", cat);
    if (!v) { Log("category {}: none", cat); return; }
    auto* arr = (GML_TArray*)v;
    for (int i = 0; i < arr->Num; i++) {
        Object it(((GUObject**)arr->Data)[i]);
        Log("  cat {} [{}] {} '{}' / '{}'", cat, i, it.Name(), Text(it, "DisplayName"), Text(it, "Subtitle"));
    }
    Log("  worn in {}: {} '{}'", cat, Worn(cat) ? Object(Worn(cat)).Name() : "none", WornColor(cat));
}

// Where the NVG actor's parts are, from the eye.
static void NvgReport(const char* when) {
    Object gm = Gear();
    Object nvg = gm ? gm.GetObj("NVG") : Object();
    Object cam = Pawn() ? Pawn().GetObj("VRReplicatedCamera") : Object();
    if (!nvg || !cam) { Log("NVG [{}] none", when); return; }
    V3 c = CompLoc(cam);
    double t = CompRot(cam).yaw * kD2R, cp = std::cos(t), sp = std::sin(t);
    auto local = [&](V3 w) { V3 d{w.x - c.x, w.y - c.y, w.z - c.z}; return V3{cp * d.x + sp * d.y, -sp * d.x + cp * d.y, d.z}; };
    Log("NVG [{}] {} class {}", when, nvg.Name(), Object((GUObject*)nvg.Class()).Name());
    Params gc(nvg, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("SceneComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object comp(((GUObject**)arr->Data)[i]);
        V3 l = local(CompLoc(comp));
        V3 rl = comp.Get<V3>("RelativeLocation");
        Rot rr = comp.Get<Rot>("RelativeRotation");
        Params par(comp, "GetAttachParent"); par.Call();
        Object parent = par.Return<GUObject*>();
        std::string mesh = "-", box;
        if (comp.IsA("StaticMeshComponent")) {
            if (Object m = comp.GetObj("StaticMesh")) {
                mesh = m.Name();
                Params bb(m, "GetBoundingBox"); bb.Call();
                V3 mn = bb.Get<V3>("ReturnValue");
                V3 mx = *(V3*)((uint8_t*)bb.Ptr("ReturnValue") + 24);
                box = std::format(" box ({:.1f},{:.1f},{:.1f})..({:.1f},{:.1f},{:.1f})", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
            }
        }
        Log("  {} [{}] parent {} rel ({:.2f},{:.2f},{:.2f}) rot ({:.1f},{:.1f},{:.1f}) eye->({:.2f},{:.2f},{:.2f}) visible {} mesh {}{}", comp.Name(),
            Object((GUObject*)comp.Class()).Name(), parent ? parent.Name() : "-", rl.x, rl.y, rl.z, rr.pitch, rr.yaw, rr.roll, l.x, l.y, l.z,
            comp.GetBool("bVisible"), mesh, box);
    }
}

// Owner-only components (every NVG part) don't render for a camera of our own: show them to it.
static void OwnerSeeOff() {
    Object gm = Gear();
    Object nvg = gm ? gm.GetObj("NVG") : Object();
    if (!nvg) return;
    Params gc(nvg, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("PrimitiveComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Params o(c, "SetOnlyOwnerSee"); o.Set("bNewOnlyOwnerSee", false).Call();
        Params n(c, "SetOwnerNoSee"); n.Set("bNewOwnerNoSee", false).Call();
    }
}

static Object s_cam;
static void ViewFrom(V3 from, V3 at, float fov = 40) {
    if (!s_cam) {
        Params mk(Lib("KismetMathLibrary"), "MakeTransform");
        mk.Set("Location", from).Set("Rotation", Rot{}).Set("Scale", V3{1, 1, 1}).Call();
        GML_PropInfo xi;
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
        Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
        b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", FindClass("CameraActor"));
        std::memcpy(b.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
        b.Set("CollisionHandlingOverride", (uint8_t)1).Call();
        s_cam = b.Return<GUObject*>();
        Params f(Lib("GameplayStatics"), "FinishSpawningActor");
        f.SetObj("Actor", s_cam);
        std::memcpy(f.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
        f.Call();
    }
    if (Object cc = s_cam.GetObj("CameraComponent")) cc.Set("FieldOfView", fov);
    double dx = at.x - from.x, dy = at.y - from.y, dz = at.z - from.z;
    Rot look{std::atan2(dz, std::sqrt(dx * dx + dy * dy)) / kD2R, std::atan2(dy, dx) / kD2R, 0};
    Params mv(s_cam, "K2_SetActorLocationAndRotation");
    mv.Set("NewLocation", from).Set("NewRotation", look).Set("bSweep", false).Set("bTeleport", true).Call();
    Params vt(PC(), "SetViewTargetWithBlend");
    vt.SetObj("NewViewTarget", s_cam).Set("BlendTime", 0.0f).Call();
}
static void ViewPlayer() {
    Params vt(PC(), "SetViewTargetWithBlend");
    vt.SetObj("NewViewTarget", Pawn()).Set("BlendTime", 0.0f).Call();
}
// Our camera around the head: `fwd` cm ahead, `right` to the right, `up` above a point just in front of the eye.
static void ViewHead(double fwd, double right, double up, double fov = 30) {
    Object cam = Pawn().GetObj("VRReplicatedCamera");
    if (!cam) return;
    V3 c = CompLoc(cam);
    double t = CompRot(cam).yaw * kD2R;
    V3 at{c.x + std::cos(t) * 10, c.y + std::sin(t) * 10, c.z + 5};
    V3 from{at.x + std::cos(t) * fwd - std::sin(t) * right, at.y + std::sin(t) * fwd + std::cos(t) * right, at.z + up};
    ViewFrom(from, at, (float)fov);
}
static void BodyVisible(bool show) {
    Params gc(Pawn(), "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("SkeletalMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Params v(c, "SetVisibility"); v.Set("bNewVisibility", show).Set("bPropagateToChildren", false).Call();
    }
}

// An AI `dist` cm in front of the eye, facing it, with no controller (it stands; it doesn't fight).
static Object s_ai;
static void SpawnAI(double dist, double side = 0) {
    GUClass* cls = LoadClass("/Game/AI/Blueprints/BP_AICharacter.BP_AICharacter_C");
    Object cam = Pawn().GetObj("VRReplicatedCamera");
    if (!cls || !cam) { Error("no AI class / camera"); return; }
    V3 c = CompLoc(cam);
    double t = CompRot(cam).yaw * kD2R;
    {  // not inside a wall: short of the first thing in the way at chest height
        V3 from{c.x, c.y, c.z + 100}, to{c.x + std::cos(t) * (dist + 200) - std::sin(t) * side, c.y + std::sin(t) * (dist + 200) + std::cos(t) * side, c.z + 100};
        Params tr(Lib("KismetSystemLibrary"), "LineTraceSingle");
        std::vector<GUObject*> ign{Pawn().ptr};
        tr.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Start", from).Set("End", to).Set("TraceChannel", (uint8_t)0)
            .Set("bTraceComplex", false).Set("DrawDebugType", (uint8_t)0).Set("bIgnoreSelf", true);
        *(GML_TArray*)tr.Ptr("ActorsToIgnore") = GML_TArray{ign.data(), 1, 1};
        tr.Call();
        if (tr.Return<bool>()) {
            GML_PropInfo hit, d;
            API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetSystemLibrary").Struct(), "LineTraceSingle"), "OutHit"), &hit);
            API->GetPropertyInfo(API->FindProperty(hit.structType, "Distance"), &d);
            double h = *(float*)((uint8_t*)tr.Ptr("OutHit") + d.offset);
            if (h < dist + 150) { Log("something {:.0f} cm out: the AI stands nearer", h); dist = std::max(150.0, h - 150); side *= dist / (dist + 150); }
        }
    }
    V3 at{c.x + std::cos(t) * dist - std::sin(t) * side, c.y + std::sin(t) * dist + std::cos(t) * side, c.z};
    // stand it on the ground under that point
    Params tr(Lib("KismetSystemLibrary"), "LineTraceSingle");
    tr.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Start", V3{at.x, at.y, at.z + 50}).Set("End", V3{at.x, at.y, at.z - 1000})
        .Set("TraceChannel", (uint8_t)0).Set("bTraceComplex", false).Set("DrawDebugType", (uint8_t)0).Set("bIgnoreSelf", true);
    tr.Call();
    if (tr.Return<bool>()) {
        GML_PropInfo hit, imp;
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetSystemLibrary").Struct(), "LineTraceSingle"), "OutHit"), &hit);
        API->GetPropertyInfo(API->FindProperty(hit.structType, "ImpactPoint"), &imp);
        V3 g = *(V3*)((uint8_t*)tr.Ptr("OutHit") + imp.offset);
        at.z = g.z + 95;
    }
    Params mk(Lib("KismetMathLibrary"), "MakeTransform");
    mk.Set("Location", at).Set("Rotation", Rot{0, CompRot(cam).yaw + 180, 0}).Set("Scale", V3{1, 1, 1}).Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", cls);
    std::memcpy(b.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
    b.Set("CollisionHandlingOverride", (uint8_t)1).Call();
    s_ai = b.Return<GUObject*>();
    if (!s_ai) { Error("AI spawn failed"); return; }
    s_ai.Set("AutoPossessAI", (uint8_t)0);  // Disabled: no brain
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", s_ai);
    std::memcpy(f.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
    f.Call();
    Log("AI spawned: {} at ({:.0f}, {:.0f}, {:.0f})", s_ai.Name(), at.x, at.y, at.z);
}

// Night: every light down to almost nothing, so the goggles see as they do at night.
static void Night() {
    int n = 0;
    for (const char* cls : {"LightComponent", "SkyLightComponent"})
        for (Object l : FindAllOf(cls)) {
            float i = l.Get<float>("Intensity");
            Params p(l, "SetIntensity");
            if (!p) continue;
            p.Set("NewIntensity", i * 0.002f).Call();
            n++;
        }
    for (Object s : FindAllOf("SkyLightComponent")) { Params r(s, "RecaptureSky"); if (r) r.Call(); }
    // and no auto exposure, which would brighten the dark back up
    API->ExecConsoleCommand("r.EyeAdaptationQuality 0");
    API->ExecConsoleCommand("r.DefaultFeature.AutoExposure 0");
    Log("night: {} lights dimmed", n);
}

static V3 AiLoc() {
    if (!s_ai) return {};
    Params p(s_ai, "K2_GetActorLocation"); p.Call();
    return p.Return<V3>();
}
// From beside the eye, looking at the AI, at `fromAi` cm from it (or from the eye: fromAi < 0).
static void ViewAI(double fromAi, double fov = 40) {
    Object cam = Pawn().GetObj("VRReplicatedCamera");
    if (!s_ai || !cam) return;
    V3 a = AiLoc(), c = CompLoc(cam);
    c.z = a.z + 40;  // the headset lies on the floor: a standing eye's height
    V3 d{a.x - c.x, a.y - c.y, 0};
    double l = std::sqrt(d.x * d.x + d.y * d.y);
    V3 from = fromAi > 0 ? V3{a.x - d.x / l * fromAi, a.y - d.y / l * fromAi, c.z} : c;
    ViewFrom(from, V3{a.x, a.y, a.z + 20}, (float)fov);
}
// The goggle's model hidden (or shown again): without a headset the player's camera looks through its tubes'
// walls; hidden, the player's own view shows what the wearer sees (post-process, heat, HUD).
static void GoggleModel(bool show) {
    Object gm = Gear();
    Object nvg = gm ? gm.GetObj("NVG") : Object();
    Object mount = nvg ? nvg.GetObj("Mount") : Object();
    if (!mount) return;
    Params v(mount, "SetVisibility");
    v.Set("bNewVisibility", show).Set("bPropagateToChildren", true).Call();
}
// A gear menu tile for `item`, pressed (its button's bound event, as a click sends it).
static void PressTile(GUObject* item) {
    GUClass* cls = LoadClass("/Game/UI/Widgets/Customization/Outfit/WB_CustomizationItem.WB_CustomizationItem_C");
    if (!cls || !item) { Error("no item widget class / item"); return; }
    Params c(Lib("WidgetBlueprintLibrary"), "Create");
    c.SetObj("WorldContextObject", Object(API->WorldContext())).Set("WidgetType", cls).SetObj("OwningPlayer", PC()).Call();
    Object w = c.Return<GUObject*>();
    if (!w) { Error("could not create the item widget"); return; }
    Params s(w, "SetOutfitItem"); s.SetObj("OutfitItem", Object(item)).Call();
    Params press(w, "BndEvt__WB_CustomizationItem_Button_46_K2Node_ComponentBoundEvent_1_OnButtonPressedEvent__DelegateSignature");
    press.Call();
    Log("tile pressed: it carries {} again ({})", Object(w.Get<GUObject*>("OutfitItem")).Name(), w.Get<GUObject*>("OutfitItem") == item ? "OK" : "WRONG");
}
static void KillAI() {
    if (!s_ai) return;
    Params p(s_ai, "GetComponentByClass");
    p.Set("ComponentClass", FindClass("DarkHealthComponent")).Call();
    Object h = p.Return<GUObject*>();
    if (!h) { Error("AI has no health"); return; }
    Params k(h, "Kill");
    k.SetObj("InstigatedBy", Object()).SetObj("DamageCauser", Object()).Set("DamageTypeClass", (GUClass*)nullptr).Call();
    Log("AI killed: dead {}", h.GetBool("bIsDead"));
}
static void Overlays(const char* when) {
    if (!s_ai) return;
    Params gc(s_ai, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("SkeletalMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Object o = c.GetObj("OverlayMaterial");
        Log("  [{}] AI mesh {} overlay {}", when, c.Name(), o ? o.Name() : "none");
    }
}
// Look straight at the AI from the player's eye (the player camera is where the headset lies).
static void FaceAI() {
    Object cam = Pawn().GetObj("VRReplicatedCamera");
    if (!s_ai || !cam) return;
    Params p(s_ai, "K2_GetActorLocation"); p.Call();
    V3 a = p.Return<V3>(), c = CompLoc(cam);
    Log("eye ({:.0f}, {:.0f}, {:.0f}) AI ({:.0f}, {:.0f}, {:.0f})", c.x, c.y, c.z, a.x, a.y, a.z);
}

// ------------------------------------------------------------------ the script

struct Step { double wait; std::function<void()> fn; };
static std::vector<Step> s_steps;
static size_t s_at = 0;
static double s_wait = -1;

static void Fit(const char* name) {
    NvgReport(name);
    OwnerSeeOff();
    BodyVisible(false);
}

static void Script() {
    auto add = [](double wait, std::function<void()> f) { s_steps.push_back({wait, std::move(f)}); };
    add(0, [] {
        Log("--- gear: helmets and night vision");
        Dev("entries");
        ListCategory(19);
        Object helmet(Worn(5));
        if (!helmet || !helmet.Get<bool>("bAllowsNVG")) {
            Object gi(API->WorldContext());
            auto* arr = (GML_TArray*)MapValue(gi, "OutfitItems", 5);
            for (int i = 0; arr && i < arr->Num; i++) {
                Object h(((GUObject**)arr->Data)[i]);
                if (h.Get<bool>("bAllowsNVG")) { Log("putting on {} (it takes NVGs)", h.Name()); SetItem(h.ptr); break; }
            }
        }
    });
    // ECOTI on the GPNVG-18 (entry 0), flipped up as the game puts it on
    add(1.5, [] { Log("--- GPNVG-18 + ECOTI"); Dev("wear 0"); });
    add(1.5, [] { Dev("state"); Fit("gpnvg ecoti"); ViewHead(0, 60, 6); });
    add(0.6, [] { Snap("gpnvg_ecoti_side"); });
    add(0.3, [] { ViewHead(55, 22, 14); });
    add(0.6, [] { Snap("gpnvg_ecoti_front"); });
    add(0.3, [] { Dev("down"); });
    add(1.0, [] { Dev("state"); OwnerSeeOff(); ViewHead(0, 60, 0); });
    add(0.6, [] { Snap("gpnvg_ecoti_down_side"); });
    add(0.3, [] { ViewHead(45, 15, 4); });
    add(0.6, [] { Snap("gpnvg_ecoti_down_front"); });
    add(0.3, [] { Dev("up"); });
    // ECOTI on the PVS-31 (entry 3)
    add(0.8, [] { Log("--- PVS-31 + ECOTI, picked from a gear menu tile"); PressTile(Entry(3)); });
    add(1.5, [] { Log("worn after the tile: {} '{}'", Worn(19) ? Object(Worn(19)).Name() : "none", WornColor(19)); Dev("state"); Fit("pvs31 ecoti"); ViewHead(0, 60, 6); });
    add(0.6, [] { Snap("pvs31_ecoti_side"); });
    add(0.3, [] { ViewHead(55, 22, 14); });
    add(0.6, [] { Snap("pvs31_ecoti_front"); });
    add(0.3, [] { Dev("down"); });
    add(1.0, [] { Dev("state"); OwnerSeeOff(); ViewHead(45, 15, 4); });
    add(0.6, [] { Snap("pvs31_ecoti_down_front"); });
    add(0.3, [] { Dev("up"); });
    // the fusion goggle (entry 6), put on as its runtime entry directly: it must turn into the tagged pair
    add(0.8, [] { Log("--- fusion goggle, worn as the runtime entry (converted)"); SetItem(Entry(6)); });
    add(1.5, [] {
        Log("worn after conversion: {} '{}'", Worn(19) ? Object(Worn(19)).Name() : "none", WornColor(19));
        Dev("state");
        Fit("fusion");
        ViewHead(0, 60, 6);
    });
    add(0.6, [] { Snap("fusion_side"); });
    add(0.3, [] { ViewHead(55, 22, 14); });
    add(0.6, [] { Snap("fusion_front"); });
    add(0.3, [] { BodyVisible(true); ViewPlayer(); Night(); Log("--- night; an AI 6 m ahead, the goggle down and on"); SpawnAI(600); });
    add(1.0, [] { FaceAI(); ViewAI(300); });
    add(0.6, [] { Snap("ai_plain"); });
    add(0.3, [] { ViewPlayer(); Dev("down"); });
    add(1.5, [] { Dev("state"); Overlays("fusion on"); ViewAI(300); });
    add(0.6, [] { Snap("ai_heat_outside"); });
    add(0.3, [] { ViewPlayer(); GoggleModel(false); });
    add(0.8, [] { Snap("ai_nvg"); });
    add(0.3, [] { Dev("wpat 0 0 0"); });
    add(0.3, [] {
        // a waypoint 25 m ahead and to the left
        Object cam = Pawn().GetObj("VRReplicatedCamera");
        V3 c = CompLoc(cam);
        double t = CompRot(cam).yaw * kD2R;
        std::string cmd = std::format("wpat {:.0f} {:.0f} {:.0f}", c.x + std::cos(t) * 2500 + std::sin(t) * 800, c.y + std::sin(t) * 2500 - std::cos(t) * 800, c.z - 100);
        Dev(cmd.c_str());
    });
    add(0.8, [] { Dev("state"); Snap("fusion_view"); });
    add(0.3, [] { GoggleModel(true); Dev("up"); });
    add(1.0, [] { Snap("night_no_nvg"); });
    add(0.3, [] { Dev("down"); });
    add(1.0, [] { Dev("state"); });
    add(0.3, [] { Object cam = Pawn().GetObj("VRReplicatedCamera"); V3 c = CompLoc(cam); ViewFrom({c.x - 250, c.y - 250, c.z + 120}, c, 60); });
    add(0.6, [] { Snap("fusion_view_outside"); });
    add(0.3, [] { ViewPlayer(); Log("--- the AI killed: it cools"); KillAI(); });
    add(1.5, [] { Dev("heat"); Overlays("dead"); Dev("state"); GoggleModel(false); });
    add(0.8, [] { Snap("ai_dead_nvg"); });
    add(0.3, [] { GoggleModel(true); });
    add(0.3, [] { Dev("up"); });
    add(1.0, [] { Dev("state"); Overlays("goggle up"); ViewAI(300); });
    add(0.6, [] { Snap("ai_goggle_up"); });
    add(0.3, [] { ViewPlayer(); });
    add(0.3, [] { Log("--- GPNVG-18 + ECOTI down: heat, no HUD"); Dev("wear 1"); });
    add(1.5, [] { SpawnAI(500, 120); });
    add(0.5, [] { Dev("down"); });
    add(1.5, [] { Dev("state"); Overlays("ecoti on"); GoggleModel(false); });
    add(0.8, [] { Snap("ecoti_nvg"); });
    add(0.3, [] { GoggleModel(true); });
    add(0.3, [] { Dev("up"); });
    add(0.8, [] {
        Log("--- outfit save and load with the fusion goggle");
        Dev("wear 6");
    });
    add(1.0, [] {
        Params s(Gear(), "SaveOutfit"); s.Set("Preset", (int32_t)7).Call();
        Log("worn at save: {} '{}'", Worn(19) ? Object(Worn(19)).Name() : "none", WornColor(19));
        SetItem(LoadObject("/Game/Characters/Data/DA_NightVision_PVS31_Green.DA_NightVision_PVS31_Green").ptr);
    });
    add(1.0, [] {
        Log("worn before load: {} '{}'", Worn(19) ? Object(Worn(19)).Name() : "none", WornColor(19));
        Params l(Gear(), "LoadOutfit"); l.Set("Preset", (int32_t)7).Call();
    });
    add(1.5, [] {
        Log("worn after load: {} '{}'", Worn(19) ? Object(Worn(19)).Name() : "none", WornColor(19));
        Dev("state");
        Params del(Lib("GameplayStatics"), "DeleteGameInSlot");
        del.SetString("SlotName", L"SavedOutfit7").Set("UserIndex", (int32_t)0).Call();
        Log("test outfit slot deleted: {}", del.Return<bool>());
        ListCategory(19);
        Log("DEV TEST COMPLETE");
    });
}

GML_AWAKE() {
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName(kMap)).Set("bAbsolute", true).Call();
        } else if (level.find(kMap) == 0 && s_steps.empty()) {
            Script();
            s_at = 0;
            s_wait = 7;
        }
    });
    On(GML_EVENT_TICK, [](void* data) {
        if (s_wait < 0 || s_at >= s_steps.size()) return;
        if ((s_wait -= data ? *(float*)data : 1.0 / 90) > 0) return;
        s_steps[s_at].fn();
        s_at++;
        s_wait = s_at < s_steps.size() ? s_steps[s_at].wait : -1;
    });
    return 0;
}
