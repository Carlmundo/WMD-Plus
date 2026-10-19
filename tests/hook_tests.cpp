#include <stdint.h>
static uintptr_t testAddress(unsigned);
#define WMD_PLUS_TESTS
#include "../plus.cpp"
#include <assert.h>
#include <stdlib.h>
#include <initializer_list>
#include <list>
#include <string>

// Keep test helpers aligned with the shared property/script-reader names used
// by the current implementation while preserving older test fixture calls.
#define releaseJoannaNode releasePropertyNode
#define assignJoannaString assignPropertyNodeString
#define joannaReadOriginal scriptReadOriginal
#define joannaReadTrampoline scriptReadTrampoline
static const char* value(void* node) { return node ? field<const char*>(node,12) : nullptr; }
static unsigned type(void* node) {
    const char* text=value(node);
    return text ? *reinterpret_cast<const unsigned*>(text-12) : 0;
}

static BYTE wormStorage[3][0x2200], manager[0x80], activities[0x100], turn[0xc0], placement[0xe8];
static void* wormManager = manager;
static unsigned started, updates;
static BYTE rewardManagerStorage[0xc8], rewardStorage[12][3][0x40];
static void* rewardPointers[12][3];
static unsigned rewardLoadCalls, rewardGrantCalls, persistedRewards;
static bool rewardProfileReady, reenterRewardLoad;
static void __fastcall rewardLoadMock(void* rewards, void*) {
    assert(rewards == rewardManagerStorage);
    ++rewardLoadCalls;
    field<BYTE>(rewards, 0xc4) = rewardProfileReady;
}
static void __fastcall rewardGrantMock(void* rewards, void*, unsigned index, unsigned xp, bool quiet) {
    assert(rewards == rewardManagerStorage && !xp && quiet);
    unsigned group = index / 3, entry = index % 3;
    assert(group < 12 && group != 8);
    void* reward = rewardPointers[group][entry];
    assert(reward && !field<BYTE>(reward, 0x30) && field<unsigned>(reward, 0x28) == group);
    field<BYTE>(reward, 0x30) = 1;
    ++rewardGrantCalls; ++persistedRewards;
    if (reenterRewardLoad) RewardsLoaded(rewards, nullptr);
}
static bool stockActivity, stockHud, stockSpeech;
static unsigned speechCalls, reactionCalls;
static unsigned placementCalls;
static BYTE hatListStorage[16], hatStorage[16*0x6c], movedHatStorage[16*0x6c];
static void* hatListResult=hatListStorage;
static unsigned hatListCalls, hatReserveCalls, hatConstructCalls, requestedHatType;
static BYTE hatSourceStorage[1024][0x28];
static unsigned hatNodesAllocated;
static unsigned failNodeAfter=~0u;
static std::list<std::string> nativeNodeStrings;
static bool failHatNodeAllocation, mismatchHatHash;
static unsigned __cdecl hatStringHashMock(const char* id) {
    unsigned hash=0x1505;
    for(;*id;++id) {
        unsigned ch=static_cast<unsigned char>(*id);
        if(ch>='A' && ch<='Z')ch+='a'-'A';
        hash=(hash*33)^ch;
    }
    return hash;
}
static void* __cdecl hatNodeAllocMock() {
    if(failHatNodeAllocation || hatNodesAllocated>=failNodeAfter)return nullptr;
    assert(hatNodesAllocated<_countof(hatSourceStorage));
    return hatSourceStorage[hatNodesAllocated++];
}
static void __fastcall hatNodeCtorMock(void* node,void*) { memset(node,0,0x28); }
static void __fastcall hatNodeRefMock(void* node,void*) { ++field<unsigned>(node,4); }
static void __fastcall hatNodeReleaseMock(void* node,void*) {
    assert(field<unsigned>(node,4)>0);
    if(!--field<unsigned>(node,4)) {
        auto begin=field<void**>(node,0x18),end=field<void**>(node,0x1c);
        for(auto it=begin;it!=end;++it)hatNodeReleaseMock(*it,nullptr);
        free(begin);field<void**>(node,0x18)=field<void**>(node,0x1c)=field<void**>(node,0x20)=nullptr;
    }
}
static void __fastcall hatStringAssignMock(void* value,void*,unsigned flags,const char* id,unsigned size) {
    assert(flags<=6 && strlen(id)==size);
    // Match the native allocation's 16-byte header, including token type.
    nativeNodeStrings.emplace_back(16,'\0');
    auto& allocation=nativeNodeStrings.back();allocation.append(id,size);
    memcpy(&allocation[4],&flags,sizeof flags);
    field<const char*>(value,0)=allocation.c_str()+16;
}
static unsigned __fastcall hatNodeHashMock(void* node,void*) {
    assert(node && field<unsigned>(node,4)>0);
    const char* id=field<const char*>(node,8);assert(id && *id);
    return hatStringHashMock(id)+(mismatchHatHash?1:0);
}
static void* __fastcall hatFindMock(void* list,void*,const char* id) {
    BYTE* begin=field<BYTE*>(list,4);
    for(unsigned i=0;i<hatCount(list);++i) {
        BYTE* record=begin+i*hatRecordSize;
        void* node=field<void*>(record,0x68);
        // Fixture stock rows use direct IDs; every newly added row must use
        // its real source identity, as the game model loader does.
        unsigned hash=node?hatNodeHashMock(node,nullptr):hatStringHashMock(field<const char*>(record,4));
        if(hash==hatStringHashMock(id))return record;
    }
    return nullptr;
}
static void* __fastcall hatListMock(void*,void*,unsigned type) {
    ++hatListCalls;requestedHatType=type;return hatListResult;
}
static void __fastcall hatReserveMock(void* vector,void*,unsigned extra) {
    ++hatReserveCalls;
    auto begin=field<BYTE*>(vector,0), end=field<BYTE*>(vector,4);
    assert(extra<=5);
    memcpy(movedHatStorage,begin,end-begin);
    field<BYTE*>(vector,0)=movedHatStorage;
    field<BYTE*>(vector,4)=movedHatStorage+(end-begin);
    field<BYTE*>(vector,8)=movedHatStorage+sizeof movedHatStorage;
}
static void __fastcall hatConstructMock(void*,void*,void* destination,const void* source) {
    ++hatConstructCalls;memcpy(destination,source,hatRecordSize);
    void* node=field<void*>(destination,0x68);
    if(node)hatNodeRefMock(node,nullptr);
}
static bool setJoannaProperty(void* root,const char* path,const char* value,unsigned type=3) {
    const char* separator=strchr(path,'.');
    char name[96]; size_t length=separator?static_cast<size_t>(separator-path):strlen(path);
    if(!length || length>=sizeof name) return false;
    memcpy(name,path,length);name[length]=0;
    void* child=fn<JoannaFindNodeFn>(0x855890)(root,name,nullptr);
    if(!child) {
        child=createHatSource(name);
        if(!child) return false;
        fn<JoannaAppendNodeFn>(0x853990)(root,child);
        releaseJoannaNode(child); // Parent retains the returned borrowed node.
    }
    if(separator) return setJoannaProperty(child,separator+1,value,type);
    assignJoannaString(child,12,value,type);
    return true;
}

static BYTE joannaMetadata[0x1c];
static void* joannaReadSource;
static void* joannaNativeSource;
static unsigned joannaNativeReads;
static unsigned joannaReadCalls;
static void** __cdecl joannaReadMock(void** out,const char* path) {
    ++joannaReadCalls;
    bool native=path && !_stricmp(path,"scripts:/Scripts/Equip/PerfectDarkStrike.txt");
    if(native) ++joannaNativeReads;
    *out=native?joannaNativeSource:joannaReadSource;
    if(*out)hatNodeRefMock(*out,nullptr);
    return out;
}
static void* __fastcall joannaFindMock(void* parent,void*,const char* name,unsigned*) {
    auto begin=field<void**>(parent,0x18),end=field<void**>(parent,0x1c);
    for(;begin!=end;++begin)if(!_stricmp(field<const char*>(*begin,8),name))return *begin;
    return nullptr;
}
static void* __fastcall joannaAppendMock(void* parent,void*,void* child) {
    auto begin=field<void**>(parent,0x18),end=field<void**>(parent,0x1c);
    size_t count=begin?end-begin:0;
    auto entries=static_cast<void**>(realloc(begin,(count+1)*sizeof(void*)));
    assert(entries);entries[count]=child;
    field<void**>(parent,0x18)=entries;
    field<void**>(parent,0x1c)=field<void**>(parent,0x20)=entries+count+1;
    hatNodeRefMock(child,nullptr);return child;
}
static const char* __cdecl joannaNameMock(unsigned) {return "stock weapon";}
static const char* joannaTranslated;
static unsigned joannaTextCalls,joannaLastTextHash;
static const char* __cdecl joannaTextMock(unsigned hash) {
    ++joannaTextCalls;joannaLastTextHash=hash;return joannaTranslated;
}
static unsigned joannaAvailabilityCalls;
static bool joannaAvailabilityResult;
static bool __cdecl joannaAvailableMock(const char*) {++joannaAvailabilityCalls;return joannaAvailabilityResult;}
static BYTE previewManagerStorage[16], previewBank[2*0x3c], previewResource[0x64];
static void* previewManagerPointer;
static unsigned short previewType=9001;
static unsigned previewFindCalls, previewCreateCalls, previewNameCalls;
static bool previewPresent, previewAllocationFails;
static void* __fastcall previewFindMock(void* cache,void*,const char* name,unsigned type) {
    assert(cache==previewManagerPointer && type==9001 && !strcmp(name,"FE_HatMullet_01.xDesc"));
    ++previewFindCalls;return previewPresent?previewResource:nullptr;
}
static void* __fastcall previewCreateMock(void* cache,void*,unsigned type,int index) {
    assert(cache==previewManagerPointer && type==9001 && index==-1);
    ++previewCreateCalls;field<unsigned>(previewResource,4)=(9001u<<16)|1;
    return previewAllocationFails?nullptr:previewResource;
}
static void __fastcall previewNameMock(void* resource,void*,const char* name) {
    assert(resource==previewResource && !strcmp(name,"objects:/UI/FE_HatMullet_01.xDesc"));
    ++previewNameCalls;field<const char*>(resource,0x48)=name;previewPresent=true;
}
static BYTE logoScene[0x20], introScene[0x20], autosaveScene[0x20], uiModeQuestion[0x20], otherScene[0x20];
static unsigned flowLookups;
static bool destinationMissing;
static void* __fastcall flowFindMock(void*,void*,unsigned id) {
    ++flowLookups;
    switch(id) {
    case 0x27110005:return logoScene;
    case 0x27110055:return introScene;
    case 0x2711001d:return destinationMissing ? nullptr : autosaveScene;
    case 0x27120006:return destinationMissing ? nullptr : uiModeQuestion;
    case 0x27110020:return otherScene;
    default:return nullptr;
    }
}
static BYTE globalsStorage[0x100], timerStorage[0x80], cameraStorage[0x120], panel[0xb0];
static void* globalsPointer=globalsStorage;
static void* timerPointer=timerStorage;
static bool acceptPlacement=true;
static bool wormSettled;
static unsigned settlingCalls;
static void __fastcall settlingMock(void* state,void*) {
    ++settlingCalls;
    if (field<int>(state,0x7c)==8 && wormSettled) {
        field<int>(state,0x7c)=9;
        field<float>(state,0xd4)=12.0f;
    }
}
static unsigned effectCalls, soundCalls, placeCalls, panelCalls;
static int panelOriginalState=2;
static HRESULT __fastcall effectMock(void*,void*,const char*,unsigned flags,const void*,const void*) {
    assert(flags==16); ++effectCalls; return S_OK;
}
static uintptr_t __cdecl soundMock(const char*,const void*,uintptr_t a,uintptr_t b,uintptr_t c) {
    assert(!a && !b && !c); ++soundCalls; return 77;
}
static void __fastcall placeMock(void* state,void*,const void* position) {
    ++placeCalls;
    if(!acceptPlacement)return;
    PlacementEffect(nullptr,nullptr,"spawn",16,position,nullptr);
    PlacementSound("spawn",position,0,0,0);
    --field<unsigned>(state,0x84); field<int>(state,0x7c)=5;
}
static void __fastcall panelMock(void* state,void*) { ++panelCalls;field<int>(state,0x64)=panelOriginalState; }
static void* __cdecl cameraMock() { return cameraStorage; }
static void __fastcall placementMock(void* state,void*) {
    ++placementCalls;
    field<BYTE>(state,0x8c)=0; // Represent the stock focus bookkeeping.
    if(field<float>(state,0x90)<=10.0f) field<int>(state,0x7c)=6;
}
static bool __fastcall activityMock(void*,void*) { return stockActivity; }
static bool __fastcall hudMock(void*,void*) { return stockHud; }
static bool __fastcall speechMock(void*,void*,float delta) {
    assert(delta==0.125f); ++speechCalls; return stockSpeech;
}
static void __fastcall reactionMock(void* state,void*,float delta) {
    ++reactionCalls;
    if(!BlockingActivity(activities,nullptr) && HudReady(nullptr,nullptr) &&
       !SpeechBlocking(nullptr,nullptr,delta)) field<int>(state,0x48)=17;
}
static int __fastcall pendingMock(void*,void*) {
    for(int i=0;i<3;++i) if(field<int>(wormStorage[i],0x80c)!=18) return i;
    return -1;
}
static bool __fastcall deathMock(void*,void*,unsigned index) {
    assert(index<3); ++started;
    field<int>(wormStorage[index],0x80c)=18;
    field<BYTE>(wormStorage[index],0x1c9d)=1;
    return true;
}
static bool __fastcall updateMock(void* state,void*) {
    ++updates;
    int& sub=field<int>(state,0x4c);
    if(sub==1) {
        if(field<float>(state,0x60)>0) return false;
        sub=2;
    } else if(sub==2) sub=3;
    else if(sub==3) {
        if(BlockingActivity(activities,nullptr)) return false;
        sub=7;
    } else if(sub==7) {
        StartDeaths(manager,nullptr,0); sub=8;
    } else if(sub==8) return !BlockingActivity(activities,nullptr);
    return false;
}
static uintptr_t testAddress(unsigned rva) {
    switch(rva) {
    case 0xe22cd0+62*0x1c:return reinterpret_cast<uintptr_t>(joannaMetadata);
    case 0x856a30:return reinterpret_cast<uintptr_t>(&joannaReadMock);
    case 0x855890:return reinterpret_cast<uintptr_t>(&joannaFindMock);
    case 0x853990:return reinterpret_cast<uintptr_t>(&joannaAppendMock);
    case 0x3a3b80: return reinterpret_cast<uintptr_t>(&rewardLoadMock);
    case 0x3a4360: return reinterpret_cast<uintptr_t>(&rewardGrantMock);
    case 0xe102984: return reinterpret_cast<uintptr_t>(&previewManagerPointer);
    case 0xf105d08: return reinterpret_cast<uintptr_t>(&previewType);
    case 0x4d4b0: return reinterpret_cast<uintptr_t>(&previewFindMock);
    case 0x4d2a0: return reinterpret_cast<uintptr_t>(&previewCreateMock);
    case 0x51f00: return reinterpret_cast<uintptr_t>(&previewNameMock);
    case 0x4d5fb0: return reinterpret_cast<uintptr_t>(&hatFindMock);
    case 0x853790: return reinterpret_cast<uintptr_t>(&hatNodeAllocMock);
    case 0x853690: return reinterpret_cast<uintptr_t>(&hatNodeCtorMock);
    case 0x853970: return reinterpret_cast<uintptr_t>(&hatNodeRefMock);
    case 0x856f00: return reinterpret_cast<uintptr_t>(&hatNodeReleaseMock);
    case 0x858b80: return reinterpret_cast<uintptr_t>(&hatStringAssignMock);
    case 0x8560f0: return reinterpret_cast<uintptr_t>(&hatNodeHashMock);
    case 0x8587a0: return reinterpret_cast<uintptr_t>(&hatStringHashMock);
    case 0x3dffd0: return reinterpret_cast<uintptr_t>(&hatListMock);
    case 0x3e48e0: return reinterpret_cast<uintptr_t>(&hatReserveMock);
    case 0x3d4d10: return reinterpret_cast<uintptr_t>(&hatConstructMock);
    case 0x3fe90: return reinterpret_cast<uintptr_t>(&flowFindMock);
    case 0xdcd5b3c: return reinterpret_cast<uintptr_t>(&wormManager);
    case 0x45b050: return reinterpret_cast<uintptr_t>(&activityMock);
    case 0x413100: return reinterpret_cast<uintptr_t>(&hudMock);
    case 0x36e670: return reinterpret_cast<uintptr_t>(&deathMock);
    case 0x36d7b0: return reinterpret_cast<uintptr_t>(&pendingMock);
    case 0x32cb10: return reinterpret_cast<uintptr_t>(&speechMock);
    case 0x471800: return reinterpret_cast<uintptr_t>(&reactionMock);
    case 0x441c50: return reinterpret_cast<uintptr_t>(&placementMock);
    case 0x441f20: return reinterpret_cast<uintptr_t>(&settlingMock);
    case 0xdcd506c: return reinterpret_cast<uintptr_t>(&globalsPointer);
    case 0xdcd56c4: return reinterpret_cast<uintptr_t>(&timerPointer);
    case 0x3530f0: return reinterpret_cast<uintptr_t>(&effectMock);
    case 0x32d160: return reinterpret_cast<uintptr_t>(&soundMock);
    case 0x4413f0: return reinterpret_cast<uintptr_t>(&placeMock);
    case 0x4f37f0: return reinterpret_cast<uintptr_t>(&panelMock);
    case 0x41a640: return reinterpret_cast<uintptr_t>(&cameraMock);
    default: assert(false); return 0;
    }
}
static void configurationTests() {
    wchar_t directory[MAX_PATH], path[MAX_PATH], besideModule[MAX_PATH], modulePath[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,directory));
    assert(GetTempFileNameW(directory,L"wmp",0,path));
    assert(DeleteFileW(path));
    Settings missing=readSettings(path);
    assert(missing.skipIntros && missing.rapidPlay && missing.rareHats && missing.unusedHats && !missing.unlockAll && !missing.joannaDarkAttack && !missing.log);
    assert(siblingPath(nullptr,L"Plus.ini",besideModule));
    assert(GetModuleFileNameW(nullptr,modulePath,MAX_PATH));
    wchar_t* separator=wcsrchr(modulePath,L'\\'); assert(separator);
    wcscpy_s(separator+1,MAX_PATH-(separator+1-modulePath),L"Plus.ini");
    assert(!wcscmp(besideModule,modulePath));
    for(unsigned mask=0;mask<128;++mask) {
        assert(WritePrivateProfileStringW(L"Settings",L"SkipIntros",mask&1?L"1":L"0",path));
        assert(WritePrivateProfileStringW(L"Settings",L"RapidPlay",mask&2?L"1":L"0",path));
        assert(WritePrivateProfileStringW(L"Settings",L"RareHats",mask&4?L"1":L"0",path));
        assert(WritePrivateProfileStringW(L"Settings",L"UnusedHats",mask&8?L"1":L"0",path));
        assert(WritePrivateProfileStringW(L"Settings",L"UnlockAllRewards",mask&16?L"1":L"0",path));
        assert(WritePrivateProfileStringW(L"Settings",L"JoannaDarkAttack",mask&32?L"1":L"0",path));
        assert(WritePrivateProfileStringW(L"Settings",L"Log",mask&64?L"1":L"0",path));
        settings=readSettings(path);
        assert(settings.skipIntros==bool(mask&1) && settings.rapidPlay==bool(mask&2) && settings.rareHats==bool(mask&4) && settings.unusedHats==bool(mask&8) && settings.unlockAll==bool(mask&16) && settings.joannaDarkAttack==bool(mask&32) && settings.log==bool(mask&64));
        enabled=true; stockHud=false;
        assert(HudReady(nullptr,nullptr)==settings.rapidPlay);
        assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==(settings.skipIntros?autosaveScene:logoScene));
        assert(StartupFlowTarget(nullptr,nullptr,0x27110055)==(settings.skipIntros?uiModeQuestion:introScene));
        // A disabled RapidPlay setting must retain the original damage timer
        // and call count even while SkipIntros is enabled.
        if(!settings.rapidPlay) {
            updateOriginal=reinterpret_cast<UpdateFn>(&updateMock);
            field<int>(turn,0x48)=12;field<int>(turn,0x4c)=1;field<float>(turn,0x60)=2;
            updates=0;UpdateDamage(turn,nullptr);
            assert(updates==1 && field<float>(turn,0x60)==2 && field<int>(turn,0x4c)==1);
            stockSpeech=true;speechCalls=0;
            assert(SpeechBlocking(nullptr,nullptr,0.125f) && speechCalls==1);
        }
        enabled=false;
        assert(!HudReady(nullptr,nullptr));
        assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==logoScene);
    }
    for(const wchar_t* invalid : {L"0",L"2",L"-1",L"true",L"1junk",L""}) {
        assert(WritePrivateProfileStringW(L"Settings",L"RapidPlay",invalid,path));
        assert(!readSettings(path).rapidPlay);
        assert(WritePrivateProfileStringW(L"Settings",L"RareHats",invalid,path));
        assert(!readSettings(path).rareHats);
        assert(WritePrivateProfileStringW(L"Settings",L"UnusedHats",invalid,path));
        assert(!readSettings(path).unusedHats);
        assert(WritePrivateProfileStringW(L"Settings",L"UnlockAllRewards",invalid,path));
        assert(!readSettings(path).unlockAll);
        assert(WritePrivateProfileStringW(L"Settings",L"JoannaDarkAttack",invalid,path));
        assert(!readSettings(path).joannaDarkAttack);
        assert(WritePrivateProfileStringW(L"Settings",L"Log",invalid,path));
        assert(!readSettings(path).log);
    }
    assert(DeleteFileW(path));
    settings={};flowLookups=updates=speechCalls=0;
    puts("PASS: DLL-relative Plus.ini path, opt-in UnlockAllRewards and Log defaults, strict values, all 128 independent setting combinations.");
}

static void unlockAllTests() {
    auto reset = []() {
        memset(rewardManagerStorage, 0, sizeof rewardManagerStorage);
        memset(rewardStorage, 0, sizeof rewardStorage);
        for (unsigned group = 0; group < 12; ++group) {
            for (unsigned i = 0; i < 3; ++i) {
                rewardPointers[group][i] = rewardStorage[group][i];
                field<unsigned>(rewardStorage[group][i], 0x28) = group;
                // Sentinel fields must survive the grant, including XP amounts,
                // seen/equipped state, tier/Forts gates and new-reward flags.
                field<unsigned>(rewardStorage[group][i], 0x1c) = 123;
                field<unsigned>(rewardStorage[group][i], 0x24) = 50;
                field<BYTE>(rewardStorage[group][i], 0x33) = 1;
            }
            unsigned offset = 0x34 + group * 12;
            field<void**>(rewardManagerStorage, offset) = rewardPointers[group];
            field<void**>(rewardManagerStorage, offset + 4) = rewardPointers[group] + 3;
            field<void**>(rewardManagerStorage, offset + 8) = rewardPointers[group] + 3;
        }
        settings = {}; settings.unlockAll = true; enabled = true;
        rewardProfileReady = true; reenterRewardLoad = false;
        rewardLoadCalls = rewardGrantCalls = persistedRewards = 0;
        grantingAllRewards = 0;
    };
    reset(); RewardsLoaded(nullptr, nullptr); assert(!rewardLoadCalls);
    enabled = false; RewardsLoaded(rewardManagerStorage, nullptr);
    assert(rewardLoadCalls == 1 && !rewardGrantCalls);
    enabled = true; settings.unlockAll = false; RewardsLoaded(rewardManagerStorage, nullptr);
    assert(rewardLoadCalls == 2 && !rewardGrantCalls);
    settings.unlockAll = true; rewardProfileReady = false; RewardsLoaded(rewardManagerStorage, nullptr);
    assert(rewardLoadCalls == 3 && !rewardGrantCalls);

    reset();
    // Null slots and already-owned/mismatched records retain their flat indices.
    rewardPointers[0][0] = nullptr;
    field<BYTE>(rewardStorage[0][1], 0x30) = 1;
    field<unsigned>(rewardStorage[0][2], 0x28) = 8;
    reenterRewardLoad = true;
    RewardsLoaded(rewardManagerStorage, nullptr);
    assert(rewardGrantCalls == 30 && persistedRewards == 30 && rewardLoadCalls == 31 && !grantingAllRewards);
    for (unsigned group = 0; group < 12; ++group) {
        for (unsigned i = 0; i < 3; ++i) {
            auto reward = rewardStorage[group][i];
            bool owned = group != 8 && !(group == 0 && i != 1);
            assert(bool(field<BYTE>(reward, 0x30)) == owned);
            assert(!field<BYTE>(reward, 0x31) && field<BYTE>(reward, 0x33) == 1);
            assert(field<unsigned>(reward, 0x1c) == 123 && field<unsigned>(reward, 0x24) == 50);
        }
    }
    RewardsLoaded(rewardManagerStorage, nullptr);
    assert(rewardGrantCalls == 30); // No duplicate native grants on later loads.
    settings.unlockAll = false; RewardsLoaded(rewardManagerStorage, nullptr);
    assert(rewardGrantCalls == 30 && field<BYTE>(rewardStorage[11][2], 0x30));

    // Validate the entire vector layout before granting any earlier reward.
    for (unsigned malformed = 0; malformed < 5; ++malformed) {
        reset(); unsigned offset = 0x34 + 11 * 12;
        uintptr_t begin = field<uintptr_t>(rewardManagerStorage, offset);
        switch (malformed) {
        case 0: field<uintptr_t>(rewardManagerStorage, offset + 4) = begin - 4; break;
        case 1: field<uintptr_t>(rewardManagerStorage, offset + 8) = begin; break;
        case 2: field<uintptr_t>(rewardManagerStorage, offset + 8) = begin + 4097 * 4; break;
        case 3: field<uintptr_t>(rewardManagerStorage, offset + 4) = begin + 1; break;
        case 4: field<uintptr_t>(rewardManagerStorage, offset) = 0; break;
        }
        RewardsLoaded(rewardManagerStorage, nullptr);
        assert(!rewardGrantCalls && !grantingAllRewards);
    }
    reset();
    // Empty groups are valid, including before the first reward definitions load.
    memset(rewardManagerStorage, 0, 0xc4);
    RewardsLoaded(rewardManagerStorage, nullptr);
    assert(!rewardGrantCalls);
    settings = {}; enabled = false;
    puts("PASS: native reward grants after profile readiness, independent enable/trace guards, XP exclusion, null/owned/type guards, stable indices, persistent ownership, reentry, duplicates, malformed/empty vectors.");
}

static void rareHatTests() {
    auto reset=[]() {
        memset(hatStorage,0,sizeof hatStorage);memset(movedHatStorage,0,sizeof movedHatStorage);
        field<BYTE*>(hatListStorage,4)=hatStorage;
        field<BYTE*>(hatListStorage,8)=hatStorage+2*hatRecordSize;
        field<BYTE*>(hatListStorage,12)=hatStorage+2*hatRecordSize;
        field<const char*>(hatStorage,4)="CU_None";
        field<const char*>(hatStorage+hatRecordSize,4)="ExistingDlcHat";
        field<const char*>(hatStorage+hatRecordSize,0x58)="DLC01";
        field<unsigned>(hatStorage+hatRecordSize,0x60)=2;
        hatListResult=hatListStorage;hatListCalls=hatReserveCalls=hatConstructCalls=0;
        hatNodesAllocated=0;failHatNodeAllocation=mismatchHatHash=false;
        settings={};settings.unusedHats=false;enabled=true;
    };
    reset();settings.rareHats=false;
    assert(RareHatList(nullptr,nullptr,5)==hatListStorage && !hatReserveCalls && hatCount(hatListStorage)==2);
    settings.rareHats=true;enabled=false;
    RareHatList(nullptr,nullptr,5);assert(!hatReserveCalls);
    enabled=true;RareHatList(nullptr,nullptr,7);
    assert(requestedHatType==7 && !hatReserveCalls);
    hatListResult=nullptr;assert(!RareHatList(nullptr,nullptr,5));
    hatListResult=hatListStorage;
    assert(RareHatList(nullptr,nullptr,5)==hatListStorage);
    assert(hatReserveCalls==1 && hatConstructCalls==5 && hatCount(hatListStorage)==7);
    assert(!memcmp(hatStorage,movedHatStorage,2*hatRecordSize));
    for(unsigned i=0;i<5;++i) {
        BYTE* record=movedHatStorage+(2+i)*hatRecordSize;
        assert(!strcmp(field<const char*>(record,4),rareHats[i].id));
        assert(!strcmp(field<const char*>(record,8),rareHats[i].name));
        assert(!strcmp(field<const char*>(record,0x10),rareHats[i].id));
        assert(!strcmp(field<const char*>(record,0x14),rareHats[i].preview));
        assert(!strcmp(field<const char*>(record,0x18),"objects:/Hats/"));
        assert(!strcmp(field<const char*>(record,0x2c),"Heads"));
        assert(field<float>(record,0x34)==1.0f && field<unsigned>(record,0x60)==4);
        assert(!field<void*>(record,0x28) && !field<void*>(record,0x54) && !field<void*>(record,0x58));
        void* source=field<void*>(record,0x68);
        assert(source && field<unsigned>(source,4)==1 && !field<BYTE>(record,0));
        // Regression for the real loading-screen crash: match resource lookup
        // dereferences the source node and compares its name hash with the GID.
        assert(hatNodeHashMock(source,nullptr)==hatStringHashMock(rareHats[i].id));
    }
    RareHatList(nullptr,nullptr,5);
    assert(hatReserveCalls==1 && hatConstructCalls==5 && hatCount(hatListStorage)==7);
    reset();field<const char*>(hatStorage+hatRecordSize,4)=rareHats[0].id;
    RareHatList(nullptr,nullptr,5);
    assert(hatConstructCalls==4 && hatCount(hatListStorage)==6);
    assert(!memcmp(hatStorage,movedHatStorage,2*hatRecordSize));
    reset();field<const char*>(hatStorage,4)="NoStockSentinel";
    RareHatList(nullptr,nullptr,5);assert(!hatReserveCalls);
    reset();field<BYTE*>(hatListStorage,8)=hatStorage+1;
    RareHatList(nullptr,nullptr,5);assert(!hatReserveCalls);
    reset();field<BYTE*>(hatListStorage,12)=hatStorage+1;
    RareHatList(nullptr,nullptr,5);assert(!hatReserveCalls);
    reset();failHatNodeAllocation=true;
    RareHatList(nullptr,nullptr,5);assert(!hatConstructCalls && hatCount(hatListStorage)==2);
    reset();mismatchHatHash=true;
    RareHatList(nullptr,nullptr,5);assert(!hatConstructCalls && hatCount(hatListStorage)==2);
    for(unsigned i=0;i<hatNodesAllocated;++i)assert(!field<unsigned>(hatSourceStorage[i],4));
    settings={};
    puts("PASS: five appended Rare definitions, native vector/source ownership, match-loading GID hash lookup, unchanged old indices and DLC/platform gates, duplicate/partial-list guards, disabled/null/unrelated/malformed lists, source allocation/hash failure cleanup.");
}

static void unusedHatTests() {
    for(unsigned mask=0;mask<4;++mask) {
        memset(hatStorage,0,sizeof hatStorage);memset(movedHatStorage,0,sizeof movedHatStorage);
        field<BYTE*>(hatListStorage,4)=hatStorage;
        field<BYTE*>(hatListStorage,8)=hatStorage+2*hatRecordSize;
        field<BYTE*>(hatListStorage,12)=hatStorage+2*hatRecordSize;
        field<const char*>(hatStorage,4)="CU_None";
        field<const char*>(hatStorage+hatRecordSize,4)="ExistingDlcHat";
        field<const char*>(hatStorage+hatRecordSize,0x58)="DLC01";
        hatListResult=hatListStorage;hatListCalls=hatReserveCalls=hatConstructCalls=0;
        hatNodesAllocated=0;failHatNodeAllocation=mismatchHatHash=false;
        settings={false,false,bool(mask&1),bool(mask&2)};enabled=true;
        RareHatList(nullptr,nullptr,5);
        unsigned expected=2+(mask&1?5:0)+(mask&2?5:0);
        assert(hatCount(hatListStorage)==expected && hatConstructCalls==expected-2);
        BYTE* begin=field<BYTE*>(hatListStorage,4);
        assert(!memcmp(begin,hatStorage,2*hatRecordSize));
        for(unsigned group=0;group<2;++group) {
            const HatDefinition* hats=group?unusedHats:rareHats;
            for(unsigned i=0;i<5;++i) {
                assert(containsHat(hatListStorage,expected,hats[i].id)==bool(mask&(1u<<group)));
                if(!(mask&(1u<<group)))continue;
                BYTE* record=static_cast<BYTE*>(hatFindMock(hatListStorage,nullptr,hats[i].id));
                assert(record && !strcmp(field<const char*>(record,8),hats[i].name));
                assert(!strcmp(field<const char*>(record,0x10),hats[i].id));
                assert(!strcmp(field<const char*>(record,0x14),hats[i].preview));
                void* source=field<void*>(record,0x68);
                assert(source && field<unsigned>(source,4)==1);
            }
        }
        unsigned reserves=hatReserveCalls;
        RareHatList(nullptr,nullptr,5);
        assert(hatCount(hatListStorage)==expected && hatConstructCalls==expected-2 && hatReserveCalls==reserves);
        // A partial unused list must append only missing entries.
        if(mask==2) {
            field<BYTE*>(hatListStorage,8)=begin+3*hatRecordSize;
            RareHatList(nullptr,nullptr,5);
            assert(hatCount(hatListStorage)==7 && hatConstructCalls==9);
        }
    }
    settings={};
    puts("PASS: independent Rare/Unused hat groups, all ten native identities and previews, preserved stock entries, repeated and partial-list lookups.");
}

static void mulletPreviewTests() {
    auto reset=[]() {
        memset(previewManagerStorage,0,sizeof previewManagerStorage);
        memset(previewBank,0,sizeof previewBank);memset(previewResource,0,sizeof previewResource);
        previewManagerPointer=previewManagerStorage;previewType=9001;mulletPreviewManager=nullptr;
        field<void*>(previewManagerStorage,8)=previewBank;
        field<void*>(previewBank+0x3c,4)=previewResource;
        previewFindCalls=previewCreateCalls=previewNameCalls=0;
        previewPresent=previewAllocationFails=false;settings={};enabled=true;
    };
    reset();settings.unusedHats=false;ensureMulletPreview();assert(!previewFindCalls);
    settings.unusedHats=true;enabled=false;ensureMulletPreview();assert(!previewFindCalls);
    reset();previewManagerPointer=nullptr;ensureMulletPreview();assert(!previewCreateCalls);
    reset();previewType=0;ensureMulletPreview();assert(!previewCreateCalls);
    reset();field<void*>(previewManagerStorage,8)=nullptr;ensureMulletPreview();assert(!previewCreateCalls);
    reset();field<void*>(previewBank+0x3c,4)=nullptr;ensureMulletPreview();assert(!previewCreateCalls && !mulletPreviewManager);
    // Native frontend banks have null optional streaming callbacks. They must
    // still register the descriptor and let the normal renderer load it.
    field<void*>(previewBank+0x3c,4)=previewResource;ensureMulletPreview();
    assert(previewCreateCalls==1 && previewNameCalls==1 && previewFindCalls==2);
    assert(field<unsigned>(previewResource,4));ensureMulletPreview();assert(previewCreateCalls==1);
    reset();previewPresent=true;ensureMulletPreview();assert(previewFindCalls==1 && !previewCreateCalls);
    reset();previewAllocationFails=true;ensureMulletPreview();ensureMulletPreview();assert(previewCreateCalls==1 && !previewNameCalls);
    previewManagerPointer=nullptr;mulletPreviewManager=nullptr;settings={};
    puts("PASS: missing Mullet preview registers installed xDesc with native cache ownership and a valid handle, null streaming callbacks, existing-entry/readiness/disabled/failure guards.");
}

static void joannaTests() {
    hatNodesAllocated=0;failHatNodeAllocation=mismatchHatHash=false;
    joannaNativeReads=joannaAvailabilityCalls=0;
    joannaNameOriginal=&joannaNameMock;joannaReadOriginal=&joannaReadMock;joannaAvailableOriginal=&joannaAvailableMock;
    joannaReadSource=createHatSource("Equip");assert(joannaReadSource);
    assert(setJoannaProperty(joannaReadSource,"PayloadMeshName","stock-fragment-model"));
    assert(setJoannaProperty(joannaReadSource,"ShockDamageMax","45",5));
    assert(setJoannaProperty(joannaReadSource,"Mass","1.25",6));
    assert(setJoannaProperty(joannaReadSource,"RequiresAim","false"));
    assert(setJoannaProperty(joannaReadSource,"RememberAsDefault","false"));
    assert(setJoannaProperty(joannaReadSource,"Gadget.StockOnly","remove-me"));
    assert(setJoannaProperty(joannaReadSource,"PostProcess.StockOnly","remove-me"));
    assert(setJoannaProperty(joannaReadSource,"Ingredients.StockOnly","remove-me"));
    joannaNativeSource=createHatSource("Equip");assert(joannaNativeSource);
    assert(setJoannaProperty(joannaNativeSource,"Gadget.SatelliteMeshName","JoannaDark"));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.StrikeNarrow.NumBullets","50",5));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.StrikeWide.NumBullets","50",5));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.TracerColour.blue","0.9",6));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.TracerThickness","6.0",6));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.LandGougeRadius","90",5));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.FireAnim_RapidFire","Fire2"));
    assert(setJoannaProperty(joannaNativeSource,"Gadget.NativeOnly","opaque-setting"));
    assert(setJoannaProperty(joannaNativeSource,"PostProcess.NativeOnly","effect-marker"));
    assert(setJoannaProperty(joannaNativeSource,"Ingredients.IsAVariationOf","OMGStrike"));
    assert(setJoannaProperty(joannaNativeSource,"EnableInCaverns","false"));
    memset(joannaMetadata,0,sizeof joannaMetadata);
    field<const char*>(joannaMetadata,0)="ClusterFragmentMk2";
    field<unsigned>(joannaMetadata,4)=0;field<unsigned>(joannaMetadata,8)=35;field<unsigned>(joannaMetadata,12)=8;
    BYTE before[sizeof joannaMetadata];memcpy(before,joannaMetadata,sizeof before);
    settings={};enabled=true;void* result=nullptr;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !joannaNativeReads);releaseJoannaNode(result);
    settings.joannaDarkAttack=true;enabled=false;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !joannaNativeReads);releaseJoannaNode(result);enabled=true;
    ReadEquipment(&result,"scripts:/Scripts/Equip/AirStrike.txt");
    assert(result==joannaReadSource && !joannaNativeReads);releaseJoannaNode(result);
    void* saved=joannaNativeSource;joannaNativeSource=nullptr;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !memcmp(before,joannaMetadata,sizeof before));releaseJoannaNode(result);
    joannaNativeSource=saved;
    assert(setJoannaProperty(saved,"Gadget.SatelliteMeshName","wrong-weapon"));
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !memcmp(before,joannaMetadata,sizeof before));releaseJoannaNode(result);
    assert(setJoannaProperty(saved,"Gadget.SatelliteMeshName","JoannaDark"));
    failHatNodeAllocation=true;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !memcmp(before,joannaMetadata,sizeof before));releaseJoannaNode(result);
    failHatNodeAllocation=false;
    unsigned firstTemporary=hatNodesAllocated;
    failNodeAfter=firstTemporary+8;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !memcmp(before,joannaMetadata,sizeof before));releaseJoannaNode(result);
    for(unsigned i=firstTemporary;i<hatNodesAllocated;++i)assert(!field<unsigned>(hatSourceStorage[i],4));
    failNodeAfter=~0u;
    // Fail after the fragment clone, during the native overlay; partial copies
    // and the temporary native-source reference must also be released.
    firstTemporary=hatNodesAllocated;failNodeAfter=firstTemporary+18;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaReadSource && !memcmp(before,joannaMetadata,sizeof before));releaseJoannaNode(result);
    for(unsigned i=firstTemporary;i<hatNodesAllocated;++i)assert(!field<unsigned>(hatSourceStorage[i],4));
    failNodeAfter=~0u;
    ReadEquipment(&result,"SCRIPTS:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result && result!=joannaReadSource && result!=joannaNativeSource);
    assert(field<unsigned>(joannaMetadata,4)==25 && field<unsigned>(joannaMetadata,8)==35);
    assert(field<unsigned>(joannaMetadata,12)==9 && field<unsigned short>(joannaMetadata,16)==61);
    assert(field<unsigned short>(joannaMetadata,18)==62 && field<unsigned short>(joannaMetadata,20)==115);
    assert(!strcmp(field<const char*>(joannaMetadata,0),"ClusterFragmentMk2"));
    auto get=[](void* parent,const char* name){return joannaFindMock(parent,nullptr,name,nullptr);};
    auto value=[](void* node){assert(node);return field<const char*>(node,12);};
    auto type=[&](void* node){return *reinterpret_cast<const unsigned*>(value(node)-12);};
    assert(!strcmp(value(get(result,"PayloadMeshName")),"stock-fragment-model"));
    assert(!strcmp(value(get(result,"ShockDamageMax")),"45") && type(get(result,"ShockDamageMax"))==5);
    assert(type(get(result,"Mass"))==6);
    assert(!get(result,"RequiresAim") && !get(result,"RememberAsDefault")); // Defaults merge supplies these.
    void* gadget=get(result,"Gadget");assert(gadget && gadget!=get(saved,"Gadget"));
    assert(!get(gadget,"StockOnly") && !strcmp(value(get(gadget,"NativeOnly")),"opaque-setting"));
    assert(!strcmp(value(get(gadget,"FireAnim_RapidFire")),"Fire2"));
    assert(!strcmp(value(get(gadget,"TracerThickness")),"6.0") && type(get(gadget,"TracerThickness"))==6);
    assert(!strcmp(value(get(gadget,"LandGougeRadius")),"90") && type(get(gadget,"LandGougeRadius"))==5);
    assert(!strcmp(value(get(get(gadget,"StrikeNarrow"),"NumBullets")),"50"));
    assert(!strcmp(value(get(get(result,"Ingredients"),"IsAVariationOf")),"OMGStrike"));
    assert(!get(get(result,"Ingredients"),"StockOnly"));
    assert(!get(get(result,"PostProcess"),"StockOnly") && get(get(result,"PostProcess"),"NativeOnly"));
    assert(get(joannaReadSource,"RequiresAim") && get(get(joannaReadSource,"Gadget"),"StockOnly"));
    assert(!get(get(joannaReadSource,"Gadget"),"SatelliteMeshName"));
    assert(!strcmp(JoannaWeaponName(62),"Joanna Dark Attack") && !strcmp(JoannaWeaponName(20),"stock weapon"));
    enabled=false;assert(!strcmp(JoannaWeaponName(62),"stock weapon"));enabled=true;
    releaseJoannaNode(result);
    unsigned allocated=hatNodesAllocated,nativeReads=joannaNativeReads;
    failHatNodeAllocation=true;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");
    assert(result==joannaCachedSource && hatNodesAllocated==allocated && joannaNativeReads==nativeReads);releaseJoannaNode(result);
    failHatNodeAllocation=false;
    assert(setJoannaProperty(saved,"Gadget.TracerThickness","99",5));
    assert(!strcmp(value(get(get(joannaCachedSource,"Gadget"),"TracerThickness")),"6.0"));
    field<unsigned>(joannaMetadata,8)=99;
    ReadEquipment(&result,"scripts:/Scripts/Equip/ClusterFragmentMk2.txt");assert(result==joannaReadSource);releaseJoannaNode(result);
    releaseJoannaNode(joannaReadSource);
    joannaReadSource=createHatSource("Scheme");assert(joannaReadSource);
    assert(setJoannaProperty(joannaReadSource,"AirStrike.ammo","7",5));
    unsigned schemeAllocations=hatNodesAllocated;
    ReadEquipment(&result,"scripts:/Scripts/Schemes/FullWormage.txt");
    assert(result==joannaReadSource && hatNodesAllocated==schemeAllocations && joannaNativeReads==nativeReads);
    assert(type(get(get(result,"AirStrike"),"ammo"))==5 && !strcmp(value(get(get(result,"AirStrike"),"ammo")),"7"));
    assert(!get(result,"ClusterFragmentMk2"));releaseJoannaNode(result);
    ReadEquipment(&result,"scripts:/Scripts/Schemes/Intermediate.txt");assert(result==joannaReadSource);releaseJoannaNode(result);
    ReadEquipment(&result,"scripts:/Scripts/Equip/PerfectDarkStrike.txt");assert(result==saved);releaseJoannaNode(result);
    assert(JoannaWeaponAvailable("ClusterFragmentMk2") && !joannaAvailabilityCalls);
    assert(JoannaWeaponAvailable("clusterfragmentmk2") && !joannaAvailabilityCalls);
    assert(!JoannaWeaponAvailable("TF2SentryGun") && joannaAvailabilityCalls==1);
    enabled=false;assert(!JoannaWeaponAvailable("ClusterFragmentMk2") && joannaAvailabilityCalls==2);
    enabled=true;settings.joannaDarkAttack=false;
    assert(!JoannaWeaponAvailable("ClusterFragmentMk2") && joannaAvailabilityCalls==3);
    settings.joannaDarkAttack=true;joannaAvailabilityResult=true;
    assert(JoannaWeaponAvailable("GoatOnARope") && joannaAvailabilityCalls==4);
    releaseJoannaNode(joannaReadSource);joannaReadSource=nullptr;
    releaseJoannaNode(joannaNativeSource);joannaNativeSource=nullptr;
    releaseJoannaNode(joannaCachedSource);joannaCachedSource=nullptr;
    for(unsigned i=0;i<hatNodesAllocated;++i)assert(!field<unsigned>(hatSourceStorage[i],4));
    settings={};joannaNameOriginal=nullptr;
    puts("PASS: native PerfectDarkStrike loading, generic owned overlay with numeric types, original gadget/effects/recipe, preserved payload physics, native defaults for inventory, missing/foreign/allocation/metadata failure cleanup, cache isolation, untouched schemes/DLC gates.");
}

static void rapidVehicleDeathConfigTests() {
    auto* source=createHatSource("Vehicle");assert(source);
    assert(setJoannaProperty(source,"DeathDelaySecs","3",6));
    joannaReadSource=source;scriptReadOriginal=&joannaReadMock;
    enabled=true;settings={};settings.rapidPlay=true;
    assert(isVehicleDefaultsConfigPath("scripts:/Scripts/Vehicles/Defaults.txt"));
    assert(isVehicleDefaultsConfigPath("CommonData\\Scripts\\Vehicles\\Defaults.txt"));
    assert(isVehicleDefaultsConfigPath("SCRIPTS:/SCRIPTS/VEHICLES/DEFAULTS.TXT"));
    assert(!isVehicleDefaultsConfigPath("scripts:/Scripts/Vehicles/Mech.txt"));
    assert(!isVehicleDefaultsConfigPath("scripts:/Scripts/Equip/Defaults.txt"));
    assert(!isVehicleDefaultsConfigPath("scripts:/Scripts/Vehicles/Defaults.txt.bak"));
    void* result=nullptr;
    ReadScript(&result,"scripts:/Scripts/Vehicles/Mech.txt");
    assert(result==source && !strcmp(value(joannaFindMock(source,nullptr,"DeathDelaySecs",nullptr)),"3"));
    releaseJoannaNode(result);
    ReadScript(&result,"scripts:/Scripts/Vehicles/Defaults.txt");
    assert(result && result!=source);
    void* delay=joannaFindMock(result,nullptr,"DeathDelaySecs",nullptr);
    assert(delay && !strcmp(value(delay),"0") && type(delay)==6);
    assert(!strcmp(value(joannaFindMock(source,nullptr,"DeathDelaySecs",nullptr)),"3"));
    releaseJoannaNode(result);
    settings.rapidPlay=false;
    ReadScript(&result,"scripts:/Scripts/Vehicles/Defaults.txt");
    assert(result==source && !strcmp(value(joannaFindMock(result,nullptr,"DeathDelaySecs",nullptr)),"3"));
    releaseJoannaNode(result);
    releaseJoannaNode(source);joannaReadSource=nullptr;scriptReadOriginal=nullptr;
    settings={};enabled=false;
    puts("PASS: RapidPlay targets only vehicle Defaults.txt, sets inherited DeathDelaySecs to zero without changing source/type when disabled or on Mech-specific files.");
}

static void joannaTextTests() {
    joannaTextOriginal=&joannaTextMock;joannaTranslated=nullptr;joannaTextCalls=0;
    enabled=true;settings={};settings.joannaDarkAttack=true;
    assert(joannaTextHash("")==0x1505);
    for(const auto& entry:joannaTexts) {
        unsigned hash=joannaTextHash(entry.key);
        assert(!strcmp(JoannaTextLookup(hash),entry.text) && joannaLastTextHash==hash);
    }
    assert(!strcmp(JoannaTextLookup(joannaTextHash("GUI.WeaponPanel.ClusterFragmentMk2.Prompt")),"Joanna Dark Attack"));
    assert(!strcmp(JoannaTextLookup(joannaTextHash("Com.BulletStrikeMode.Narrow")),"Focused Fire"));
    assert(!strcmp(JoannaTextLookup(joannaTextHash("Com.BulletStrikeMode.Wide")),"Wide Fire"));
    assert(!JoannaTextLookup(joannaTextHash("Gui.WeaponPanel.Uzi.Prompt")));
    assert(!JoannaTextLookup(joannaTextHash("Com.BulletStrikeMode.Narrow.Extra")));
    assert(!JoannaTextLookup(0));
    joannaTranslated="existing native translation";
    for(const auto& entry:joannaTexts)assert(JoannaTextLookup(joannaTextHash(entry.key))==joannaTranslated);
    assert(JoannaTextLookup(0)==joannaTranslated);
    joannaTranslated=nullptr;enabled=false;
    assert(!JoannaTextLookup(joannaTextHash(joannaTexts[0].key)));
    enabled=true;settings.joannaDarkAttack=false;
    assert(!JoannaTextLookup(joannaTextHash(joannaTexts[0].key)));
    joannaTextOriginal=nullptr;settings={};enabled=false;
    puts("PASS: targeted missing Joanna menu/mode/description text, native translation precedence, unrelated/disabled guards and original hash forwarding.");
}

static void joannaTrampolineTests() {
    auto image=static_cast<BYTE*>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(image);
    base=reinterpret_cast<uintptr_t>(image);settings={};
    assert(prepareJoanna() && !joannaNameTrampoline && !joannaReadTrampoline && !joannaAvailableTrampoline && !joannaTextTrampoline);
    const BYTE name[]={0x55,0x8b,0xec,0x81,0xec,0x04,0x08,0x00,0x00};
    const BYTE read[]={0x55,0x8b,0xec,0x83,0xec,0x1c};
    const BYTE available[]={0x55,0x8b,0xec,0x6a,0xff};
    BYTE text[]={0x55,0x8b,0xec,0x83,0x3d,0,0,0,0,0};
    *reinterpret_cast<uint32_t*>(text+5)=static_cast<uint32_t>(base+0xdd219b0);
    memcpy(image+0x31a400,name,sizeof name);memcpy(image+0x856a30,read,sizeof read);memcpy(image+0x31b0a0,available,sizeof available);
    memcpy(image+0x442250,text,sizeof text);
    settings.joannaDarkAttack=true;image[0x31a408]=1;
    assert(!prepareJoanna() && !joannaNameTrampoline);image[0x31a408]=0;
    image[0x856a35]=0;assert(!prepareScriptReader() && !joannaReadTrampoline);image[0x856a35]=0x1c;
    image[0x31b0a4]=0;assert(!prepareJoanna() && !joannaAvailableTrampoline);image[0x31b0a4]=0xff;
    image[0x442259]=1;assert(!prepareJoanna() && !joannaTextTrampoline);image[0x442259]=0;
    image[0x442255]^=1;assert(!prepareJoanna() && !joannaTextTrampoline);image[0x442255]^=1;
    assert(prepareJoanna() && prepareScriptReader());
    auto check=[](BYTE* stub,unsigned rva,const BYTE* bytes,unsigned count) {
        assert(stub && !memcmp(stub,bytes,count) && stub[count]==0xe9);
        auto destination=reinterpret_cast<uintptr_t>(stub+count+5)+*reinterpret_cast<int32_t*>(stub+count+1);
        assert(destination==base+rva+count);
    };
    check(joannaNameTrampoline,0x31a400,name,sizeof name);
    check(joannaReadTrampoline,0x856a30,read,sizeof read);
    check(joannaAvailableTrampoline,0x31b0a0,available,sizeof available);
    check(joannaTextTrampoline,0x442250,text,sizeof text);
    assert(VirtualFree(joannaTextTrampoline,0,MEM_RELEASE));joannaTextTrampoline=nullptr;joannaTextOriginal=nullptr;
    assert(VirtualFree(joannaNameTrampoline,0,MEM_RELEASE));assert(VirtualFree(joannaReadTrampoline,0,MEM_RELEASE));assert(VirtualFree(joannaAvailableTrampoline,0,MEM_RELEASE));
    joannaNameTrampoline=joannaReadTrampoline=joannaAvailableTrampoline=nullptr;
    joannaNameOriginal=nullptr;joannaReadOriginal=nullptr;joannaAvailableOriginal=nullptr;
    assert(VirtualFree(image,0,MEM_RELEASE));base=0;settings={};
    puts("PASS: complete 9/6/5/10-byte native prologues, ASLR language pointer validation, disabled preparation, executable trampolines and exact return destinations.");
}

static void patchSelectionTests() {
    auto image=static_cast<BYTE*>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    assert(image);
    base=reinterpret_cast<uintptr_t>(image);
    trampoline=reinterpret_cast<BYTE*>(&updateMock);
    for(unsigned mask=0;mask<64;++mask) {
        settings={bool(mask&1),bool(mask&2),bool(mask&4),bool(mask&8),bool(mask&16),bool(mask&32)};
        for(auto& p:patches) memcpy(image+p.rva,p.original,5);
        // Unselected sites need not match: this proves disabled features are
        // neither validated nor touched, including during restoration.
        for(auto& p:patches) if(!selected(p)) image[p.rva]=0xcc;
        assert(control(1)==0);
        unsigned count=0;
        for(auto& p:patches) {
            assert(p.applied==selected(p));
            if(p.applied) {
                BYTE expected[5];jump(expected,p.opcode,base+p.rva,reinterpret_cast<uintptr_t>(p.target));
                assert(!memcmp(image+p.rva,expected,5));++count;
            } else assert(image[p.rva]==0xcc);
        }
        unsigned expected=(mask&1?2u:0u)+(mask&2?17u:0u)+(mask&12?2u:0u)+
            (mask&16?4u:0u)+(mask&32?3u:0u)+((mask&2)||(mask&32)?1u:0u);
        assert(count==expected);
        assert(control(0)==0 && !installed && !enabled);
        for(auto& p:patches) {
            assert(!p.applied);
            if(selected(p)) assert(!memcmp(image+p.rva,p.original,5));
            else assert(image[p.rva]==0xcc);
        }
    }
    settings={true,true,true,true,true,true};
    for(auto& mismatch:patches) {
        for(auto& p:patches) memcpy(image+p.rva,p.original,5);
        image[mismatch.rva]=0xcc;
        assert(control(1)==11 && !installed);
        for(auto& p:patches) {
            assert(!p.applied);
            if(&p!=&mismatch) assert(!memcmp(image+p.rva,p.original,5));
        }
    }
    assert(VirtualFree(image,0,MEM_RELEASE));base=0;trampoline=nullptr;
    puts("PASS: all 64 hook combinations (up to 29 sites), untouched disabled sites, exact restoration, all-or-nothing validation at every signature.");
}

int main() {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    field<uintptr_t>(logoScene,0)=field<uintptr_t>(introScene,0)=field<uintptr_t>(otherScene,0)=base+0xeea7bc;
    field<unsigned>(logoScene,4)=0x27110005;field<unsigned>(introScene,4)=0x27110055;
    field<unsigned>(autosaveScene,4)=0x2711001d;field<unsigned>(uiModeQuestion,4)=0x27120006;
    field<unsigned>(otherScene,4)=0x27110020;
    configurationTests();
    enabled=false;assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==logoScene && flowLookups==1);
    enabled=true;assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==autosaveScene && flowLookups==3);
    assert(StartupFlowTarget(nullptr,nullptr,0x27110055)==uiModeQuestion && flowLookups==5);
    assert(StartupFlowTarget(nullptr,nullptr,0x27110020)==otherScene && flowLookups==6);
    assert(StartupFlowTarget(nullptr,nullptr,0x2711001d)==autosaveScene && flowLookups==7);
    assert(!StartupFlowTarget(nullptr,nullptr,0x27110099) && flowLookups==8);
    destinationMissing=true;
    assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==logoScene);
    assert(StartupFlowTarget(nullptr,nullptr,0x27110055)==introScene);
    destinationMissing=false;field<unsigned>(autosaveScene,4)=0x271100ff;
    assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==logoScene);
    field<unsigned>(autosaveScene,4)=0x2711001d;
    field<uintptr_t>(logoScene,0)=0;
    assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==logoScene);
    field<uintptr_t>(logoScene,0)=base+0xeea7bc;field<unsigned>(logoScene,4)=0x271100ff;
    assert(StartupFlowTarget(nullptr,nullptr,0x27110005)==logoScene);
    field<unsigned>(logoScene,4)=0x27110005;
    field<unsigned>(manager,0x2c)=3;
    for(unsigned i=0;i<3;++i) {
        field<void*>(manager,0x3c+4*i)=wormStorage[i];
        field<int>(wormStorage[i],0x58)=100+i;
        field<int>(wormStorage[i],0x1d10)=9;
    }
    enabled=false; stockHud=false; stockActivity=true; currentTurn=turn;
    assert(!HudReady(nullptr,nullptr));
    assert(BlockingActivity(activities,nullptr));
    enabled=true; assert(HudReady(nullptr,nullptr));
    assert(!BlockingActivity(activities,nullptr));
    // Pending death explosions must block even before their activity is marked.
    field<int>(wormStorage[0],0x80c)=18;
    field<BYTE>(wormStorage[0],0x1c9d)=1;
    assert(BlockingActivity(activities,nullptr));
    field<BYTE>(wormStorage[0],0x1c9d)=0;
    field<unsigned>(activities,0x2c)=1;
    field<int>(activities,0x34)=100;
    field<BYTE>(activities,0x38)=1;
    field<int>(activities,0x3c)=9;
    assert(!BlockingActivity(activities,nullptr));
    // A different activity on the same worm is still gameplay-blocking.
    field<int>(activities,0x3c)=3;
    assert(BlockingActivity(activities,nullptr));
    field<int>(activities,0x3c)=9;
    field<unsigned>(activities,0x2c)=2;
    field<int>(activities,0x4c)=999;
    field<BYTE>(activities,0x50)=1;
    assert(BlockingActivity(activities,nullptr));
    field<BYTE>(activities,0x50)=0;
    assert(!BlockingActivity(activities,nullptr));
    // A living worm with the same kind must not become cosmetic.
    field<int>(wormStorage[0],0x80c)=0;
    assert(BlockingActivity(activities,nullptr));
    field<unsigned>(activities,0x2c)=0;
    updateOriginal=reinterpret_cast<UpdateFn>(&updateMock);
    field<int>(turn,0x48)=12; field<int>(turn,0x4c)=1;
    field<float>(turn,0x60)=2;
    enabled=false; updates=0;
    assert(!UpdateDamage(turn,nullptr));
    assert(updates==1 && field<int>(turn,0x4c)==1 && field<float>(turn,0x60)==2);
    enabled=true; updates=started=0;
    assert(!UpdateDamage(turn,nullptr));
    assert(field<int>(turn,0x4c)==8 && updates==4 && started==3);
    // Do not advance until all three queued explosions have been submitted.
    assert(!UpdateDamage(turn,nullptr));
    for(unsigned i=0;i<3;++i) field<BYTE>(wormStorage[i],0x1c9d)=0;
    assert(UpdateDamage(turn,nullptr));
    // The later reaction phase repeats HUD and speech gates. Keep stock
    // behavior in trace mode and preserve blocking gameplay in Rapid Play.
    field<int>(turn,0x48)=16;
    enabled=false; stockActivity=false; stockHud=true; stockSpeech=true;
    UpdateReactions(turn,nullptr,0.125f);
    assert(field<int>(turn,0x48)==16 && speechCalls==1);
    enabled=true; stockHud=false;
    field<unsigned>(activities,0x2c)=1;
    field<int>(activities,0x34)=999;
    field<BYTE>(activities,0x38)=1;
    UpdateReactions(turn,nullptr,0.125f);
    assert(field<int>(turn,0x48)==16);
    field<BYTE>(activities,0x38)=0;
    UpdateReactions(turn,nullptr,0.125f);
    assert(field<int>(turn,0x48)==17 && speechCalls==1 && reactionCalls==3);
    // Restrict the new skip to manual placement's completed-placement state.
    field<int>(placement,0x78)=4; field<int>(placement,0x7c)=5;
    field<float>(placement,0x90)=20.0f; field<BYTE>(placement,0x8c)=1;
    enabled=false; UpdatePlacementDelay(placement,nullptr);
    assert(field<float>(placement,0x90)==20.0f && field<int>(placement,0x7c)==5);
    assert(!field<BYTE>(placement,0x8c));
    enabled=true; field<int>(placement,0x78)=2;
    UpdatePlacementDelay(placement,nullptr);
    assert(field<float>(placement,0x90)==20.0f && field<int>(placement,0x7c)==5);
    field<int>(placement,0x78)=4; field<int>(placement,0x7c)=4;
    UpdatePlacementDelay(placement,nullptr);
    assert(field<float>(placement,0x90)==20.0f && field<int>(placement,0x7c)==4);
    field<int>(placement,0x7c)=5; field<BYTE>(placement,0x8c)=1;
    UpdatePlacementDelay(placement,nullptr);
    assert(field<float>(placement,0x90)==-1.0f && field<int>(placement,0x7c)==6);
    assert(!field<BYTE>(placement,0x8c) && placementCalls==4);
    // Expire the handoff only after stock settling succeeds; never skip physics.
    field<float>(timerStorage,0x3c)=10.0f;
    field<int>(placement,0x7c)=8;field<float>(placement,0xd4)=99.0f;
    UpdatePlacementSettling(placement,nullptr);
    assert(field<int>(placement,0x7c)==8 && field<float>(placement,0xd4)==99.0f);
    wormSettled=true;enabled=false;UpdatePlacementSettling(placement,nullptr);
    assert(field<int>(placement,0x7c)==9 && field<float>(placement,0xd4)==12.0f);
    enabled=true;field<int>(placement,0x78)=2;field<int>(placement,0x7c)=8;
    UpdatePlacementSettling(placement,nullptr);
    assert(field<float>(placement,0xd4)==12.0f);
    field<int>(placement,0x78)=4;field<int>(placement,0x7c)=8;
    settings.rapidPlay=false;UpdatePlacementSettling(placement,nullptr);
    assert(field<float>(placement,0xd4)==12.0f);
    settings.rapidPlay=true;field<int>(placement,0x7c)=8;
    timerPointer=nullptr;UpdatePlacementSettling(placement,nullptr);
    assert(field<int>(placement,0x7c)==9 && field<float>(placement,0xd4)==-1.0f);
    timerPointer=timerStorage;field<int>(placement,0x7c)=8;
    field<float>(timerStorage,0x3c)=0.0f;UpdatePlacementSettling(placement,nullptr);
    assert(field<int>(placement,0x7c)==9 && field<float>(placement,0xd4)<0.0f);
    // State 9 must not be rearmed or rewritten on a repeated call.
    field<float>(placement,0xd4)=-0.25f;
    field<float>(timerStorage,0x3c)=10.25f;UpdatePlacementSettling(placement,nullptr);
    assert(field<float>(placement,0xd4)==-0.25f && settlingCalls==7);
    // Suppress only accepted manual-placement cosmetics; keep the original
    // position validation, bookkeeping and other sounds/effects intact.
    field<unsigned>(globalsStorage,0x44)=1;
    field<DWORD>(timerStorage,0x44)=100;
    field<float>(cameraStorage,0x108)=1080.0f;
    field<int>(panel,0x90)=1;
    field<int>(placement,0x78)=4;field<int>(placement,0x7c)=4;field<unsigned>(placement,0x84)=3;
    enabled=false;PlaceWorm(placement,nullptr,nullptr);
    assert(effectCalls==1 && soundCalls==1 && !placedNameplates[1].worm && !instantPlacementContext);
    enabled=true;field<int>(placement,0x78)=2;field<int>(placement,0x7c)=4;
    PlaceWorm(placement,nullptr,nullptr);
    assert(effectCalls==2 && soundCalls==2 && !placedNameplates[1].worm);
    field<int>(placement,0x78)=4;field<int>(placement,0x7c)=4;acceptPlacement=false;
    PlaceWorm(placement,nullptr,nullptr);
    assert(!placedNameplates[1].worm && field<unsigned>(placement,0x84)==1);
    acceptPlacement=true;PlaceWorm(placement,nullptr,nullptr);
    assert(effectCalls==2 && soundCalls==2 && placeCalls==4 && !instantPlacementContext);
    assert(placedNameplates[1].worm==wormStorage[1] && !field<unsigned>(placement,0x84));
    // An unrelated worm's label and damage HUD state must stay unchanged.
    field<int>(panel,0x90)=0;UpdatePlacedNameplate(panel,nullptr);
    assert(field<int>(panel,0x64)==2 && placedNameplates[1].worm);
    field<int>(panel,0x90)=1;panelOriginalState=4;UpdatePlacedNameplate(panel,nullptr);
    assert(field<int>(panel,0x64)==4 && placedNameplates[1].worm);
    panelOriginalState=2;UpdatePlacedNameplate(panel,nullptr);
    assert(field<int>(panel,0x64)==3 && field<float>(panel,0x84)==1080.0f && !placedNameplates[1].worm);
    // Expired/replayed markers and replaced worms cannot affect another label.
    placedNameplates[1]={manager,wormStorage[1],100};field<DWORD>(timerStorage,0x44)=401;
    UpdatePlacedNameplate(panel,nullptr);
    assert(field<int>(panel,0x64)==2 && !placedNameplates[1].worm);
    placedNameplates[1]={manager,wormStorage[1],100};field<DWORD>(timerStorage,0x44)=99;
    UpdatePlacedNameplate(panel,nullptr);
    assert(field<int>(panel,0x64)==2 && !placedNameplates[1].worm);
    placedNameplates[1]={manager,wormStorage[2],100};field<DWORD>(timerStorage,0x44)=100;
    UpdatePlacedNameplate(panel,nullptr);
    assert(field<int>(panel,0x64)==2 && !placedNameplates[1].worm && panelCalls==6);
    puts("PASS: logo-only flow routing, warnings/profile destinations retained, disabled/unrelated/missing/mismatched flow guards, damage/death regression, placement mode guards, settling before immediate handoff, zero-time expiry, deadline not rearmed, RapidPlay setting guard, accepted-placement cosmetics, stock forwarding, targeted nameplate snap, expiry/replay/object guards.");
    rareHatTests();
    unusedHatTests();
    mulletPreviewTests();
    unlockAllTests();
    joannaTests();
    rapidVehicleDeathConfigTests();
    joannaTextTests();
    joannaTrampolineTests();
    patchSelectionTests();
}

