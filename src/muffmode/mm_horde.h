// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstdint>

struct gclient_t;
struct gentity_t;
struct gtime_t;

// [MuffMode] GT_HORDE wave spawning, scoring, and round/match orchestration.
void MM_Horde_Init();
void MM_Horde_RunSpawning();
void MM_Horde_BeginWave();
void MM_Horde_AdjustPlayerScore(gclient_t *cl, int32_t offset);

int  MM_Horde_CountFighters();
int  MM_Horde_WavePointBudget();

bool MM_Horde_ShouldSkipEntitiesReset();
bool MM_Horde_UsesWaveTechs();
gtime_t MM_Horde_WeaponRespawnDelay(gtime_t base_delay);
int  MM_Horde_CountdownWaveNumber();
void MM_Horde_AdvanceRoundNumber();
void MM_Horde_OnRoundCountdown();
void MM_Horde_OnRoundStarted();
void MM_Horde_OnRoundEnd();
void MM_Horde_CleanWaveTransition();
void MM_Horde_OnPlayerDeath(gentity_t *ent);
void MM_Horde_NotifyEliminatedSpectator(gentity_t *ent);
void MM_Horde_OnMonsterKilled(gentity_t *ent);
bool MM_Horde_IsSharedReward(gentity_t *ent);
void MM_Horde_OnSharedRewardPickedUp(gentity_t *ent, gentity_t *other);
bool MM_Horde_GetBossHealthBar(const char **name, uint8_t *health_byte);
bool MM_Horde_ShouldAllowJorgMakron(gentity_t *jorg);
void MM_Horde_OnJorgMakronSpawned(gentity_t *jorg, gentity_t *makron);

// CheckDMExitRules / round tick; return true when defeat intermission was queued.
bool MM_Horde_CheckAllFightersLost();

// CheckDMWarmupState hook for when every playing client has left mid-match;
// counts it as a defeat. Returns true when defeat intermission was queued.
bool MM_Horde_CheckDesertionDefeat();

// Returns true when the wave is cleared (caller should invoke Round_End).
bool MM_Horde_UpdateRoundInProgress();

// CheckDMExitRules hook; return true when intermission was queued.
bool MM_Horde_CheckMatchEnd();

bool MM_Horde_SkipFragScoreLimit();
bool MM_Horde_SkipMercyLimit();
// [MuffMode] Bonus ammo from monster kills scaled by player count.
void MM_Horde_AdjustAmmoDrop(gentity_t *attacker);
