#pragma once

// Definitions only: models and menu previews are supplied by the installed game.
struct HatDefinition { const char* id; const char* preview; const char* name; };
static const HatDefinition rareHats[] = {
    {"CU_RareBattletoadsHat_01", "FE_RareBattletoadsHat_01", "Battletoads"},
    {"CU_RareBlastCorpsHat_01", "FE_RareBlastCorpsHat_01", "Blast Corps"},
    {"CU_RareJetForceGeminiJunoHat_01", "FE_RareJetForceGeminiJunoHat_01", "Jet Force Gemini (Juno)"},
    {"CU_RareJetpacSpacemanHat_01", "FE_RareJetpacSpacemanHat_01", "Jetpac Spaceman"},
    {"CU_RareKillerInstinctKilgoreHat_01", "FE_RareKillerInstinctKilgoreHat_01", "Killer Instinct (Kilgore)"},
};
static const HatDefinition unusedHats[] = {
    {"Cu_HatChef_01", "FE_HatChef_01", "Chef"},
    {"CU_HatHippieHair_01", "FE_HatHippieHair_01", "Hippie Hair"},
    {"CU_HatIceCreamCone_01", "FE_HatIceCreamCone_01", "Ice Cream Cone"},
    {"CU_HatMullet_01", "FE_HatMullet_01", "Mullet"},
    {"CU_HatPants_01", "FE_HatPants_01", "Pants"},
};
static constexpr unsigned hatRecordSize = 0x6c;
static LONG addingRareHats;
using HatListFn = void*(__thiscall*)(void*, unsigned);
using HatReserveFn = void(__thiscall*)(void*, unsigned);
using HatConstructFn = void(__thiscall*)(void*, void*, const void*);
using HatNodeAllocFn = void*(__cdecl*)();
using HatNodeMethodFn = void(__thiscall*)(void*);
using HatStringAssignFn = void(__thiscall*)(void*, unsigned, const char*, unsigned);
using HatNodeHashFn = unsigned(__thiscall*)(void*);
using HatStringHashFn = unsigned(__cdecl*)(const char*);
using HatFindFn = void*(__thiscall*)(void*, const char*);

struct HatSourceOwner {
    void* node;
    ~HatSourceOwner() { if (node) fn<HatNodeMethodFn>(0x856f00)(node); }
};
static void* createHatSource(const char* id) {
    void* node = fn<HatNodeAllocFn>(0x853790)();
    if (!node) return nullptr;
    fn<HatNodeMethodFn>(0x853690)(node); // Native property node constructor.
    fn<HatNodeMethodFn>(0x853970)(node); // Own a temporary reference.
    fn<HatStringAssignFn>(0x858b80)(static_cast<BYTE*>(node) + 8, 0, id,
        static_cast<unsigned>(strlen(id)));
    // Match loading identifies attachments by the source node's name hash,
    // rather than the menu's display/resource fields. Validate that path now.
    if (fn<HatNodeHashFn>(0x8560f0)(node) != fn<HatStringHashFn>(0x8587a0)(id)) {
        fn<HatNodeMethodFn>(0x856f00)(node);
        return nullptr;
    }
    return node;
}

static unsigned hatCount(void* list) {
    uintptr_t begin = field<uintptr_t>(list, 4), end = field<uintptr_t>(list, 8);
    uintptr_t capacity = field<uintptr_t>(list, 12);
    if (!begin || end < begin || capacity < end ||
        (end - begin) % hatRecordSize || (capacity - begin) % hatRecordSize ||
        (capacity - begin) / hatRecordSize > 4096) return 0;
    return static_cast<unsigned>((end - begin) / hatRecordSize);
}
static bool containsHat(void* list, unsigned count, const char* id) {
    BYTE* begin = field<BYTE*>(list, 4);
    for (unsigned i = 0; i < count; ++i) {
        const char* existing = field<const char*>(begin + i * hatRecordSize, 4);
        if (existing && !strcmp(existing, id)) return true;
    }
    return false;
}
template<size_t N> static void addHatDefinitions(void* list, const HatDefinition (&hats)[N], bool unused) {
    if (!list || !enabled) return;
    // This getter runs on the same game-thread path as normal hat selection.
    // Avoid a reentrant append if a native allocation causes another lookup.
    if (InterlockedCompareExchange(&addingRareHats, 1, 0)) return;
    struct Reset { ~Reset() { InterlockedExchange(&addingRareHats, 0); } } reset;
    unsigned count = hatCount(list);
    if (!count || count > 4096 - N || !containsHat(list, count, "CU_None")) return;
    bool missing[N] = {}; unsigned needed = 0;
    for (unsigned i = 0; i < N; ++i) {
        missing[i] = !containsHat(list, count, hats[i].id);
        if (missing[i]) ++needed;
    }
    if (!needed) return;
    // The game's vector growth and record constructor retain its allocator and
    // reference-counting rules. Existing entries keep their original indices.
    fn<HatReserveFn>(0x3e48e0)(static_cast<BYTE*>(list) + 4, needed);
    uintptr_t end = field<uintptr_t>(list, 8), capacity = field<uintptr_t>(list, 12);
    if (hatCount(list) != count || capacity - end < needed * hatRecordSize) return;
    for (unsigned i = 0; i < N; ++i) {
        if (!missing[i]) continue;
        HatSourceOwner source{createHatSource(hats[i].id)};
        if (!source.node) { log(unused ? "unused-hat-source-failed" : "rare-hat-source-failed", i); continue; }
        BYTE record[hatRecordSize] = {};
        field<const char*>(record, 4) = hats[i].id;
        field<const char*>(record, 8) = hats[i].name;
        field<const char*>(record, 0x10) = hats[i].id;
        field<const char*>(record, 0x14) = hats[i].preview;
        field<const char*>(record, 0x18) = "objects:/Hats/";
        field<const char*>(record, 0x2c) = "Heads";
        field<BYTE>(record, 0x30) = 1; // CoversHead; eyes/mouth use normal worm presentation.
        field<float>(record, 0x34) = 1.0f;
        field<unsigned>(record, 0x60) = 4; // Normal unrestricted platform value.
        field<void*>(record, 0x68) = source.node;
        // Strings above live in this DLL for the entire process. No UGC texture
        // override, DLC requirement or reward gate is added. The native record
        // constructor retains the source node; the temporary reference is released.
        BYTE allocator = 0;
        void* destination = field<void*>(list, 8);
        fn<HatConstructFn>(0x3d4d10)(&allocator, destination, record);
        field<BYTE*>(list, 8) += hatRecordSize;
        log(unused ? "unused-hat-added" : "rare-hat-added", count++, i);
        // Exercise the same native GID lookup used by the in-game model loader.
        void* resolved = fn<HatFindFn>(0x4d5fb0)(list, hats[i].id);
        log(resolved == destination ?
            (unused ? "unused-hat-resolve-ok" : "rare-hat-resolve-ok") :
            (unused ? "unused-hat-resolve-failed" : "rare-hat-resolve-failed"), count - 1, i);
    }
}
// Unlike the other restored hats, Mullet's FE descriptor is missing from
// the frontend resource manifest. The menu only searches registered resources;
// adding a hat definition does not cause that descriptor to be loaded.
using HatPreviewFindFn = void*(__thiscall*)(void*, const char*, unsigned);
using HatPreviewCreateFn = void*(__thiscall*)(void*, unsigned, int);
using HatPreviewNameFn = void(__thiscall*)(void*, const char*);
static void* mulletPreviewManager;
static void ensureMulletPreview() {
    if (!enabled || !settings.unusedHats) return;
    void* manager = *fn<void**>(0xe102984);
    unsigned type = *fn<unsigned short*>(0xf105d08);
    if (!manager || manager == mulletPreviewManager || type < 9000 || type > 9016) return;
    BYTE* banks = field<BYTE*>(manager, 8);
    if (!banks) return;
    BYTE* bank = banks + (type - 9000) * 0x3c;
    // Wait for the descriptor bank. Its optional streaming callbacks are null
    // for normal frontend entries: the UI renderer loads from their handles.
    if (!field<void*>(bank, 4)) return;
    mulletPreviewManager = manager; // One attempt per cache, including failure.
    constexpr const char* name = "FE_HatMullet_01.xDesc";
    if (fn<HatPreviewFindFn>(0x4d4b0)(manager, name, type)) return;
    // -1 requests a free native resource slot. The cache owns this record and
    // releases it normally; retain all existing resource IDs and asset files.
    void* resource = fn<HatPreviewCreateFn>(0x4d2a0)(manager, type, -1);
    if (!resource) { log("mullet-preview-create-failed"); return; }
    // Existing FE entries normally point to uidata:ui_xom/. Mullet is not
    // registered there. Keep its basename lookup but use its installed file.
    fn<HatPreviewNameFn>(0x51f00)(resource, "objects:/UI/FE_HatMullet_01.xDesc");
    bool found = fn<HatPreviewFindFn>(0x4d4b0)(manager, name, type) == resource;
    unsigned handle = field<unsigned>(resource, 4);
    log(found && handle ? "mullet-preview-registered" : "mullet-preview-register-failed",
        found, static_cast<int>(handle));
}

static void* __fastcall RareHatList(void* manager, void*, unsigned type) {
    void* list = fn<HatListFn>(0x3dffd0)(manager, type);
    if (type == 5) {
        if (list && hatCount(list) && containsHat(list, hatCount(list), "CU_None"))
            ensureMulletPreview();
        if (settings.rareHats) addHatDefinitions(list, rareHats, false);
        if (settings.unusedHats) addHatDefinitions(list, unusedHats, true);
    }
    return list;
}
