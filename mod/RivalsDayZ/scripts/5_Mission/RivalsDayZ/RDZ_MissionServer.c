// Hosted games: everyone starts on the Chernogorsk rooftop, both AI heroes start hunting
// when the first player arrives, and players get the host's invite line.

modded class MissionServer
{
	static bool s_RDZ_HasStart;
	static vector s_RDZ_Start;
	bool m_RDZ_HuntStarted;

	vector RDZ_StartPos()
	{
		if (!s_RDZ_HasStart)
		{
			RDZ_Data.Init();
			s_RDZ_Start = RDZ_Geo.SpawnPointPos(RDZ_Data.PLAYER_START, "0 0 0");
			s_RDZ_HasStart = true;
		}
		vector offset = Vector(Math.RandomFloat(-1.5, 1.5), 0, Math.RandomFloat(-1.5, 1.5));
		return s_RDZ_Start + offset;
	}

	override PlayerBase CreateCharacter(PlayerIdentity identity, vector pos, ParamsReadContext ctx, string characterName)
	{
		return super.CreateCharacter(identity, RDZ_StartPos(), ctx, characterName);
	}

	override void OnClientReadyEvent(PlayerIdentity identity, PlayerBase player)
	{
		super.OnClientReadyEvent(identity, player);
		if (!player)
			return;
		RDZ_Data.Init();
		string invite = RDZ_ReadInvite();
		if (invite != "")
			GetGame().RPCSingleParam(player, RDZ_Net.RPC_INVITE, new Param1<string>(invite), true, identity);
		player.RDZ_Toast("Open the spawn menu (J by default) to pick your hero. You're being hunted.");
		if (!m_RDZ_HuntStarted)
		{
			m_RDZ_HuntStarted = true;
			foreach (string heroId : RDZ_Data.HeroOrder)
			{
				RDZ_HunterDef def = RDZ_Lookup.HunterOfHero(heroId);
				if (def)
					RDZ_Hunters.Spawn(def, player.GetPosition());
			}
		}
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		RDZ_Server.Tick(timeslice);
	}

	//! The launcher writes the invite line into the server profile before starting the server.
	string RDZ_ReadInvite()
	{
		string path = "$profile:RivalsDayZ\\invite.txt";
		if (!FileExist(path))
			return "";
		FileHandle fh = OpenFile(path, FileMode.READ);
		if (fh == 0)
			return "";
		string line;
		FGets(fh, line);
		CloseFile(fh);
		return line;
	}
}
