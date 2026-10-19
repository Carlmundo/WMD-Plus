#pragma once
// Experimental restoration uses hidden crafted slot 62 (ClusterFragmentMk2).
// Its payload ID/physics stay intact; Joanna settings come from the installed
// PerfectDarkStrike script via the native reader. No game assets are shipped.
static constexpr unsigned joannaWeaponId = 62;
static constexpr const char* joannaInternalId = "ClusterFragmentMk2";
using JoannaNameFn = const char*(__cdecl*)(unsigned);
using JoannaAvailableFn = bool(__cdecl*)(const char*);
using JoannaTextFn = const char*(__cdecl*)(unsigned);
static JoannaNameFn joannaNameOriginal;
static JoannaAvailableFn joannaAvailableOriginal;
static JoannaTextFn joannaTextOriginal;
static BYTE* joannaNameTrampoline;
static BYTE* joannaAvailableTrampoline;
static BYTE* joannaTextTrampoline;
static void* joannaCachedSource; // Native retained definition, process lifetime.
using JoannaFindNodeFn = void*(__thiscall*)(void*, const char*, unsigned*);
using JoannaAppendNodeFn = void*(__thiscall*)(void*, void*);
static const char* __cdecl JoannaWeaponName(unsigned id) {
    if (enabled && settings.joannaDarkAttack && id == joannaWeaponId)
        return "Joanna Dark Attack";
    return joannaNameOriginal(id);
}
static bool __cdecl JoannaWeaponAvailable(const char* id) {
    if(enabled && settings.joannaDarkAttack && id && !_stricmp(id,joannaInternalId))
        return true;
    return joannaAvailableOriginal(id);
}
// Same case-sensitive hash used by the native language lookup (0x4421a0).
static constexpr unsigned joannaTextHash(const char* key) {
    unsigned hash=0x1505;
    for(;*key;++key) hash=(hash*33)^static_cast<unsigned char>(*key);
    return hash;
}
struct JoannaText { const char* key; const char* text; };
static const JoannaText joannaTexts[]={
    {"Gui.WeaponPanel.ClusterFragmentMk2.Prompt","Joanna Dark Attack"},
    {"GUI.WeaponPanel.ClusterFragmentMk2.Prompt","Joanna Dark Attack"},
    {"GUI.WEAPONPANEL.CLUSTERFRAGMENTMK2.PROMPT","Joanna Dark Attack"},
    {"Gui.WeaponPanel.PerfectDarkStrike.Prompt","Joanna Dark Attack"},
    {"Com.BulletStrikeMode.Narrow","Focused Fire"},
    {"Com.BulletStrikeMode.Wide","Wide Fire"},
    {"Gui.CraftPanelDescription.PerfectDarkStrike","Call in Joanna Dark."},
    {"Gui.CraftPanelLongDescription.PerfectDarkStrike",
        "Call in Joanna Dark to attack the target. Toggle between focused and wide fire."},
};
static const char* __cdecl JoannaTextLookup(unsigned hash) {
    const char* original=joannaTextOriginal(hash);
    if(original || !enabled || !settings.joannaDarkAttack) return original;
    for(const auto& entry:joannaTexts)
        if(hash==joannaTextHash(entry.key)) return entry.text;
    return original;
}
// Overlay complete native top-level properties; keep root fragment payload data.
// Child clones retain numeric token types and are private to this definition.
static bool overlayJoannaSource(void* destination,void* source) {
    auto begin=field<void**>(source,0x18),end=field<void**>(source,0x1c);
    if(end<begin || end-begin>3000) return false;
    for(;begin!=end;++begin) {
        void* copy=clonePropertyNode(*begin);
        if(!copy) return false;
        const char* name=field<const char*>(copy,8);
        void* existing=fn<JoannaFindNodeFn>(0x855890)(destination,name,nullptr);
        if(existing) {
            auto children=field<void**>(destination,0x18),last=field<void**>(destination,0x1c);
            bool replaced=false;
            for(;children!=last;++children) if(*children==existing) {
                *children=copy;releasePropertyNode(existing);replaced=true;break;
            }
            if(!replaced) {releasePropertyNode(copy);return false;}
        } else {
            fn<JoannaAppendNodeFn>(0x853990)(destination,copy);
            releasePropertyNode(copy);
        }
    }
    return true;
}
static void removeJoannaProperty(void* parent,const char* name) {
    auto begin=field<void**>(parent,0x18),end=field<void**>(parent,0x1c);
    for(auto at=begin;at!=end;++at) {
        const char* existing=field<const char*>(*at,8);
        if(existing && !_stricmp(existing,name)) {
            void* removed=*at;
            memmove(at,at+1,static_cast<size_t>(end-at-1)*sizeof(void*));
            *--end=nullptr;field<void**>(parent,0x1c)=end;
            releasePropertyNode(removed);return;
        }
    }
}
static bool isJoannaSource(void* source) {
    if(!source) return false;
    void* gadget=fn<JoannaFindNodeFn>(0x855890)(source,"Gadget",nullptr);
    if(!gadget) return false;
    void* mesh=fn<JoannaFindNodeFn>(0x855890)(gadget,"SatelliteMeshName",nullptr);
    const char* name=mesh?field<const char*>(mesh,12):nullptr;
    return name && !strcmp(name,"JoannaDark") &&
        fn<JoannaFindNodeFn>(0x855890)(gadget,"StrikeNarrow",nullptr) &&
        fn<JoannaFindNodeFn>(0x855890)(gadget,"StrikeWide",nullptr);
}
static bool registerJoannaMetadata() {
    BYTE* record=fn<BYTE*>(0xe22cd0+joannaWeaponId*0x1c);
    const char* id=field<const char*>(record,0);
    if(!id || strcmp(id,joannaInternalId) || field<unsigned>(record,8)!=35) return false;
    unsigned kind=field<unsigned>(record,4),flags=field<unsigned>(record,12);
    if(kind==25 && flags==9) return true;
    if(kind!=0 || flags!=8) return false;
    BYTE updated[0x1c];memcpy(updated,record,sizeof updated);
    field<unsigned>(updated,4)=25; // Existing BulletStrike gadget at owner +0xa4.
    field<unsigned>(updated,12)=9; // Inventory weapon plus craftable variation.
    field<unsigned short>(updated,16)=61;
    field<unsigned short>(updated,18)=62;
    field<unsigned short>(updated,20)=115;
    field<unsigned short>(updated,22)=96;
    BYTE previous[0x1c];memcpy(previous,record,sizeof previous);
    if(write(record,updated,sizeof updated)) return true;
    // A protection-restore failure can occur after the bytes were copied.
    // Restore the stock record before keeping the original script definition.
    write(record,previous,sizeof previous);
    return false;
}
static void** __cdecl ReadEquipment(void** output, const char* path) {
    void** result=scriptReadOriginal(output,path);
    if (!output || !*output || !path) return result;
    if (!enabled || !settings.joannaDarkAttack ||
        _stricmp(path,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt")) return result;
    // A later equipment load must not fall back to an unconfigured stock node
    // after its slot has already been published as BulletStrike.
    if(joannaCachedSource) {
        if(!registerJoannaMetadata()) {log("joanna-metadata-failed");return result;}
        fn<HatNodeMethodFn>(0x853970)(joannaCachedSource);
        releasePropertyNode(*output);
        *output=joannaCachedSource;
        log("joanna-config-reused",joannaWeaponId);
        return result;
    }
    void* nativeSource=nullptr;
    scriptReadOriginal(&nativeSource,"scripts:/Scripts/Equip/PerfectDarkStrike.txt");
    if(!isJoannaSource(nativeSource)) {
        releasePropertyNode(nativeSource);log("joanna-native-source-failed");return result;
    }
    void* replacement=clonePropertyNode(*output);
    if(!replacement) {
        releasePropertyNode(nativeSource);log("joanna-source-failed");return result;
    }
    // These describe inventory use, not fragment physics. Drop the fragment's
    // values so the shipped Joanna file and the normal Defaults merge decide.
    const char* inventory[]={"EnableInCaverns","CanHoldOnPack","CanHoldOnRope",
        "CanHoldOnPara","RequiresAim","FinishesTurn","RememberAsDefault",
        "CausesDamage","AlarmsWorms"};
    for(const char* name:inventory) removeJoannaProperty(replacement,name);
    bool copied=overlayJoannaSource(replacement,nativeSource);
    releasePropertyNode(nativeSource);
    if(!copied) {
        releasePropertyNode(replacement);log("joanna-native-copy-failed");return result;
    }
    if(!registerJoannaMetadata()) {
        releasePropertyNode(replacement);log("joanna-metadata-failed");return result;
    }
    releasePropertyNode(*output);
    *output=replacement;
    fn<HatNodeMethodFn>(0x853970)(replacement);
    joannaCachedSource=replacement;
    log("joanna-native-config",joannaWeaponId,25);
    log("joanna-registered",joannaWeaponId,25);
    return result;
}
static bool prepareJoanna() {
    if(!settings.joannaDarkAttack) return true;
    const BYTE name[]={0x55,0x8b,0xec,0x81,0xec,0x04,0x08,0x00,0x00};
    const BYTE available[]={0x55,0x8b,0xec,0x6a,0xff};
    // cmp [language-table pointer],0 uses the current ASLR module address.
    BYTE text[]={0x55,0x8b,0xec,0x83,0x3d,0,0,0,0,0};
    *reinterpret_cast<uint32_t*>(text+5)=static_cast<uint32_t>(base+0xdd219b0);
    if(!prepareHookTrampoline(joannaNameTrampoline,0x31a400,name,sizeof name) ||
       !prepareHookTrampoline(joannaAvailableTrampoline,0x31b0a0,available,sizeof available) ||
       !prepareHookTrampoline(joannaTextTrampoline,0x442250,text,sizeof text)) return false;
    joannaNameOriginal=reinterpret_cast<JoannaNameFn>(joannaNameTrampoline);
    joannaAvailableOriginal=reinterpret_cast<JoannaAvailableFn>(joannaAvailableTrampoline);
    joannaTextOriginal=reinterpret_cast<JoannaTextFn>(joannaTextTrampoline);
    return true;
}
