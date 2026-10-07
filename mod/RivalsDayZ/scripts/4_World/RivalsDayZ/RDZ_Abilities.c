// Server side of every hero ability. The owning client only sends "use ability <bit>"
// with its camera ray; everything here runs where the game is authoritative (the host's
// server, or the single process when playing solo).

class RDZ_Abilities
{
	static void Handle(PlayerBase p, int bit, vector camPos, vector camDir)
	{
		if (!RDZ.IsAuthority() || !p || !p.IsAlive() || !p.RDZ_HasBit(bit) || !p.RDZ_CooldownReady(bit))
			return;
		RDZ_AbilityDef a = RDZ_Lookup.AbilityByBit(bit);
		if (!a)
			return;

		// The camera may sit behind the shoulder; anything further away is not trusted.
		vector head = p.GetPosition() + "0 1.6 0";
		if (vector.Distance(camPos, head) > 5)
			camPos = head;
		camDir.Normalize();

		bool used = false;
		switch (a.Kind)
		{
			case RDZ_Kind.SWING:
				used = DoSwing(p, a, camPos, camDir);
				break;
			case RDZ_Kind.ZIP_YANK:
				used = DoZipYank(p, a, camPos, camDir);
				break;
			case RDZ_Kind.WEB_SHOT:
				used = DoWebShot(p, a, camPos, camDir);
				break;
			case RDZ_Kind.AREA_DRAIN:
				used = RDZ_AreaDrains.Start(p, a, p.RDZ_DisplayName());
				break;
		}
		if (used)
		{
			p.RDZ_StartCooldown(a);
			RDZ_Fx.Play(p, a.Sound);
		}
	}

	//! Find where a web sticks: along the aim, else up-forward (so swinging works without precise aim).
	static bool FindWebAnchor(Object self, vector from, vector dir, float range, float minY, out vector anchor)
	{
		vector hit, n;
		Object o;
		if (RDZ_Geo.Ray(from, from + dir * range, self, hit, n, o) && hit[1] >= minY && !RDZ_Geo.IsCreature(o))
		{
			anchor = hit;
			return true;
		}
		vector flat = RDZ.Flat(dir);
		if (flat.Length() < 0.01)
			flat = "0 0 1";
		flat.Normalize();
		float up = RDZ_Move.SWING_AUTO_AIM_UP_DEG * Math.DEG2RAD;
		array<float> yaws = {0, 20, -20, 40, -40};
		foreach (float yawDeg : yaws)
		{
			float y = yawDeg * Math.DEG2RAD;
			vector f = Vector(flat[0] * Math.Cos(y) - flat[2] * Math.Sin(y), 0, flat[0] * Math.Sin(y) + flat[2] * Math.Cos(y));
			vector d = f * Math.Cos(up) + Vector(0, Math.Sin(up), 0);
			if (RDZ_Geo.Ray(from, from + d * range, self, hit, n, o) && hit[1] >= minY && !RDZ_Geo.IsCreature(o))
			{
				anchor = hit;
				return true;
			}
		}
		return false;
	}

	static bool DoSwing(PlayerBase p, RDZ_AbilityDef a, vector camPos, vector camDir)
	{
		vector anchor;
		float minY = p.GetPosition()[1] + RDZ_Move.SWING_MIN_ANCHOR_HEIGHT;
		if (!FindWebAnchor(p, camPos, camDir, a.Range, minY, anchor))
		{
			p.RDZ_Toast("Nothing to web onto.");
			return false;
		}
		p.RDZ_SendWeb(RDZ_Net.MODE_SWING, anchor, vector.Zero, 0);
		return true;
	}

	//! Get Over Here!: a creature on the aim line is yanked in; otherwise web-zip to the surface.
	static bool DoZipYank(PlayerBase p, RDZ_AbilityDef a, vector camPos, vector camDir)
	{
		vector hit, n;
		Object o;
		if (!RDZ_Geo.Ray(camPos, camPos + camDir * a.Range, p, hit, n, o, 0.3))
			return false;
		if (RDZ_Geo.IsCreature(o))
		{
			vector dest = p.GetPosition() + RDZ.Flat(camDir).Normalized() * 1.5;
			Yank(EntityAI.Cast(o), dest, a.Speed, p);
			Damage(EntityAI.Cast(o), a.Damage, p);
			return true;
		}
		p.RDZ_SendWeb(RDZ_Net.MODE_ZIP, hit, n, a.Speed);
		return true;
	}

	//! Web-Cluster: damage and web the first creature on the aim line.
	static bool DoWebShot(PlayerBase p, RDZ_AbilityDef a, vector camPos, vector camDir)
	{
		vector hit, n;
		Object o;
		if (!RDZ_Geo.Ray(camPos, camPos + camDir * a.Range, p, hit, n, o, 0.5) || !RDZ_Geo.IsCreature(o))
			return false;
		WebTarget(EntityAI.Cast(o), a.Duration, a.Damage, p);
		return true;
	}

	// --- effects on targets (also used by the AI heroes) ------------------------------

	static void WebTarget(EntityAI target, float duration, float damage, EntityAI source)
	{
		if (!target || !target.IsAlive())
			return;
		Damage(target, damage, source);
		PlayerBase tp = PlayerBase.Cast(target);
		if (tp)
		{
			tp.RDZ_SendWeb(RDZ_Net.MODE_HELD, tp.GetPosition(), vector.Zero, duration);
			tp.RDZ_Toast("You're webbed!");
			return;
		}
		ZombieBase z = ZombieBase.Cast(target);
		if (z)
			z.RDZ_Hold(duration);
	}

	static void Yank(EntityAI target, vector dest, float speed, EntityAI source)
	{
		if (!target || !target.IsAlive())
			return;
		PlayerBase tp = PlayerBase.Cast(target);
		if (tp)
		{
			tp.RDZ_SendWeb(RDZ_Net.MODE_PULLED, dest, vector.Zero, speed);
			return;
		}
		ZombieBase z = ZombieBase.Cast(target);
		if (z)
			z.RDZ_Fly(dest, Math.Max(RDZ_Move.YANK_TIME, vector.Distance(z.GetPosition(), dest) / Math.Max(speed, 1)), 0.5);
	}

	static void Damage(EntityAI target, float amount, EntityAI source)
	{
		if (!target || amount <= 0 || !target.IsAlive())
			return;
		target.AddHealth("", "Health", -amount);
	}

	static void Heal(EntityAI who, float amount)
	{
		if (!who || amount <= 0 || !who.IsAlive())
			return;
		who.AddHealth("", "Health", amount);
	}

	// --- melee: Power Absorption + Mimicry -----------------------------------------------

	//! A melee hit landed on victim. The attacker may be a player or an AI hero.
	static void OnMeleeHit(EntityAI victim, EntityAI source, TotalDamageResult dr)
	{
		if (!victim || !source)
			return;
		EntityAI attacker = source;
		if (source.GetHierarchyRootPlayer())
			attacker = source.GetHierarchyRootPlayer();
		if (attacker == victim)
			return;

		int attackerMask = PowersOf(attacker);
		if (attackerMask == 0)
			return;

		// Power Absorption: extra drain damage, part of it back as health.
		RDZ_AbilityDef drain = RDZ_Lookup.AbilityOfKind(RDZ_Kind.DRAIN_MELEE, attackerMask);
		if (drain)
		{
			float dealt = 0;
			if (dr)
				dealt = dr.GetDamage("", "Health");
			Damage(victim, drain.Damage, attacker);
			Heal(attacker, (dealt + drain.Damage) * drain.HealPct / 100.0);
			RDZ_Fx.Play(attacker, drain.Sound);
		}

		// Mimicry: take the victim's stealable powers.
		RDZ_AbilityDef mimic = RDZ_Lookup.AbilityOfKind(RDZ_Kind.MIMICRY, attackerMask);
		if (!mimic)
			return;
		int stolen = RDZ_Lookup.StealableMask(PowersOf(victim));
		if (stolen == 0)
			return;
		PlayerBase ap = PlayerBase.Cast(attacker);
		if (ap && !ap.RDZ_CooldownReady(mimic.Bit))
			return;
		string victimName = DisplayNameOf(victim);
		string attackerName = DisplayNameOf(attacker);
		if (ap)
		{
			ap.RDZ_GainMimic(stolen, mimic.Duration, victimName);
			ap.RDZ_StartCooldown(mimic);
		}
		ZombieBase az = ZombieBase.Cast(attacker);
		if (az)
			az.RDZ_GainMimic(stolen, mimic.Duration);

		PlayerBase vp = PlayerBase.Cast(victim);
		if (vp)
			vp.RDZ_LoseToDrain(stolen, mimic.Duration, attackerName);
		ZombieBase vz = ZombieBase.Cast(victim);
		if (vz)
			vz.RDZ_LoseToDrain(stolen, mimic.Duration);
		RDZ_Fx.Play(attacker, mimic.Sound);
	}

	static int PowersOf(EntityAI e)
	{
		PlayerBase p = PlayerBase.Cast(e);
		if (p)
			return p.RDZ_GetPowers();
		ZombieBase z = ZombieBase.Cast(e);
		if (z)
			return z.RDZ_GetPowers();
		return 0;
	}

	static string DisplayNameOf(EntityAI e)
	{
		PlayerBase p = PlayerBase.Cast(e);
		if (p)
			return p.RDZ_DisplayName();
		ZombieBase z = ZombieBase.Cast(e);
		if (z && z.RDZ_GetHunterHero() != "")
			return RDZ_Content.HeroName(z.RDZ_GetHunterHero());
		return "something";
	}
}

modded class PlayerBase
{
	string RDZ_DisplayName()
	{
		RDZ_HeroDef hero = RDZ_Lookup.HeroOfMask(m_RDZ_ItemMask);
		if (hero)
			return RDZ_Content.HeroName(hero.Id);
		if (GetIdentity())
			return GetIdentity().GetName();
		return "Survivor";
	}
}

// ---------------------------------------------------------------------------
// Rogue's area drain: damage over time around her, then pull everyone in.

class RDZ_AreaDrain
{
	EntityAI Owner;
	RDZ_AbilityDef Def;
	float EndsAt;
	float NextTick;
}

class RDZ_AreaDrains
{
	static ref array<ref RDZ_AreaDrain> s_Active = new array<ref RDZ_AreaDrain>;
	static const float TICK = 0.5;

	static bool Start(EntityAI owner, RDZ_AbilityDef a, string ownerName)
	{
		if (!owner || !a)
			return false;
		RDZ_AreaDrain d = new RDZ_AreaDrain;
		d.Owner = owner;
		d.Def = a;
		d.EndsAt = RDZ.Now() + a.Duration;
		d.NextTick = RDZ.Now();
		s_Active.Insert(d);
		return true;
	}

	static void Tick()
	{
		float now = RDZ.Now();
		for (int i = s_Active.Count() - 1; i >= 0; i--)
		{
			RDZ_AreaDrain d = s_Active.Get(i);
			if (!d.Owner || !d.Owner.IsAlive())
			{
				s_Active.Remove(i);
				continue;
			}
			if (now >= d.EndsAt)
			{
				PullIn(d);
				s_Active.Remove(i);
				continue;
			}
			if (now < d.NextTick)
				continue;
			d.NextTick = now + TICK;
			array<EntityAI> victims = Victims(d);
			float total = 0;
			foreach (EntityAI v : victims)
			{
				float dmg = d.Def.Damage * TICK;
				RDZ_Abilities.Damage(v, dmg, d.Owner);
				total += dmg;
			}
			RDZ_Abilities.Heal(d.Owner, total * d.Def.HealPct / 100.0);
		}
	}

	protected static array<EntityAI> Victims(RDZ_AreaDrain d)
	{
		array<EntityAI> result = new array<EntityAI>;
		array<Object> objects = new array<Object>;
		array<CargoBase> cargos = new array<CargoBase>;
		GetGame().GetObjectsAtPosition3D(d.Owner.GetPosition(), d.Def.Range, objects, cargos);
		foreach (Object o : objects)
		{
			if (o == d.Owner || !RDZ_Geo.IsCreature(o))
				continue;
			EntityAI e = EntityAI.Cast(o);
			if (e && e.IsAlive())
				result.Insert(e);
		}
		return result;
	}

	protected static void PullIn(RDZ_AreaDrain d)
	{
		array<EntityAI> victims = Victims(d);
		foreach (EntityAI v : victims)
		{
			vector away = v.GetPosition() - d.Owner.GetPosition();
			away[1] = 0;
			if (away.Length() < 0.1)
				continue;
			vector dest = d.Owner.GetPosition() + away.Normalized() * 1.2;
			RDZ_Abilities.Yank(v, dest, d.Def.Speed, d.Owner);
		}
	}
}

// ---------------------------------------------------------------------------
// Sounds converted from the player's Rivals install. Each client plays its own copy;
// nothing plays when the launcher could not convert that file.

class RDZ_Fx
{
	//! Authority: play on every client (and locally when playing solo).
	static void Play(Object on, string assetId)
	{
		if (!on || assetId == "")
			return;
		if (GetGame().IsMultiplayer())
		{
			if (GetGame().IsServer())
				GetGame().RPCSingleParam(on, RDZ_Net.RPC_FX, new Param1<string>(assetId), false);
			return;
		}
		PlayLocal(on, assetId);
	}

	static void PlayLocal(Object on, string assetId)
	{
		if (!RDZ.HasUI() || !on)
			return;
		if (RDZ_Content.AssetPath(assetId) == "")
			return;
		string soundSet = RDZ_Lookup.SoundSetOf(assetId);
		if (soundSet != "")
			SEffectManager.PlaySoundOnObject(soundSet, on);
	}
}

// ---------------------------------------------------------------------------
// Spawn menu buttons (server side).

class RDZ_Menu
{
	static void Handle(PlayerBase p, string menuId)
	{
		if (!RDZ.IsAuthority() || !p || !p.IsAlive())
			return;
		RDZ_Data.Init();
		float now = RDZ.Now();
		if (GetGame().IsMultiplayer() && now - p.m_RDZ_LastMenuUse < RDZ_MP.SPAWN_MENU_COOLDOWN_S)
		{
			p.RDZ_Toast("Spawn menu is cooling down.");
			return;
		}
		p.m_RDZ_LastMenuUse = now;
		foreach (RDZ_MenuDef m : RDZ_Data.Menu)
		{
			if (m.Id != menuId)
				continue;
			if (m.Action == "give_kit")
				GiveKit(p, m);
			else if (m.Action == "spawn_hunter")
				SpawnHunter(p, m);
			else if (m.Action == "clear_hunters")
			{
				RDZ_Hunters.ClearAll();
				p.RDZ_Toast("AI heroes removed.");
			}
			return;
		}
	}

	static void GiveKit(PlayerBase p, RDZ_MenuDef m)
	{
		RDZ_HeroDef hero = RDZ_Data.Heroes.Get(m.Hero);
		if (!hero)
			return;
		EntityAI oldGloves = p.FindAttachmentBySlotName("Gloves");
		if (oldGloves)
			p.ServerDropEntity(oldGloves);
		EntityAI item = p.GetInventory().CreateAttachment(hero.PowerItem);
		if (!item)
			item = EntityAI.Cast(GetGame().CreateObjectEx(hero.PowerItem, p.GetPosition(), ECE_PLACE_ON_SURFACE));
		if (!item)
			RDZ.Log("Could not create " + hero.PowerItem);
		foreach (string extra : m.ExtraItems)
		{
			if (!GetGame().ConfigIsExisting("CfgVehicles " + extra))
			{
				RDZ.Log("Spawn menu: " + extra + " does not exist in this DayZ version, skipped.");
				continue;
			}
			if (!p.GetInventory().CreateAttachment(extra))
				GetGame().CreateObjectEx(extra, p.GetPosition(), ECE_PLACE_ON_SURFACE);
		}
		p.RDZ_Toast(RDZ_Content.HeroName(hero.Id) + " kit equipped.");
		// Solo: the other heroes come after you as soon as you pick.
		if (!GetGame().IsMultiplayer())
			RDZ_Hunters.EnsureRivals(hero.Id, p.GetPosition());
	}

	static void SpawnHunter(PlayerBase p, RDZ_MenuDef m)
	{
		RDZ_HunterDef def = RDZ_Lookup.HunterOfHero(m.Hero);
		if (!def)
			return;
		if (RDZ_Hunters.Spawn(def, p.GetPosition()))
			p.RDZ_Toast(RDZ_Content.HeroName(m.Hero) + " is hunting you.");
		else
			p.RDZ_Toast("Could not spawn " + RDZ_Content.HeroName(m.Hero) + ".");
	}
}
