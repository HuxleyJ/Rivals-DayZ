// Hooks the player's command handler: starts the web command when the server asked for one
// (sync juncture), keeps it running, and turns "jump at a tall wall" into wall-crawling
// for anyone with Spider-Man's crawl power. Runs identically on server and owning client.

modded class PlayerBase
{
	override bool ModCommandHandlerInside(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
	{
		if (super.ModCommandHandlerInside(pDt, pCurrentCommandID, pCurrentCommandFinished))
			return true;

		RDZ_WebCommand running = null;
		if (pCurrentCommandID == DayZPlayerConstants.COMMANDID_SCRIPT)
			running = RDZ_WebCommand.Cast(GetCommand_Script());

		if (m_RDZ_PendingWeb)
		{
			m_RDZ_PendingWeb = false;
			if (running)
			{
				running.Apply(m_RDZ_PendingMode, m_RDZ_PendingPoint, m_RDZ_PendingNormal, m_RDZ_PendingParam);
				return true;
			}
			if (RDZ_CanStartWeb(pCurrentCommandID))
			{
				RDZ_StartWeb(m_RDZ_PendingMode, m_RDZ_PendingPoint, m_RDZ_PendingNormal, m_RDZ_PendingParam);
				return true;
			}
		}

		if (running)
			return true;

		if (pCurrentCommandID == DayZPlayerConstants.COMMANDID_MOVE && RDZ_HasPower(RDZ_Kind.CRAWL))
		{
			HumanInputController hic = GetInputController();
			if (hic && hic.IsJumpClimb())
			{
				vector wallHit, wallNormal;
				if (RDZ_FindCrawlWall(hic.GetHeadingAngle(), wallHit, wallNormal))
				{
					RDZ_StartWeb(RDZ_Net.MODE_CRAWL, wallHit, wallNormal, 0);
					return true;
				}
			}
		}
		return false;
	}

	bool RDZ_CanStartWeb(int commandId)
	{
		if (!IsAlive() || IsUnconscious())
			return false;
		return commandId == DayZPlayerConstants.COMMANDID_MOVE || commandId == DayZPlayerConstants.COMMANDID_FALL || commandId == DayZPlayerConstants.COMMANDID_SCRIPT;
	}

	void RDZ_StartWeb(int mode, vector point, vector normal, float param)
	{
		RDZ_WebCommand cmd = new RDZ_WebCommand(this, RDZ_GetAnimTable(), mode, point, normal, param);
		StartCommand_Script(cmd);
	}

	//! A wall in front of the chest that is taller than CRAWL_MIN_WALL_HEIGHT.
	bool RDZ_FindCrawlWall(float heading, out vector hit, out vector normal)
	{
		RDZ_AbilityDef crawl = RDZ_Lookup.AbilityOfKind(RDZ_Kind.CRAWL, m_RDZ_Powers);
		if (!crawl)
			return false;
		vector dir = RDZ.HeadingToDir(heading);
		vector chest = GetPosition() + "0 1.2 0";
		Object o;
		if (!RDZ_Geo.Ray(chest, chest + dir * crawl.Range, this, hit, normal, o) || !RDZ_Geo.IsSteep(normal) || RDZ_Geo.IsCreature(o))
			return false;
		vector high = GetPosition() + Vector(0, RDZ_Move.CRAWL_MIN_WALL_HEIGHT, 0);
		vector h2, n2;
		Object o2;
		return RDZ_Geo.Ray(high, high + dir * (crawl.Range + 0.4), this, h2, n2, o2);
	}
}
