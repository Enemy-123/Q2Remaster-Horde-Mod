// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"

/*
========================
fire_flechette
========================
*/
//explosive flechettes!
//THINK(delayed_flechette_explode)(edict_t* self) -> void {
//	T_RadiusDamage(self, self->owner, 40, nullptr, 100, DAMAGE_NO_REG_ARMOR, MOD_ETF_RIFLE);
//
//	gi.WriteByte(svc_temp_entity);
//	gi.WriteByte(TE_EXPLOSION1);
//	gi.WritePosition(self->s.origin);
//	gi.multicast(self->s.origin, MULTICAST_PHS, false);
//
//	G_FreeEdict(self);
//}
//
//// En flechette_touch:
//if (!other->takedamage) {
//	self->movetype = MOVETYPE_NONE;
//	self->think = delayed_flechette_explode;
//	self->nextthink = level.time + 1_sec;
//	return;
//}
///

TOUCH(flechette_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
			self->dmg, (int)self->dmg_radius, DAMAGE_NO_REG_ARMOR, MOD_ETF_RIFLE);
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_FLECHETTE);
		gi.WritePosition(self->s.origin);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}

	G_FreeEdict(self);
}

void fire_flechette(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, int kick)
{
	edict_t* flechette;

	flechette = G_Spawn();
	flechette->s.origin = start;
	flechette->s.old_origin = start;
	flechette->s.angles = vectoangles(dir);
	flechette->velocity = dir * speed;
	flechette->svflags |= SVF_PROJECTILE;
	flechette->movetype = MOVETYPE_FLYMISSILE;
	flechette->clipmask = MASK_PROJECTILE;
	flechette->flags |= FL_DODGE;

	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		flechette->clipmask &= ~CONTENTS_PLAYER;

	flechette->solid = SOLID_BBOX;
	flechette->s.renderfx = RF_FULLBRIGHT;
	flechette->s.modelindex = gi.modelindex("models/proj/flechette/tris.md2");

	flechette->owner = self;
	flechette->touch = flechette_touch;
	flechette->nextthink = level.time + gtime_t::from_sec(8000.f / speed);
	flechette->think = G_FreeEdict;
	flechette->dmg = damage;
	flechette->dmg_radius = (float)kick;

	gi.linkentity(flechette);

	trace_t tr = gi.traceline(self->s.origin, flechette->s.origin, flechette, flechette->clipmask);
	if (tr.fraction < 1.0f)
	{
		flechette->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		flechette->touch(flechette, tr.ent, tr, false);
	}
}

// **************************
// PROX
// **************************

constexpr gtime_t PROX_TIME_TO_LIVE = 45_sec; // 45, 30, 15, 10
constexpr gtime_t PROX_TIME_DELAY = 500_ms;
constexpr float	  PROX_BOUND_SIZE = 96;
constexpr float	  PROX_DAMAGE_RADIUS = 192;
constexpr int32_t PROX_HEALTH = 20;
constexpr int32_t PROX_DAMAGE = 80;
constexpr float PROX_DAMAGE_OPEN_MULTIPLIER = 1.5f;


// Estructura para configurar el comportamiento de las granadas fragmentarias
struct ClusterConfig {
	int num_grenades;          // Número total de granadas
	int direct_grenades;       // Granadas que caen directamente
	float spread_angle;        // Ángulo de dispersión para fragmentación
	float min_velocity;        // Velocidad mínima de las granadas
	float max_velocity;        // Velocidad máxima de las granadas
	float min_fuse_time;       // Tiempo mínimo de explosión
	float max_fuse_time;       // Tiempo máximo de explosión
	float damage_multiplier;   // Multiplicador de daño para fragmentos
	float homing_bias;         // Influencia del direcionamiento hacia enemigos (0-1)
	float search_radius;       // Radio de búsqueda de enemigos
};

// Configuración por defecto para el clustering
static const ClusterConfig DEFAULT_CLUSTER_CONFIG = {
	15,     // num_grenades
	3,      // direct_grenades
	45.0f,  // spread_angle
	400.0f, // min_velocity
	600.0f, // max_velocity
	0.5f,   // min_fuse_time
	2.0f,   // max_fuse_time
	0.5f,   // damage_multiplier
	0.3f,   // homing_bias (30% de influencia)
	384.0f  // search_radius (2x PROX_DAMAGE_RADIUS)
};

// Función para encontrar el enemigo más cercano
edict_t* FindNearestEnemy(edict_t* owner, const vec3_t& origin, float search_radius) {
	edict_t* nearest = nullptr;
	float nearest_dist = search_radius * search_radius;
	edict_t* current = nullptr;

	while ((current = findradius(current, origin, search_radius)) != nullptr) {
		// Ignorar entidades no válidas o muertas
		if (!current->inuse || current->health <= 0 || !current->classname) {
			continue;
		}

		// Verificar si es un objetivo válido (monstruo o jugador enemigo)
		if (!(current->svflags & SVF_MONSTER) && !current->client) {
			continue;
		}

		// Evitar dañar a compañeros de equipo
		if (CheckTeamDamage(current, owner)) {
			continue;
		}

		// Calcular distancia usando el vec3_t moderno
		vec3_t const diff = current->s.origin - origin;
		float const dist = diff.lengthSquared();

		// Actualizar si encontramos uno más cercano
		if (dist < nearest_dist) {
			nearest = current;
			nearest_dist = dist;
		}
	}

	return nearest;
}

// Función separada para el manejo de granadas fragmentarias
void SpawnClusterGrenades(edict_t* owner, const vec3_t& origin, int base_damage) {
	const ClusterConfig& config = DEFAULT_CLUSTER_CONFIG;

	// Buscar el enemigo más cercano para influenciar la dirección
	const edict_t* nearest_enemy = FindNearestEnemy(owner, origin, config.search_radius);

	// Calcular dirección base hacia el enemigo si existe
	vec3_t enemy_dir{};
	if (nearest_enemy) {
		enemy_dir = nearest_enemy->s.origin - origin;
		enemy_dir = safe_normalized(enemy_dir); // Uso de safe_normalized
	}

	// Calcular el daño para cada granada fragmentaria
	int const fragment_damage = static_cast<int>(base_damage * config.damage_multiplier);

	for (int n = 0; n < config.num_grenades; n++) {
		vec3_t forward;

		if (n < config.direct_grenades) {
			// Granadas que caen directamente hacia abajo
			forward = vec3_t{ 0, 0, -1 };
		}
		else {
			// Granadas con dispersión radial
			float const pitch = -config.spread_angle + (frandom() - 0.5f) * 30.0f;
			float yaw = (n - config.direct_grenades) * (360.0f / (config.num_grenades - config.direct_grenades));
			// Añadir una pequeña variación al yaw para más naturalidad
			yaw += (frandom() - 0.5f) * 10.0f;

			vec3_t const angles{ pitch, yaw, 0 };
			auto [fwd, right, up] = AngleVectors(angles);
			forward = fwd;

			// Si hay un enemigo cercano, influenciar la dirección usando operadores de vec3_t
			if (nearest_enemy && enemy_dir) {
				// Aplicar una influencia más fuerte hacia el enemigo
				float const distance_factor = 1.0f - (nearest_enemy->s.origin - origin).length() / config.search_radius;
				float const adjusted_bias = config.homing_bias * (1.0f + distance_factor * 0.5f);

				// Interpolar entre la dirección aleatoria y la dirección al enemigo
				forward = forward * (1.0f - adjusted_bias) + enemy_dir * adjusted_bias;
				forward = safe_normalized(forward);
			}
		}

		// Velocidad aleatoria para cada granada
		float const velocity = config.min_velocity + frandom() * (config.max_velocity - config.min_velocity);

		// Tiempo de explosión aleatorio
		float const explode_time = config.min_fuse_time + frandom() * (config.max_fuse_time - config.min_fuse_time);

		// Lanzar la granada con los parámetros calculados
		fire_grenade2(owner, origin, forward, fragment_damage, velocity,
			gtime_t::from_sec(explode_time),
			static_cast<float>(fragment_damage), false);
	}
}

static void Prox_ExplodeReal(edict_t* ent, edict_t* other, vec3_t normal) {
	// Cleanup trigger field
	if (ent->teamchain && ent->teamchain->owner == ent) {
		G_FreeEdict(ent->teamchain);
	}

	// Determine owner for damage attribution
	edict_t* owner = ent->teammaster ? ent->teammaster : ent;

	// Generate noise for owner awareness
	if (ent->teammaster) {
		PlayerNoise(owner, ent->s.origin, PNOISE_IMPACT);
	}

	// Direct damage to triggering entity
	if (other) {
		vec3_t const dir = other->s.origin - ent->s.origin;
		T_Damage(other, ent, owner, dir, ent->s.origin, normal,
			ent->dmg, ent->dmg, DAMAGE_NONE, MOD_PROX);
	}

	// Quad damage sound effect
	if (ent->dmg > PROX_DAMAGE * PROX_DAMAGE_OPEN_MULTIPLIER) {
		gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
	}

	// Prepare for explosion
	ent->takedamage = false;
	vec3_t const origin = ent->s.origin + normal;

	// Apply radius damage
	T_RadiusDamage(ent, owner, static_cast<float>(ent->dmg), other,
		PROX_DAMAGE_RADIUS, DAMAGE_NONE, MOD_PROX);

	// Visual explosion effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(ent->groundentity ? TE_GRENADE_EXPLOSION : TE_ROCKET_EXPLOSION);
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	// Enhanced proximity mine features
	if (g_upgradeproxs->integer) {
		SpawnClusterGrenades(owner, origin, ent->dmg);
	}

	G_FreeEdict(ent);
}



THINK(Prox_Explode) (edict_t* ent) -> void
{
	Prox_ExplodeReal(ent, nullptr, (ent->velocity * -0.02f));
}

//===============
//===============
DIE(prox_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// Check if inflictor is not nullptr and if set off by another prox, delay a little (chained explosions)
	if (inflictor && strcmp(inflictor->classname, "prox_mine") == 0)
	{
		self->takedamage = false;
		Prox_Explode(self);
	}
	else
	{
		self->takedamage = false;
		self->think = Prox_Explode;
		self->nextthink = level.time + FRAME_TIME_S;
	}
}

TOUCH(Prox_Field_Touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (!ent || !other) {
		// Log error or handle null ent or other
		return;
	}

	edict_t* prox = nullptr;

	if (!(other->svflags & SVF_MONSTER)) {
		// explode only if it's a monster
		return;
	}

	// trigger the prox mine if it's still there, and still mine.
	if (ent->owner) {
		prox = ent->owner;
	}
	else {
		// Log error or handle null owner
		return;
	}

	// teammate avoidance
	if (prox->teammaster && CheckTeamDamage(prox->teammaster, other)) {
		return;
	}

	if (G_IsDeathmatch() && g_horde && g_horde->integer && other->client) {
		// no self damage using traps on DM/Horde
		return;
	}

	if (other == prox) {
		// don't set self off
		return;
	}

	if (prox->think == Prox_Explode) {
		// we're set to blow!
		return;
	}

	if (prox->teamchain == ent) {
		if (gi.soundindex && gi.sound) {
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
		}
		prox->think = Prox_Explode;
		prox->nextthink = level.time + PROX_TIME_DELAY;
		return;
	}

	if (ent) {
		ent->solid = SOLID_NOT;
		G_FreeEdict(ent);
	}
}

//===============
//===============
THINK(prox_seek) (edict_t* ent) -> void
{
	if (level.time > gtime_t::from_sec(ent->wait))
	{
		Prox_Explode(ent);
	}
	else
	{
		ent->s.frame++;
		if (ent->s.frame > 13)
			ent->s.frame = 9;
		ent->think = prox_seek;
		ent->nextthink = level.time + 10_hz;
	}
}

//===============
//===============
THINK(prox_open) (edict_t* ent) -> void
{
	edict_t* search;

	search = nullptr;

	if (ent->s.frame == 9) // end of opening animation
	{
		// set the owner to nullptr so the owner can walk through it.  needs to be done here so the owner
		// doesn't get stuck on it while it's opening if fired at point blank wall
		ent->s.sound = 0;

		if (G_IsDeathmatch() && !g_horde->integer)
			ent->owner = nullptr;

		if (ent->teamchain)
			ent->teamchain->touch = Prox_Field_Touch;
		while ((search = findradius(search, ent->s.origin, PROX_DAMAGE_RADIUS + 10)) != nullptr)
		{
			if (!search->classname) // tag token and other weird shit
				continue;

			// teammate avoidance
			if (CheckTeamDamage(search, ent->teammaster))
				continue;

			// if it's a monster or player with health > 0
			// or it's a player start point
			// and we can see it
			// blow up
			if (
				search != ent &&
				(
					(((search->svflags & SVF_MONSTER) ||
						(G_IsDeathmatch() && !g_horde->integer &&
							(search->client || (search->classname && !strcmp(search->classname, "prox_mine"))))) &&
						(search->health > 0)) ||
					(G_IsDeathmatch() && !g_horde->integer &&
						search->classname && // Added null check here
						((!strcmp(search->classname, "misc_teleporter_dest")) ||
							(!strncmp(search->classname, "item_flag_", 10))))
					) &&
				(visible(search, ent)))
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
				Prox_Explode(ent);
				return;
			}
		}

		if (g_dm_strong_mines->integer)
			ent->wait = (level.time + PROX_TIME_TO_LIVE).seconds();
		else
		{
			switch ((int)(ent->dmg / (PROX_DAMAGE * PROX_DAMAGE_OPEN_MULTIPLIER)))
			{
			case 1:
				ent->wait = (level.time + PROX_TIME_TO_LIVE).seconds();
				break;
			case 2:
				ent->wait = (level.time + 30_sec).seconds();
				break;
			case 4:
				ent->wait = (level.time + 15_sec).seconds();
				break;
			case 8:
				ent->wait = (level.time + 10_sec).seconds();
				break;
			default:
				ent->wait = (level.time + PROX_TIME_TO_LIVE).seconds();
				break;
			}
		}

		ent->think = prox_seek;
		ent->nextthink = level.time + 200_ms;
	}
	else
	{
		if (ent->s.frame == 0)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxopen.wav"), 1, ATTN_NORM, 0);
			ent->dmg *= PROX_DAMAGE_OPEN_MULTIPLIER;
		}
		ent->s.frame++;
		ent->think = prox_open;
		ent->nextthink = level.time + 10_hz;
	}
}

//===============
//===============
TOUCH(prox_land) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	edict_t* field;
	vec3_t	   dir;
	vec3_t	   forward, right, up;
	movetype_t movetype = MOVETYPE_NONE;
	int		   stick_ok = 0;
	vec3_t	   land_point;

	// must turn off owner so owner can shoot it and set it off
	// moved to prox_open so owner can get away from it if fired at pointblank range into
	// wall
	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	if (tr.plane.normal)
	{
		land_point = ent->s.origin + (tr.plane.normal * -10.0f);
		if (gi.pointcontents(land_point) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Prox_Explode(ent);
			return;
		}
	}

	constexpr float PROX_STOP_EPSILON = 0.1f;

	if (!tr.plane.normal || (other->svflags & SVF_MONSTER) || other->client || (other->flags & FL_DAMAGEABLE))
	{
		if (other != ent->teammaster)
			Prox_ExplodeReal(ent, other, tr.plane.normal);

		return;
	}
	else if (other != world)
	{
		// Here we need to check to see if we can stop on this entity.
		// Note that plane can be nullptr

		// PMM - code stolen from g_phys (ClipVelocity)
		vec3_t out{};
		float  backoff, change;
		int	   i;

		if ((other->movetype == MOVETYPE_PUSH) && (tr.plane.normal[2] > 0.7f))
			stick_ok = 1;
		else
			stick_ok = 0;

		backoff = ent->velocity.dot(tr.plane.normal) * 1.5f;
		for (i = 0; i < 3; i++)
		{
			change = tr.plane.normal[i] * backoff;
			out[i] = ent->velocity[i] - change;
			if (out[i] > -PROX_STOP_EPSILON && out[i] < PROX_STOP_EPSILON)
				out[i] = 0;
		}

		if (out[2] > 60)
			return;

		movetype = MOVETYPE_BOUNCE;

		// if we're here, we're going to stop on an entity
		if (stick_ok)
		{ // it's a happy entity
			ent->velocity = {};
			ent->avelocity = {};
		}
		else // no-stick.  teflon time
		{
			if (tr.plane.normal[2] > 0.7f)
			{
				Prox_Explode(ent);
				return;
			}
			return;
		}
	}
	else if (other->s.modelindex != MODELINDEX_WORLD)
		return;

	dir = vectoangles(tr.plane.normal);
	AngleVectors(dir, forward, right, up);

	if (gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
	{
		Prox_Explode(ent);
		return;
	}

	ent->svflags &= ~SVF_PROJECTILE;

	field = G_Spawn();

	field->s.origin = ent->s.origin;
	field->mins = { -PROX_BOUND_SIZE, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE };
	field->maxs = { PROX_BOUND_SIZE, PROX_BOUND_SIZE, PROX_BOUND_SIZE };
	field->movetype = MOVETYPE_NONE;
	field->solid = SOLID_TRIGGER;
	field->owner = ent;
	field->classname = "prox_field";
	field->teammaster = ent;
	gi.linkentity(field);

	ent->velocity = {};
	ent->avelocity = {};
	// rotate to vertical
	dir[PITCH] = dir[PITCH] + 90;
	ent->s.angles = dir;
	ent->takedamage = true;
	ent->movetype = movetype; // either bounce or none, depending on whether we stuck to something
	ent->die = prox_die;
	ent->teamchain = field;
	ent->health = PROX_HEALTH;
	ent->nextthink = level.time;
	ent->think = prox_open;
	ent->touch = nullptr;
	ent->solid = SOLID_BBOX;

	gi.linkentity(ent);
}

THINK(Prox_Think) (edict_t* self) -> void
{
	if (self->timestamp <= level.time)
	{
		Prox_Explode(self);
		return;
	}

	self->s.angles = vectoangles(self->velocity.normalized());
	self->s.angles[PITCH] -= 90;
	self->nextthink = level.time;
}

//===============
//===============
void fire_prox(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int prox_damage_multiplier, int speed)
{
	edict_t* prox;
	vec3_t	 dir;
	vec3_t	 forward, right, up;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	prox = G_Spawn();
	prox->s.origin = start;
	prox->velocity = aimdir * speed;

	float const gravityAdjustment = level.gravity / 800.f;

	prox->velocity += up * (200 + crandom() * 10.0f) * gravityAdjustment;
	prox->velocity += right * (crandom() * 10.0f);

	prox->s.angles = dir;
	prox->s.angles[PITCH] -= 90;
	prox->movetype = MOVETYPE_BOUNCE;
	prox->solid = SOLID_BBOX;
	prox->svflags |= SVF_PROJECTILE;
	prox->s.effects |= EF_GRENADE;
	prox->flags |= (FL_DODGE | FL_TRAP);
	prox->clipmask = MASK_PROJECTILE | CONTENTS_LAVA | CONTENTS_SLIME;

	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		prox->clipmask &= ~CONTENTS_PLAYER;

	prox->s.renderfx |= RF_IR_VISIBLE;
	// FIXME - this needs to be bigger.  Has other effects, though.  Maybe have to change origin to compensate
	//  so it sinks in correctly.  Also in lavacheck, might have to up the distance
	prox->mins = { -6, -6, -6 };
	prox->maxs = { 6, 6, 6 };
	prox->s.modelindex = gi.modelindex("models/weapons/g_prox/tris.md2");
	prox->owner = self;
	prox->teammaster = self;
	prox->touch = prox_land;
	prox->think = Prox_Think;
	prox->nextthink = level.time;
	prox->dmg = PROX_DAMAGE * prox_damage_multiplier;
	prox->classname = "prox_mine";
	prox->flags |= FL_DAMAGEABLE;
	prox->flags |= FL_MECHANICAL;

	switch (prox_damage_multiplier)
	{
	case 1:
		prox->timestamp = level.time + PROX_TIME_TO_LIVE;
		break;
	case 2:
		prox->timestamp = level.time + 30_sec;
		break;
	case 4:
		prox->timestamp = level.time + 15_sec;
		break;
	case 8:
		prox->timestamp = level.time + 10_sec;
		break;
	default:
		prox->timestamp = level.time + PROX_TIME_TO_LIVE;
		break;
	}

	gi.linkentity(prox);
}

// *************************
// MELEE WEAPONS
// *************************

struct player_melee_data_t
{
	edict_t* self;
	const vec3_t& start;
	const vec3_t& aim;
	int reach;
};

static BoxEdictsResult_t fire_player_melee_BoxFilter(edict_t* check, void* data_v)
{
	const player_melee_data_t* data = (const player_melee_data_t*)data_v;

	if (!check->inuse || !check->takedamage || check == data->self)
		return BoxEdictsResult_t::Skip;

	// check distance
	vec3_t const closest_point_to_check = closest_point_to_box(data->start, check->s.origin + check->mins, check->s.origin + check->maxs);
	vec3_t const closest_point_to_self = closest_point_to_box(closest_point_to_check, data->self->s.origin + data->self->mins, data->self->s.origin + data->self->maxs);

	vec3_t dir = (closest_point_to_check - closest_point_to_self);
	float const len = dir.normalize();

	if (len > data->reach)
		return BoxEdictsResult_t::Skip;

	// check angle if we aren't intersecting
	vec3_t const shrink{ 2, 2, 2 };
	if (!boxes_intersect(check->absmin + shrink, check->absmax - shrink, data->self->absmin + shrink, data->self->absmax - shrink))
	{
		dir = (((check->absmin + check->absmax) / 2) - data->start).normalized();

		if (dir.dot(data->aim) < 0.70f)
			return BoxEdictsResult_t::Skip;
	}

	return BoxEdictsResult_t::Keep;
}

bool fire_player_melee(edict_t* self, const vec3_t& start, const vec3_t& aim, int reach, int damage, int kick, mod_t mod)
{
	constexpr size_t MAX_HIT = 4;

	vec3_t const reach_vec{ float(reach - 1), float(reach - 1), float(reach - 1) };
	edict_t* targets[MAX_HIT];

	player_melee_data_t data{
		self,
		start,
		aim,
		reach
	};

	// find all the things we could maybe hit
	size_t const num = gi.BoxEdicts(self->absmin - reach_vec, self->absmax + reach_vec, targets, q_countof(targets), AREA_SOLID, fire_player_melee_BoxFilter, &data);

	if (!num)
		return false;

	bool was_hit = false;

	for (size_t i = 0; i < num; i++)
	{
		edict_t* hit = targets[i];

		if (!hit->inuse || !hit->takedamage)
			continue;
		else if (!CanDamage(self, hit))
			continue;

		// do the damage
		vec3_t const closest_point_to_check = closest_point_to_box(start, hit->s.origin + hit->mins, hit->s.origin + hit->maxs);

		if (hit->svflags & SVF_MONSTER)
			hit->pain_debounce_time -= random_time(5_ms, 75_ms);

		if (mod.id == MOD_CHAINFIST)
			T_Damage(hit, self, self, aim, closest_point_to_check, -aim, damage, kick / 2,
				DAMAGE_DESTROY_ARMOR | DAMAGE_NO_KNOCKBACK, mod);
		else
			T_Damage(hit, self, self, aim, closest_point_to_check, -aim, damage, kick / 2, DAMAGE_NO_KNOCKBACK, mod);

		was_hit = true;
	}

	return was_hit;
}

// *************************
// NUKE
// *************************

constexpr gtime_t NUKE_DELAY = 4_sec;
constexpr gtime_t NUKE_TIME_TO_LIVE = 6_sec;
constexpr float	  NUKE_RADIUS = 1024;
constexpr int32_t NUKE_DAMAGE = 800;
constexpr gtime_t NUKE_QUAKE_TIME = 3_sec;
constexpr float	  NUKE_QUAKE_STRENGTH = 100;

THINK(Nuke_Quake) (edict_t* self) -> void
{
	uint32_t i;
	edict_t* e;

	if (self->last_move_time < level.time)
	{
		gi.positioned_sound(self->s.origin, self, CHAN_AUTO, self->noise_index, 0.75, ATTN_NONE, 0);
		self->last_move_time = level.time + 500_ms;
	}

	for (i = 1, e = g_edicts + i; i < globals.num_edicts; i++, e++)
	{
		if (!e->inuse)
			continue;
		if (!e->client)
			continue;
		if (!e->groundentity)
			continue;

		e->groundentity = nullptr;
		e->velocity[0] += crandom() * 150;
		e->velocity[1] += crandom() * 150;
		e->velocity[2] = self->speed * (100.0f / e->mass);
	}

	if (level.time < self->timestamp)
		self->nextthink = level.time + FRAME_TIME_S;
	else
		G_FreeEdict(self);
}

static void Nuke_Explode(edict_t* ent)
{

	if (ent->teammaster->client)
		PlayerNoise(ent->teammaster, ent->s.origin, PNOISE_IMPACT);

	T_RadiusNukeDamage(ent, ent->teammaster, (float)ent->dmg, ent, ent->dmg_radius, MOD_NUKE);

	edict_t* check = nullptr;
	float radius = ent->dmg_radius * 1.5f; // Un poco más de radio que el daño para estar seguros

	while ((check = findradius(check, ent->s.origin, radius)) != nullptr)
	{
		if (!check->client || !check->inuse)
			continue;

		// Marcar protección contra daño de caída
		check->client->landmark_free_fall = true;
	}

	if (ent->dmg > NUKE_DAMAGE)
		gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

	gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/grenlx1a.wav"), 1, ATTN_NONE, 0);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1_BIG);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_NUKEBLAST);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_ALL, false);

	// become a quake
	ent->svflags |= SVF_NOCLIENT;
	ent->noise_index = gi.soundindex("world/rumble.wav");
	ent->think = Nuke_Quake;
	ent->speed = NUKE_QUAKE_STRENGTH;
	ent->timestamp = level.time + NUKE_QUAKE_TIME;
	ent->nextthink = level.time + FRAME_TIME_S;
	ent->last_move_time = 0_ms;
}

DIE(nuke_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	self->takedamage = false;
	if ((attacker) && !(strcmp(attacker->classname, "nuke")))
	{
		G_FreeEdict(self);
		return;
	}
	Nuke_Explode(self);
}

THINK(Nuke_Think) (edict_t* ent) -> void
{
	float			attenuation;
	float constexpr default_atten = 1.8f;
	int				nuke_damage_multiplier;
	player_muzzle_t muzzleflash;

	nuke_damage_multiplier = ent->dmg / NUKE_DAMAGE;
	switch (nuke_damage_multiplier)
	{
	case 1:
		attenuation = default_atten / 1.4f;
		muzzleflash = MZ_NUKE1;
		break;
	case 2:
		attenuation = default_atten / 2.0f;
		muzzleflash = MZ_NUKE2;
		break;
	case 4:
		attenuation = default_atten / 3.0f;
		muzzleflash = MZ_NUKE4;
		break;
	case 8:
		attenuation = default_atten / 5.0f;
		muzzleflash = MZ_NUKE8;
		break;
	default:
		attenuation = default_atten;
		muzzleflash = MZ_NUKE1;
		break;
	}

	if (ent->wait < level.time.seconds())
		Nuke_Explode(ent);
	else if (level.time >= (gtime_t::from_sec(ent->wait) - NUKE_TIME_TO_LIVE))
	{
		ent->s.frame++;

		if (ent->s.frame > 11)
			ent->s.frame = 6;

		if (gi.pointcontents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Nuke_Explode(ent);
			return;
		}

		ent->think = Nuke_Think;
		ent->nextthink = level.time + 10_hz;
		ent->health = 1;
		ent->owner = nullptr;

		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(muzzleflash);
		gi.multicast(ent->s.origin, MULTICAST_PHS, false);

		if (ent->timestamp <= level.time)
		{
			if ((gtime_t::from_sec(ent->wait) - level.time) <= (NUKE_TIME_TO_LIVE / 2.0f))
			{
				gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
				ent->timestamp = level.time + 300_ms;
			}
			else
			{
				gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
				ent->timestamp = level.time + 500_ms;
			}
		}
	}
	else
	{
		if (ent->timestamp <= level.time)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
			ent->timestamp = level.time + 1_sec;
		}
		ent->nextthink = level.time + FRAME_TIME_S;
	}
}

TOUCH(nuke_bounce) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (tr.surface && tr.surface->id)
	{
		if (frandom() > 0.5f)
			gi.sound(ent, CHAN_BODY, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
		else
			gi.sound(ent, CHAN_BODY, gi.soundindex("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
	}
}

void fire_nuke(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int speed)
{
	edict_t* nuke;
	vec3_t	 dir;
	vec3_t	 forward, right, up;
	int		 const damage_modifier = P_DamageModifier(self);

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	nuke = G_Spawn();
	nuke->s.origin = start;
	nuke->velocity = aimdir * speed;
	nuke->velocity += up * (200 + crandom() * 10.0f);
	nuke->velocity += right * (crandom() * 10.0f);
	nuke->movetype = MOVETYPE_BOUNCE;
	nuke->clipmask = MASK_PROJECTILE;
	nuke->solid = SOLID_BBOX;
	nuke->s.effects |= EF_GRENADE;
	nuke->s.renderfx |= RF_IR_VISIBLE;
	nuke->mins = { -8, -8, 0 };
	nuke->maxs = { 8, 8, 16 };
	nuke->s.modelindex = gi.modelindex("models/weapons/g_nuke/tris.md2");
	nuke->owner = self;
	nuke->teammaster = self;
	nuke->nextthink = level.time + FRAME_TIME_S;
	nuke->wait = (level.time + NUKE_DELAY + NUKE_TIME_TO_LIVE).seconds();
	nuke->think = Nuke_Think;
	nuke->touch = nuke_bounce;

	nuke->health = 10000;
	nuke->takedamage = true;
	nuke->flags |= FL_DAMAGEABLE;
	nuke->dmg = NUKE_DAMAGE * damage_modifier;
	if (damage_modifier == 1)
		nuke->dmg_radius = NUKE_RADIUS;
	else
		nuke->dmg_radius = NUKE_RADIUS + NUKE_RADIUS * (0.25f * (float)damage_modifier);
	// this yields 1.0, 1.5, 2.0, 3.0 times radius

	nuke->classname = "nuke";
	nuke->die = nuke_die;

	gi.linkentity(nuke);
}
// *************************
// TESLA
// *************************

constexpr gtime_t TESLA_TIME_TO_LIVE = 60_sec;
constexpr float	  TESLA_DAMAGE_RADIUS = 200;
constexpr int32_t TESLA_DAMAGE = 4;
constexpr int32_t TESLA_KNOCKBACK = 8;

constexpr gtime_t TESLA_ACTIVATE_TIME = 1.2_sec;

constexpr int32_t TESLA_EXPLOSION_DAMAGE_MULT = 50; // this is the amount the damage is multiplied by for underwater explosions
constexpr float	  TESLA_EXPLOSION_RADIUS = 200;

constexpr int MAX_TESLAS = 9; // Define el máximo de teslas permitidas por jugador

void tesla_remove(edict_t* self)
{
	edict_t* cur, * next;

	self->takedamage = false;
	if (self->teamchain)
	{
		cur = self->teamchain;
		while (cur)
		{
			next = cur->teamchain;
			G_FreeEdict(cur);
			cur = next;
		}
	}
	else if (self->air_finished)
		gi.Com_Print("tesla_mine without a field!\n");

	if (self->owner && self->owner->client)
	{
		self->owner->client->num_teslas--; // Decrementar el contador de teslas del jugador
	}

	self->owner = self->teammaster; // Going away, set the owner correctly.
	// PGM - grenade explode does damage to self->enemy
	self->enemy = nullptr;

	// play quad sound if quadded and an underwater explosion
	if ((self->dmg_radius) && (self->dmg > (TESLA_DAMAGE * TESLA_EXPLOSION_DAMAGE_MULT)))
		gi.sound(self, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

	Grenade_Explode(self);
}

DIE(tesla_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	tesla_remove(self);
}

void tesla_blow(edict_t* self)
{
	self->dmg *= TESLA_EXPLOSION_DAMAGE_MULT;
	self->dmg_radius = TESLA_EXPLOSION_RADIUS;
	tesla_remove(self);
}

TOUCH(tesla_zap) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	// Ignorar el contacto si el otro es un jugador
	if (other->client) {
		return;
	}

	// Código existente para manejar el contacto aquí
}

//static BoxEdictsResult_t tesla_think_active_BoxFilter(edict_t* check, void* data)
//{
//	edict_t* self = (edict_t*)data;
//
//	if (!check->inuse)
//		return BoxEdictsResult_t::Skip;
//	if (check == self)
//		return BoxEdictsResult_t::Skip;
//	if (check->health < -40)
//		return BoxEdictsResult_t::Skip;
//
//	// Monster-owned tesla checks, later will be useful for monster's teslas
//	if (self->owner && (self->owner->svflags & SVF_MONSTER)) {
//		// Don't attack owner
//		if (check == self->owner)
//			return BoxEdictsResult_t::Skip;
//
//		// Don't attack teammates of owner
//		if (check->svflags & SVF_MONSTER && OnSameTeam(check, self->owner))
//			return BoxEdictsResult_t::Skip;
//
//		// Attack players and non-team monsters
//		if ((check->client && !OnSameTeam(check, self->owner)) ||
//			(check->svflags & SVF_MONSTER && !OnSameTeam(check, self->owner)))
//			return BoxEdictsResult_t::Keep;
//	}
//
//	// Regular tesla checks
//	if (check->client) {
//		if (!G_IsDeathmatch() && !g_horde->integer)
//			return BoxEdictsResult_t::Skip;
//		else if (CheckTeamDamage(check, self->teammaster))
//			return BoxEdictsResult_t::Skip;
//	}
//
//	if (!(check->svflags & SVF_MONSTER) && !(check->flags & FL_DAMAGEABLE) && check->client)
//		return BoxEdictsResult_t::Skip;
//
//	const char* classname = check->classname;
//	if (!classname)
//		return BoxEdictsResult_t::Keep;
//
//	if (((!G_IsDeathmatch() || g_horde->integer) && (check->flags & FL_TRAP)) ||
//		check->monsterinfo.invincible_time > level.time)
//		return BoxEdictsResult_t::Skip;
//
//	if (strcmp(classname, "monster_sentrygun") == 0 ||
//		strcmp(classname, "emitter") == 0 ||
//		strcmp(classname, "nuke") == 0)
//		return BoxEdictsResult_t::Skip;
//
//	return BoxEdictsResult_t::Keep;
//}

//constexpr size_t MAX_TOUCH_ENTITIES = 1024; // Tamaño reducido para el array touch


constexpr float TESLA_WALL_OFFSET = 3.0f;      // Offset para paredes
constexpr float TESLA_CEILING_OFFSET = -20.4f;   // Offset optimizado para techos

constexpr float TESLA_FLOOR_OFFSET = -12.0f;     // Offset para suelo
constexpr float TESLA_ORB_OFFSET = 12.0f;      // Altura de la esfera normal
constexpr float TESLA_ORB_OFFSET_CEIL = -18.0f;  // Altura de la esfera cuando está en techo
// Función modificada para el origen del rayo
static vec3_t calculate_tesla_ray_origin(const edict_t* self) {
	vec3_t ray_origin = self->s.origin;
	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);

	// En pared
	if (fabs(self->s.angles[PITCH]) > 45 && fabs(self->s.angles[PITCH]) < 135) {
		// Primero, mover hacia afuera de la pared
		ray_origin = ray_origin + (forward * TESLA_WALL_OFFSET);

		// Ajustar la altura de manera consistente
		ray_origin = ray_origin + (up * 16.0f);

		// No necesitamos ajustes basados en yaw ya que queremos
		// que la posición sea simétrica en ambos lados
	}
	// En techo
	else if (fabs(self->s.angles[PITCH]) > 150 || fabs(self->s.angles[PITCH]) < -150) {
		ray_origin = ray_origin - (up * TESLA_ORB_OFFSET_CEIL);
		ray_origin = ray_origin - (up * 4.0f);
	}
	// En suelo
	else {
		ray_origin = ray_origin + (up * TESLA_ORB_OFFSET);
	}

	return ray_origin;
}
// Función para calcular el punto objetivo del rayo tesla
vec3_t calculate_tesla_ray_target(const edict_t* self, const edict_t* target) {
	// Calcular el centro del objetivo
	vec3_t target_center = target->s.origin;
	vec3_t target_mins = target->mins;
	vec3_t target_maxs = target->maxs;

	// Ajustar el punto objetivo al centro de masa del target
	target_center[0] += (target_mins[0] + target_maxs[0]) * 0.5f;
	target_center[1] += (target_mins[1] + target_maxs[1]) * 0.5f;
	target_center[2] += (target_mins[2] + target_maxs[2]) * 0.5f;

	return target_center;
}

// Función mejorada para la detección de colisiones del rayo
bool tesla_ray_trace(const edict_t* self, const edict_t* target, trace_t& tr) {
	vec3_t const ray_start = calculate_tesla_ray_origin(self);
	vec3_t const ray_end = calculate_tesla_ray_target(self, target);

	// Realizar el trace
	tr = gi.traceline(ray_start, ray_end, self, MASK_PROJECTILE);

	// Verificar si el rayo alcanza al objetivo
	return tr.fraction == 1.0f || tr.ent == target;
}

// New helper function to adapt FindMTarget logic for tesla
struct TeslaTarget {
	edict_t* ent;
	float priority;
	float dist_squared;
};

bool IsValidTeslaTarget(edict_t* self, edict_t* ent) {
	if (!ent || !ent->inuse || ent == self ||
		ent->health <= 0 || ent->deadflag ||
		ent->solid == SOLID_NOT)
		return false;

	// Tesla specific checks
	if (ent->monsterinfo.invincible_time > level.time)
		return false;

	// Skip sentries, emitters, nukes
	if (!strcmp(ent->classname, "monster_sentrygun") ||
		!strcmp(ent->classname, "emitter") ||
		!strcmp(ent->classname, "nuke"))
		return false;

	// Owner team check for monsters
	if (self->owner && (self->owner->svflags & SVF_MONSTER)) {
		if (ent == self->owner ||
			(ent->svflags & SVF_MONSTER && OnSameTeam(ent, self->owner)))
			return false;
	}

	return true;
}

float CalculateTeslaPriority(edict_t* self, edict_t* target, float dist_squared) {
	float priority = 1.0f / (dist_squared + 1.0f);

	// Prioritize monsters slightly higher
	if (target->svflags & SVF_MONSTER)
		priority *= 1.2f;

	return priority;
}

THINK(tesla_think_active)(edict_t* self) -> void {
	if (!self)
		return;

	if (level.time > self->air_finished) {
		tesla_remove(self);
		return;
	}

	// Setup positioning and bounds
	vec3_t start = self->s.origin;
	const bool is_on_wall = fabs(self->s.angles[PITCH]) > 45 && fabs(self->s.angles[PITCH]) < 135;

	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);

	if (is_on_wall) {
		start = start + (forward * 16);
	}
	else {
		if (self->s.angles[PITCH] > 150 || self->s.angles[PITCH] < -150) {
			start = start + (up * -16);
		}
		else {
			start = start + (up * 16);
		}
	}

	if (!self->teamchain) {
		gi.Com_Print("Warning: tesla_think_active called with null teamchain\n");
		return;
	}

	// Setup teamchain bounds
	if (is_on_wall) {
		float constexpr radius = TESLA_DAMAGE_RADIUS * 1.5f;
		self->teamchain->mins = { -radius / 2, -radius, -radius };
		self->teamchain->maxs = { radius, radius, radius };
		self->teamchain->s.origin = self->s.origin + (forward * (radius / 2));
	}
	else {
		if (self->s.angles[PITCH] > 150 || self->s.angles[PITCH] < -150) {
			self->teamchain->mins = { -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS };
			self->teamchain->maxs = { TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, 0 };
		}
		else {
			self->teamchain->mins = { -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, 0 };
			self->teamchain->maxs = { TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS };
		}
	}
	gi.linkentity(self->teamchain);

	// Target acquisition with priority
	constexpr int max_targets = 3;
	const float max_range_squared = TESLA_DAMAGE_RADIUS * TESLA_DAMAGE_RADIUS;

	std::vector<TeslaTarget> potential_targets;
	potential_targets.reserve(max_targets); // Pre-allocate space for efficiency

	// Find potential targets using active_monsters()
	for (auto ent : active_monsters()) {
		if (!IsValidTeslaTarget(self, ent))
			continue;

		float dist_squared = DistanceSquared(self->s.origin, ent->s.origin);
		if (dist_squared > max_range_squared)
			continue;

		trace_t tr;
		if (!tesla_ray_trace(self, ent, tr))
			continue;

		TeslaTarget target{
			ent,
			CalculateTeslaPriority(self, ent, dist_squared),
			dist_squared
		};
		potential_targets.push_back(target);
	}

	// Sort by priority
	std::sort(potential_targets.begin(), potential_targets.end(),
		[](const TeslaTarget& a, const TeslaTarget& b) {
			return a.priority > b.priority;
		});

	// Attack phase
	int targets_attacked = 0;
	for (const auto& target : potential_targets) {
		if (targets_attacked >= max_targets)
			break;

		trace_t tr;
		if (tesla_ray_trace(self, target.ent, tr)) {
			vec3_t const ray_start = calculate_tesla_ray_origin(self);
			vec3_t const ray_end = tr.endpos;
			vec3_t dir = ray_end - ray_start;
			dir.normalize();

			T_Damage(target.ent, self, self->teammaster, dir, tr.endpos, tr.plane.normal,
				self->dmg, TESLA_KNOCKBACK, DAMAGE_NO_ARMOR, MOD_TESLA);

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_LIGHTNING);
			gi.WriteEntity(self);
			gi.WriteEntity(target.ent);
			gi.WritePosition(ray_start);
			gi.WritePosition(ray_end);
			gi.multicast(ray_start, MULTICAST_PVS, false);

			targets_attacked++;
		}
	}

	if (self->inuse) {
		self->think = tesla_think_active;
		self->nextthink = level.time + 10_hz;
	}
}

THINK(tesla_activate) (edict_t* self) -> void
{
	edict_t* trigger;
	edict_t* search;

	if (gi.pointcontents(self->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WATER))
	{
		tesla_blow(self);
		return;
	}

	// only check for spawn points in deathmatch
	if (G_IsDeathmatch())
	{
		search = nullptr;
		while ((search = findradius(search, self->s.origin, 1.5f * TESLA_DAMAGE_RADIUS)) != nullptr)
		{
			// [Paril-KEX] don't allow traps to be placed near flags or teleporters
			// if it's a monster or player with health > 0
			// or it's a player start point
			// and we can see it
			// blow up
			if (search->classname && ((G_IsDeathmatch() && !g_horde->integer &&
				((!strncmp(search->classname, "info_player_", 12)) ||
					(!strcmp(search->classname, "misc_teleporter_dest")) ||
					(!strncmp(search->classname, "item_flag_", 10))))) &&
				(visible(search, self)))
			{
				BecomeExplosion1(self);
				return;
			}
		}
	}

	trigger = G_Spawn();
	trigger->s.origin = self->s.origin;
	trigger->mins = { -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, self->mins[2] };
	trigger->maxs = { TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS };
	trigger->movetype = MOVETYPE_NONE;
	trigger->solid = SOLID_TRIGGER;
	trigger->owner = self;
	trigger->touch = tesla_zap;
	trigger->classname = "tesla trigger";
	// doesn't need to be marked as a teamslave since the move code for bounce looks for teamchains
	gi.linkentity(trigger);

	//self->s.angles = {};
	// clear the owner if in deathmatch and not horde
	if (G_IsDeathmatch() && !g_horde->integer)
		self->owner = nullptr;
	self->teamchain = trigger;
	self->think = tesla_think_active;
	self->nextthink = level.time + FRAME_TIME_S;
	self->air_finished = level.time + TESLA_TIME_TO_LIVE;
}

THINK(tesla_think) (edict_t* ent) -> void
{
	if (gi.pointcontents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
	{
		tesla_remove(ent);
		return;
	}

	//ent->s.angles = {};

	if (!(ent->s.frame))
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/teslaopen.wav"), 1, ATTN_NORM, 0);

	ent->s.frame++;
	if (ent->s.frame > 14)
	{
		ent->s.frame = 14;
		ent->think = tesla_activate;
		ent->nextthink = level.time + 10_hz;
	}
	else
	{
		if (ent->s.frame > 9)
		{
			if (ent->s.frame == 10)
			{
				if (ent->owner && ent->owner->client)
				{
					PlayerNoise(ent->owner, ent->s.origin, PNOISE_WEAPON); // PGM
				}
				ent->s.skinnum = 1;
			}
			else if (ent->s.frame == 12)
				ent->s.skinnum = 2;
			else if (ent->s.frame == 14)
				ent->s.skinnum = 3;
		}
		ent->think = tesla_think;
		ent->nextthink = level.time + 15_hz;
	}
}

// Constantes para ajustar el comportamiento del rebote
constexpr float TESLA_BOUNCE_MULTIPLIER = 1.35f;    // Multiplicador base del rebote
constexpr float TESLA_MIN_BOUNCE_SPEED = 120.0f;    // Velocidad mínima después de un rebote
constexpr float TESLA_BOUNCE_RANDOM = 70.0f;        // Factor de aleatoriedad en el rebote
constexpr float TESLA_VERTICAL_BOOST = 180.0f;      // Impulso vertical adicional

TOUCH(tesla_lava) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void {
	if (!other->inuse || !(other->solid == SOLID_BSP || other->movetype == MOVETYPE_PUSH))
		return;

	// Bounce logic for non-world entities
	if (other != world && (other->movetype != MOVETYPE_PUSH || other->svflags & SVF_MONSTER || other->client || strcmp(other->classname, "func_train") == 0)) {
		if (tr.plane.normal) {
			vec3_t out{};
			float const backoff = ent->velocity.dot(tr.plane.normal) * TESLA_BOUNCE_MULTIPLIER;
			for (int i = 0; i < 3; i++) {
				float change = tr.plane.normal[i] * backoff;
				out[i] = ent->velocity[i] - change;
				out[i] += crandom() * TESLA_BOUNCE_RANDOM;
				if (fabs(out[i]) < TESLA_MIN_BOUNCE_SPEED && out[i] != 0) {
					out[i] = (out[i] < 0 ? -TESLA_MIN_BOUNCE_SPEED : TESLA_MIN_BOUNCE_SPEED);
				}
			}
			if (tr.plane.normal[2] > 0) {
				out[2] += TESLA_VERTICAL_BOOST;
			}
			if (out.length() < TESLA_MIN_BOUNCE_SPEED) {
				out.normalize();
				out = out * TESLA_MIN_BOUNCE_SPEED;
			}
			ent->velocity = out;
			ent->avelocity = { crandom() * 240, crandom() * 240, crandom() * 240 };
			if (ent->velocity.length() > 0 && strcmp(other->classname, "func_train")) {
				gi.sound(ent, CHAN_VOICE, gi.soundindex(frandom() > 0.5f ?
					"weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
			}
		}
		return;
	}


	if (tr.plane.normal) {
		const float slope = fabs(tr.plane.normal[2]);
		if (slope > 0.85f) {
			if (tr.plane.normal[2] > 0) {
				// Suelo
				ent->s.angles = {};
				ent->mins = { -4, -4, 0 };
				ent->maxs = { 4, 4, 8 };
				ent->s.origin = ent->s.origin + (tr.plane.normal * TESLA_FLOOR_OFFSET);
				ent->s.origin[2] += TESLA_ORB_OFFSET;
			}
			else {
				// Techo
				ent->s.angles = { 180, 0, 0 };
				ent->mins = { -4, -4, -8 };
				ent->maxs = { 4, 4, 0 };
				ent->s.origin = ent->s.origin + (tr.plane.normal * TESLA_CEILING_OFFSET);
				ent->s.origin[2] += TESLA_ORB_OFFSET_CEIL;
			}
		}
		else {
			vec3_t dir = vectoangles(tr.plane.normal);
			vec3_t forward;
			AngleVectors(dir, &forward, nullptr, nullptr);

			// Detectar si es una pared "plana" basándonos en los componentes X/Y de la normal
			bool is_flat_wall = (fabs(tr.plane.normal[0]) > 0.95f || fabs(tr.plane.normal[1]) > 0.95f);

			if (is_flat_wall) {
				// Usar el comportamiento original para paredes planas
				ent->s.angles[PITCH] = dir[PITCH] + 90;
				ent->s.angles[YAW] = dir[YAW];
				ent->s.angles[ROLL] = 0;
				ent->mins = { 0, -4, -4 };
				ent->maxs = { 8, 4, 4 };
				ent->s.origin = ent->s.origin + (forward * -TESLA_WALL_OFFSET);
			}
			else {
				// Usar el comportamiento nuevo para superficies curvas/inclinadas
				ent->s.angles = dir;
				ent->s.angles[PITCH] += 90;
				ent->mins = { -4, -4, -4 };
				ent->maxs = { 4, 4, 4 };
				ent->s.origin = ent->s.origin + (forward * -TESLA_WALL_OFFSET);
			}
		}
	}

	if (gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME)) {
		tesla_blow(ent);
		return;
	}

	ent->velocity = {};
	ent->avelocity = {};
	ent->takedamage = true;
	ent->movetype = MOVETYPE_NONE;
	ent->die = tesla_die;
	ent->touch = nullptr;
	ent->solid = SOLID_BBOX;
	ent->think = tesla_think;
	ent->nextthink = level.time;
	gi.linkentity(ent);
}
// Función para contar y manejar el número de teslas de un jugador
void check_player_tesla_limit(edict_t* self)
{
	if (!self->client)
		return;

	if (self->client->num_teslas >= MAX_TESLAS)
	{
		edict_t* oldest_tesla = nullptr;
		gtime_t oldest_timestamp = level.time;

		for (edict_t* e = g_edicts; e < g_edicts + globals.num_edicts; ++e)
		{
			if (e->inuse && e->classname && strcmp(e->classname, "tesla_mine") == 0 && e->owner == self)
			{
				if (e->timestamp < oldest_timestamp)
				{
					oldest_timestamp = e->timestamp;
					oldest_tesla = e;
				}
			}
		}

		if (oldest_tesla)
		{
			G_FreeEdict(oldest_tesla);
			self->client->num_teslas--; // Decrementar el contador al eliminar la más antigua
		}
	}
}


void fire_tesla(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int tesla_damage_multiplier, int speed)
{
	// Verificar y manejar el límite de teslas por jugador
	check_player_tesla_limit(self);

	edict_t* tesla;
	vec3_t   dir;
	vec3_t   forward, right, up;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	tesla = G_Spawn();
	tesla->s.origin = start;
	tesla->velocity = aimdir * speed;

	const float gravityAdjustment = level.gravity / 800.f;

	tesla->velocity += up * (200 + crandom() * 10.0f) * gravityAdjustment;
	tesla->velocity += right * (crandom() * 10.0f);
	tesla->avelocity = { crandom() * 90, crandom() * 90, crandom() * 120 };

	tesla->s.angles = {};
	tesla->movetype = MOVETYPE_BOUNCE;
	tesla->solid = SOLID_BBOX;
	tesla->s.effects |= EF_GRENADE;
	tesla->s.renderfx |= RF_IR_VISIBLE;
	//tesla->mins = { -12, -12, 0 };
	//tesla->maxs = { 12, 12, 20 };
	tesla->mins = { -4, -4, 0 };
	tesla->maxs = { 4, 4, 8 };
	tesla->s.modelindex = gi.modelindex("models/weapons/g_tesla/tris.md2");

	tesla->owner = self;
	tesla->teammaster = self;
	// Asigna el equipo como una cadena de caracteres
	if (self->client->resp.ctf_team == CTF_TEAM1) {
		tesla->team = TEAM1;
	}
	else if (self->client->resp.ctf_team == CTF_TEAM2) {
		tesla->team = TEAM2;
	}
	else {
		tesla->team = "neutral"; // O cualquier valor por defecto que quieras
	}

	tesla->wait = (level.time + TESLA_TIME_TO_LIVE).seconds();
	tesla->think = tesla_think;
	tesla->nextthink = level.time + TESLA_ACTIVATE_TIME;
	tesla->timestamp = level.time;

	tesla->touch = tesla_lava;

	if (G_IsDeathmatch())
		tesla->health = 50;
	else
		tesla->health = 50;

	tesla->takedamage = true;
	tesla->die = tesla_die;
	tesla->dmg = TESLA_DAMAGE * tesla_damage_multiplier;
	tesla->classname = "tesla_mine";
	tesla->flags |= (FL_DAMAGEABLE | FL_TRAP);
	tesla->clipmask = (MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA) & ~CONTENTS_DEADMONSTER;
//	tesla->svflags = SVF_PLAYER;
	if (self->client && !G_ShouldPlayersCollide(true))
		tesla->clipmask &= ~CONTENTS_PLAYER;

	tesla->flags |= FL_MECHANICAL;

	gi.linkentity(tesla);

	if (self->client)
	{
		self->client->num_teslas++; // Incrementar el contador de teslas del jugador
	}
}

// *************************
//  HEATBEAM
// *************************

struct heatbeam_pierce_t : pierce_args_t {
	edict_t* self;
	vec3_t aimdir;
	int damage;
	int kick;
	mod_t mod;
	bool water_hit;
	static constexpr gtime_t HIT_DELAY = 100_ms;

	heatbeam_pierce_t(edict_t* self, const vec3_t& aimdir, int damage, int kick, mod_t mod) :
		self(self), aimdir(aimdir), damage(damage), kick(kick), mod(mod), water_hit(false)
	{
	}

	bool hit(contents_t& mask, vec3_t& end) override {
		if (tr.contents & MASK_WATER) {
			water_hit = true;
			mask &= ~MASK_WATER;

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_HEATBEAM_SPARKS);
			gi.WritePosition(tr.endpos);
			gi.WriteDir(tr.plane.normal);
			gi.multicast(tr.endpos, MULTICAST_PVS, false);

			return true;
		}

		if (tr.ent && tr.ent->takedamage) {
			if (!tr.ent->beam_hit_time || level.time >= tr.ent->beam_hit_time + HIT_DELAY) {
				int current_damage = water_hit ? damage / 2 : damage;

				T_Damage(tr.ent, self, self->owner, aimdir, tr.endpos, vec3_origin,
					current_damage, kick, DAMAGE_ENERGY, mod);

				tr.ent->beam_hit_time = level.time;
				damage *= 0.98f;

				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_HEATBEAM_STEAM);
				gi.WritePosition(tr.endpos);
				gi.WriteDir(tr.plane.normal);
				gi.multicast(tr.endpos, MULTICAST_PVS, false);
			}

			return mark(tr.ent);
		}

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_HEATBEAM_STEAM);
		gi.WritePosition(tr.endpos);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(tr.endpos, MULTICAST_PVS, false);

		return false;
	}
};

static void fire_beams(edict_t* self, const vec3_t& start, const vec3_t& aimdir, const vec3_t& offset,
	int damage, int kick, int te_beam, int te_impact, mod_t mod)
{
	if (g_piercingbeam->integer) {
		vec3_t end = start + (aimdir * 8192);
		contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

		if (self->client && !G_ShouldPlayersCollide(true))
			content_mask &= ~CONTENTS_PLAYER;

		heatbeam_pierce_t pierce(self, aimdir, damage, kick, mod);
		pierce_trace(start, end, self, pierce, content_mask);

		// Draw beam effect
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(te_beam);
		gi.WriteEntity(self);
		gi.WritePosition(start);
		gi.WritePosition(pierce.tr.endpos);
		gi.multicast(self->s.origin, MULTICAST_ALL, false);

		if (pierce.water_hit) {
			vec3_t pos = pierce.tr.endpos + (aimdir * -2);

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BUBBLETRAIL2);
			gi.WritePosition(start);
			gi.WritePosition(pierce.tr.endpos);
			gi.multicast(pos, MULTICAST_PVS, false);
		}
	}
		else {
			trace_t    tr;
			vec3_t     dir;
			vec3_t     forward, right, up;
			vec3_t     end;
			vec3_t     water_start, endpoint;
			bool       water = false, underwater = false;
			contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

			if (self->client && !G_ShouldPlayersCollide(true))
				content_mask &= ~CONTENTS_PLAYER;

			vec3_t beam_endpt;

			dir = vectoangles(aimdir);
			AngleVectors(dir, forward, right, up);
			end = start + (forward * 8192);

			if (gi.pointcontents(start) & MASK_WATER) {
				underwater = true;
				water_start = start;
				content_mask &= ~MASK_WATER;
			}

			tr = gi.traceline(start, end, self, content_mask);

			if (tr.contents & MASK_WATER) {
				water = true;
				water_start = tr.endpos;

				if (start != tr.endpos) {
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_HEATBEAM_SPARKS);
					gi.WritePosition(water_start);
					gi.WriteDir(tr.plane.normal);
					gi.multicast(tr.endpos, MULTICAST_PVS, false);
				}

				tr = gi.traceline(water_start, end, self, content_mask & ~MASK_WATER);
			}

			endpoint = tr.endpos;

			if (water)
				damage = damage / 2;

			if (!((tr.surface) && (tr.surface->flags & SURF_SKY))) {
				if (tr.fraction < 1.0f) {
					if (tr.ent->takedamage) {
						T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_ENERGY, mod);
					}
					else {
						if ((!water) && !(tr.surface && (tr.surface->flags & SURF_SKY))) {
							gi.WriteByte(svc_temp_entity);
							gi.WriteByte(TE_HEATBEAM_STEAM);
							gi.WritePosition(tr.endpos);
							gi.WriteDir(tr.plane.normal);
							gi.multicast(tr.endpos, MULTICAST_PVS, false);

							if (self->client)
								PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
						}
					}
				}
			}

			if ((water) || (underwater)) {
				vec3_t pos;
				dir = tr.endpos - water_start;
				dir.normalize();
				pos = tr.endpos + (dir * -2);

				if (gi.pointcontents(pos) & MASK_WATER)
					tr.endpos = pos;
				else
					tr = gi.traceline(pos, water_start, tr.ent != world ? tr.ent : nullptr, MASK_WATER);

				pos = water_start + tr.endpos;
				pos *= 0.5f;

				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_BUBBLETRAIL2);
				gi.WritePosition(water_start);
				gi.WritePosition(tr.endpos);
				gi.multicast(pos, MULTICAST_PVS, false);
			}

			beam_endpt = (!underwater && !water) ? tr.endpos : endpoint;

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(te_beam);
			gi.WriteEntity(self);
			gi.WritePosition(start);
			gi.WritePosition(beam_endpt);
			gi.multicast(self->s.origin, MULTICAST_ALL, false);
		}

}
/*
=================
fire_heat

Fires a single heat beam.  Zap.
=================
*/
void fire_heatbeam(edict_t* self, const vec3_t& start, const vec3_t& aimdir, const vec3_t& offset, int damage, int kick, bool monster)
{
	if (monster)
		fire_beams(self, start, aimdir, offset, damage, kick, TE_MONSTER_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
	else
		fire_beams(self, start, aimdir, offset, damage, kick, TE_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
}

/*
=================
fire_blaster2

Fires a single green blaster bolt.  Used by monsters, generally.
=================
*/
TOUCH(blaster2_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	mod_t mod;
	int	  damagestat;

	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner && self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		// the only time players will be firing blaster2 bolts will be from the
		// defender sphere.
		if (self->owner && self->owner->client)
			mod = MOD_DEFENDER_SPHERE;
		else
			mod = MOD_BLASTER2;

		if (self->owner)
		{
			damagestat = self->owner->takedamage;
			self->owner->takedamage = false;
			if (self->dmg >= 5)
				T_RadiusDamage(self, self->owner, (float)(self->dmg * 2), other, self->dmg_radius, DAMAGE_ENERGY, MOD_UNKNOWN);
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, mod);
			self->owner->takedamage = damagestat;
		}
		else
		{
			if (self->dmg >= 5)
				T_RadiusDamage(self, self->owner, (float)(self->dmg * 2), other, self->dmg_radius, DAMAGE_ENERGY, MOD_UNKNOWN);
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, mod);
		}
	}
	else
	{
		// PMM - yeowch this will get expensive
		if (self->dmg >= 5)
			T_RadiusDamage(self, self->owner, (float)(self->dmg * 2), self->owner, self->dmg_radius, DAMAGE_ENERGY, MOD_UNKNOWN);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER2);
		gi.WritePosition(self->s.origin);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}

	G_FreeEdict(self);
}

void fire_blaster2(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, effects_t effect, bool hyper)
{
	edict_t* bolt;
	trace_t	 tr;

	bolt = G_Spawn();
	bolt->s.origin = start;
	bolt->s.old_origin = start;
	bolt->s.angles = vectoangles(dir);
	bolt->velocity = dir * speed;
	bolt->svflags |= SVF_PROJECTILE;
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_PROJECTILE;
	bolt->flags |= FL_DODGE;

	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		bolt->clipmask &= ~CONTENTS_PLAYER;

	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	if (effect)
		bolt->s.effects |= EF_TRACKER;
	bolt->dmg_radius = 128;
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.skinnum = 2;
	bolt->s.scale = 2.5f;
	if (self->client && (self->client->pers.weapon->id == IT_WEAPON_MACHINEGUN || self->client->pers.weapon->id == IT_WEAPON_CHAINGUN))
	{
		bolt->s.scale = 1.0f;
	}
	bolt->touch = blaster2_touch;

	bolt->owner = self;
	bolt->nextthink = level.time + 2_sec;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	gi.linkentity(bolt);

	tr = gi.traceline(self->s.origin, bolt->s.origin, bolt, bolt->clipmask);
	if (tr.fraction < 1.0f)
	{
		bolt->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		bolt->touch(bolt, tr.ent, tr, false);
	}
}

// *************************
// tracker
// *************************

constexpr damageflags_t TRACKER_DAMAGE_FLAGS = (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY | DAMAGE_NO_KNOCKBACK);
constexpr damageflags_t TRACKER_IMPACT_FLAGS = (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY);

constexpr gtime_t TRACKER_DAMAGE_TIME = 500_ms;

THINK(tracker_pain_daemon_think) (edict_t* self) -> void
{
	constexpr vec3_t pain_normal = { 0, 0, 1 };
	int				 hurt;

	if (!self->inuse)
		return;

	if ((level.time - self->timestamp) > TRACKER_DAMAGE_TIME)
	{
		if (!self->enemy->client)
			self->enemy->s.effects &= ~EF_TRACKERTRAIL;
		G_FreeEdict(self);
	}
	else
	{
		if (self->enemy->health > 0)
		{
			vec3_t  const center = (self->enemy->absmax + self->enemy->absmin) * 0.5f;

			T_Damage(self->enemy, self, self->owner, vec3_origin, center, pain_normal,
				self->dmg, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);

			// if we kill the player, we'll be removed.
			if (self->inuse)
			{
				// if we killed a monster, gib them.
				if (self->enemy->health < 1)
				{
					if (self->enemy->gib_health)
						hurt = -self->enemy->gib_health;
					else
						hurt = 500;

					T_Damage(self->enemy, self, self->owner, vec3_origin, center,
						pain_normal, hurt, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);
				}

				self->nextthink = level.time + 10_hz;

				if (self->enemy->client)
					self->enemy->client->tracker_pain_time = self->nextthink;
				else
					self->enemy->s.effects |= EF_TRACKERTRAIL;
			}
		}
		else
		{
			if (!self->enemy->client)
				self->enemy->s.effects &= ~EF_TRACKERTRAIL;
			G_FreeEdict(self);
		}
	}
}

void tracker_pain_daemon_spawn(edict_t* owner, edict_t* enemy, int damage)
{
	edict_t* daemon;

	if (enemy == nullptr)
		return;

	daemon = G_Spawn();
	daemon->classname = "pain daemon";
	daemon->think = tracker_pain_daemon_think;
	daemon->nextthink = level.time;
	daemon->timestamp = level.time;
	daemon->owner = owner;
	daemon->enemy = enemy;
	daemon->dmg = damage;
}

void tracker_explode(edict_t* self)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TRACKER_EXPLOSION);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(self);
}

TOUCH(tracker_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	float damagetime;

	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if ((other->svflags & SVF_MONSTER) || other->client)
		{
			if (other->health > 0) // knockback only for living creatures
			{
				// PMM - kickback was times 4 .. reduced to 3
				// now this does no damage, just knockback
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
					/* self->dmg */ 0, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);

				if (!(other->flags & (FL_FLY | FL_SWIM)))
					other->velocity[2] += 140;

				damagetime = ((float)self->dmg) * 0.1f;
				damagetime = damagetime / TRACKER_DAMAGE_TIME.seconds();

				tracker_pain_daemon_spawn(self->owner, other, (int)damagetime);
			}
			else // lots of damage (almost autogib) for dead bodies
			{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
					self->dmg * 4, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);
			}
		}
		else // full damage in one shot for inanimate objects
		{
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
				self->dmg, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);
		}
	}

	tracker_explode(self);
	return;
}

THINK(tracker_fly) (edict_t* self) -> void
{
	vec3_t dest;
	vec3_t dir;
	vec3_t center;

	if ((!self->enemy) || (!self->enemy->inuse) || (self->enemy->health < 1))
	{
		tracker_explode(self);
		return;
	}

	// PMM - try to hunt for center of enemy, if possible and not client
	if (self->enemy->client)
	{
		dest = self->enemy->s.origin;
		dest[2] += self->enemy->viewheight;
	}
	// paranoia
	else if (!self->enemy->absmin || !self->enemy->absmax)
	{
		dest = self->enemy->s.origin;
	}
	else
	{
		center = (self->enemy->absmin + self->enemy->absmax) * 0.5f;
		dest = center;
	}

	dir = dest - self->s.origin;
	dir.normalize();
	self->s.angles = vectoangles(dir);
	self->velocity = dir * self->speed;
	self->monsterinfo.saved_goal = dest;

	self->nextthink = level.time + 10_hz;
}

void fire_tracker(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, edict_t* enemy)
{
	edict_t* bolt;
	trace_t	 tr;

	bolt = G_Spawn();
	bolt->s.origin = start;
	bolt->s.old_origin = start;
	bolt->s.angles = vectoangles(dir);
	bolt->velocity = dir * speed;
	bolt->svflags |= SVF_PROJECTILE;
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_PROJECTILE;

	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		bolt->clipmask &= ~CONTENTS_PLAYER;

	bolt->solid = SOLID_BBOX;
	bolt->speed = (float)speed;
	bolt->s.effects = EF_TRACKER;
	bolt->s.sound = gi.soundindex("weapons/disrupt.wav");
	bolt->s.modelindex = gi.modelindex("models/proj/disintegrator/tris.md2");
	bolt->touch = tracker_touch;
	bolt->enemy = enemy;
	bolt->owner = self;
	bolt->dmg = damage;
	bolt->classname = "tracker";
	gi.linkentity(bolt);

	if (enemy)
	{
		bolt->nextthink = level.time + 10_hz;
		bolt->think = tracker_fly;
	}
	else
	{
		bolt->nextthink = level.time + 7_sec; //reduce?
		bolt->think = G_FreeEdict;
	}

	tr = gi.traceline(self->s.origin, bolt->s.origin, bolt, bolt->clipmask);
	if (tr.fraction < 1.0f)
	{
		bolt->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		bolt->touch(bolt, tr.ent, tr, false);
	}
}