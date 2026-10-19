#pragma once

// RewardManager has twelve vectors of Reward* at +0x34, each twelve bytes.
// Type 8 contains XP events, not collectible rewards. Never grant those.
static constexpr unsigned rewardGroupCount = 12;
static LONG grantingAllRewards;

static bool rewardCounts(void* manager, unsigned (&counts)[rewardGroupCount]) {
    for (unsigned group = 0; group < rewardGroupCount; ++group) {
        unsigned offset = 0x34 + group * 12;
        uintptr_t begin = field<uintptr_t>(manager, offset);
        uintptr_t end = field<uintptr_t>(manager, offset + 4);
        uintptr_t capacity = field<uintptr_t>(manager, offset + 8);
        if (end < begin || capacity < end || (begin | end | capacity) % sizeof(void*) ||
            (!begin && capacity) || (capacity - begin) / sizeof(void*) > 4096) return false;
        counts[group] = static_cast<unsigned>((end - begin) / sizeof(void*));
    }
    return true;
}

static void __fastcall RewardsLoaded(void* manager, void*) {
    if (!manager) return;
    // Retain the game's lazy profile refresh and its readiness flag. This runs
    // after user discovery, never during suspended-thread hook installation.
    fn<void(__thiscall*)(void*)>(0x3a3b80)(manager);
    if (!enabled || !settings.unlockAll || !field<BYTE>(manager, 0xc4)) return;
    if (InterlockedCompareExchange(&grantingAllRewards, 1, 0)) return;
    struct Reset { ~Reset() { InterlockedExchange(&grantingAllRewards, 0); } } reset;
    unsigned counts[rewardGroupCount];
    if (!rewardCounts(manager, counts)) { log("unlock-all-invalid-vectors"); return; }

    unsigned index = 0, granted = 0;
    for (unsigned group = 0; group < rewardGroupCount; ++group) {
        auto rewards = field<void**>(manager, 0x34 + group * 12);
        for (unsigned i = 0; i < counts[group]; ++i, ++index) {
            if (group == 8) continue;
            void* reward = rewards[i];
            if (!reward || field<unsigned>(reward, 0x28) != group || field<BYTE>(reward, 0x30)) continue;
            // Native grant updates both the live record and the player's reward
            // store. The final flag suppresses newly-earned popup bookkeeping.
            fn<void(__thiscall*)(void*, unsigned, unsigned, bool)>(0x3a4360)(manager, index, 0, true);
            ++granted;
        }
    }
    if (granted) log("unlock-all-granted", granted);
}
