#include "../features.hpp"
#include "thirdperson.h"

void features::misc::bunny_hop(c_usercmd* cmd) 
{
	static bool last_jumped = false, should_fake = false;

	if (!last_jumped && should_fake) {
		should_fake = false;
		cmd->buttons |= in_jump;
	}
	else if (cmd->buttons & in_jump) {
		if (csgo::local_player->flags() & fl_onground) 
		{
			last_jumped = true;
			should_fake = true;
		}
		else 
		{
			cmd->buttons &= ~in_jump;
			last_jumped = false;
		}
	}
	else 
	{
		last_jumped = false;
		should_fake = false;
	}
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

float best_cam_dist() {
	ray_t ray;
	trace_t tr;
	trace_world_only filter;
	vec3_t forward, backward, end;
	filter.pSkip1 = csgo::local_player;
	vec3_t va;
	interfaces::engine->get_view_angles(va);

	math::angle_vectors_alternative(va, &forward);
	backward = forward * -1;
	backward *= menu.config.third_person_distance;
	end = csgo::local_player->get_eye_pos() + backward;

	ray.initialize(csgo::local_player->get_eye_pos(), end);

	interfaces::trace_ray->trace_ray(ray, MASK_SHOT_HULL, &filter, &tr);

	if (!tr.entity)
		return menu.config.third_person_distance;

	return (tr.end - csgo::local_player->get_eye_pos()).length() - 20.f;
}

void features::misc::thirdperson()
{
	if (!csgo::local_player)
		return;

	interfaces::input->m_fCameraInThirdPerson = menu.config.third_person && menu.config.thirdperson_pair.second && csgo::local_player->is_alive();
	if (!interfaces::input->m_fCameraInThirdPerson)
		return;

	auto weapon = csgo::local_player->active_weapon();

	const auto weapon_type = weapon->get_weapon_data()->m_iWeaponType;

	if ((weapon_type == WEAPONTYPE_GRENADE || weapon_type == WEAPONTYPE_C4))
		interfaces::input->m_fCameraInThirdPerson = false;

	if ((weapon_type == WEAPONTYPE_PISTOL || weapon_type == WEAPONTYPE_MACHINEGUN || weapon_type == WEAPONTYPE_RIFLE || weapon_type == WEAPONTYPE_SHOTGUN || weapon_type == WEAPONTYPE_SNIPER_RIFLE || weapon_type == WEAPONTYPE_SUBMACHINEGUN) && menu.config.third_disable_on_weapon)
		interfaces::input->m_fCameraInThirdPerson = false;

	vec3_t angles;
	interfaces::engine->get_view_angles(angles);

	vec3_t backward(angles.x, angles.y + 180.f, angles.z);
	backward = backward.clamp();
	backward.normalize();

	vec3_t range;
	math::angle_vectors_alternative(backward, &range);
	range *= 8192.f;

	auto start = csgo::local_player->get_eye_pos();

	trace_filter filter;
	filter.skip = csgo::local_player;

	ray_t ray;
	ray.initialize(start, start + range);

	trace_t tr;
	interfaces::trace_ray->trace_ray(ray, MASK_SHOT | CONTENTS_GRATE, &filter, &tr);
	angles.z = std::min<int>(start.distance_to(tr.end), best_cam_dist()); // 150 is better distance

	interfaces::input->m_vecCameraOffset = angles;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void features::misc::auto_pistol(c_usercmd* cmd)
{
	if (!csgo::local_player)
		return;

	if (!csgo::local_player->is_alive())
		return;

	auto weapon = csgo::local_player->active_weapon();
	if (!weapon)
		return;

	static bool fire = false;
	if (weapon->get_weapon_data()->m_iWeaponType == WEAPONTYPE_PISTOL && weapon->item_definition_index() != WEAPON_REVOLVER && cmd->buttons & in_attack)
	{
		if (fire)
			cmd->buttons &= ~in_attack;
		fire = cmd->buttons & in_attack ? true : false;
	}
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

vec3_t quick_peck_start_pos{};
bool has_shot;

void goto_start(c_usercmd* cmd) 
{
	if (!csgo::local_player || csgo::local_player->dormant() || !csgo::local_player->is_alive()) 
		return;

	vec3_t playerLoc = csgo::local_player->abs_origin();

	float yaw = cmd->viewangles.y;
	vec3_t VecForward = playerLoc - quick_peck_start_pos;
	vec3_t translatedVelocity = vec3_t{
		(float)(VecForward.x * cos(yaw / 180 * (float)M_PI) + VecForward.y * sin(yaw / 180 * (float)M_PI)),
		(float)(VecForward.y * cos(yaw / 180 * (float)M_PI) - VecForward.x * sin(yaw / 180 * (float)M_PI)),
		VecForward.z
	};

	cmd->forwardmove = -translatedVelocity.x * 20.f;
	cmd->sidemove = translatedVelocity.y * 20.f;

	if (translatedVelocity.x == 0)
		menu.config.do_quick_peek = false;
}

void features::misc::quick_peak(c_usercmd* cmd)
{
	if (!csgo::local_player || csgo::local_player->dormant() || !csgo::local_player->is_alive()) 
		return;

	if (menu.config.do_quick_peek)
	{
		if (quick_peck_start_pos.x == NULL && quick_peck_start_pos.y == NULL && quick_peck_start_pos.z == NULL)
			quick_peck_start_pos = csgo::local_player->abs_origin();
		else 
		{
			if (cmd->buttons & in_attack) 
				has_shot = true;
			if (has_shot)
				goto_start(cmd);
		}
	}
	else 
	{
		has_shot = false;
		quick_peck_start_pos = vec3_t{ 0, 0, 0 };
	}
}

void features::misc::draw_start_pos()
{
	if (quick_peck_start_pos.x != NULL && quick_peck_start_pos.y != NULL && quick_peck_start_pos.z != NULL)
	{
		vec3_t out;
		if (interfaces::debug_overlay->world_to_screen(quick_peck_start_pos, out))
			render::draw_circle(out.x, out.y, 30, 60, color(0, 0, 0));
	}
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

DWORD* offset = NULL;
typedef int(__fastcall* set_clan_tag_fn)(const char*, const char*);
size_t pos = 0;
std::string clantag;
float last_time = 0;

void features::misc::set_clan_tag(const char* tag)
{
	if (!offset)
		offset = (DWORD*)utilities::pattern_scan(GetModuleHandleA("engine.dll"), "53 56 57 8B DA 8B F9 FF 15");
	reinterpret_cast<set_clan_tag_fn>(offset)(tag, "Overflow");
}

void features::misc::animated_clan_tag(const char* tag)
{
	if (clantag != menu.config.clan_tag || clantag.length() < pos)
	{
		clantag = menu.config.clan_tag;
		pos = 0;
	}

	if (last_time + menu.config.clan_tag_delay > interfaces::globals->realtime)
		return;

	last_time = interfaces::globals->realtime;

	reinterpret_cast<set_clan_tag_fn>(offset)(clantag.substr(0, pos).c_str(), "Overflow");
	pos++;
}