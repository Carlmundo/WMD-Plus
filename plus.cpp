#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"

// Windows x86 / Steam build 24946469. All addresses below are RVAs.
static uintptr_t base;
static bool installed, enabled;
static Settings settings;
static bool rapidPlayEnabled() { return enabled && settings.rapidPlay; }
static bool skipIntrosEnabled() { return enabled && settings.skipIntros; }
static HANDLE logFile = INVALID_HANDLE_VALUE;
static void* currentTurn;
static void* lastTurn;
static int lastMain = -1, lastSub = -1;
static BYTE* trampoline;
static HMODULE selfModule;
static bool instantPlacementContext;
struct PlacedNameplate { void* manager; void* worm; DWORD frame; };
static PlacedNameplate placedNameplates[64];
template<class T> static T& field(void* p, unsigned off) {
    return *reinterpret_cast<T*>(reinterpret_cast<BYTE*>(p) + off);
}
template<class T> static T fn(unsigned rva) {
#ifdef WMD_PLUS_TESTS
    return reinterpret_cast<T>(testAddress(rva));
#else
    return reinterpret_cast<T>(base + rva);
#endif
}
static void log(const char* event, int a = 0, int b = 0) {
    if (logFile != INVALID_HANDLE_VALUE) {
        char line[160]; int length = sprintf_s(line,"%lu %s %d %d\r\n",GetTickCount(),event,a,b);
        DWORD written; WriteFile(logFile,line,length,&written,nullptr);
    }
}
using UpdateFn = bool(__thiscall*)(void*);
using StartDeathFn = bool(__thiscall*)(void*, unsigned);
using PendingDeathFn = int(__thiscall*)(void*);
using ActivityFn = bool(__thiscall*)(void*);
using ReactionFn = void(__thiscall*)(void*, float);
using SpeechDelayFn = bool(__thiscall*)(void*, float);
static UpdateFn updateOriginal;

using FlowFindFn = void*(__thiscall*)(void*, unsigned);
static void* __fastcall StartupFlowTarget(void* system, void*, unsigned id) {
    auto find = fn<FlowFindFn>(0x3fe90);
    void* original = find(system, id);
    if (skipIntrosEnabled() && (id == 0x27110005 || id == 0x27110055)) {
        log("startup-flow-candidate", id & 0xffff, original ? field<unsigned>(original, 4) : 0);
        if (original) log("startup-flow-type", static_cast<int>(field<uintptr_t>(original, 0) - base));
    }
    if (!skipIntrosEnabled() || !original || field<unsigned>(original, 4) != id ||
        field<uintptr_t>(original, 0) != base + 0xeea7bc) return original;
    // Stock system.flw: both scenes have no callbacks or initialization work.
    // Follow each scene's existing Exit destination, retaining all surrounding
    // warnings, user discovery and menu setup. Only question routing is hooked.
    unsigned next = id == 0x27110005 ? 0x2711001d : // Logo Screen -> Autosave Warning
        id == 0x27110055 ? 0x27120006 : 0;         // Intro Video -> UI Mode
    if (!next) return original;
    void* destination = find(system, next);
    if (!destination || field<unsigned>(destination, 4) != next) return original;
    log("startup-flow-skip", id & 0xffff, next & 0xffff);
    return destination;
}

static bool __fastcall HudReady(void* hud, void*) {
    return rapidPlayEnabled() || fn<ActivityFn>(0x413100)(hud);
}

static bool __fastcall BlockingActivity(void* man, void*) {
    if (!rapidPlayEnabled() || !currentTurn) return fn<ActivityFn>(0x45b050)(man);
    auto worms = *fn<void**>(0xdcd5b3c);
    if (!worms) return fn<ActivityFn>(0x45b050)(man);
    unsigned count = field<unsigned>(man, 0x2c);
    unsigned wormCount = field<unsigned>(worms, 0x2c);
    if (count > 256 || wormCount > 64) return true;
    // Starting a death sets this flag before the engine marks its activity as
    // active. Keep at least that gameplay update before treating it as cosmetic.
    for (unsigned w = 0; w < wormCount; ++w) {
        void* worm = field<void*>(worms, 0x3c + w * 4);
        if (!worm) continue;
        int state = field<int>(worm, 0x80c);
        if ((state == 18 || state == 19) && field<BYTE>(worm, 0x1c9d)) return true;
    }
    for (unsigned i = 0; i < count; ++i) {
        BYTE* entry = static_cast<BYTE*>(man) + 0x34 + i * 0x18;
        if (!entry[4]) continue;
        bool cosmetic = false;
        for (unsigned w = 0; w < wormCount; ++w) {
            void* worm = field<void*>(worms, 0x3c + w * 4);
            if (!worm || field<int>(worm, 0x58) != field<int>(entry, 0)) continue;
            int state = field<int>(worm, 0x80c);
            // Death's explosion must have been submitted by Worm::Update first.
            // Match this worm's own configured WormDying activity, not a global kind.
            cosmetic = (state == 18 || state == 19) &&
                !field<BYTE>(worm, 0x1c9d) &&
                field<int>(entry, 8) == field<int>(worm, 0x1d10);
            break;
        }
        if (!cosmetic) return true;
    }
    return false;
}

static bool __fastcall StartDeaths(void* worms, void*, unsigned first) {
    bool result = fn<StartDeathFn>(0x36e670)(worms, first);
    log("death-start", first);
    if (!rapidPlayEnabled()) return result;
    // The stock selector excludes already dying/dead worms and performs the
    // stock kill bookkeeping. Do not duplicate its eligibility rules.
    unsigned count = field<unsigned>(worms, 0x2c);
    if (count > 64) return result;
    for (unsigned n = 0; n < count; ++n) {
        int next = fn<PendingDeathFn>(0x36d7b0)(worms);
        if (next < 0) break;
        // Guard against a refused state transition repeatedly returning one id.
        if (static_cast<unsigned>(next) == first) break;
        fn<StartDeathFn>(0x36e670)(worms, next);
        log("death-start", next);
        first = next;
    }
    return result;
}

static bool __fastcall UpdateDamage(void* turn, void*) {
    void* previous = currentTurn;
    currentTurn = turn;
    bool result = false;
    // Collapse only state transitions that stock would otherwise defer until
    // its next tick. Never repeat a state that is still waiting on gameplay.
    for (int i = 0; i < (rapidPlayEnabled() ? 16 : 1); ++i) {
        int main = field<int>(turn, 0x48), sub = field<int>(turn, 0x4c);
        if (lastTurn != turn || lastMain != main || lastSub != sub) {
            log("damage-state", main, sub); lastTurn = turn; lastMain = main; lastSub = sub;
        }
        if (rapidPlayEnabled() && sub == 1) field<float>(turn, 0x60) = 0.0f;
        result = updateOriginal(turn);
        if (result || field<int>(turn, 0x48) != main || field<int>(turn, 0x4c) == sub) break;
        // Let newly started deaths submit explosions on the next engine update.
        int nextSub = field<int>(turn, 0x4c);
        if (nextSub == 6 || nextSub == 8) break;
    }
    currentTurn = previous;
    return result;
}

// WaitReactions repeats the HUD gate after UpdateDamage has completed. Its
// SoundMan predicate only compares the remaining speech duration with delta;
// letting speech play in the background does not require changing sound state.
static bool __fastcall SpeechBlocking(void* sound, void*, float delta) {
    return !rapidPlayEnabled() && fn<SpeechDelayFn>(0x32cb10)(sound, delta);
}
static void __fastcall UpdateReactions(void* turn, void*, float delta) {
    void* previous = currentTurn;
    currentTurn = turn;
    if (lastTurn != turn || lastMain != 16) {
        log("reactions-start", field<int>(turn, 0x48));
        lastTurn = turn; lastMain = 16; lastSub = -1;
    }
    fn<ReactionFn>(0x471800)(turn, delta);
    if (field<int>(turn, 0x48) != 16) log("reactions-finished", field<int>(turn, 0x48));
    currentTurn = previous;
}

static void __fastcall UpdatePlacementDelay(void* placement, void*) {
    // Main 4 is active manual Teleport In; substate 5 is the post-placement
    // presentation wait. Its normal handler arms a deadline at now + 2.5 s.
    // An already expired deadline skips that timer while retaining the handler's
    // focus bookkeeping and its transition into normal activity/settling checks.
    bool skip = rapidPlayEnabled() && field<int>(placement, 0x78) == 4 &&
        field<int>(placement, 0x7c) == 5;
    if (skip) field<float>(placement, 0x90) = -1.0f;
    fn<void(__thiscall*)(void*)>(0x441c50)(placement);
    if (skip) log("placement-delay-skip", field<int>(placement, 0x84), field<int>(placement, 0x7c));
}

static void __fastcall UpdatePlacementSettling(void* placement, void*) {
    bool skip = rapidPlayEnabled() && field<int>(placement, 0x78) == 4 &&
        field<int>(placement, 0x7c) == 8;
    // Let stock finish checking that the worm has settled. Expire only the
    // newly armed handoff; state 9 keeps its normal bookkeeping on its next tick.
    fn<void(__thiscall*)(void*)>(0x441f20)(placement);
    if (!skip || field<int>(placement, 0x78) != 4 ||
        field<int>(placement, 0x7c) != 9) return;
    // A past deadline also works at simulation time zero. Do not advance the
    // placement dispatcher again here or change the next-player countdown.
    field<float>(placement, 0xd4) = -1.0f;
    log("placement-handoff-ms", 0, field<int>(placement, 0x84));
}

static DWORD placementFrame() {
    void* timer = *fn<void**>(0xdcd56c4);
    return timer ? field<DWORD>(timer, 0x44) : 0;
}
using PlacementEffectFn = HRESULT(__thiscall*)(void*, const char*, unsigned, const void*, const void*);
static HRESULT __fastcall PlacementEffect(void* effects, void*, const char* name,
    unsigned flags, const void* position, const void* color) {
    if (instantPlacementContext) return S_OK;
    return fn<PlacementEffectFn>(0x3530f0)(effects, name, flags, position, color);
}
using PlacementSoundFn = uintptr_t(__cdecl*)(const char*, const void*, uintptr_t, uintptr_t, uintptr_t);
static uintptr_t __cdecl PlacementSound(const char* name, const void* position,
    uintptr_t a, uintptr_t b, uintptr_t c) {
    if (instantPlacementContext) return 0;
    return fn<PlacementSoundFn>(0x32d160)(name, position, a, b, c);
}
static void __fastcall PlaceWorm(void* placement, void*, const void* position) {
    bool previous = instantPlacementContext;
    instantPlacementContext = rapidPlayEnabled() && field<int>(placement, 0x78) == 4 &&
        field<int>(placement, 0x7c) == 4;
    unsigned before = field<unsigned>(placement, 0x84);
    void* manager = nullptr; void* worm = nullptr; unsigned id = 64;
    if (instantPlacementContext) {
        manager = *fn<void**>(0xdcd5b3c);
        void* globals = *fn<void**>(0xdcd506c);
        if (manager && globals && field<unsigned>(manager, 0x2c) <= 64) {
            id = field<unsigned>(globals, 0x44);
            if (id < field<unsigned>(manager, 0x2c)) worm = field<void*>(manager, 0x3c + id * 4);
        }
    }
    // The original callback still validates the position, moves and activates
    // the worm, decrements the remaining count, and changes placement state.
    fn<void(__thiscall*)(void*, const void*)>(0x4413f0)(placement, position);
    if (instantPlacementContext && before > 0 &&
        field<unsigned>(placement, 0x84) == before - 1 && worm && id < 64) {
        placedNameplates[id] = {manager, worm, placementFrame()};
        log("placement-instant", id, before - 1);
    }
    instantPlacementContext = previous;
}
static void __fastcall UpdatePlacedNameplate(void* panel, void*) {
    fn<void(__thiscall*)(void*)>(0x4f37f0)(panel);
    // State 2 is the nameplate's slide-in; state 3 is fully visible. Match only
    // an accepted, recently placed worm. Other HUD states and vehicles stay stock.
    int id = field<int>(panel, 0x90);
    if (!rapidPlayEnabled() || id < 0 || id >= 64 || field<int>(panel, 0x64) != 2) return;
    auto& mark = placedNameplates[id];
    if (!mark.worm) return;
    if (placementFrame() - mark.frame > 300) { mark = {}; return; }
    void* manager = *fn<void**>(0xdcd5b3c);
    if (!manager || manager != mark.manager || field<unsigned>(manager, 0x2c) > 64 ||
        static_cast<unsigned>(id) >= field<unsigned>(manager, 0x2c) ||
        field<void*>(manager, 0x3c + id * 4) != mark.worm) { mark = {}; return; }
    void* camera = fn<void*(__cdecl*)()>(0x41a640)();
    if (!camera) return;
    // The stock slide targets the camera bounds' upper Y value (GetBounds +8).
    float target = field<float>(camera, 0x108);
    if (!(target > 0.0f && target < 1000000.0f)) return;
    field<float>(panel, 0x84) = target;
    field<int>(panel, 0x64) = 3;
    mark = {};
    log("placement-nameplate-snap", id);
}

#include "hats.h"
#include "unlock_all.h"
static bool write(void* at, const void* bytes, size_t len);

using PropertyAppendNodeFn = void*(__thiscall*)(void*, void*);
using ScriptReadFn = void**(__cdecl*)(void**, const char*);
static ScriptReadFn scriptReadOriginal;
static BYTE* scriptReadTrampoline;
static bool prepareHookTrampoline(BYTE*& stub, unsigned rva, const BYTE* prologue, unsigned size);
static void releasePropertyNode(void* node) {
    if (node) fn<HatNodeMethodFn>(0x856f00)(node);
}
static void assignPropertyNodeString(void* node, unsigned offset, const char* value, unsigned type = 3) {
    if (!value) value = "";
    fn<HatStringAssignFn>(0x858b80)(static_cast<BYTE*>(node) + offset, type, value,
        static_cast<unsigned>(strlen(value)));
}
static void* clonePropertyNode(void* source, unsigned depth = 0) {
    if (!source || depth > 12) return nullptr;
    auto begin = field<void**>(source, 0x18), end = field<void**>(source, 0x1c);
    if (end < begin || end - begin > 3000) return nullptr;
    const char* name = field<const char*>(source, 8);
    if (!name) return nullptr;
    void* result = createHatSource(name);
    if (!result) return nullptr;
    const char* value = field<const char*>(source, 12);
    // Preserve numeric token types so native integer/real readers keep working.
    assignPropertyNodeString(result, 12, value,
        value ? *reinterpret_cast<const unsigned*>(value - 12) : 3);
    for (; begin != end; ++begin) {
        void* child = clonePropertyNode(*begin, depth + 1);
        if (!child) { releasePropertyNode(result); return nullptr; }
        fn<PropertyAppendNodeFn>(0x853990)(result, child);
        releasePropertyNode(child);
    }
    return result;
}
static bool prepareHookTrampoline(BYTE*& stub, unsigned rva, const BYTE* prologue, unsigned size) {
    if (memcmp(reinterpret_cast<void*>(base + rva), prologue, size)) return false;
    if (stub) return true;
    stub = static_cast<BYTE*>(VirtualAlloc(nullptr, size + 5, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!stub) return false;
    memcpy(stub, prologue, size);
    BYTE* branch = stub + size;
    branch[0] = 0xe9;
    *reinterpret_cast<int32_t*>(branch + 1) = static_cast<int32_t>(base + rva + size - reinterpret_cast<uintptr_t>(branch) - 5);
    DWORD previous;
    if (!VirtualProtect(stub, size + 5, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(stub, 0, MEM_RELEASE); stub = nullptr; return false;
    }
    FlushInstructionCache(GetCurrentProcess(), stub, size + 5);
    return true;
}
#include "joanna_dark.h"

static bool prepareScriptReader() {
    const BYTE read[] = {0x55,0x8b,0xec,0x83,0xec,0x1c};
    if (!prepareHookTrampoline(scriptReadTrampoline, 0x856a30, read, sizeof read)) return false;
    scriptReadOriginal = reinterpret_cast<ScriptReadFn>(scriptReadTrampoline);
    return true;
}

static bool fastDropCrateConfig(void* source) {
    if (!source) return false;
    void* spawn = fn<JoannaFindNodeFn>(0x855890)(source, "ParachuteSpawnHeight", nullptr);
    void* release = fn<JoannaFindNodeFn>(0x855890)(source, "ParachuteDropHeight", nullptr);
    void* gravity = fn<JoannaFindNodeFn>(0x855890)(source, "GravityResponse", nullptr);
    const char* spawnText = spawn ? field<const char*>(spawn, 12) : nullptr;
    const char* gravityText = gravity ? field<const char*>(gravity, 12) : nullptr;
    if (!spawnText || !release || !gravityText) return false;
    char* end = nullptr;
    float spawnHeight = strtof(spawnText, &end);
    if (end == spawnText || *end || spawnHeight < -1000000.0f || spawnHeight > 1000000.0f) return false;
    float gravityScale = strtof(gravityText, &end);
    if (end == gravityText || *end || !(gravityScale > 0.0f) || gravityScale > 100.0f) return false;
    char height[32], fasterGravity[32];
    sprintf_s(height, "%.9g", spawnHeight);
    sprintf_s(fasterGravity, "%.9g", gravityScale * 2.0f);
    unsigned releaseType = *reinterpret_cast<const unsigned*>(field<const char*>(release, 12) - 12);
    unsigned gravityType = *reinterpret_cast<const unsigned*>(gravityText - 12);
    assignPropertyNodeString(release, 12, height, releaseType);
    assignPropertyNodeString(gravity, 12, fasterGravity, gravityType);
    return true;
}
static bool isCrateConfigPath(const char* path) {
    if (!path) return false;
    const char* slash = nullptr;
    for (const char* at = path; *at; ++at)
        if (*at == '/' || *at == '\\') slash = at;
    if (!slash || _stricmp(slash + 1, "Crates.txt")) return false;
    const char* parentSlash = nullptr;
    for (const char* at = path; at < slash; ++at)
        if (*at == '/' || *at == '\\') parentSlash = at;
    const char* parent = parentSlash ? parentSlash + 1 : path;
    return static_cast<size_t>(slash - parent) == strlen("Scripts") &&
        !_strnicmp(parent, "Scripts", strlen("Scripts"));
}
static bool isVehicleDefaultsConfigPath(const char* path) {
    if (!path) return false;
    const char* separators[3] = {};
    unsigned count = 0;
    for (const char* at = path; *at; ++at) {
        if (*at != '/' && *at != '\\') continue;
        if (count < 3) separators[count++] = at;
        else { separators[0] = separators[1]; separators[1] = separators[2]; separators[2] = at; }
    }
    if (count < 3) return false;
    const char* file = separators[2] + 1;
    const char* vehicle = separators[1] + 1;
    const char* scripts = separators[0] + 1;
    return !_stricmp(file, "Defaults.txt") &&
        static_cast<size_t>(separators[2] - vehicle) == strlen("Vehicles") &&
        !_strnicmp(vehicle, "Vehicles", strlen("Vehicles")) &&
        static_cast<size_t>(separators[1] - scripts) == strlen("Scripts") &&
        !_strnicmp(scripts, "Scripts", strlen("Scripts"));
}
static bool fastVehicleDeathConfig(void* source) {
    if (!source) return false;
    void* delay = fn<JoannaFindNodeFn>(0x855890)(source, "DeathDelaySecs", nullptr);
    const char* value = delay ? field<const char*>(delay, 12) : nullptr;
    if (!value) return false;
    unsigned type = *reinterpret_cast<const unsigned*>(value - 12);
    assignPropertyNodeString(delay, 12, "0", type);
    return true;
}
static void** __cdecl ReadScript(void** output, const char* path) {
    void** result = ReadEquipment(output, path);
    // These stock definitions can load before match setup. Patch them whenever
    // RapidPlay is enabled so all schemes use the same fast behavior.
    if (!settings.rapidPlay || !output || !*output) return result;
    bool crateConfig = isCrateConfigPath(path);
    bool vehicleDefaults = isVehicleDefaultsConfigPath(path);
    if (!crateConfig && !vehicleDefaults) return result;
    void* replacement = clonePropertyNode(*output);
    bool changed = replacement && (crateConfig ? fastDropCrateConfig(replacement) :
        fastVehicleDeathConfig(replacement));
    if (changed) {
        releasePropertyNode(*output);
        *output = replacement;
        if (crateConfig) log("rapidplay-fast-drops", 1, 2);
        else log("rapidplay-fast-vehicle-death", 3, 0);
    } else {
        releasePropertyNode(replacement);
        if (crateConfig) log("rapidplay-fast-drops-failed");
        else log("rapidplay-fast-vehicle-death-failed");
    }
    return result;
}

enum class Feature { RapidPlay, SkipIntros, HatDefinitions, UnlockAll, JoannaDarkAttack, ScriptReader };
struct Patch {
    unsigned rva; BYTE original[5]; void* target; BYTE opcode;
    Feature feature = Feature::RapidPlay;
    bool applied = false;
};
static bool selected(const Patch& p) {
    switch (p.feature) {
    case Feature::RapidPlay: return settings.rapidPlay;
    case Feature::SkipIntros: return settings.skipIntros;
    case Feature::HatDefinitions: return settings.rareHats || settings.unusedHats;
    case Feature::UnlockAll: return settings.unlockAll;
    case Feature::JoannaDarkAttack: return settings.joannaDarkAttack;
    case Feature::ScriptReader: return settings.rapidPlay || settings.joannaDarkAttack;
    }
    return false;
}
static Patch patches[] = {
    {0x471920, {0x55,0x8b,0xec,0x51,0x53}, reinterpret_cast<void*>(&UpdateDamage), 0xe9},
    {0x471a41, {0xe8,0xba,0x16,0xfa,0xff}, reinterpret_cast<void*>(&HudReady), 0xe8},
    {0x471ab5, {0xe8,0x46,0x16,0xfa,0xff}, reinterpret_cast<void*>(&HudReady), 0xe8},
    {0x471a36, {0xe8,0x15,0x96,0xfe,0xff}, reinterpret_cast<void*>(&BlockingActivity), 0xe8},
    {0x471aa6, {0xe8,0xa5,0x95,0xfe,0xff}, reinterpret_cast<void*>(&BlockingActivity), 0xe8},
    {0x471b94, {0xe8,0xb7,0x94,0xfe,0xff}, reinterpret_cast<void*>(&BlockingActivity), 0xe8},
    {0x471b5a, {0xe8,0x11,0xcb,0xef,0xff}, reinterpret_cast<void*>(&StartDeaths), 0xe8},
    {0x46e275, {0xe8,0x86,0x35,0x00,0x00}, reinterpret_cast<void*>(&UpdateReactions), 0xe8},
    {0x47180d, {0xe8,0x3e,0x98,0xfe,0xff}, reinterpret_cast<void*>(&BlockingActivity), 0xe8},
    {0x47181d, {0xe8,0xde,0x18,0xfa,0xff}, reinterpret_cast<void*>(&HudReady), 0xe8},
    {0x471839, {0xe8,0xd2,0xb2,0xeb,0xff}, reinterpret_cast<void*>(&SpeechBlocking), 0xe8},
    {0x4416de, {0xe8,0x6d,0x05,0x00,0x00}, reinterpret_cast<void*>(&UpdatePlacementDelay), 0xe8},
    {0x441946, {0xe8,0xa5,0xfa,0xff,0xff}, reinterpret_cast<void*>(&PlaceWorm), 0xe8},
    {0x4414ae, {0xe8,0x3d,0x1c,0xf1,0xff}, reinterpret_cast<void*>(&PlacementEffect), 0xe8},
    {0x4414c1, {0xe8,0x9a,0xbc,0xee,0xff}, reinterpret_cast<void*>(&PlacementSound), 0xe8},
    {0x4f315a, {0xe8,0x91,0x06,0x00,0x00}, reinterpret_cast<void*>(&UpdatePlacedNameplate), 0xe8},
    {0x4416f9, {0xe8,0x22,0x08,0x00,0x00}, reinterpret_cast<void*>(&UpdatePlacementSettling), 0xe8},
    {0x48da7, {0xe8,0xe4,0x70,0xff,0xff}, reinterpret_cast<void*>(&StartupFlowTarget), 0xe8, Feature::SkipIntros},
    {0x48ff0, {0xe8,0x9b,0x6e,0xff,0xff}, reinterpret_cast<void*>(&StartupFlowTarget), 0xe8, Feature::SkipIntros},
    {0x2f6644, {0xe8,0x87,0x99,0x0e,0x00}, reinterpret_cast<void*>(&RareHatList), 0xe8, Feature::HatDefinitions},
    {0x2f805b, {0xe8,0x70,0x7f,0x0e,0x00}, reinterpret_cast<void*>(&RareHatList), 0xe8, Feature::HatDefinitions},
    {0x37bcaf, {0xe8,0xcc,0x7e,0x02,0x00}, reinterpret_cast<void*>(&RewardsLoaded), 0xe8, Feature::UnlockAll},
    {0x37bd85, {0xe8,0xf6,0x7d,0x02,0x00}, reinterpret_cast<void*>(&RewardsLoaded), 0xe8, Feature::UnlockAll},
    {0x37c3aa, {0xe8,0xd1,0x77,0x02,0x00}, reinterpret_cast<void*>(&RewardsLoaded), 0xe8, Feature::UnlockAll},
    {0x37d5d7, {0xe8,0xa4,0x65,0x02,0x00}, reinterpret_cast<void*>(&RewardsLoaded), 0xe8, Feature::UnlockAll},
    {0x856a30, {0x55,0x8b,0xec,0x83,0xec}, reinterpret_cast<void*>(&ReadScript), 0xe9, Feature::ScriptReader},
    {0x31b0a0, {0x55,0x8b,0xec,0x6a,0xff}, reinterpret_cast<void*>(&JoannaWeaponAvailable), 0xe9, Feature::JoannaDarkAttack},
    {0x31a400, {0x55,0x8b,0xec,0x81,0xec}, reinterpret_cast<void*>(&JoannaWeaponName), 0xe9, Feature::JoannaDarkAttack},
    {0x442250, {0x55,0x8b,0xec,0x83,0x3d}, reinterpret_cast<void*>(&JoannaTextLookup), 0xe9, Feature::JoannaDarkAttack},
};
static bool write(void* at, const void* bytes, size_t len) {
    DWORD old; if (!VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, bytes, len); FlushInstructionCache(GetCurrentProcess(), at, len);
    DWORD ignored; return VirtualProtect(at, len, old, &ignored) != 0;
}
static void jump(BYTE* bytes, BYTE opcode, uintptr_t at, uintptr_t to) {
    bytes[0] = opcode; *reinterpret_cast<int32_t*>(bytes + 1) = static_cast<int32_t>(to - at - 5);
}

// Runs outside DllMain while the other game threads are suspended.
static DWORD control(unsigned mode) {
    if (mode == 3) return installed ? (enabled ? 1 : 2) : 0;
    if (mode == 0) {
        if (installed) {
            for (auto& p : patches) {
                if (!p.applied) continue;
                BYTE expected[5]; jump(expected,p.opcode,base+p.rva,reinterpret_cast<uintptr_t>(p.target));
                if (memcmp(reinterpret_cast<void*>(base+p.rva),expected,5)) {
                    log("restore-signature-mismatch",p.rva); return 11;
                }
            }
            for (auto& p : patches) {
                if (!p.applied) continue;
                if (!write(reinterpret_cast<void*>(base+p.rva), p.original, 5)) return 12;
                p.applied = false;
            }
        }
        installed = enabled = false; log("restored"); return 0;
    }
    if (mode != 1 && mode != 2) return 10;
    if (!installed) {
        // Validate every selected patch before changing a single game byte.
        for (auto& p : patches) {
            if (!selected(p)) continue;
            if (memcmp(reinterpret_cast<void*>(base+p.rva), p.original, 5)) {
                log("signature-mismatch", p.rva); return 11;
            }
        }
        if (settings.rapidPlay && !trampoline) return 12;
        if (settings.rapidPlay) updateOriginal = reinterpret_cast<UpdateFn>(trampoline);
        for (auto& p : patches) {
            if (!selected(p)) continue;
            BYTE bytes[5]; jump(bytes,p.opcode,base+p.rva,reinterpret_cast<uintptr_t>(p.target));
            // Include this site in rollback if its protection restore fails.
            p.applied = true;
            if (!write(reinterpret_cast<void*>(base+p.rva), bytes, 5)) {
                for (auto& rollback : patches) {
                    if (rollback.applied && write(reinterpret_cast<void*>(base+rollback.rva), rollback.original, 5))
                        rollback.applied = false;
                }
                return 12;
            }
        }
        installed = true;
    }
    enabled = mode == 1;
    lastTurn = nullptr; lastMain = lastSub = -1;
    log(enabled ? "enabled" : "stock-trace"); return 0;
}

// File I/O and allocation finish before any other game thread is suspended.
static bool prepare(bool supportedGame) {
    base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH];
    if (!siblingPath(selfModule, L"Plus.ini", path)) return false;
    settings = readSettings(path);
    if (settings.log && logFile == INVALID_HANDLE_VALUE && siblingPath(selfModule, L"WMD-Plus.log", path)) {
        logFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    log("config-SkipIntros", settings.skipIntros);
    log("config-RapidPlay", settings.rapidPlay);
    log("config-RareHats", settings.rareHats);
    log("config-UnusedHats", settings.unusedHats);
    log("config-UnlockAllRewards", settings.unlockAll);
    log("config-JoannaDarkAttack", settings.joannaDarkAttack);
    log("config-Log", settings.log);
    if (supportedGame && (settings.rapidPlay || settings.joannaDarkAttack) && !prepareScriptReader()) {
        log("script-reader-prepare-failed"); return false;
    }
    if (supportedGame && !prepareJoanna()) { log("joanna-prepare-failed"); return false; }
    if (settings.rapidPlay && !trampoline) {
        trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if (trampoline) {
            memcpy(trampoline,patches[0].original,5);
            jump(trampoline+5,0xe9,reinterpret_cast<uintptr_t>(trampoline+5),base+patches[0].rva+5);
            DWORD old;
            if (!VirtualProtect(trampoline,10,PAGE_EXECUTE_READ,&old)) {
                VirtualFree(trampoline,0,MEM_RELEASE); trampoline = nullptr;
            } else FlushInstructionCache(GetCurrentProcess(),trampoline,10);
        }
    }
    return true;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
