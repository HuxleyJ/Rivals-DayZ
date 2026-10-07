// Hero powers on players: which powers a player has (from the gloves they wear, plus
// powers stolen by Rogue's Mimicry, minus powers Rogue drained from them), ability
// requests from the owning client, cooldowns, and the network plumbing.

modded class PlayerBase
{
	int m_RDZ_Powers;			// synced to all clients
	int m_RDZ_ItemMask;			// server
	int m_RDZ_MimicMask;		// server: powers stolen by this player
	float m_RDZ_MimicUntil;
	int m_RDZ_DrainedMask;		// server: powers stolen from this player
	float m_RDZ_DrainedUntil;
	float m_RDZ_LastMenuUse;
	ref map<int, float> m_RDZ_CooldownEnd;
	ref RDZ_AnimTable m_RDZ_Anim;

	bool m_RDZ_PendingWeb;
	int m_RDZ_PendingMode;
	vector m_RDZ_PendingPoint;
	vector m_RDZ_PendingNormal;
	float m_RDZ_PendingParam;

	override void Init()
	{
		super.Init();
		RegisterNetSyncVariableInt("m_RDZ_Powers");
		m_RDZ_CooldownEnd = new map<int, float>;
		m_RDZ_LastMenuUse = -1000;
	}

	// --- powers --------------------------------------------------------------

	int RDZ_GetPowers()
	{
		return m_RDZ_Powers;
	}

	bool RDZ_HasPower(int kind)
	{
		return RDZ_Lookup.AbilityOfKind(kind, m_RDZ_Powers) != null;
	}

	bool RDZ_HasBit(int bit)
	{
		return (m_RDZ_Powers & bit) != 0;
	}

	//! Server: recompute the synced mask from the worn item, Mimicry and drains.
	void RDZ_RecomputePowers()
	{
		if (!RDZ.IsAuthority())
			return;
		float now = RDZ.Now();
		if (m_RDZ_MimicMask != 0 && now >= m_RDZ_MimicUntil)
		{
			m_RDZ_MimicMask = 0;
			RDZ_Toast("Mimicry wore off.");
		}
		if (m_RDZ_DrainedMask != 0 && now >= m_RDZ_DrainedUntil)
		{
			m_RDZ_DrainedMask = 0;
			RDZ_Toast("Your powers are back.");
		}
		int powers = (m_RDZ_ItemMask & ~m_RDZ_DrainedMask) | m_RDZ_MimicMask;
		if (powers != m_RDZ_Powers)
		{
			m_RDZ_Powers = powers;
			SetSynchDirty();
		}
	}

	protected void RDZ_UpdateItemMask()
	{
		if (!RDZ.IsAuthority())
			return;
		int mask = 0;
		EntityAI gloves = FindAttachmentBySlotName("Gloves");
		if (gloves)
			mask = RDZ_Lookup.ItemMask(gloves.GetType());
		if (mask != m_RDZ_ItemMask)
		{
			m_RDZ_ItemMask = mask;
			RDZ_HeroDef hero = RDZ_Lookup.HeroOfMask(mask);
			if (hero)
			{
				RDZ_Toast("You have " + RDZ_Content.HeroName(hero.Id) + "'s powers.");
				RDZ_Fx.Play(this, hero.VoiceSpawn);
			}
		}
		RDZ_RecomputePowers();
	}

	//! Server: Rogue took `mask` from someone for `duration` seconds.
	void RDZ_GainMimic(int mask, float duration, string fromName)
	{
		m_RDZ_MimicMask |= mask;
		m_RDZ_MimicUntil = RDZ.Now() + duration;
		RDZ_Toast("Mimicry: you took " + fromName + "'s powers for " + Math.Round(duration).ToString() + "s!");
		RDZ_RecomputePowers();
	}

	//! Server: this player's powers in `mask` were drained for `duration` seconds.
	void RDZ_LoseToDrain(int mask, float duration, string byName)
	{
		m_RDZ_DrainedMask |= mask;
		m_RDZ_DrainedUntil = RDZ.Now() + duration;
		RDZ_Toast(byName + " drained your powers for " + Math.Round(duration).ToString() + "s!");
		RDZ_RecomputePowers();
	}

	override void EEItemAttached(EntityAI item, string slot_name)
	{
		super.EEItemAttached(item, slot_name);
		if (slot_name == "Gloves")
			RDZ_UpdateItemMask();
	}

	override void EEItemDetached(EntityAI item, string slot_name)
	{
		super.EEItemDetached(item, slot_name);
		if (slot_name == "Gloves")
			RDZ_UpdateItemMask();
	}

	override void OnVariablesSynchronized()
	{
		super.OnVariablesSynchronized();
		// m_RDZ_Powers is already updated here; the HUD reads it every frame.
	}

	// --- melee: Rogue's Power Absorption and Mimicry ----------------------------

	override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef)
	{
		super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
		if (RDZ.IsAuthority() && damageType == DamageType.CLOSE_COMBAT)
			RDZ_Abilities.OnMeleeHit(this, source, damageResult);
	}

	// --- cooldowns --------------------------------------------------------------

	bool RDZ_CooldownReady(int bit)
	{
		return RDZ_CooldownLeft(bit) <= 0;
	}

	float RDZ_CooldownLeft(int bit)
	{
		float end;
		if (m_RDZ_CooldownEnd.Find(bit, end))
			return Math.Max(0, end - RDZ.Now());
		return 0;
	}

	void RDZ_StartCooldown(RDZ_AbilityDef a)
	{
		if (a.Cooldown <= 0)
			return;
		m_RDZ_CooldownEnd.Set(a.Bit, RDZ.Now() + a.Cooldown);
		if (GetGame().IsMultiplayer() && GetIdentity())
			GetGame().RPCSingleParam(this, RDZ_Net.RPC_COOLDOWN, new Param2<int, float>(a.Bit, a.Cooldown), true, GetIdentity());
	}

	// --- client -> server ability requests -----------------------------------------

	//! Owning client: ask the server to use the ability with this bit, aimed from the camera.
	void RDZ_RequestAbility(int bit)
	{
		if (!RDZ_HasBit(bit) || !RDZ_CooldownReady(bit))
			return;
		vector camPos = GetGame().GetCurrentCameraPosition();
		vector camDir = GetGame().GetCurrentCameraDirection();
		if (GetGame().IsMultiplayer() && GetGame().IsClient())
		{
			if (!ScriptInputUserData.CanStoreInputUserData())
				return;
			ScriptInputUserData ctx = new ScriptInputUserData;
			ctx.Write(RDZ_Net.UDT_ABILITY);
			ctx.Write(bit);
			ctx.Write(camPos);
			ctx.Write(camDir);
			ctx.Send();
			return;
		}
		RDZ_Abilities.Handle(this, bit, camPos, camDir);
	}

	override bool OnInputUserDataProcess(int userDataType, ParamsReadContext ctx)
	{
		if (userDataType == RDZ_Net.UDT_ABILITY)
		{
			int bit;
			vector camPos, camDir;
			if (ctx.Read(bit) && ctx.Read(camPos) && ctx.Read(camDir))
				RDZ_Abilities.Handle(this, bit, camPos, camDir);
			return true;
		}
		return super.OnInputUserDataProcess(userDataType, ctx);
	}

	// --- web movement, started on the server and the owner together -------------------

	//! Server: start (or redirect) this player's web movement on server and owner.
	void RDZ_SendWeb(int mode, vector point, vector normal, float param)
	{
		ScriptJunctureData ctx = new ScriptJunctureData;
		ctx.Write(mode);
		ctx.Write(point);
		ctx.Write(normal);
		ctx.Write(param);
		SendSyncJuncture(RDZ_Net.SJ_WEB, ctx);
	}

	override void OnSyncJuncture(int pJunctureID, ParamsReadContext pCtx)
	{
		if (pJunctureID == RDZ_Net.SJ_WEB)
		{
			int mode;
			vector point, normal;
			float param;
			if (pCtx.Read(mode) && pCtx.Read(point) && pCtx.Read(normal) && pCtx.Read(param))
			{
				m_RDZ_PendingWeb = true;
				m_RDZ_PendingMode = mode;
				m_RDZ_PendingPoint = point;
				m_RDZ_PendingNormal = normal;
				m_RDZ_PendingParam = param;
			}
			return;
		}
		super.OnSyncJuncture(pJunctureID, pCtx);
	}

	RDZ_AnimTable RDZ_GetAnimTable()
	{
		if (!m_RDZ_Anim)
			m_RDZ_Anim = new RDZ_AnimTable(this);
		return m_RDZ_Anim;
	}

	void RDZ_OnWebCommandEnded()
	{
	}

	// --- RPC ---------------------------------------------------------------------------

	void RDZ_Toast(string text)
	{
		if (GetGame().IsMultiplayer())
		{
			if (GetGame().IsServer() && GetIdentity())
				GetGame().RPCSingleParam(this, RDZ_Net.RPC_TOAST, new Param1<string>(text), true, GetIdentity());
			return;
		}
		RDZ_ClientState.Toast(text);
	}

	//! Owning client: ask the server to run a spawn-menu button.
	void RDZ_RequestMenu(string menuId)
	{
		if (GetGame().IsMultiplayer() && GetGame().IsClient())
		{
			GetGame().RPCSingleParam(this, RDZ_Net.RPC_MENU, new Param1<string>(menuId), true);
			return;
		}
		RDZ_Menu.Handle(this, menuId);
	}

	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		switch (rpc_type)
		{
			case RDZ_Net.RPC_COOLDOWN:
			{
				Param2<int, float> cd = new Param2<int, float>(0, 0);
				if (ctx.Read(cd))
					m_RDZ_CooldownEnd.Set(cd.param1, RDZ.Now() + cd.param2);
				return;
			}
			case RDZ_Net.RPC_FX:
			{
				Param1<string> fx = new Param1<string>("");
				if (ctx.Read(fx))
					RDZ_Fx.PlayLocal(this, fx.param1);
				return;
			}
			case RDZ_Net.RPC_TOAST:
			{
				Param1<string> toast = new Param1<string>("");
				if (ctx.Read(toast))
					RDZ_ClientState.Toast(toast.param1);
				return;
			}
			case RDZ_Net.RPC_INVITE:
			{
				Param1<string> inv = new Param1<string>("");
				if (ctx.Read(inv))
					RDZ_ClientState.s_Invite = inv.param1;
				return;
			}
			case RDZ_Net.RPC_MENU:
			{
				Param1<string> menu = new Param1<string>("");
				if (GetGame().IsServer() && ctx.Read(menu) && sender && GetIdentity() && sender.GetId() == GetIdentity().GetId())
					RDZ_Menu.Handle(this, menu.param1);
				return;
			}
		}
		super.OnRPC(sender, rpc_type, ctx);
	}
}
