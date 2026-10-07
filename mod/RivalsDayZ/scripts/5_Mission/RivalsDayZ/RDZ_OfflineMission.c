// Solo: the offline hunt. Started by the mission folder's init.c
// (DayZ_x64.exe -mission=.\@RivalsDayZ\Missions\RivalsDayZ_Hunt.chernarusplus).
// No server, no loot economy: you start on a Chernogorsk rooftop, pick a hero, and the
// other hero comes for you.

class RDZ_OfflineMission extends MissionGameplay
{
	protected bool m_RDZ_AskedHero;
	protected float m_RDZ_AskDelay = 2.0;

	override void OnInit()
	{
		super.OnInit();
		RDZ_Data.Init();
		RDZ_Content.Load();
		GetGame().GetWorld().SetDate(2026, 7, 14, 17, 30);
		vector start = RDZ_Geo.SpawnPointPos(RDZ_Data.PLAYER_START, "0 0 0");
		PlayerBase p = PlayerBase.Cast(GetGame().CreatePlayer(null, GetGame().CreateRandomPlayer(), start, 0, "NONE"));
		if (!p)
		{
			RDZ.Log("Could not create the player.");
			return;
		}
		GetGame().SelectPlayer(null, p);
		RDZ.Log("Solo hunt started at " + start.ToString());
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		RDZ_Server.Tick(timeslice);
		if (m_RDZ_AskedHero || GetDayZGame().IsLoading() || !GetGame().GetPlayer())
			return;
		m_RDZ_AskDelay -= timeslice;
		if (m_RDZ_AskDelay > 0)
			return;
		m_RDZ_AskedHero = true;
		RDZ_ClientState.Toast("Pick your hero. The other one is coming for you.");
		RDZ_OpenSpawnMenu();
	}
}
