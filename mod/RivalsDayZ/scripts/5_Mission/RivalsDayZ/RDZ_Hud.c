// Hero panel (name and portrait from the player's Rivals install, abilities with keys and
// cooldowns), toasts, the invite line and markers over the AI heroes.

class RDZ_Hud
{
	protected Widget m_Root;
	protected Widget m_Panel;
	protected Widget m_Color;
	protected ImageWidget m_Portrait;
	protected TextWidget m_Name;
	protected TextWidget m_Source;
	protected ref array<TextWidget> m_Lines = new array<TextWidget>;
	protected TextWidget m_Toast;
	protected TextWidget m_Invite;
	protected string m_PortraitPath;
	protected float m_ToastLeft;

	protected ref array<ZombieBase> m_Hunters = new array<ZombieBase>;
	protected ref array<Widget> m_Markers = new array<Widget>;
	protected float m_ScanTimer;

	void RDZ_Hud()
	{
		m_Root = GetGame().GetWorkspace().CreateWidgets("RivalsDayZ/gui/layouts/rdz_hud.layout");
		m_Panel = m_Root.FindAnyWidget("HeroPanel");
		m_Color = m_Root.FindAnyWidget("HeroColor");
		m_Portrait = ImageWidget.Cast(m_Root.FindAnyWidget("Portrait"));
		m_Name = TextWidget.Cast(m_Root.FindAnyWidget("HeroName"));
		m_Source = TextWidget.Cast(m_Root.FindAnyWidget("HeroSource"));
		for (int i = 0; i < 8; i++)
			m_Lines.Insert(TextWidget.Cast(m_Root.FindAnyWidget("Line" + i)));
		m_Toast = TextWidget.Cast(m_Root.FindAnyWidget("Toast"));
		m_Invite = TextWidget.Cast(m_Root.FindAnyWidget("Invite"));
	}

	void ~RDZ_Hud()
	{
		if (m_Root)
			m_Root.Unlink();
		foreach (Widget w : m_Markers)
		{
			if (w)
				w.Unlink();
		}
	}

	void Update(float dt, PlayerBase p)
	{
		if (!m_Root || !m_Panel)
			return;
		UpdateToast(dt);
		if (m_Invite)
		{
			if (RDZ_ClientState.s_Invite != "")
				m_Invite.SetText(RDZ_ClientState.s_Invite);
			else
				m_Invite.SetText("");
		}
		if (!p || !p.IsAlive())
		{
			m_Panel.Show(false);
			HideMarkers();
			return;
		}
		UpdatePanel(p);
		UpdateMarkers(dt, p);
	}

	protected void UpdateToast(float dt)
	{
		m_ToastLeft -= dt;
		if (m_ToastLeft <= 0)
		{
			string next = RDZ_ClientState.PopToast();
			if (next != "")
			{
				m_Toast.SetText(next);
				m_ToastLeft = 4;
			}
			else
			{
				m_Toast.SetText("");
			}
		}
	}

	protected void UpdatePanel(PlayerBase p)
	{
		int powers = p.RDZ_GetPowers();
		if (powers == 0)
		{
			m_Panel.Show(true);
			m_Name.SetText("No powers");
			m_Source.SetText("Press " + KeyOf(RDZ_Inputs.IN_SPAWN_MENU) + " to pick Spider-Man or Rogue");
			ClearLines(0);
			m_Portrait.Show(false);
			return;
		}

		int itemMask = 0;
		EntityAI gloves = p.FindAttachmentBySlotName("Gloves");
		if (gloves)
			itemMask = RDZ_Lookup.ItemMask(gloves.GetType());
		RDZ_HeroDef hero = RDZ_Lookup.HeroOfMask(itemMask);
		if (!hero)
			hero = RDZ_Lookup.HeroOfMask(powers);

		m_Panel.Show(true);
		if (hero)
		{
			m_Name.SetText(RDZ_Content.HeroName(hero.Id));
			m_Color.SetColor(hero.Color);
			UpdatePortrait(hero);
		}
		if (RDZ_Content.HasRivals())
			m_Source.SetText("Read from your Marvel Rivals install");
		else
			m_Source.SetText("Built-in names (Rivals not read yet)");

		int line = 0;
		foreach (string id : RDZ_Data.AbilityOrder)
		{
			RDZ_AbilityDef a = RDZ_Data.Abilities.Get(id);
			if (!(powers & a.Bit) || line >= m_Lines.Count())
				continue;
			string text = "[" + AbilityKey(a) + "]  " + RDZ_Content.AbilityName(a.Id);
			if (!(itemMask & a.Bit))
				text += "  (stolen)";
			float left = p.RDZ_CooldownLeft(a.Bit);
			if (left > 0)
				text += "  " + Math.Ceil(left).ToString() + "s";
			m_Lines.Get(line).SetText(text);
			line++;
		}
		ClearLines(line);
	}

	protected void UpdatePortrait(RDZ_HeroDef hero)
	{
		string path = RDZ_Content.AssetPath(hero.Portrait);
		if (path == "")
		{
			m_Portrait.Show(false);
			return;
		}
		if (path != m_PortraitPath)
		{
			m_PortraitPath = path;
			m_Portrait.LoadImageFile(0, path);
			m_Portrait.SetImage(0);
		}
		m_Portrait.Show(true);
	}

	protected void ClearLines(int from)
	{
		for (int i = from; i < m_Lines.Count(); i++)
			m_Lines.Get(i).SetText("");
	}

	protected string AbilityKey(RDZ_AbilityDef a)
	{
		if (a.Input != "")
			return KeyOf(a.Input);
		if (a.Kind == RDZ_Kind.CRAWL)
			return "Jump at wall";
		return "Melee";
	}

	protected string KeyOf(string uaName)
	{
		return InputUtils.GetButtonNameFromInput(uaName, EInputDeviceType.MOUSE_AND_KEYBOARD);
	}

	// --- markers over AI heroes -------------------------------------------------------

	protected void UpdateMarkers(float dt, PlayerBase p)
	{
		m_ScanTimer -= dt;
		if (m_ScanTimer <= 0)
		{
			m_ScanTimer = 0.5;
			m_Hunters.Clear();
			array<Object> objects = new array<Object>;
			array<CargoBase> cargos = new array<CargoBase>;
			GetGame().GetObjectsAtPosition3D(p.GetPosition(), 400, objects, cargos);
			foreach (Object o : objects)
			{
				ZombieBase z = ZombieBase.Cast(o);
				if (z && z.IsAlive() && z.RDZ_IsHunter())
					m_Hunters.Insert(z);
			}
		}
		while (m_Markers.Count() < m_Hunters.Count())
			m_Markers.Insert(GetGame().GetWorkspace().CreateWidgets("RivalsDayZ/gui/layouts/rdz_marker.layout"));
		for (int i = 0; i < m_Markers.Count(); i++)
		{
			Widget w = m_Markers.Get(i);
			if (i >= m_Hunters.Count() || !m_Hunters.Get(i))
			{
				w.Show(false);
				continue;
			}
			ZombieBase h = m_Hunters.Get(i);
			vector screen = GetGame().GetScreenPos(h.GetPosition() + "0 2.3 0");
			if (screen[2] <= 0)
			{
				w.Show(false);
				continue;
			}
			RDZ_HeroDef hero = RDZ_Data.Heroes.Get(h.RDZ_GetHunterHero());
			TextWidget t = TextWidget.Cast(w);
			float dist = Math.Round(vector.Distance(p.GetPosition(), h.GetPosition()));
			t.SetText(RDZ_Content.HeroName(h.RDZ_GetHunterHero()) + "  " + dist.ToString() + "m");
			if (hero)
				t.SetColor(hero.Color);
			w.SetPos(screen[0] - 120, screen[1] - 12);
			w.Show(true);
		}
	}

	protected void HideMarkers()
	{
		foreach (Widget w : m_Markers)
			w.Show(false);
	}
}
