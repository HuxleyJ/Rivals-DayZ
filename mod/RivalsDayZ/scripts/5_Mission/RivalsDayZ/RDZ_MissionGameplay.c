// Client side every frame: mashup keys -> ability requests, spawn menu, HUD.

modded class MissionGameplay
{
	ref RDZ_Hud m_RDZ_Hud;
	ref RDZ_SpawnMenu m_RDZ_Menu;

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		RDZ_ClientUpdate(timeslice);
	}

	void RDZ_ClientUpdate(float dt)
	{
		if (!RDZ.HasUI())
			return;
		RDZ_Data.Init();
		if (!m_RDZ_Hud)
			m_RDZ_Hud = new RDZ_Hud();

		PlayerBase p = PlayerBase.Cast(GetGame().GetPlayer());
		m_RDZ_Hud.Update(dt, p);
		if (!p || !p.IsAlive() || GetGame().GetUIManager().GetMenu())
			return;

		if (GetUApi().GetInputByName(RDZ_Inputs.IN_SPAWN_MENU).LocalPress())
		{
			RDZ_OpenSpawnMenu();
			return;
		}
		RDZ_TryInput(p, RDZ_Inputs.IN_ABILITY1);
		RDZ_TryInput(p, RDZ_Inputs.IN_ABILITY2);
		RDZ_TryInput(p, RDZ_Inputs.IN_ABILITY3);
	}

	void RDZ_OpenSpawnMenu()
	{
		if (GetGame().GetUIManager().GetMenu())
			return;
		m_RDZ_Menu = new RDZ_SpawnMenu();
		GetGame().GetUIManager().ShowScriptedMenu(m_RDZ_Menu, null);
	}

	protected void RDZ_TryInput(PlayerBase p, string uaName)
	{
		if (!GetUApi().GetInputByName(uaName).LocalPress())
			return;
		int bit = RDZ_PickAbility(p, uaName);
		if (bit != 0)
			p.RDZ_RequestAbility(bit);
	}

	//! Ability on this key the player has right now; stolen (Mimicry) powers win a tie.
	protected int RDZ_PickAbility(PlayerBase p, string uaName)
	{
		int powers = p.RDZ_GetPowers();
		int itemMask = 0;
		EntityAI gloves = p.FindAttachmentBySlotName("Gloves");
		if (gloves)
			itemMask = RDZ_Lookup.ItemMask(gloves.GetType());
		int own = 0;
		foreach (string id : RDZ_Data.AbilityOrder)
		{
			RDZ_AbilityDef a = RDZ_Data.Abilities.Get(id);
			if (a.Input != uaName || !(powers & a.Bit))
				continue;
			if (!(itemMask & a.Bit))
				return a.Bit;
			if (own == 0)
				own = a.Bit;
		}
		return own;
	}
}
