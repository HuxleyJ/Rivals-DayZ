// The spawn menu (default key J): one button per row of the spawn_menu sheet.

class RDZ_SpawnMenu extends UIScriptedMenu
{
	protected ref array<Widget> m_Buttons = new array<Widget>;
	protected ref array<string> m_Ids = new array<string>;

	override Widget Init()
	{
		RDZ_Data.Init();
		layoutRoot = GetGame().GetWorkspace().CreateWidgets("RivalsDayZ/gui/layouts/rdz_spawnmenu.layout");
		int row = 0;
		foreach (RDZ_MenuDef m : RDZ_Data.Menu)
		{
			Widget b = GetGame().GetWorkspace().CreateWidgets("RivalsDayZ/gui/layouts/rdz_menubutton.layout", layoutRoot);
			b.SetPos(30, 58 + row * 60);
			TextWidget label = TextWidget.Cast(b.FindAnyWidget("Label"));
			if (label)
				label.SetText(LabelOf(m));
			m_Buttons.Insert(b);
			m_Ids.Insert(m.Id);
			row++;
		}
		return layoutRoot;
	}

	protected string LabelOf(RDZ_MenuDef m)
	{
		if (m.Action == "give_kit")
			return RDZ_Content.HeroName(m.Hero) + " kit";
		if (m.Action == "spawn_hunter")
			return "Spawn AI " + RDZ_Content.HeroName(m.Hero);
		return m.Label;
	}

	override bool OnClick(Widget w, int x, int y, int button)
	{
		int idx = m_Buttons.Find(w);
		if (idx < 0 && w && w.GetParent())
			idx = m_Buttons.Find(w.GetParent());
		if (idx < 0)
			return false;
		PlayerBase p = PlayerBase.Cast(GetGame().GetPlayer());
		if (p)
			p.RDZ_RequestMenu(m_Ids.Get(idx));
		Close();
		return true;
	}

	override void OnShow()
	{
		super.OnShow();
		LockControls();
	}

	override void OnHide()
	{
		super.OnHide();
		UnlockControls();
	}

	override void Update(float timeslice)
	{
		super.Update(timeslice);
		if (GetUApi().GetInputByName(RDZ_Inputs.IN_SPAWN_MENU).LocalPress() || GetUApi().GetInputByName("UAUIBack").LocalPress())
			Close();
	}
}
