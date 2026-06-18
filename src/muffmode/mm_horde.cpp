// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include "muffmode/mm_horde.h"

#include <climits>
#include <vector>

// Late-wave tuning cvars are referenced by helpers defined before the main extern block below.
extern cvar_t *g_horde_content_peak_wave;
extern cvar_t *g_horde_late_wave_factor;
extern cvar_t *g_horde_weight_floor;
extern cvar_t *g_horde_theme_min_monsters;

namespace {
// Themed-wave categories. A monster row may carry several (bitwise OR); 0 = no theme.
enum : uint32_t {
	HCAT_SWARM  = 1 << 0,	// sheer numbers of cheap/light bodies
	HCAT_AERIAL = 1 << 1,	// flying - vertical threat
	HCAT_HEAVY  = 1 << 2,	// armored bruisers - burst-damage check
	HCAT_MELEE  = 1 << 3,	// chargers - keep them at range
	HCAT_INFEST = 1 << 4,	// wall/ceiling crawlers - ambush
};

// Weighted spawn table row. monsters[] uses drops[] for death loot; items[] uses classname only.
struct weighted_item_t {
	const char             *classname;
	int32_t                 min_level = -1, max_level = -1;
	float                   weight = 1.0f;
	float                   lvl_w_adjust = 0;
	std::array<item_id_t, 4> drops = {};
	int32_t                 spawn_points = 1;
	uint32_t                categories = 0;	// HCAT_* mask for themed waves
};

constexpr weighted_item_t items[] = {
	{ "item_health_small" },

	{ "item_health", -1, -1, 1.0f, 0 },
	{ "item_health_large", -1, -1, 0.85f, 0 },

	{ "item_armor_shard" },
	{ "item_armor_jacket", -1, 4, 0.65f, 0 },
	{ "item_armor_combat", 2, -1, 0.62f, 0 },
	{ "item_armor_body", 4, -1, 0.35f, 0 },

	{ "weapon_shotgun", -1, -1, 0.98f, 0 },
	{ "weapon_supershotgun", 2, -1, 1.02f, 0 },
	{ "weapon_machinegun", -1, -1, 1.05f, 0 },
	{ "weapon_chaingun", 3, -1, 1.01f, 0 },
	{ "weapon_grenadelauncher", 4, -1, 0.75f, 0 },
	{ "weapon_hyperblaster", 5, -1, 0.70f, 0 },
	{ "weapon_rocketlauncher", 6, -1, 0.65f, 0 },
	{ "weapon_railgun", 8, -1, 0.45f, 0 },

	{ "ammo_shells", -1, -1, 1.25f, 0 },
	{ "ammo_bullets", -1, -1, 1.25f, 0 },
	{ "ammo_grenades", 2, -1, 1.25f, 0 },
	{ "ammo_cells", 5, -1, 1.0f, 0 },
	{ "ammo_rockets", 6, -1, 1.0f, 0 },
	{ "ammo_slugs", 8, -1, 0.9f, 0 },
};

// Tuned for a 12-wave arc: soldiers -> gekks/flyers -> mid-tier -> heavies (8-10) -> commander finale (11-12).
// 1-point chaff stays available all game so leftover budget points are always spendable.
// Soldier-family weights are kept low so waves 2-4 diversify quickly (soldier share ~73/61/46%).
constexpr weighted_item_t monsters[] = {
	// chaff
	{ "monster_soldier_light", -1, -1, 1.00f, -0.04f, { IT_HEALTH_SMALL }, 1, HCAT_SWARM },
	{ "monster_soldier", -1, -1, 0.75f, -0.03f, { IT_AMMO_BULLETS_SMALL, IT_HEALTH_SMALL }, 1, HCAT_SWARM },
	{ "monster_soldier_ss", 2, 9, 0.85f, -0.08f, { IT_AMMO_SHELLS_SMALL, IT_HEALTH_SMALL }, 1, HCAT_SWARM },
	// early variety
	{ "monster_gekk", 2, 10, 1.35f, -0.10f, {}, 2, HCAT_SWARM | HCAT_MELEE | HCAT_INFEST },
	{ "monster_soldier_hypergun", 2, 10, 0.90f, 0, { IT_AMMO_CELLS_SMALL, IT_HEALTH_SMALL }, 2 },
	{ "monster_soldier_lasergun", 3, 10, 0.90f, 0.03f, { IT_AMMO_CELLS_SMALL, IT_HEALTH_SMALL }, 2 },
	{ "monster_soldier_ripper", 3, 10, 0.90f, 0.03f, { IT_AMMO_CELLS_SMALL, IT_HEALTH_SMALL }, 2 },
	{ "monster_infantry", 3, -1, 1.05f, 0.05f, { IT_AMMO_BULLETS_SMALL, IT_AMMO_BULLETS }, 2 },
	{ "monster_flyer", 3, -1, 1.10f, 0.02f, { IT_AMMO_CELLS_SMALL }, 2, HCAT_AERIAL },
	{ "monster_kamikaze", 3, -1, 0.85f, 0, {}, 2, HCAT_AERIAL | HCAT_MELEE },
	// mid-tier
	{ "monster_gunner", 4, -1, 1.05f, 0.15f, { IT_AMMO_GRENADES, IT_AMMO_BULLETS_SMALL }, 3 },
	// max_level was 14 (capped at the finale); uncapped to -1 so Melee/Infestation/Heavy/Aerial
	// themes keep on-category bodies past wave 14. Active through wave 14 already, so waves 1-14
	// are unchanged - this only adds them at wave 15+.
	{ "monster_berserk", 4, -1, 1.05f, 0.05f, { IT_ARMOR_SHARD }, 3, HCAT_MELEE },
	{ "monster_parasite", 4, -1, 1.00f, -0.05f, {}, 3, HCAT_INFEST },
	{ "monster_gladb", 5, -1, 1.00f, 0.05f, { IT_AMMO_CELLS_SMALL }, 3, HCAT_HEAVY },
	{ "monster_stalker", 5, -1, 0.95f, 0.05f, { IT_AMMO_CELLS_SMALL }, 3, HCAT_INFEST },
	{ "monster_fixbot", 5, -1, 0.60f, 0.02f, { IT_HEALTH_SMALL, IT_AMMO_CELLS_SMALL }, 3, HCAT_AERIAL },
	{ "monster_brain", 6, -1, 0.95f, 0, { IT_AMMO_CELLS_SMALL }, 3, HCAT_MELEE | HCAT_INFEST },
	{ "monster_mutant", 6, -1, 0.90f, 0, {}, 3, HCAT_MELEE },
	{ "monster_floater", 6, -1, 0.90f, 0, {}, 3, HCAT_AERIAL },
	{ "monster_arachnid", 6, -1, 0.85f, 0.03f, { IT_AMMO_SLUGS_SMALL }, 4, HCAT_INFEST },
	{ "monster_gladiator", 7, -1, 1.00f, 0.10f, { IT_AMMO_SLUGS }, 4, HCAT_HEAVY },
	// heavies
	{ "monster_hover", 8, -1, 0.85f, 0, {}, 4, HCAT_AERIAL },
	{ "monster_guncmdr", 8, -1, 0.50f, 0.10f, { IT_AMMO_GRENADES, IT_AMMO_BULLETS_SMALL, IT_AMMO_BULLETS, IT_AMMO_CELLS_SMALL }, 5, HCAT_HEAVY },
	{ "monster_chick", 8, -1, 0.95f, 0, { IT_AMMO_ROCKETS_SMALL, IT_AMMO_ROCKETS }, 4 },
	{ "monster_daedalus", 9, -1, 0.85f, 0.05f, { IT_AMMO_CELLS_SMALL }, 5, HCAT_AERIAL },
	{ "monster_medic", 9, -1, 0.80f, 0, { IT_HEALTH_SMALL, IT_HEALTH_MEDIUM }, 5 },
	{ "monster_tank", 10, -1, 0.80f, 0.05f, { IT_AMMO_ROCKETS }, 6, HCAT_HEAVY },
	{ "monster_chick_heat", 10, -1, 0.85f, 0.05f, { IT_AMMO_CELLS_SMALL, IT_AMMO_CELLS }, 4 },
	{ "monster_shambler", 10, -1, 0.75f, 0.05f, {}, 6, HCAT_HEAVY },
	{ "monster_guardian", 10, -1, 0.65f, 0.08f, { IT_AMMO_CELLS_SMALL, IT_ARMOR_SHARD }, 6, HCAT_HEAVY },
	// finale
	{ "monster_tank_commander", 11, -1, 0.45f, 0.15f, { IT_AMMO_ROCKETS_SMALL, IT_AMMO_BULLETS_SMALL, IT_AMMO_ROCKETS, IT_AMMO_BULLETS }, 8, HCAT_HEAVY },
	{ "monster_medic_commander", 11, -1, 0.40f, 0.12f, { IT_AMMO_CELLS_SMALL, IT_HEALTH_MEDIUM, IT_HEALTH_LARGE }, 8 },
};

// level.horde_wave_roster is a per-wave bitmask over indices into monsters[].
static_assert(q_countof(monsters) <= 32, "horde_wave_roster bitmask supports at most 32 monster rows");

// True once a wave runs past the tuned content curve (default wave 12), whether that's reached via
// an endless run (roundlimit 0) or a high finite roundlimit. Drives the budget taper, weight floor,
// and item clamp; never alters level.round_number (HUD/scoring keep the true wave number).
static bool Horde_IsLateWave()
{
	return GT(GT_HORDE) && level.round_number > g_horde_content_peak_wave->integer;
}

// Count monsters[] rows a theme could actually spawn at `wave`: matching category, valid level
// range, and positive effective weight (with the late-wave floor, matching Horde_PickMonster). Used
// to keep a theme banner from showing when the theme has no real bodies at this wave.
static int Horde_CountThemeCandidates(uint32_t category, int wave)
{
	const bool late_wave = GT(GT_HORDE) && wave > g_horde_content_peak_wave->integer;
	int        count = 0;

	for (auto &monster : monsters) {
		if (monster.min_level != -1 && wave < monster.min_level)
			continue;
		if (monster.max_level != -1 && wave > monster.max_level)
			continue;
		if (!(monster.categories & category))
			continue;

		float weight = monster.weight + ((wave - max(1, monster.min_level)) * monster.lvl_w_adjust);
		if (late_wave)
			weight = max(weight, g_horde_weight_floor->value);
		if (weight <= 0)
			continue;

		count++;
	}

	return count;
}

// Themed-wave definitions. A themed wave filters the monster pool to one category, scales the
// spawn budget, and shows a banner. min_wave keeps a theme out of waves where its monsters
// don't exist yet; the per-monster min_level still applies on top.
enum class horde_theme_t : int8_t { NONE = 0, SWARM, AERIAL, HEAVY, MELEE, INFESTATION };

struct horde_theme_def_t {
	horde_theme_t theme;
	uint32_t      category;		// HCAT_* this theme allows
	int32_t       min_wave;		// earliest wave this theme can appear
	float         budget_mult;	// >1 = more, cheaper bodies; <1 = fewer, tougher
	const char   *announce;		// center-print banner
};

constexpr horde_theme_def_t horde_themes[] = {
	{ horde_theme_t::SWARM,       HCAT_SWARM,  3, 1.30f, "THE SWARM APPROACHES!" },
	{ horde_theme_t::AERIAL,      HCAT_AERIAL, 4, 1.00f, "AERIAL ASSAULT!" },
	{ horde_theme_t::HEAVY,       HCAT_HEAVY,  7, 0.80f, "HEAVY ASSAULT!" },
	{ horde_theme_t::MELEE,       HCAT_MELEE,  4, 1.10f, "THEY'RE CLOSING IN!" },
	{ horde_theme_t::INFESTATION, HCAT_INFEST, 4, 1.15f, "INFESTATION!" },
};

struct horde_boss_def_t {
	const char *classname;
	const char *name;
	vec3_t      mins;
	vec3_t      maxs;
	float       health_factor;
};

constexpr horde_boss_def_t horde_bosses[] = {
	{ "monster_boss5",     "Super Tank",  { -64.f, -64.f, 0.f }, { 64.f, 64.f, 112.f },  1.10f },
	{ "monster_boss2",     "Hornet",      { -56.f, -56.f, 0.f }, { 56.f, 56.f,  80.f },  1.05f },
	{ "monster_jorg",      "Jorg",        { -80.f, -80.f, 0.f }, { 80.f, 80.f, 140.f },  1.25f },
	{ "monster_makron",    "Makron",      { -30.f, -30.f, 0.f }, { 30.f, 30.f,  90.f },  1.15f },
	{ "monster_gladiator", "Gladiator",   { -32.f, -32.f, -24.f }, { 32.f, 32.f, 42.f }, 0.95f },
	{ "monster_chick",     "Iron Maiden", { -16.f, -16.f, 0.f }, { 16.f, 16.f,  56.f }, 0.90f },
};

constexpr const char *HORDE_EMERGENCY_BOSS_CLASSNAME = "monster_chick";

struct horde_reward_def_t {
	item_id_t id;
	int      weight;
	bool     rare;
};

constexpr horde_reward_def_t horde_boss_rewards[] = {
	{ IT_PACK,               28, false },
	{ IT_POWER_SHIELD,       24, false },
	{ IT_POWERUP_HASTE,      20, false },
	{ IT_WEAPON_BFG,         16, false },
	{ IT_POWERUP_QUAD,        6, true },
	{ IT_POWERUP_PROTECTION,  6, true },
};

static const horde_theme_def_t *Horde_FindTheme(horde_theme_t theme)
{
	if (theme == horde_theme_t::NONE)
		return nullptr;
	for (auto &def : horde_themes)
		if (def.theme == theme)
			return &def;
	return nullptr;
}

static uint32_t Horde_ActiveThemeCategory()
{
	const horde_theme_def_t *def = Horde_FindTheme(static_cast<horde_theme_t>(level.horde_wave_theme));
	return def ? def->category : 0;
}

struct picked_item_t {
	const weighted_item_t *item;
	float                  weight;
};

gentity_t *FindClosestPlayerToPoint(vec3_t point)
{
	float      bestplayerdistance = 9999999;
	gentity_t *closest = nullptr;

	for (auto ec : active_clients()) {
		if (!ClientIsPlaying(ec->client) || ec->health <= 0 || ec->client->eliminated)
			continue;

		vec3_t v = point - ec->s.origin;
		float  playerdistance = v.length();

		if (playerdistance < bestplayerdistance) {
			bestplayerdistance = playerdistance;
			closest = ec;
		}
	}

	return closest;
}

gitem_t *Horde_PickItem()
{
	static std::array<picked_item_t, q_countof(items)> picked_items;
	size_t                                              num_picked_items = 0;
	float                                               total_weight = 0;

	// Past the content peak, freeze the loot curve at the peak so late waves keep dropping the
	// early/mid weapons instead of only top-tier gear. Waves <= peak use the true wave number.
	const int loot_wave = Horde_IsLateWave() ? g_horde_content_peak_wave->integer : level.round_number;

	for (auto &item : items) {
		if (item.min_level != -1 && loot_wave < item.min_level)
			continue;
		if (item.max_level != -1 && loot_wave > item.max_level)
			continue;

		// clamp so "-1 = always available" rows ramp from wave 1, not wave -1
		float weight = item.weight + ((loot_wave - max(1, item.min_level)) * item.lvl_w_adjust);

		if (weight <= 0)
			continue;

		total_weight += weight;
		picked_items[num_picked_items++] = { &item, total_weight };
	}

	if (!total_weight)
		return nullptr;

	float r = frandom() * total_weight;

	for (size_t i = 0; i < num_picked_items; i++)
		if (r < picked_items[i].weight)
			return FindItemByClassname(picked_items[i].item->classname);

	return nullptr;
}

static gitem_t *Horde_PickDropItem(const weighted_item_t *monster_row)
{
	if (monster_row) {
		item_id_t choices[4];
		int       num_choices = 0;

		for (item_id_t id : monster_row->drops) {
			if (id != IT_NULL)
				choices[num_choices++] = id;
		}

		if (num_choices > 0)
			return GetItemByIndex(choices[irandom(num_choices)]);
	}

	return Horde_PickItem();
}

// Champions always drop a strong reward: armor, adrenaline, or an occasional powerup.
static gitem_t *Horde_PickChampionDrop()
{
	static constexpr item_id_t champion_drops[] = {
		IT_ARMOR_COMBAT, IT_ARMOR_COMBAT, IT_ARMOR_BODY,
		IT_ADRENALINE, IT_POWERUP_DOUBLE, IT_POWERUP_QUAD,
	};

	gitem_t *item = GetItemByIndex(champion_drops[irandom(q_countof(champion_drops))]);
	return item ? item : Horde_PickItem();
}

static const char *Horde_PickMonster(weighted_item_t const **out_row, int remaining_points, uint32_t theme_category, uint32_t roster_mask)
{
	static std::array<picked_item_t, q_countof(monsters)> picked_monsters;
	size_t                                                num_picked_monsters = 0;
	float                                                 total_weight = 0;
	const bool                                            late_wave = Horde_IsLateWave();

	if (out_row)
		*out_row = nullptr;

	for (auto &monster : monsters) {
		if (monster.min_level != -1 && level.round_number < monster.min_level)
			continue;
		if (monster.max_level != -1 && level.round_number > monster.max_level)
			continue;
		if (monster.spawn_points > remaining_points)
			continue;
		if (theme_category && !(monster.categories & theme_category))
			continue;
		if (roster_mask && !(roster_mask & (1u << static_cast<size_t>(&monster - monsters))))
			continue;

		// clamp so "-1 = always available" rows ramp from wave 1, not wave -1
		float weight = monster.weight + ((level.round_number - max(1, monster.min_level)) * monster.lvl_w_adjust);

		// Past the content peak, decayed weights would cull chaff and starve themes; hold a floor
		// so every still-eligible row stays spendable in late waves.
		if (late_wave)
			weight = max(weight, g_horde_weight_floor->value);

		if (weight <= 0)
			continue;

		total_weight += weight;
		picked_monsters[num_picked_monsters++] = { &monster, total_weight };
	}

	if (!total_weight)
		return nullptr;

	float r = frandom() * total_weight;

	for (size_t i = 0; i < num_picked_monsters; i++) {
		if (r < picked_monsters[i].weight) {
			if (out_row)
				*out_row = picked_monsters[i].item;
			return picked_monsters[i].item->classname;
		}
	}

	return nullptr;
}

// When weighted pick finds nothing (e.g. all weights zero), use the highest-tier affordable row still
// valid for this wave. theme_category (0 = any) keeps a themed wave's fallback on-category.
static const char *Horde_PickMonsterFallback(weighted_item_t const **out_row, int remaining_points, uint32_t theme_category = 0)
{
	const weighted_item_t *choice = nullptr;

	if (out_row)
		*out_row = nullptr;
	int32_t                    best_cap = -1;

	for (auto &monster : monsters) {
		if (monster.min_level != -1 && level.round_number < monster.min_level)
			continue;
		if (monster.spawn_points > remaining_points)
			continue;
		if (theme_category && !(monster.categories & theme_category))
			continue;

		const int32_t cap = monster.max_level == -1 ? INT32_MAX : monster.max_level;
		if (level.round_number > cap)
			continue;

		if (cap > best_cap) {
			best_cap = cap;
			choice = &monster;
		}
	}

	if (!choice) {
		best_cap = -1;
		for (auto &monster : monsters) {
			if (monster.min_level != -1 && level.round_number < monster.min_level)
				continue;
			if (monster.spawn_points > remaining_points)
				continue;
			if (theme_category && !(monster.categories & theme_category))
				continue;

			const int32_t cap = monster.max_level == -1 ? INT32_MAX : monster.max_level;
			if (cap > best_cap) {
				best_cap = cap;
				choice = &monster;
			}
		}
	}

	if (out_row && choice)
		*out_row = choice;

	return choice ? choice->classname : nullptr;
}

static const char *Horde_PickMonsterForWave(weighted_item_t const **out_row, int remaining_points)
{
	// Themed waves and roster waves are mutually exclusive (BeginWave only builds a roster when the
	// theme is NONE).
	const uint32_t theme_category = Horde_ActiveThemeCategory();

	// Themed waves are strict: a category banner means every spawn must be on-category. Try the
	// weighted pick, then a category-respecting fallback; never fall through to the unrestricted
	// pool. Returning null ends the wave cleanly (leftover budget forfeited) instead of spawning an
	// off-theme body under a themed banner.
	if (theme_category) {
		if (const char *pick = Horde_PickMonster(out_row, remaining_points, theme_category, 0))
			return pick;
		return Horde_PickMonsterFallback(out_row, remaining_points, theme_category);
	}

	// Non-themed waves bias by this wave's roster, then fall through to the unrestricted pool and the
	// fallback picker so a wave can never stall.
	if (level.horde_wave_roster)
		if (const char *pick = Horde_PickMonster(out_row, remaining_points, 0, level.horde_wave_roster))
			return pick;

	if (const char *pick = Horde_PickMonster(out_row, remaining_points, 0, 0))
		return pick;

	static int32_t fallback_warn_wave = -1;
	const char    *fallback = Horde_PickMonsterFallback(out_row, remaining_points);

	if (fallback && fallback_warn_wave != level.round_number) {
		fallback_warn_wave = level.round_number;
		gi.Com_PrintFmt("MM_Horde: no weighted monster for wave {}; using fallback {}\n", level.round_number, fallback);
	}

	return fallback;
}
} // namespace

extern cvar_t *g_horde_starting_wave;
extern cvar_t *g_horde_points_base;
extern cvar_t *g_horde_points_per_wave;
extern cvar_t *g_horde_points_min;
extern cvar_t *g_horde_points_max;
extern cvar_t *g_horde_spawn_interval_min;
extern cvar_t *g_horde_spawn_interval_max;
extern cvar_t *g_horde_warmup_cap;
extern cvar_t *g_horde_max_alive;
extern cvar_t *g_horde_wave_spawn_delay_ms;
extern cvar_t *g_horde_player_scale;
extern cvar_t *g_horde_player_scale_factor;
extern cvar_t *g_horde_player_scale_max;
extern cvar_t *g_horde_lives;
extern cvar_t *g_horde_mark_monsters_threshold;
extern cvar_t *g_horde_mark_monsters_max;
extern cvar_t *g_horde_map_scale;
extern cvar_t *g_horde_map_scale_ref;
extern cvar_t *g_horde_map_scale_factor;
extern cvar_t *g_horde_champions;
extern cvar_t *g_horde_champion_max_per_run;
extern cvar_t *g_horde_champion_chance;
extern cvar_t *g_horde_champion_min_wave;
extern cvar_t *g_horde_champion_health_mult;
extern cvar_t *g_horde_champion_health_floor;
extern cvar_t *g_horde_champion_health_per_wave;
extern cvar_t *g_horde_champion_damage_mult;
extern cvar_t *g_horde_champion_speed_mult;
extern cvar_t *g_horde_champion_strong_ratio;
extern cvar_t *g_horde_champion_force; // DEBUG/TEST: force a champion every wave
extern cvar_t *g_horde_themed_waves;
extern cvar_t *g_horde_theme_chance;
extern cvar_t *g_horde_theme_min_wave;
extern cvar_t *g_horde_wave_variety;
extern cvar_t *g_horde_wave_min_types;
extern cvar_t *g_horde_techs;
extern cvar_t *g_horde_ammo_respawn_scale;
extern cvar_t *g_horde_ammo_drop_scale;
extern cvar_t *g_horde_boss_waves;
extern cvar_t *g_horde_boss_interval;
extern cvar_t *g_horde_boss_health_base;
extern cvar_t *g_horde_boss_health_per_point;
extern cvar_t *g_horde_boss_health_mult;
extern cvar_t *g_horde_boss_health_per_wave;
extern cvar_t *g_horde_boss_player_health_scale;
extern cvar_t *g_horde_boss_damage_mult;
extern cvar_t *g_horde_boss_jorg_makron_chance;
extern cvar_t *g_horde_boss_makron_health_mult;
extern cvar_t *g_horde_boss_makron_damage_mult;

static bool HordeActive()
{
	return g_gametype->integer == static_cast<int>(GT_HORDE);
}

static bool Horde_IsBossWaveNumber(int wave)
{
	const int interval = g_horde_boss_interval->integer;
	return HordeActive() && g_horde_boss_waves->integer && interval > 0 && wave > 0 && (wave % interval) == 0;
}

static const horde_boss_def_t *Horde_PickBoss(uint32_t tried_mask)
{
	constexpr uint32_t all_mask = (1u << q_countof(horde_bosses)) - 1u;
	tried_mask &= all_mask;
	if (tried_mask == all_mask)
		return nullptr;

	for (size_t attempt = 0; attempt < q_countof(horde_bosses); attempt++) {
		const size_t index = irandom(q_countof(horde_bosses));
		if (!(tried_mask & (1u << index)))
			return &horde_bosses[index];
	}

	for (size_t index = 0; index < q_countof(horde_bosses); index++)
		if (!(tried_mask & (1u << index)))
			return &horde_bosses[index];

	return nullptr;
}

static uint32_t Horde_BossMask(const horde_boss_def_t *boss)
{
	if (!boss)
		return 0;

	for (size_t index = 0; index < q_countof(horde_bosses); index++)
		if (&horde_bosses[index] == boss)
			return 1u << static_cast<uint32_t>(index);

	return 0;
}

static const horde_boss_def_t *Horde_LastBoss()
{
	const int index = level.horde_last_boss_index - 1;
	if (index < 0 || index >= static_cast<int>(q_countof(horde_bosses)))
		return nullptr;

	return &horde_bosses[index];
}

static uint32_t Horde_LastBossMask()
{
	return Horde_BossMask(Horde_LastBoss());
}

static void Horde_RememberBoss(const horde_boss_def_t *boss)
{
	if (!boss)
		return;

	for (size_t index = 0; index < q_countof(horde_bosses); index++) {
		if (&horde_bosses[index] != boss)
			continue;

		level.horde_last_boss_index = static_cast<int8_t>(index + 1);
		return;
	}
}

static const horde_boss_def_t *Horde_EmergencyBoss()
{
	for (const horde_boss_def_t &boss : horde_bosses)
		if (!Q_strcasecmp(boss.classname, HORDE_EMERGENCY_BOSS_CLASSNAME))
			return &boss;

	return nullptr;
}

static const char *Horde_BossHealthBarName(const gentity_t *boss)
{
	if (!boss || !boss->classname)
		return "BOSS";

	if (!Q_strcasecmp(boss->classname, "monster_boss5"))
		return "BOSS: Super Tank";
	if (!Q_strcasecmp(boss->classname, "monster_boss2"))
		return "BOSS: Hornet";
	if (!Q_strcasecmp(boss->classname, "monster_jorg"))
		return "BOSS: Jorg";
	if (!Q_strcasecmp(boss->classname, "monster_makron"))
		return "BOSS: Makron";
	if (!Q_strcasecmp(boss->classname, "monster_gladiator"))
		return "BOSS: Gladiator";
	if (!Q_strcasecmp(boss->classname, "monster_chick"))
		return "BOSS: Iron Maiden";

	return "BOSS";
}

static const horde_reward_def_t *Horde_PickBossReward(bool *rare, uint32_t tried_mask)
{
	int total_weight = 0;

	if (rare)
		*rare = false;

	for (size_t i = 0; i < q_countof(horde_boss_rewards); i++) {
		if (tried_mask & (1u << i))
			continue;
		const horde_reward_def_t &reward = horde_boss_rewards[i];
		total_weight += max(0, reward.weight);
	}

	if (total_weight <= 0)
		return nullptr;

	int roll = irandom(total_weight);
	for (size_t i = 0; i < q_countof(horde_boss_rewards); i++) {
		if (tried_mask & (1u << i))
			continue;
		const horde_reward_def_t &reward = horde_boss_rewards[i];
		const int weight = max(0, reward.weight);
		if (roll >= weight) {
			roll -= weight;
			continue;
		}

		if (rare)
			*rare = reward.rare;
		return &reward;
	}

	return nullptr;
}

static void Horde_ClearBossRewards()
{
	for (size_t i = globals.num_entities; i > 1; i--) {
		gentity_t *ent = &g_entities[i - 1];
		if (!ent->inuse)
			continue;
		if (!ent->spawnflags.has(SPAWNFLAG_ITEM_HORDE_SHARED_REWARD))
			continue;

		ent->think = nullptr;
		ent->nextthink = 0_ms;
		G_FreeEntity(ent);
	}
}

static bool Horde_IsBossCountdownRefillItem(gentity_t *ent)
{
	if (!ent || !ent->inuse || !ent->item)
		return false;
	if (ent->spawnflags.has(SPAWNFLAG_ITEM_DROPPED | SPAWNFLAG_ITEM_DROPPED_PLAYER | SPAWNFLAG_ITEM_HORDE_SHARED_REWARD))
		return false;

	const item_id_t id = ent->item->id;
	if (id == IT_FLAG_RED || id == IT_FLAG_BLUE)
		return false;

	const item_flags_t flags = ent->item->flags;
	if (flags & (IF_KEY | IF_TECH))
		return false;

	return !!(flags & (IF_WEAPON | IF_AMMO | IF_ARMOR | IF_POWER_ARMOR | IF_HEALTH | IF_POWERUP | IF_TIMED | IF_SPHERE));
}

static bool Horde_ItemNeedsRespawn(gentity_t *ent)
{
	return (ent->svflags & (SVF_NOCLIENT | SVF_RESPAWNING)) || ent->solid == SOLID_NOT || ent->think == RespawnItem;
}

static void Horde_ScheduleItemRespawn(gentity_t *ent)
{
	ent->think = RespawnItem;
	ent->nextthink = level.time + FRAME_TIME_MS;
}

static int Horde_RefreshMapItemsForBossCountdown()
{
	int refreshed = 0;

	for (size_t i = 1; i < globals.num_entities; i++) {
		gentity_t *ent = &g_entities[i];
		if (!Horde_IsBossCountdownRefillItem(ent))
			continue;

		if (ent->team) {
			if (ent != ent->teammaster)
				continue;

			bool team_needs_respawn = false;
			for (gentity_t *member = ent; member; member = member->chain) {
				if (Horde_IsBossCountdownRefillItem(member) && Horde_ItemNeedsRespawn(member)) {
					team_needs_respawn = true;
					break;
				}
			}

			if (!team_needs_respawn)
				continue;

			for (gentity_t *member = ent; member; member = member->chain) {
				if (!Horde_IsBossCountdownRefillItem(member))
					continue;
				member->svflags |= SVF_NOCLIENT;
				member->solid = SOLID_NOT;
				member->nextthink = 0_ms;
				gi.linkentity(member);
			}

			Horde_ScheduleItemRespawn(ent);
			refreshed++;
			continue;
		}

		if (!Horde_ItemNeedsRespawn(ent))
			continue;

		Horde_ScheduleItemRespawn(ent);
		refreshed++;
	}

	return refreshed;
}

static void Horde_RefillMapItemsForBossCountdown()
{
	const int wave = MM_Horde_CountdownWaveNumber();
	if (!Horde_IsBossWaveNumber(wave))
		return;

	const int refreshed = Horde_RefreshMapItemsForBossCountdown();
	gi.LocBroadcast_Print(PRINT_CHAT, "Boss wave incoming: map supplies refreshed.\n");
	gi.Com_PrintFmt("MM_Horde: boss wave {} refreshed {} map item spawns\n", wave, refreshed);
}

static void Horde_SpawnBossReward(const vec3_t &origin)
{
	uint32_t tried_mask = 0;

	for (size_t attempts = 0; attempts < q_countof(horde_boss_rewards); attempts++) {
		bool rare = false;
		const horde_reward_def_t *reward_def = Horde_PickBossReward(&rare, tried_mask);
		if (!reward_def)
			return;

		const uint32_t reward_index = static_cast<uint32_t>(reward_def - horde_boss_rewards);
		tried_mask |= 1u << reward_index;

		gitem_t *item = GetItemByIndex(reward_def->id);
		if (!item)
			continue;

		gentity_t *reward = G_Spawn();
		reward->s.origin = origin;
		reward->s.origin[2] += 24.f;

		if (!SpawnItem(reward, item))
			continue;

		reward->spawnflags |= SPAWNFLAG_ITEM_HORDE_SHARED_REWARD;
		reward->svflags |= SVF_INSTANCED;
		reward->item_picked_up_by.reset();

		const char *reward_name = reward->item && reward->item->use_name ? reward->item->use_name : item->use_name;
		gi.LocBroadcast_Print(PRINT_CENTER, "{}: {}!\n", rare ? "Rare boss reward" : "Boss reward", reward_name);
		gi.LocBroadcast_Print(PRINT_CHAT, "Boss dropped {}! Claim it before the next wave.\n", reward_name);
		return;
	}
}

bool MM_Horde_UsesWaveTechs()
{
	return HordeActive() && g_horde_techs->integer && AllowTechs();
}

bool MM_Horde_IsSharedReward(gentity_t *ent)
{
	return HordeActive() && ent && ent->spawnflags.has(SPAWNFLAG_ITEM_HORDE_SHARED_REWARD);
}

void MM_Horde_OnSharedRewardPickedUp(gentity_t *ent, gentity_t *other)
{
	if (!MM_Horde_IsSharedReward(ent) || !other || !other->client)
		return;

	const int player_number = other->s.number - 1;
	if (player_number < 0 || player_number >= MAX_CLIENTS)
		return;

	ent->item_picked_up_by[player_number] = true;
}

gtime_t MM_Horde_WeaponRespawnDelay(gtime_t base_delay)
{
	if (!HordeActive() || g_horde_ammo_respawn_scale->value <= 0.f)
		return base_delay;

	const int fighters = level.horde_fighters_snapshotted > 0
		? level.horde_fighters_snapshotted
		: MM_Horde_CountFighters();
	if (fighters <= 1)
		return base_delay;

	const float divisor = 1.f + (fighters - 1) * g_horde_ammo_respawn_scale->value;
	const gtime_t scaled = base_delay / divisor;

	return scaled < 1_sec ? 1_sec : scaled;
}

static int Horde_MarkMonsterSlots()
{
	return clamp(g_horde_mark_monsters_max->integer, 1, static_cast<int>(POI_HORDE_MONSTER_END - POI_HORDE_MONSTER_0 + 1));
}

static bool Horde_ClientWantsMonsterMarkers(gclient_t *cl)
{
	if (!cl || !cl->pers.connected)
		return false;
	if (ClientIsPlaying(cl))
		return true;

	return cl->eliminated && cl->sess.team != TEAM_SPECTATOR;
}

static bool Horde_IsLivingMonster(const gentity_t *ent)
{
	if (!ent->inuse || !(ent->svflags & SVF_MONSTER))
		return false;
	if (ent->health <= 0 || ent->deadflag || (ent->svflags & SVF_DEADMONSTER))
		return false;
	if (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)
		return false;

	return true;
}

static void Horde_SendMonsterPOI(gentity_t *player, int slot, const vec3_t &pos)
{
	gi.WriteByte(svc_poi);
	gi.WriteShort(static_cast<uint16_t>(POI_HORDE_MONSTER_0 + slot));
	gi.WriteShort(600);
	gi.WritePosition(pos);
	gi.WriteShort(level.pic_ping);
	gi.WriteByte(208);
	gi.WriteByte(POI_FLAG_NONE);
	gi.unicast(player, false);
}

static void Horde_ClearMonsterPOI(gentity_t *player, int slot)
{
	gi.WriteByte(svc_poi);
	gi.WriteShort(static_cast<uint16_t>(POI_HORDE_MONSTER_0 + slot));
	gi.WriteShort(0xFFFF);
	gi.WritePosition(vec3_origin);
	gi.WriteShort(0);
	gi.WriteByte(0);
	gi.WriteByte(POI_FLAG_NONE);
	gi.unicast(player, false);
}

static void Horde_ClearMonsterPOIsForClient(gentity_t *player)
{
	const int slots = Horde_MarkMonsterSlots();

	for (int slot = 0; slot < slots; slot++)
		Horde_ClearMonsterPOI(player, slot);
}

static void Horde_ClearMonsterPOIsForAll()
{
	for (auto ec : active_clients()) {
		if (!ec->client || !Horde_ClientWantsMonsterMarkers(ec->client))
			continue;

		Horde_ClearMonsterPOIsForClient(ec);
	}

	level.horde_mark_living = -1;
}

static void MM_Horde_UpdateMonsterMarkers()
{
	if (!HordeActive())
		return;
	if (level.round_state != roundst_t::ROUND_IN_PROGRESS)
		return;

	const int threshold = g_horde_mark_monsters_threshold->integer;
	const int living = level.total_monsters - level.killed_monsters;

	if (threshold < 1 || living > threshold) {
		if (level.horde_mark_living >= 0 && level.horde_mark_living <= threshold)
			Horde_ClearMonsterPOIsForAll();

		level.horde_mark_living = static_cast<int16_t>(living);
		return;
	}

	const bool newly_marking = level.horde_mark_living > threshold || level.horde_mark_living < 0;
	const bool count_changed = level.horde_mark_living != living;
	const bool throttle = level.horde_mark_time > level.time && !count_changed;

	if (throttle)
		return;

	level.horde_mark_time = level.time + 500_ms;
	level.horde_mark_living = static_cast<int16_t>(living);

	if (newly_marking) {
		for (auto ec : active_clients()) {
			if (!ec->client || !Horde_ClientWantsMonsterMarkers(ec->client))
				continue;

			gi.local_sound(ec, CHAN_AUTO, gi.soundindex("misc/help_marker.wav"), 1.f, ATTN_NORM, 0, GetUnicastKey());
		}
	}

	const int max_slots = Horde_MarkMonsterSlots();
	gentity_t *marked[8] = {};
	int        num_marked = 0;

	for (size_t i = 1; i < globals.num_entities && num_marked < max_slots; i++) {
		gentity_t *ent = &g_entities[i];

		if (!Horde_IsLivingMonster(ent))
			continue;

		marked[num_marked++] = ent;
	}

	for (auto ec : active_clients()) {
		if (!ec->client || !Horde_ClientWantsMonsterMarkers(ec->client))
			continue;

		for (int slot = 0; slot < max_slots; slot++) {
			if (slot < num_marked) {
				vec3_t pos = marked[slot]->s.origin;
				pos[2] += marked[slot]->maxs[2] * 0.5f;
				Horde_SendMonsterPOI(ec, slot, pos);
			} else {
				Horde_ClearMonsterPOI(ec, slot);
			}
		}
	}
}

static int Horde_LivesPerWave()
{
	return max(1, g_horde_lives->integer);
}

static bool Horde_ClientIsActiveFighter(gentity_t *ec)
{
	if (!ec->client || !ClientIsPlaying(ec->client))
		return false;
	if (ec->client->eliminated)
		return false;
	if (ec->health > 0)
		return true;

	return ec->client->pers.lives > 0;
}

static bool Horde_HasActiveFighter()
{
	for (auto ec : active_clients()) {
		if (Horde_ClientIsActiveFighter(ec))
			return true;
	}

	return false;
}

static void MM_Horde_GrantWaveLives()
{
	const int lives = Horde_LivesPerWave();

	for (auto ec : active_clients()) {
		if (!ClientIsPlaying(ec->client))
			continue;

		const bool was_eliminated = ec->client->eliminated;

		ec->client->pers.lives = lives;
		ec->client->eliminated = false;
		ec->client->horde_elim_msg_wave = 0;

		// Eliminated fighters spectate in freecam with deadflag cleared and health restored.
		if (was_eliminated || ec->deadflag || ec->health <= 0)
			ClientRespawn(ec);
	}
}

static float Horde_MultiplierFromFighters(int fighters)
{
	if (!g_horde_player_scale->integer)
		return 1.f;

	float factor = g_horde_player_scale_factor->value;
	if (factor < 0.f)
		factor = 0.f;

	return 1.f + (fighters - 1) * factor;
}

static float Horde_MapScaleMultiplier()
{
	if (!g_horde_map_scale->integer)
		return 1.f;

	if (level.horde_map_scale_mult != 0.f)
		return level.horde_map_scale_mult;

	if (level.num_spawn_spots < 2)
	{
		level.horde_map_scale_mult = 1.f;
		return 1.f;
	}

	vec3_t bmin = level.spawn_spots[0]->s.origin;
	vec3_t bmax = bmin;
	for (int i = 1; i < level.num_spawn_spots; i++)
	{
		const vec3_t &o = level.spawn_spots[i]->s.origin;
		bmin.x = min(bmin.x, o.x);
		bmin.y = min(bmin.y, o.y);
		bmin.z = min(bmin.z, o.z);
		bmax.x = max(bmax.x, o.x);
		bmax.y = max(bmax.y, o.y);
		bmax.z = max(bmax.z, o.z);
	}

	const float diagonal = (bmax - bmin).length();
	const float ref      = max(1.f, g_horde_map_scale_ref->value);
	const float factor   = clamp(g_horde_map_scale_factor->value, 0.f, 10.f);
	const float ratio    = diagonal / ref;
	const float mult     = 1.f + (ratio - 1.f) * factor;

	level.horde_map_scale_mult = max(0.1f, mult);
	return level.horde_map_scale_mult;
}

int MM_Horde_CountFighters()
{
	int fighters = 0;

	for (auto ec : active_clients()) {
		if (!ClientIsPlaying(ec->client) || ec->health <= 0 || ec->client->eliminated)
			continue;
		fighters++;
	}

	const int max_fighters = clamp(g_horde_player_scale_max->integer, 1, 32);
	return clamp(max(fighters, 1), 1, max_fighters);
}

int MM_Horde_WavePointBudget()
{
	const int   fighters = MM_Horde_CountFighters();
	const float pmult    = Horde_MultiplierFromFighters(fighters);
	const float msmult   = Horde_MapScaleMultiplier();
	const int   base     = g_horde_points_base->integer;
	const int   per_wave = g_horde_points_per_wave->integer;
	const int   min_pts  = g_horde_points_min->integer;
	const int   max_pts  = g_horde_points_max->integer;
	const int   peak     = g_horde_content_peak_wave->integer;

	// Linear up to the tuned content peak (wave 12). Beyond it - reached via endless (roundlimit 0)
	// or a high finite roundlimit - growth tapers by g_horde_late_wave_factor so late waves stay
	// playable instead of piling up 190+ points of commanders. Continuous at the peak.
	int budget;
	if (level.round_number <= peak)
		budget = base + level.round_number * per_wave;
	else
		budget = base + peak * per_wave +
			static_cast<int>((level.round_number - peak) * per_wave * g_horde_late_wave_factor->value);

	if (min_pts > 0)
		budget = max(budget, min_pts);
	if (max_pts > 0)
		budget = min(budget, max_pts);

	return max(1, static_cast<int>(budget * pmult * msmult));
}

static gtime_t Horde_SpawnInterval(bool warmup)
{
	if (warmup)
		return 5_sec;

	const float min_sec = max(0.05f, g_horde_spawn_interval_min->value);
	const float max_sec = max(min_sec, g_horde_spawn_interval_max->value);
	return random_time(gtime_t::from_sec(min_sec), gtime_t::from_sec(max_sec));
}

bool MM_Horde_ShouldSkipEntitiesReset()
{
	return HordeActive();
}

// Remove all techs from every playing client and free every in-world tech
// entity so the next countdown starts with a clean slate.
static void Horde_ClearTechs()
{
	for (auto ec : active_clients())
	{
		if (!ec->client)
			continue;
		for (item_id_t id : tech_ids)
			ec->client->pers.inventory[id] = 0;
	}

	static constexpr const char *tech_classnames[] = {
		"item_tech1", "item_tech2", "item_tech3", "item_tech4"
	};

	for (size_t i = globals.num_entities; i > 1; i--)
	{
		gentity_t *ent = &g_entities[i - 1];
		if (!ent->inuse)
			continue;

		const char *cn = ent->classname;
		if (!cn)
			continue;

		for (const char *tcn : tech_classnames)
		{
			if (!Q_strcasecmp(cn, tcn))
			{
				ent->think = nullptr;
				ent->nextthink = 0_ms;
				G_FreeEntity(ent);
				break;
			}
		}
	}
}

static void Horde_SpawnCountdownTechs()
{
	if (!MM_Horde_UsesWaveTechs())
		return;

	if (level.num_spawn_spots < 1)
		return;

	Horde_ClearTechs();

	// Pick unique random spawn points via Fisher-Yates shuffle.
	std::vector<int> spots;
	spots.reserve(level.num_spawn_spots);
	for (int i = 0; i < level.num_spawn_spots; i++)
		spots.push_back(i);
	for (int i = (int)spots.size() - 1; i > 0; i--)
	{
		int j = irandom(i + 1);
		int t = spots[i];
		spots[i] = spots[j];
		spots[j] = t;
	}

	// [MuffMode] Always spawn exactly 1 of each tech type (4 total).
	constexpr int kTechCount = q_countof(tech_ids);
	for (int i = 0; i < kTechCount; i++)
	{
		gentity_t *spot = level.spawn_spots[spots[i % static_cast<int>(spots.size())]];
		gitem_t *tech_item = GetItemByIndex(tech_ids[i]);
		if (!spot || !tech_item)
			continue;

		gentity_t *ent = G_Spawn();
		ent->s.origin = spot->s.origin;
		if (SpawnItem(ent, tech_item))
			ent->spawnflags = SPAWNFLAG_ITEM_DROPPED;
	}
}

int MM_Horde_CountdownWaveNumber()
{
	if (notGT(GT_HORDE))
		return level.round_number + 1;

	if (!level.round_number && g_horde_starting_wave->integer > 0)
		return g_horde_starting_wave->integer;

	return level.round_number + 1;
}

void MM_Horde_AdvanceRoundNumber()
{
	if (notGT(GT_HORDE))
		return;

	if (!level.round_number && g_horde_starting_wave->integer > 0)
		level.round_number = g_horde_starting_wave->integer;
	else
		level.round_number++;
}

void MM_Horde_OnRoundCountdown()
{
	if (notGT(GT_HORDE))
		return;

	Horde_RefillMapItemsForBossCountdown();

	// [MuffMode] Spawn tech items before each wave countdown so players can
	// grab one before the wave starts. Techs are stripped when the wave clears.
	Horde_SpawnCountdownTechs();
	MM_Horde_GrantWaveLives();
}

void MM_Horde_OnRoundStarted()
{
	if (notGT(GT_HORDE))
		return;

	Horde_ClearBossRewards();

	// Begin the wave first so the theme is chosen before we announce it.
	MM_Horde_BeginWave();

	gi.LocBroadcast_Print(PRINT_CHAT, "Wave {} has begun!\n", level.round_number);
	if (const horde_theme_def_t *theme = Horde_FindTheme(static_cast<horde_theme_t>(level.horde_wave_theme)))
		gi.LocBroadcast_Print(PRINT_CENTER, "{}", theme->announce);
	else
		gi.LocBroadcast_Print(PRINT_CENTER, brandom() ? "INCOMING!" : "LOCK AND LOAD!");
	AnnouncerSound(world, "fight", nullptr, false);
}

void MM_Horde_NotifyEliminatedSpectator(gentity_t *ent)
{
	if (!HordeActive())
		return;
	if (level.round_state != roundst_t::ROUND_IN_PROGRESS)
		return;
	if (!ent->client || !ent->client->eliminated)
		return;
	if (ent->client->sess.team == TEAM_SPECTATOR)
		return;
	if (ent->client->horde_elim_msg_wave == level.round_number)
		return;

	ent->client->horde_elim_msg_wave = static_cast<int16_t>(level.round_number);
	gi.LocClient_Print(ent, PRINT_CENTER, "You will rejoin when the next wave countdown begins.");
}

void MM_Horde_OnPlayerDeath(gentity_t *ent)
{
	if (!HordeActive())
		return;
	if (level.round_state != roundst_t::ROUND_IN_PROGRESS)
		return;
	if (!ent->client || !ClientIsPlaying(ent->client))
		return;

	if (ent->client->pers.lives > 0)
		ent->client->pers.lives--;

	if (ent->client->pers.lives <= 0) {
		ClientSetEliminated(ent);
		ent->client->respawn_time = level.time + 1_sec;
		MM_Horde_NotifyEliminatedSpectator(ent);
	}
}

bool MM_Horde_CheckAllFightersLost()
{
	if (!HordeActive())
		return false;
	if (level.round_state != roundst_t::ROUND_IN_PROGRESS)
		return false;
	if (level.num_playing_clients < 1)
		return false;
	if (Horde_HasActiveFighter())
		return false;

	gi.Broadcast_Print(PRINT_CENTER, "DEFEATED!");
	QueueIntermission("ALL FIGHTERS LOST!", true, false);
	return true;
}

bool MM_Horde_CheckDesertionDefeat()
{
	if (!HordeActive())
		return false;
	if (level.match_state != matchst_t::MATCH_IN_PROGRESS)
		return false;
	if (level.intermission_queued || level.intermission_time)
		return false;

	gi.Broadcast_Print(PRINT_CENTER, "DEFEATED!");
	QueueIntermission("ALL FIGHTERS LOST!", true, false);
	return true;
}

void MM_Horde_CleanWaveTransition()
{
	if (!HordeActive())
		return;

	level.horde_boss_health_entity = nullptr;

	// Remove dead monster corpses between waves (Horde skips Entities_Reset).
	for (size_t i = globals.num_entities; i > 1; i--) {
		gentity_t *ent = &g_entities[i - 1];

		if (!ent->inuse)
			continue;
		if (!(ent->svflags & SVF_MONSTER))
			continue;
		if (ent->health > 0 && !ent->deadflag && !(ent->svflags & SVF_DEADMONSTER))
			continue;

		G_FreeEntity(ent);
	}

	level.total_monsters = 0;
	level.killed_monsters = 0;

	if (g_debug_monster_kills->integer)
		level.monsters_registered.fill(nullptr);

	Horde_ClearMonsterPOIsForAll();
	level.horde_mark_time = 0_ms;
}

void MM_Horde_OnRoundEnd()
{
	if (notGT(GT_HORDE))
		return;

	level.horde_all_spawned = false;
	level.horde_boss_wave = false;
	level.horde_boss_spawned = false;
	level.horde_boss_jorg_makron_pending = false;
	level.horde_boss_health_entity = nullptr;
	MM_Horde_CleanWaveTransition();
	// [MuffMode] Strip techs from all players and free every in-world tech
	// entity so the next countdown starts with a clean slate.
	Horde_ClearTechs();
}

bool MM_Horde_UpdateRoundInProgress()
{
	if (notGT(GT_HORDE))
		return false;

	if (MM_Horde_CheckAllFightersLost())
		return false;

	MM_Horde_RunSpawning();
	MM_Horde_UpdateMonsterMarkers();

	if (level.horde_all_spawned && !(level.total_monsters - level.killed_monsters) && !level.horde_boss_jorg_makron_pending) {
		gi.LocBroadcast_Print(PRINT_CENTER, "Monsters eliminated!\n");
		gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("ctf/flagcap.wav"), 1, ATTN_NONE, 0);
		return true;
	}

	return false;
}

bool MM_Horde_CheckMatchEnd()
{
	if (notGT(GT_HORDE))
		return false;

	if (roundlimit->integer <= 0 || level.round_number < roundlimit->integer)
		return false;

	const int winner = level.sorted_clients[0];
	if (winner < 0)
		QueueIntermission("MATCH ENDED", false, false);
	else
		QueueIntermission(G_Fmt("{} WINS with a final score of {}.", game.clients[winner].resp.netname,
			game.clients[winner].resp.score).data(),
			false, false);
	return true;
}

bool MM_Horde_SkipFragScoreLimit()
{
	return HordeActive();
}

bool MM_Horde_SkipMercyLimit()
{
	return HordeActive();
}

static void Horde_PrecacheTableMonsters()
{
	for (auto &monster : monsters) {
		gentity_t *e = G_Spawn();
		e->classname = monster.classname;
		// don't let precache spawns inflate level.total_monsters; it starves
		// the warmup spawner, which caps on total_monsters - killed_monsters
		e->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		ED_CallSpawn(e);
		if (e->inuse)
			G_FreeEntity(e);
	}
}

void MM_Horde_Init()
{
	if (notGT(GT_HORDE))
		return;

	// The monster table's content curve peaks at waves 11-12; the global default
	// roundlimit of 8 would end the match before heavies and commanders appear.
	// Apply a horde default of 12 once per load, only when still at the global default.
	static bool roundlimit_defaulted = false;
	if (!roundlimit_defaulted) {
		roundlimit_defaulted = true;
		if (roundlimit->integer == 8) {
			gi.cvar_forceset("roundlimit", "12");
			gi.Com_PrintFmt("MM_Horde: roundlimit at global default (8), using horde default of 12.\n");
		}
	}

	Horde_PrecacheTableMonsters();
}

void MM_Horde_BeginWave()
{
	if (notGT(GT_HORDE))
		return;

	// [MuffMode] Clean up any dead monsters from warmup or previous map state
	// before the first wave (OnRoundEnd hasn't run yet for wave 1).
	MM_Horde_CleanWaveTransition();

	level.horde_all_spawned = false;
	level.horde_boss_wave = Horde_IsBossWaveNumber(level.round_number);
	level.horde_boss_spawned = false;
	level.horde_boss_jorg_makron_pending = false;
	level.horde_boss_health_entity = nullptr;

	if (level.horde_boss_wave) {
		const int fighters = MM_Horde_CountFighters();
		level.horde_fighters_snapshotted = static_cast<int8_t>(fighters);
		level.horde_wave_theme = static_cast<int8_t>(horde_theme_t::NONE);
		level.horde_wave_roster = 0;
		level.horde_champion_pending = false;
		level.horde_spawn_points_remaining = 1;

		const int delay_ms = max(0, g_horde_wave_spawn_delay_ms->integer);
		level.horde_monster_spawn_time = level.time + gtime_t::from_ms(delay_ms);
		return;
	}

	// Pick this wave's theme. Rare (g_horde_theme_chance), never the same as the previous
	// themed wave, and only themes whose monsters exist by this wave are eligible.
	{
		const horde_theme_t prev = static_cast<horde_theme_t>(level.horde_wave_theme);
		horde_theme_t chosen = horde_theme_t::NONE;

		if (g_horde_themed_waves->integer &&
			level.round_number >= g_horde_theme_min_wave->integer &&
			frandom() < g_horde_theme_chance->value) {
			const horde_theme_def_t *eligible[q_countof(horde_themes)];
			int num_eligible = 0;

			for (auto &def : horde_themes) {
				if (level.round_number < def.min_wave || def.theme == prev)
					continue;
				// Skip themes that can't field enough on-category bodies at this wave, so a banner
				// never shows for a theme that would spawn off-category fillers (or nothing).
				if (Horde_CountThemeCandidates(def.category, level.round_number) < g_horde_theme_min_monsters->integer)
					continue;
				eligible[num_eligible++] = &def;
			}

			if (num_eligible > 0)
				chosen = eligible[irandom(num_eligible)]->theme;
		}

		level.horde_wave_theme = static_cast<int8_t>(chosen);
	}

	// Build this wave's monster roster: a random subset of the eligible types so runs vary.
	// Non-themed waves only (a themed wave is already a category subset). 0 = unrestricted.
	level.horde_wave_roster = 0;
	if (g_horde_wave_variety->integer &&
		static_cast<horde_theme_t>(level.horde_wave_theme) == horde_theme_t::NONE) {
		int eligible[q_countof(monsters)], cheap[q_countof(monsters)];
		int num_eligible = 0, num_cheap = 0, min_cost = INT_MAX;

		for (size_t i = 0; i < q_countof(monsters); i++) {
			const weighted_item_t &m = monsters[i];
			if (m.min_level != -1 && level.round_number < m.min_level)
				continue;
			if (m.max_level != -1 && level.round_number > m.max_level)
				continue;
			eligible[num_eligible++] = static_cast<int>(i);
			min_cost = min(min_cost, m.spawn_points);
		}

		const int min_types = max(1, g_horde_wave_min_types->integer);
		if (num_eligible > min_types) {
			for (int k = 0; k < num_eligible; k++)
				if (monsters[eligible[k]].spawn_points == min_cost)
					cheap[num_cheap++] = eligible[k];

			const int roster_size = irandom(min_types, num_eligible + 1);	// inclusive max
			uint32_t  mask = 0;
			int       picked = 0;

			// Guarantee one random cheap grunt so the budget always spends down cleanly.
			if (num_cheap > 0) {
				mask |= 1u << cheap[irandom(num_cheap)];
				picked = 1;
			}

			// Shuffle the eligible list, then fill the remaining roster slots from it.
			for (int i = num_eligible - 1; i > 0; i--) {
				const int j = irandom(i + 1);
				const int t = eligible[i];
				eligible[i] = eligible[j];
				eligible[j] = t;
			}

			for (int k = 0; k < num_eligible && picked < roster_size; k++) {
				if (mask & (1u << eligible[k]))
					continue;	// already seeded the grunt
				mask |= 1u << eligible[k];
				picked++;
			}

			level.horde_wave_roster = mask;
		}
	}

	// Decide whether this wave hosts a champion.
	// Up to the content peak: spend the per-run budget (mm_match seeds 0-2), spread across the waves
	// remaining until the peak. Past the peak the budget is gone, so switch to a steady per-wave rate
	// derived from the same knobs (max_per_run * champion_chance champions per peak-length span) so
	// champions keep appearing at the tuned cadence for any wave count.
	level.horde_champion_pending = false;
	if (g_horde_champions->integer &&
		level.round_number >= g_horde_champion_min_wave->integer) {
		if (Horde_IsLateWave()) {
			const int   span = max(1, g_horde_content_peak_wave->integer - g_horde_champion_min_wave->integer + 1);
			const float rate = g_horde_champion_max_per_run->value * g_horde_champion_chance->value / span;

			if (frandom() < min(rate, 1.0f))
				level.horde_champion_pending = true;
		} else if (level.horde_champions_remaining > 0) {
			// Spread the run's budget across the waves left until the peak (== roundlimit for the
			// standard 12-wave run, so that case is unchanged).
			const int last_budget_wave = roundlimit->integer > 0
				? min(roundlimit->integer, g_horde_content_peak_wave->integer)
				: g_horde_content_peak_wave->integer;
			const int waves_left = max(1, last_budget_wave - level.round_number + 1);

			if (frandom() < static_cast<float>(level.horde_champions_remaining) / waves_left) {
				level.horde_champion_pending = true;
				level.horde_champions_remaining--;
			}
		}
	}

	// DEBUG/TEST: force a champion every wave (overrides the roll above), regardless of min_wave.
	if (g_horde_champion_force->integer)
		level.horde_champion_pending = true;

	const int fighters = MM_Horde_CountFighters();
	level.horde_fighters_snapshotted = static_cast<int8_t>(fighters);

	level.horde_spawn_points_remaining = MM_Horde_WavePointBudget();

	if (const horde_theme_def_t *theme = Horde_FindTheme(static_cast<horde_theme_t>(level.horde_wave_theme)))
		level.horde_spawn_points_remaining =
			max(1, static_cast<int>(level.horde_spawn_points_remaining * theme->budget_mult));

	const int delay_ms = max(0, g_horde_wave_spawn_delay_ms->integer);
	level.horde_monster_spawn_time = level.time + gtime_t::from_ms(delay_ms);
}

// Horde spawn points are deathmatch player spawns; their origins are placed for
// the player hull (which gets a +9 lift and stuck-fixing in client spawn code) and
// can sit low enough that monster hulls start embedded in the floor — on bloodrun
// every spawn origin is only 15u above its floor. A monster spawned embedded in a
// thin floor gets teleported through it by M_droptofloor (a trace does not clip
// against a brush it starts inside), e.g. into the blood pool under the walkway at
// 1104 208 -633. Lift the origin clear before validating, and nudge as a fallback.
// Also rejects spots whose ground is liquid. Returns false if the spot is unusable.
static bool Horde_ValidateSpawnOrigin(vec3_t &origin, const vec3_t &check_mins, const vec3_t &check_maxs, bool allow_nudge = true)
{
	origin[2] += 16.f;

	if (!CheckSpawnPoint(origin, check_mins, check_maxs)) {
		if (!allow_nudge)
			return false;
		if (G_FixStuckObject_Generic(origin, check_mins, check_maxs,
				[](const vec3_t &start, const vec3_t &mins, const vec3_t &maxs, const vec3_t &end) {
					return gi.trace(start, mins, maxs, end, nullptr, MASK_MONSTERSOLID);
				}) == stuck_result_t::NO_GOOD_POSITION)
			return false;
		if (!CheckSpawnPoint(origin, check_mins, check_maxs))
			return false;
	}

	vec3_t grounded = origin;
	if (!M_droptofloor_generic(grounded, check_mins, check_maxs, false, nullptr, MASK_MONSTERSOLID, false))
		return false;
	if (origin[2] - grounded[2] > 96.f)
		return false;
	if (!CheckGroundSpawnPoint(grounded, check_mins, check_maxs, 96.f, -1.f))
		return false;
	if (gi.pointcontents(grounded) & (CONTENTS_LAVA | CONTENTS_SLIME))
		return false;

	origin = grounded;
	return true;
}

static float Horde_PlayerRangeFromSpot(gentity_t *spot)
{
	float best_distance = 999999.f;

	for (auto ec : active_clients()) {
		if (!ec->client || !ClientIsPlaying(ec->client))
			continue;
		if (ec->health <= 0 || ec->client->eliminated)
			continue;

		const float distance = (spot->s.origin - ec->s.origin).length();
		if (distance < best_distance)
			best_distance = distance;
	}

	return best_distance;
}

static bool Horde_BossCanMoveToSample(const horde_boss_def_t *boss, const vec3_t &origin, const vec3_t &offset)
{
	vec3_t sample_origin = origin + offset;

	if (!Horde_ValidateSpawnOrigin(sample_origin, boss->mins, boss->maxs, false))
		return false;
	if (fabs(sample_origin[2] - origin[2]) > 48.f)
		return false;

	const trace_t tr = gi.trace(origin, boss->mins, boss->maxs, sample_origin, nullptr, MASK_MONSTERSOLID);
	return !tr.startsolid && !tr.allsolid && tr.fraction == 1.f;
}

// Bosses use oversized hulls and can fit at a deathmatch spawn while still being
// trapped in a corridor or side room. Require open movement in several directions
// so boss waves prefer arenas/courtyards over narrow player-only routes.
static bool Horde_BossSpawnHasArenaClearance(const horde_boss_def_t *boss, const vec3_t &origin)
{
	constexpr float near_radius = 192.f;
	constexpr float far_radius = 320.f;
	constexpr float diagonal = 0.70710678f;
	constexpr vec3_t directions[] = {
		{  1.f,       0.f,       0.f },
		{ -1.f,       0.f,       0.f },
		{  0.f,       1.f,       0.f },
		{  0.f,      -1.f,       0.f },
		{  diagonal,  diagonal, 0.f },
		{  diagonal, -diagonal, 0.f },
		{ -diagonal,  diagonal, 0.f },
		{ -diagonal, -diagonal, 0.f },
	};

	int near_clear = 0;
	int far_clear = 0;

	for (const vec3_t &dir : directions) {
		if (Horde_BossCanMoveToSample(boss, origin, dir * near_radius))
			near_clear++;
		if (Horde_BossCanMoveToSample(boss, origin, dir * far_radius))
			far_clear++;
	}

	return near_clear >= 3 && far_clear >= 2;
}

static bool Horde_ConsiderBossSpawnSpot(const horde_boss_def_t *boss, gentity_t *spot, vec3_t &best_origin, vec3_t &best_angles, float &best_distance, bool require_arena_clearance)
{
	if (!spot)
		return false;

	vec3_t candidate_origin = spot->s.origin;
	if (!Horde_ValidateSpawnOrigin(candidate_origin, boss->mins, boss->maxs, false))
		return false;
	if (require_arena_clearance && !Horde_BossSpawnHasArenaClearance(boss, candidate_origin))
		return false;

	const float distance = Horde_PlayerRangeFromSpot(spot);
	if (distance <= best_distance && best_distance < 999999.f)
		return true;

	best_origin = candidate_origin;
	best_angles = spot->s.angles;
	best_distance = distance;
	return true;
}

static bool Horde_SelectBossSpawnPoint(const horde_boss_def_t *boss, vec3_t &spawn_origin, vec3_t &spawn_angles, bool require_arena_clearance = true)
{
	float best_distance = -1.f;
	bool  found = false;

	auto consider_class = [&](const char *classname) {
		gentity_t *spot = nullptr;
		while ((spot = G_FindByString<&gentity_t::classname>(spot, classname)) != nullptr) {
			if (Horde_ConsiderBossSpawnSpot(boss, spot, spawn_origin, spawn_angles, best_distance, require_arena_clearance))
				found = true;
		}
	};

	consider_class("info_player_deathmatch");
	if (!found) {
		consider_class("info_player_team_red");
		consider_class("info_player_team_blue");
	}
	if (!found)
		consider_class("info_player_start");

	return found;
}

static void Horde_ApplyBossScaling(gentity_t *boss, bool makron_phase, float boss_health_factor = 1.f)
{
	if (!boss || !(boss->svflags & SVF_MONSTER))
		return;

	const int fighters = level.horde_fighters_snapshotted > 0
		? level.horde_fighters_snapshotted
		: MM_Horde_CountFighters();
	const float player_scale = max(0.f, g_horde_boss_player_health_scale->value);
	const float player_mult = 1.f + max(0, fighters - 1) * player_scale;
	const float base_hp = max(0.f, g_horde_boss_health_base->value);
	const float hp_per_point = max(0.f, g_horde_boss_health_per_point->value);
	const float hp_per_wave = max(0.f, g_horde_boss_health_per_wave->value);
	const float global_mult = max(0.1f, g_horde_boss_health_mult->value);
	const float target_health = max(1.f, base_hp +
		(hp_per_point * static_cast<float>(MM_Horde_WavePointBudget())) +
		(hp_per_wave * static_cast<float>(level.round_number)));
	const float boss_mult = max(0.1f, boss_health_factor);
	const float phase_health_mult = makron_phase ? max(0.1f, g_horde_boss_makron_health_mult->value) : 1.f;
	const float phase_damage_mult = makron_phase ? max(0.1f, g_horde_boss_makron_damage_mult->value) : 1.f;
	const int boss_hp = max(1, static_cast<int>(ceil(target_health * global_mult * player_mult * boss_mult * phase_health_mult)));

	boss->health = boss->max_health = boss_hp;
	boss->monsterinfo.base_health = boss_hp;
	boss->monsterinfo.champion_damage_scale = max(1.f, g_horde_boss_damage_mult->value * phase_damage_mult);
	boss->spawnflags |= SPAWNFLAG_MONSTER_HORDE_BOSS;
	boss->item = nullptr;
}

static bool Horde_SpawnBossMonster()
{
	const horde_boss_def_t *boss = nullptr;
	vec3_t spawn_origin;
	vec3_t spawn_angles;
	uint32_t tried_bosses = 0;

	for (size_t attempt = 0; attempt < q_countof(horde_bosses); attempt++) {
		const horde_boss_def_t *candidate = Horde_PickBoss(tried_bosses | Horde_LastBossMask());
		if (!candidate)
			break;

		tried_bosses |= Horde_BossMask(candidate);
		if (!Horde_SelectBossSpawnPoint(candidate, spawn_origin, spawn_angles))
			continue;

		boss = candidate;
		break;
	}

	if (!boss) {
		if (const horde_boss_def_t *fallback = Horde_LastBoss()) {
			if (Horde_SelectBossSpawnPoint(fallback, spawn_origin, spawn_angles))
				boss = fallback;
		}
	}

	if (!boss) {
		if (const horde_boss_def_t *fallback = Horde_EmergencyBoss()) {
			if (Horde_SelectBossSpawnPoint(fallback, spawn_origin, spawn_angles, false))
				boss = fallback;
		}
	}

	if (!boss)
		return false;

	gentity_t *e = G_Spawn();
	e->classname = boss->classname;

	e->s.origin = spawn_origin;
	e->s.angles = spawn_angles;

	st = {};
	ED_CallSpawn(e);

	if (!e->inuse || !(e->svflags & SVF_MONSTER)) {
		if (e->inuse)
			G_FreeEntity(e);
		return false;
	}

	Horde_RememberBoss(boss);
	Horde_ApplyBossScaling(e, false, boss->health_factor);
	e->enemy = FindClosestPlayerToPoint(e->s.origin);
	if (e->enemy)
		FoundTarget(e);

	level.horde_boss_spawned = true;
	level.horde_all_spawned = true;
	level.horde_spawn_points_remaining = 0;
	level.horde_boss_health_entity = e;

	gi.LocBroadcast_Print(PRINT_CENTER, "BOSS WAVE!\n{} has arrived!\n", boss->name);
	gi.LocBroadcast_Print(PRINT_CHAT, "Boss wave: {}!\n", boss->name);
	return true;
}

void MM_Horde_RunSpawning()
{
	if (notGT(GT_HORDE))
		return;

	bool warmup = level.match_state == MATCH_WARMUP_DEFAULT || level.match_state == MATCH_WARMUP_READYUP;

	if (!warmup && level.round_state != ROUND_IN_PROGRESS)
		return;

	const int warmup_cap = max(1, g_horde_warmup_cap->integer);
	if (warmup && (level.total_monsters - level.killed_monsters >= warmup_cap))
		return;

	// Cap concurrently-alive monsters during live waves. Without this, a high-budget
	// swarm wave (many cheap monsters) can pile up hundreds of homing entities on a
	// single player and overflow that client's network message buffer (SZ_GetSpace).
	// Spawning pauses while at the cap and resumes as monsters die, so the wave still
	// spawns its full budget over time - only peak concurrency is bounded. 0 disables.
	const int alive_cap = g_horde_max_alive->integer;
	if (!warmup && alive_cap > 0 && (level.total_monsters - level.killed_monsters >= alive_cap))
		return;

	if (level.horde_all_spawned)
		return;

	if (!warmup && level.horde_boss_wave) {
		if (level.horde_boss_spawned) {
			level.horde_all_spawned = true;
			return;
		}

		if (level.horde_monster_spawn_time <= level.time) {
			if (!Horde_SpawnBossMonster())
				level.horde_monster_spawn_time = level.time + 1_sec;
		}
		return;
	}

	if (!warmup && level.horde_spawn_points_remaining <= 0) {
		level.horde_all_spawned = true;
		if (level.horde_champion_pending)
			level.horde_champion_pending = false;
		return;
	}

	if (level.horde_monster_spawn_time <= level.time) {
		const int              remaining = warmup ? INT_MAX : level.horde_spawn_points_remaining;
		const weighted_item_t *monster_row = nullptr;
		const char            *monster_class = Horde_PickMonsterForWave(&monster_row, remaining);
		if (!monster_class) {
			if (!warmup) {
				level.horde_all_spawned = true;
				if (level.horde_champion_pending)
					level.horde_champion_pending = false;
			} else
				level.horde_monster_spawn_time = level.time + 5_sec;
			return;
		}

		gentity_t *e = G_Spawn();
		e->classname = monster_class;
		select_spawn_result_t result = SelectDeathmatchSpawnPoint(nullptr, vec3_origin, SPAWN_FARTHEST, false, true, false, false);

		if (result.any_valid && result.spot) {
			// Validate spawn point fits a large monster (tank commander is the worst-case hull).
			// CheckSpawnPoint also rejects non-world solids (doors, movers) unlike a raw startsolid check.
			constexpr vec3_t horde_check_mins = { -32.f, -32.f, -16.f };
			constexpr vec3_t horde_check_maxs = {  32.f,  32.f,  64.f };
			vec3_t spawn_origin = result.spot->s.origin;
			if (!Horde_ValidateSpawnOrigin(spawn_origin, horde_check_mins, horde_check_maxs)) {
				// Try a different candidate by excluding the failed spot from selection.
				// avoid_point is honoured when g_dm_respawn_point_min_dist > 0 (default 256).
				select_spawn_result_t retry = SelectDeathmatchSpawnPoint(nullptr, result.spot->s.origin, SPAWN_FARTHEST, false, true, false, false);
				bool retry_ok = false;
				if (retry.any_valid && retry.spot && retry.spot != result.spot) {
					spawn_origin = retry.spot->s.origin;
					if (Horde_ValidateSpawnOrigin(spawn_origin, horde_check_mins, horde_check_maxs)) {
						result = retry;
						retry_ok = true;
					}
				}
				if (!retry_ok) {
					// No spot can safely hold a large monster right now. Spawning anyway would
					// place it embedded and let monster_start_go's stuck-fixing relocate it
					// through thin floors or into walls; skip this attempt and retry shortly.
					G_FreeEntity(e);
					level.horde_monster_spawn_time = warmup ? level.time + 5_sec : level.time + 1_sec;
					return;
				}
			}

			e->s.origin = spawn_origin;
			e->s.angles = result.spot->s.angles;

			// The first valid spawn of a champion-pending wave becomes the champion. Base health is
			// scaled 3x via st before spawn; after spawn we apply a health floor + wave scaling so even
			// a weak monster (e.g. light soldier) becomes a real threat, plus tapered damage/speed buffs
			// and the EF_DOUBLE shell (applied after spawn, since monster_start zeroes the powerup timers).
			// The buffs taper by base strength: weak monsters get the full punch, heavy ones (tank etc.)
			// stay beefy bullet-sponges without becoming one-shot deleters.
			const bool is_champion = level.horde_champion_pending && !warmup;

			e->item = is_champion ? Horde_PickChampionDrop() : Horde_PickDropItem(monster_row);
			st = {};
			st.health_multiplier = is_champion ? g_horde_champion_health_mult->value : 1.0f;
			ED_CallSpawn(e);

			if (!e->inuse || !(e->svflags & SVF_MONSTER)) {
				if (e->inuse)
					G_FreeEntity(e);
				level.horde_monster_spawn_time = warmup ? level.time + 5_sec : level.time + 1_sec;
				return;
			}

			if (is_champion) {
				// natural = full health after spawn (already includes the 3x mult and any co-op scaling).
				const int natural = e->health;

				// Put the floor on the same footing as the (possibly co-op-scaled) natural health by
				// mirroring whatever multiplier co-op applied. base_health is the pre-co-op health set by
				// G_Monster_ScaleCoopHealth; it stays 0 when no co-op scaling happened (pure DM/horde).
				const float coop_mult = (e->monsterinfo.base_health > 0)
					? (float)natural / (float)e->monsterinfo.base_health
					: 1.0f;

				const float floor_base = g_horde_champion_health_floor->value +
					g_horde_champion_health_per_wave->value * (float)level.round_number;
				const float floor_hp = floor_base * coop_mult;

				// Weakness signal drives the taper: 1.0 for a sub-floor monster (full help), 0.0 once its
				// natural health reaches strong_ratio x floor.
				const float strong_hp = floor_hp * g_horde_champion_strong_ratio->value;
				const float denom = max(1.0f, strong_hp - floor_hp);
				const float weakness = clamp((strong_hp - (float)natural) / denom, 0.0f, 1.0f);

				// Health: lift weak monsters to the floor; leave naturally-tough ones at their 3x. Co-op
				// re-scaling on later joins only adds health, so the floor is never undercut.
				const int champ_hp = max(natural, (int)floor_hp);
				e->health = e->max_health = champ_hp;

				// Tapered offensive buffs. Damage scale is stored for T_Damage; speed folds into the
				// frame-distance multiplier (already = MODEL_SCALE * s.scale at this point).
				e->monsterinfo.champion_damage_scale = lerp(1.0f, g_horde_champion_damage_mult->value, weakness);
				e->monsterinfo.scale *= lerp(1.0f, g_horde_champion_speed_mult->value, weakness);

				// EF_DOUBLE shell marks every champion regardless of tier.
				e->monsterinfo.double_time = HOLD_FOREVER;
				level.horde_champion_pending = false;
			}

			level.horde_monster_spawn_time = level.time + Horde_SpawnInterval(warmup);

			e->enemy = FindClosestPlayerToPoint(e->s.origin);
			if (e->enemy)
				FoundTarget(e);

			if (!warmup && monster_row) {
				level.horde_spawn_points_remaining -= monster_row->spawn_points;

				if (level.horde_spawn_points_remaining <= 0) {
					level.horde_all_spawned = true;
					if (level.horde_champion_pending)
						level.horde_champion_pending = false;
				}
			}
		} else {
			G_FreeEntity(e);
			level.horde_monster_spawn_time = warmup ? level.time + 5_sec : level.time + 1_sec;
		}
	}
}

void MM_Horde_AdjustPlayerScore(gclient_t *cl, int32_t offset)
{
	if (notGT(GT_HORDE))
		return;
	if (!cl || !cl->pers.connected)
		return;

	if (IsScoringDisabled())
		return;

	G_AdjustPlayerScore(cl, offset, false, 0);
}

void MM_Horde_OnMonsterKilled(gentity_t *ent)
{
	if (!HordeActive() || !ent || !ent->spawnflags.has(SPAWNFLAG_MONSTER_HORDE_BOSS))
		return;

	if (level.horde_boss_health_entity == ent)
		level.horde_boss_health_entity = nullptr;

	if (ent->classname && !Q_strcasecmp(ent->classname, "monster_jorg")) {
		const float makron_chance = clamp(g_horde_boss_jorg_makron_chance->value, 0.f, 1.f);
		if (makron_chance > 0.f && frandom() < makron_chance) {
			level.horde_boss_jorg_makron_pending = true;
			gi.LocBroadcast_Print(PRINT_CENTER, "Jorg is not done!\n");
			gi.LocBroadcast_Print(PRINT_CHAT, "Jorg is not done! Makron phase incoming.\n");
			return;
		}
	}

	level.horde_boss_jorg_makron_pending = false;
	Horde_SpawnBossReward(ent->s.origin);
}

bool MM_Horde_GetBossHealthBar(const char **name, uint8_t *health_byte)
{
	if (name)
		*name = nullptr;

	if (!HordeActive())
		return false;

	gentity_t *boss = level.horde_boss_health_entity;
	if (!boss)
		return false;

	if (!boss->inuse || !(boss->svflags & SVF_MONSTER) ||
		!boss->spawnflags.has(SPAWNFLAG_MONSTER_HORDE_BOSS) ||
		boss->health <= 0 || boss->max_health <= 0) {
		level.horde_boss_health_entity = nullptr;
		return false;
	}

	const float health_remaining = clamp((float) boss->health / (float) boss->max_health, 0.f, 1.f);
	const uint8_t health_value = (uint8_t) clamp((int) (health_remaining * 0b01111111), 0, 0b01111111);

	if (name)
		*name = Horde_BossHealthBarName(boss);
	if (health_byte)
		*health_byte = health_value | 0b10000000;

	return true;
}

bool MM_Horde_ShouldAllowJorgMakron(gentity_t *jorg)
{
	if (!HordeActive())
		return true;
	if (!jorg || !jorg->spawnflags.has(SPAWNFLAG_MONSTER_HORDE_BOSS))
		return true;

	return level.horde_boss_jorg_makron_pending;
}

void MM_Horde_OnJorgMakronSpawned(gentity_t *jorg, gentity_t *makron)
{
	if (!HordeActive() || !jorg || !jorg->spawnflags.has(SPAWNFLAG_MONSTER_HORDE_BOSS))
		return;
	if (!level.horde_boss_jorg_makron_pending)
		return;

	level.horde_boss_jorg_makron_pending = false;

	if (!makron || !makron->inuse || !(makron->svflags & SVF_MONSTER)) {
		Horde_SpawnBossReward(jorg->s.origin);
		return;
	}

	Horde_ApplyBossScaling(makron, true);
	level.horde_boss_health_entity = makron;
	gi.LocBroadcast_Print(PRINT_CENTER, "Makron emerges!\n");
	gi.LocBroadcast_Print(PRINT_CHAT, "Makron has entered the boss wave!\n");
}

// [MuffMode] Give bonus ammo on monster kill, scaled by the number of active
// fighters so larger groups aren't starved by map ammo scarcity.
void MM_Horde_AdjustAmmoDrop(gentity_t *attacker)
{
	if (notGT(GT_HORDE))
		return;
	if (!attacker || !attacker->client)
		return;
	if (g_horde_ammo_drop_scale->value <= 0.f)
		return;

	const int fighters = level.horde_fighters_snapshotted;
	if (fighters <= 1)
		return;

	static constexpr struct { item_id_t id; ammo_t tag; } common_ammo[] = {
		{ IT_AMMO_SHELLS,    AMMO_SHELLS },
		{ IT_AMMO_BULLETS,   AMMO_BULLETS },
		{ IT_AMMO_CELLS,     AMMO_CELLS },
		{ IT_AMMO_ROCKETS,   AMMO_ROCKETS },
		{ IT_AMMO_SLUGS,     AMMO_SLUGS },
		{ IT_AMMO_GRENADES,  AMMO_GRENADES },
	};

	const float extra = g_horde_ammo_drop_scale->value * (fighters - 1);
	int         count = (int)extra;
	if (frandom() < (extra - count))
		count++;

	for (int i = 0; i < count; i++)
	{
		auto &entry = common_ammo[irandom(q_countof(common_ammo))];
		gitem_t *item = GetItemByIndex(entry.id);
		if (!item)
			continue;
		Add_Ammo(attacker, item, item->quantity);
	}
}
