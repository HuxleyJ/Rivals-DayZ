// AI heroes: infected bodies driven by the hunters sheet. Server only.

class RDZ_HunterState
{
	ZombieBase Body;
	RDZ_HunterDef Def;
	float NextAbility;
	float RespawnAt;
	float NextTaunt;
}

class RDZ_Hunters
{
	static ref array<ref RDZ_HunterState> s_List = new array<ref RDZ_HunterState>;

	static bool Spawn(RDZ_HunterDef def, vector near)
	{
		RDZ_HunterState st = new RDZ_HunterState;
		st.Def = def;
		if (!SpawnBody(st, near))
			return false;
		s_List.Insert(st);
		return true;
	}

	protected static bool SpawnBody(RDZ_HunterState st, vector near)
	{
		vector pos = RDZ_Geo.SpawnPointPos(st.Def.Spawn, near);
		foreach (string cls : st.Def.Classes)
		{
			if (!GetGame().ConfigIsExisting("CfgVehicles " + cls))
				continue;
			ZombieBase z = ZombieBase.Cast(GetGame().CreateObjectEx(cls, pos, ECE_CREATEPHYSICS | ECE_INITAI));
			if (!z)
				continue;
			z.RDZ_MakeHunter(st.Def);
			st.Body = z;
			st.RespawnAt = 0;
			st.NextAbility = RDZ.Now() + 2;
			RDZ.Log("Spawned " + st.Def.Id + " as " + cls + " at " + pos.ToString());
			RDZ_Fx.Play(z, st.Def.Taunt);
			return true;
		}
		RDZ.Log("None of the infected classes for " + st.Def.Id + " exist; check the hunters sheet.");
		return false;
	}

	//! Spawn an AI hero for every hero other than `pickedHero` that is not already hunting.
	static void EnsureRivals(string pickedHero, vector near)
	{
		RDZ_Data.Init();
		foreach (string heroId : RDZ_Data.HeroOrder)
		{
			if (heroId == pickedHero || HasHunterFor(heroId))
				continue;
			RDZ_HunterDef def = RDZ_Lookup.HunterOfHero(heroId);
			if (def)
				Spawn(def, near);
		}
	}

	static bool HasHunterFor(string heroId)
	{
		foreach (RDZ_HunterState st : s_List)
		{
			if (st.Def.Hero == heroId)
				return true;
		}
		return false;
	}

	static void ClearAll()
	{
		foreach (RDZ_HunterState st : s_List)
		{
			if (st.Body)
				GetGame().ObjectDelete(st.Body);
		}
		s_List.Clear();
	}

	static void Tick(float dt)
	{
		float now = RDZ.Now();
		for (int i = s_List.Count() - 1; i >= 0; i--)
		{
			RDZ_HunterState st = s_List.Get(i);
			if (!st.Body || !st.Body.IsAlive())
			{
				if (st.Def.Respawn <= 0)
				{
					s_List.Remove(i);
					continue;
				}
				if (st.RespawnAt == 0)
					st.RespawnAt = now + st.Def.Respawn;
				else if (now >= st.RespawnAt)
				{
					PlayerBase anyone = NearestPlayer(Vector(0, 0, 0), 100000);
					if (anyone)
						SpawnBody(st, anyone.GetPosition());
					else
						st.RespawnAt = now + 5;
				}
				continue;
			}
			Think(st, now);
		}
	}

	static PlayerBase NearestPlayer(vector from, float range)
	{
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		if (!GetGame().IsMultiplayer() && GetGame().GetPlayer())
		{
			players.Clear();
			players.Insert(GetGame().GetPlayer());
		}
		PlayerBase best;
		float bestD = range;
		foreach (Man m : players)
		{
			PlayerBase p = PlayerBase.Cast(m);
			if (!p || !p.IsAlive())
				continue;
			float d = vector.Distance(from, p.GetPosition());
			if (d < bestD)
			{
				bestD = d;
				best = p;
			}
		}
		return best;
	}

	protected static void Think(RDZ_HunterState st, float now)
	{
		ZombieBase z = st.Body;
		PlayerBase target = NearestPlayer(z.GetPosition(), st.Def.AggroRange);
		if (!target || z.RDZ_IsBusy() || now < st.NextAbility)
			return;
		st.NextAbility = now + st.Def.AbilityInterval * Math.RandomFloat(0.7, 1.3);

		int powers = z.RDZ_GetPowers();
		vector zp = z.GetPosition();
		vector tp = target.GetPosition();
		float dist = vector.Distance(zp, tp);
		bool sees = HasLineOfSight(z, target);

		if (now >= st.NextTaunt && dist < 60)
		{
			RDZ_Fx.Play(z, st.Def.Taunt);
			st.NextTaunt = now + 20;
		}

		RDZ_AbilityDef swing = RDZ_Lookup.AbilityOfKind(RDZ_Kind.SWING, powers);
		RDZ_AbilityDef web = RDZ_Lookup.AbilityOfKind(RDZ_Kind.WEB_SHOT, powers);
		RDZ_AbilityDef yank = RDZ_Lookup.AbilityOfKind(RDZ_Kind.ZIP_YANK, powers);
		RDZ_AbilityDef drain = RDZ_Lookup.AbilityOfKind(RDZ_Kind.AREA_DRAIN, powers);

		if (drain && dist <= drain.Range)
		{
			RDZ_AreaDrains.Start(z, drain, "");
			RDZ_Fx.Play(z, drain.Sound);
			return;
		}
		if (web && sees && dist <= web.Range && Math.RandomFloat01() < 0.5)
		{
			RDZ_Abilities.WebTarget(target, web.Duration, web.Damage, z);
			RDZ_Fx.Play(z, web.Sound);
			return;
		}
		if (yank && sees && dist > 6 && dist <= yank.Range && Math.RandomFloat01() < 0.4)
		{
			vector toZ = RDZ.Flat(zp - tp).Normalized();
			RDZ_Abilities.Yank(target, zp - toZ * 1.5, yank.Speed, z);
			RDZ_Fx.Play(z, yank.Sound);
			return;
		}
		if (swing && (dist > 10 || Math.AbsFloat(tp[1] - zp[1]) > 3))
		{
			// Web-hop toward the player: land on a roof or the street near them.
			vector dest = RDZ_Geo.FindRooftop(tp, 4, 14, z);
			float hop = vector.Distance(zp, dest);
			if (hop > 3)
			{
				z.RDZ_Fly(dest, hop / 16.0, Math.Clamp(hop * 0.3, 2, 12));
				RDZ_Fx.Play(z, swing.Sound);
			}
		}
	}

	static bool HasLineOfSight(Object from, Object to)
	{
		vector a = from.GetPosition() + "0 1.6 0";
		vector b = to.GetPosition() + "0 1.4 0";
		vector hit, n;
		Object o;
		if (!RDZ_Geo.Ray(a, b, from, hit, n, o))
			return true;
		return o == to;
	}
}

// ---------------------------------------------------------------------------
// Server tick shared by the offline mission and hosted servers.

class RDZ_Server
{
	static float s_PowerTimer;

	static void Tick(float dt)
	{
		if (!RDZ.IsAuthority())
			return;
		RDZ_AreaDrains.Tick();
		RDZ_Hunters.Tick(dt);
		s_PowerTimer += dt;
		if (s_PowerTimer < 0.5)
			return;
		s_PowerTimer = 0;
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		if (!GetGame().IsMultiplayer() && GetGame().GetPlayer())
		{
			players.Clear();
			players.Insert(GetGame().GetPlayer());
		}
		foreach (Man m : players)
		{
			PlayerBase p = PlayerBase.Cast(m);
			if (p)
				p.RDZ_RecomputePowers();
		}
	}
}
