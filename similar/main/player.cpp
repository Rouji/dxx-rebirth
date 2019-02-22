/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 *
 * Player Stuff
 *
 */

#include "player.h"
#include "physfsx.h"
#include "compiler-range_for.h"

namespace dsx {
void player_rw_swap(player_rw *p, int swap)
{
	if (!swap)
		return;

	p->objnum = SWAPINT(p->objnum);
	p->flags = SWAPINT(p->flags);
	p->energy = SWAPINT(p->energy);
	p->shields = SWAPINT(p->shields);
	p->killer_objnum = SWAPSHORT(p->killer_objnum);
#if defined(DXX_BUILD_DESCENT_II)
	p->primary_weapon_flags = SWAPSHORT(p->primary_weapon_flags);
#endif
	p->vulcan_ammo = SWAPSHORT(p->vulcan_ammo);
	for (int i = 0; i < MAX_SECONDARY_WEAPONS; i++)
		p->secondary_ammo[i] = SWAPSHORT(p->secondary_ammo[i]);
	p->last_score = SWAPINT(p->last_score);
	p->score = SWAPINT(p->score);
	p->time_level = SWAPINT(p->time_level);
	p->time_total = SWAPINT(p->time_total);
	p->cloak_time = SWAPINT(p->cloak_time);
	p->invulnerable_time = SWAPINT(p->invulnerable_time);
#if defined(DXX_BUILD_DESCENT_II)
	p->KillGoalCount = SWAPSHORT(p->KillGoalCount);
#endif
	p->net_killed_total = SWAPSHORT(p->net_killed_total);
	p->net_kills_total = SWAPSHORT(p->net_kills_total);
	p->num_kills_level = SWAPSHORT(p->num_kills_level);
	p->num_kills_total = SWAPSHORT(p->num_kills_total);
	p->num_robots_level = SWAPSHORT(p->num_robots_level);
	p->num_robots_total = SWAPSHORT(p->num_robots_total);
	p->hostages_rescued_total = SWAPSHORT(p->hostages_rescued_total);
	p->hostages_total = SWAPSHORT(p->hostages_total);
	p->homing_object_dist = SWAPINT(p->homing_object_dist);
}
}

/*
 * reads a player_ship structure from a PHYSFS_File
 */
void player_ship_read(player_ship *ps, PHYSFS_File *fp)
{
	ps->model_num = PHYSFSX_readInt(fp);
	ps->expl_vclip_num = PHYSFSX_readInt(fp);
	ps->mass = PHYSFSX_readFix(fp);
	ps->drag = PHYSFSX_readFix(fp);
	ps->max_thrust = PHYSFSX_readFix(fp);
	ps->reverse_thrust = PHYSFSX_readFix(fp);
	ps->brakes = PHYSFSX_readFix(fp);
	ps->wiggle = PHYSFSX_readFix(fp);
	ps->max_rotthrust = PHYSFSX_readFix(fp);
	range_for (auto &i, ps->gun_points)
		PHYSFSX_readVector(fp, i);
}
