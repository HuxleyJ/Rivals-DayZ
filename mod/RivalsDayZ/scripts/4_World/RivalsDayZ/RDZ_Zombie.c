// Infected bodies used as AI heroes, plus webbed / yanked infected.

//! Holds an infected in place (webbed).
class RDZ_InfectedHold : DayZInfectedCommandScript
{
	float m_Left;

	// Same constructor signature as DayZInfectedCommandScript; set m_Left before starting.
	void RDZ_InfectedHold(DayZInfected pInfected)
	{
	}

	override void PrePhysUpdate(float pDt)
	{
		PrePhys_SetTranslation(vector.Zero);
	}

	override bool PostPhysUpdate(float pDt)
	{
		m_Left -= pDt;
		return m_Left > 0;
	}
}

//! Moves an infected along an arc (AI Spider-Man's web hops, Get Over Here! on infected).
class RDZ_InfectedFly : DayZInfectedCommandScript
{
	DayZInfected m_Body;
	vector m_From;
	vector m_To;
	float m_Time;
	float m_Arc;
	float m_T;

	// Same constructor signature as DayZInfectedCommandScript; call Setup before starting.
	void RDZ_InfectedFly(DayZInfected pInfected)
	{
		m_Body = pInfected;
	}

	void Setup(vector to, float time, float arc)
	{
		m_From = m_Body.GetPosition();
		m_To = to;
		m_Time = Math.Max(time, 0.1);
		m_Arc = arc;
	}

	override void PrePhysUpdate(float pDt)
	{
		PrePhys_SetTranslation(vector.Zero);
	}

	override bool PostPhysUpdate(float pDt)
	{
		m_T += pDt / m_Time;
		float t = Math.Min(m_T, 1.0);
		vector p = m_From + (m_To - m_From) * t;
		p[1] = p[1] + Math.Sin(t * Math.PI) * m_Arc;
		PostPhys_SetPosition(p);
		return m_T < 1.0;
	}
}

modded class ZombieBase
{
	int m_RDZ_HeroIndex;		// synced: 0 = plain infected, else 1 + index in RDZ_Data.HeroOrder
	string m_RDZ_HunterId;		// server
	int m_RDZ_BaseMask;			// server: the hero's own powers
	int m_RDZ_MimicMask;
	float m_RDZ_MimicUntil;
	int m_RDZ_DrainedMask;
	float m_RDZ_DrainedUntil;
	float m_RDZ_ExtraHealth;	// server: toughness pool on top of the body's own health

	bool m_RDZ_PendingHold;
	float m_RDZ_HoldTime;
	bool m_RDZ_PendingFly;
	vector m_RDZ_FlyTo;
	float m_RDZ_FlyTime;
	float m_RDZ_FlyArc;

	override void Init()
	{
		super.Init();
		RegisterNetSyncVariableInt("m_RDZ_HeroIndex", 0, 8);
	}

	void RDZ_MakeHunter(RDZ_HunterDef def)
	{
		RDZ_Data.Init();
		m_RDZ_HunterId = def.Id;
		m_RDZ_BaseMask = def.UsesMask;
		m_RDZ_ExtraHealth = Math.Max(0, def.Health - GetHealth("", "Health"));
		m_RDZ_HeroIndex = RDZ_Data.HeroOrder.Find(def.Hero) + 1;
		SetSynchDirty();
	}

	string RDZ_GetHunterHero()
	{
		RDZ_Data.Init();
		if (m_RDZ_HeroIndex <= 0 || m_RDZ_HeroIndex > RDZ_Data.HeroOrder.Count())
			return "";
		return RDZ_Data.HeroOrder.Get(m_RDZ_HeroIndex - 1);
	}

	bool RDZ_IsHunter()
	{
		return m_RDZ_HeroIndex > 0;
	}

	int RDZ_GetPowers()
	{
		if (!RDZ_IsHunter())
			return 0;
		float now = RDZ.Now();
		int mimic = 0;
		if (now < m_RDZ_MimicUntil)
			mimic = m_RDZ_MimicMask;
		int drained = 0;
		if (now < m_RDZ_DrainedUntil)
			drained = m_RDZ_DrainedMask;
		return (m_RDZ_BaseMask & ~drained) | mimic;
	}

	void RDZ_GainMimic(int mask, float duration)
	{
		m_RDZ_MimicMask = mask;
		m_RDZ_MimicUntil = RDZ.Now() + duration;
	}

	void RDZ_LoseToDrain(int mask, float duration)
	{
		m_RDZ_DrainedMask = mask;
		m_RDZ_DrainedUntil = RDZ.Now() + duration;
	}

	void RDZ_Hold(float duration)
	{
		m_RDZ_PendingHold = true;
		m_RDZ_HoldTime = duration;
	}

	void RDZ_Fly(vector to, float time, float arc)
	{
		m_RDZ_PendingFly = true;
		m_RDZ_FlyTo = to;
		m_RDZ_FlyTime = time;
		m_RDZ_FlyArc = arc;
	}

	bool RDZ_IsBusy()
	{
		return m_RDZ_PendingFly || m_RDZ_PendingHold || GetCommand_Script() != null;
	}

	override bool ModCommandHandlerBefore(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
	{
		if (super.ModCommandHandlerBefore(pDt, pCurrentCommandID, pCurrentCommandFinished))
			return true;
		if (!IsAlive())
			return false;
		if (m_RDZ_PendingHold)
		{
			m_RDZ_PendingHold = false;
			RDZ_InfectedHold hold = new RDZ_InfectedHold(this);
			hold.m_Left = m_RDZ_HoldTime;
			StartCommand_Script(hold);
			return true;
		}
		if (m_RDZ_PendingFly)
		{
			m_RDZ_PendingFly = false;
			RDZ_InfectedFly fly = new RDZ_InfectedFly(this);
			fly.Setup(m_RDZ_FlyTo, m_RDZ_FlyTime, m_RDZ_FlyArc);
			StartCommand_Script(fly);
			return true;
		}
		if (pCurrentCommandID == DayZInfectedConstants.COMMANDID_SCRIPT && !pCurrentCommandFinished)
			return true;
		return false;
	}

	override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef)
	{
		super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
		if (!RDZ.IsAuthority())
			return;
		if (m_RDZ_ExtraHealth > 0 && IsAlive() && damageResult)
		{
			float dealt = damageResult.GetDamage("", "Health");
			float refund = Math.Min(dealt, m_RDZ_ExtraHealth);
			m_RDZ_ExtraHealth -= refund;
			AddHealth("", "Health", refund);
		}
		if (damageType == DamageType.CLOSE_COMBAT)
			RDZ_Abilities.OnMeleeHit(this, source, damageResult);
	}

	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		if (rpc_type == RDZ_Net.RPC_FX)
		{
			Param1<string> fx = new Param1<string>("");
			if (ctx.Read(fx))
				RDZ_Fx.PlayLocal(this, fx.param1);
			return;
		}
		super.OnRPC(sender, rpc_type, ctx);
	}
}
