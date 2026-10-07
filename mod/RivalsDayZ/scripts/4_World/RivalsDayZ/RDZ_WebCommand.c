// Spider-Man movement as one scripted human command with internal states, so swinging,
// flying, crawling and zipping chain into each other without handing control back to
// vanilla (which would start a fall or a move command in between).
//
// It runs on the server and on the owning client from the same inputs (started together by
// a sync juncture, or by the jump input for crawling), like DayZ's own scripted commands.

class RDZ_AnimTable
{
	int CmdPose;
	int VarSpeed;

	void RDZ_AnimTable(Human human)
	{
		HumanAnimInterface hai = human.GetAnimInterface();
		CmdPose = hai.BindCommand(RDZ_Move.ANIM_POSE_COMMAND);
		VarSpeed = hai.BindVariableFloat(RDZ_Move.ANIM_SPEED_VAR);
	}
}

class RDZ_WebCommand : HumanCommandScript
{
	static const int ST_SWING = 1;
	static const int ST_AIR = 2;
	static const int ST_CRAWL = 3;
	static const int ST_ZIP = 4;
	static const int ST_HELD = 5;


	PlayerBase m_Player;
	HumanInputController m_Input;
	RDZ_AnimTable m_Anim;

	int m_State;
	vector m_Pos;
	vector m_Vel;
	vector m_Anchor;
	float m_RopeLen;
	vector m_Target;
	vector m_Normal;
	float m_Timer;
	float m_Speed;
	bool m_CanCrawl;
	bool m_Finish;
	bool m_FinishSent;
	bool m_LastJump;
	float m_CrawlSpeed;
	vector HAND_OFFSET;		// where the web attaches, relative to the feet
	vector CHEST_OFFSET;

	void RDZ_WebCommand(PlayerBase player, RDZ_AnimTable anim, int mode, vector point, vector normal, float param)
	{
		m_Player = player;
		m_Anim = anim;
		HAND_OFFSET = "0 1.9 0";
		CHEST_OFFSET = "0 1.2 0";
		m_CrawlSpeed = 2.6;
		RDZ_AbilityDef crawl = RDZ_Lookup.AbilityOfKind(RDZ_Kind.CRAWL, 0x7FFFFFFF);
		if (crawl && crawl.Speed > 0)
			m_CrawlSpeed = crawl.Speed;
		m_Input = player.GetInputController();
		m_Pos = player.GetPosition();
		player.PhysicsGetVelocity(m_Vel);
		m_LastJump = true;	// ignore the jump press that may have started us
		Apply(mode, point, normal, param);
	}

	//! Switch mode while running (new web while flying, juncture during a crawl, ...).
	void Apply(int mode, vector point, vector normal, float param)
	{
		m_CanCrawl = m_Player.RDZ_HasPower(RDZ_Kind.CRAWL);
		switch (mode)
		{
			case RDZ_Net.MODE_SWING:
				m_State = ST_SWING;
				m_Anchor = point;
				m_RopeLen = vector.Distance(m_Pos + HAND_OFFSET, point);
				break;
			case RDZ_Net.MODE_ZIP:
			case RDZ_Net.MODE_PULLED:
				m_State = ST_ZIP;
				m_Target = point;
				m_Normal = normal;
				m_Speed = param;
				if (mode == RDZ_Net.MODE_PULLED)
					m_CanCrawl = false;
				break;
			case RDZ_Net.MODE_CRAWL:
				EnterCrawl(point, normal);
				break;
			case RDZ_Net.MODE_HELD:
				m_State = ST_HELD;
				m_Timer = param;
				m_Vel = vector.Zero;
				break;
		}
	}

	bool IsSwinging()
	{
		return m_State == ST_SWING;
	}

	vector GetAnchor()
	{
		return m_Anchor;
	}

	override void OnActivate()
	{
		dBodyEnableGravity(m_Player, false);
		PreAnim_CallCommand(m_Anim.CmdPose, 1, 1);
	}

	override void OnDeactivate()
	{
		dBodyEnableGravity(m_Player, true);
		m_Player.RDZ_OnWebCommandEnded();
	}

	override void PreAnimUpdate(float pDt)
	{
		if (m_Finish)
		{
			PreAnim_CallCommand(m_Anim.CmdPose, 0, 0);
			return;
		}

		float stroke = RDZ_Move.ANIM_SWING_STROKE;
		if (m_State == ST_CRAWL)
		{
			float spd;
			vector dir;
			m_Input.GetMovement(spd, dir);
			if (spd > 0)
				stroke = RDZ_Move.ANIM_CRAWL_STROKE;
			else
				stroke = 0;
		}
		PreAnim_SetFloat(m_Anim.VarSpeed, stroke);

		if (m_State == ST_CRAWL)
			SetHeading(RDZ.DirToHeading(m_Normal * -1), 0.1, 10);
		else if (m_State != ST_HELD)
			SetHeading(m_Input.GetHeadingAngle(), 0.15, 8);
	}

	override void PrePhysUpdate(float pDt)
	{
		// Movement is ours; cancel the animation's root motion.
		PrePhys_SetTranslation(vector.Zero);
	}

	override bool PostPhysUpdate(float pDt)
	{
		if (m_Finish)
		{
			if (m_FinishSent)
				return false;
			m_FinishSent = true;
			return true;
		}

		if (pDt <= 0)
			return true;
		if (pDt > 0.1)
			pDt = 0.1;

		bool jump = m_Input.IsJumpClimb();
		bool jumpPressed = jump && !m_LastJump;
		m_LastJump = jump;

		switch (m_State)
		{
			case ST_SWING:
				UpdateSwing(pDt, jumpPressed);
				break;
			case ST_AIR:
				UpdateAir(pDt);
				break;
			case ST_CRAWL:
				UpdateCrawl(pDt, jumpPressed);
				break;
			case ST_ZIP:
				UpdateZip(pDt);
				break;
			case ST_HELD:
				m_Timer -= pDt;
				if (m_Timer <= 0)
					GoAirOrFinish();
				break;
		}

		PostPhys_SetPosition(m_Pos);
		if (m_State == ST_CRAWL && RDZ_Move.CRAWL_TILT_DEG != 0)
		{
			vector facing = m_Normal * -1;
			vector yaw = facing.VectorToAngles();
			vector mat[3];
			Math3D.YawPitchRollMatrix(Vector(yaw[0], RDZ_Move.CRAWL_TILT_DEG, 0), mat);
			float q[4];
			Math3D.MatrixToQuat(mat, q);
			PostPhys_SetRotation(q);
		}
		return true;
	}

	// --- states ------------------------------------------------------------

	protected void UpdateSwing(float dt, bool jumpPressed)
	{
		if (jumpPressed)
		{
			m_Vel = m_Vel + Vector(0, RDZ_Move.SWING_RELEASE_UP, 0);
			m_State = ST_AIR;
			return;
		}

		float spd;
		vector local;
		m_Input.GetMovement(spd, local);
		float heading = m_Input.GetHeadingAngle();
		vector fwd = RDZ.HeadingToDir(heading);

		vector accel = Vector(0, -RDZ_Move.SWING_GRAVITY, 0);
		if (local[2] > 0.1)
		{
			// Pump along the current swing direction (or forward when nearly still).
			vector along = RDZ.Flat(m_Vel);
			if (along.Length() < 0.5)
				along = fwd;
			accel = accel + along.Normalized() * (RDZ_Move.SWING_PUMP_ACCEL * local[2]);
		}
		if (spd >= 3)
			m_RopeLen = Math.Max(2.0, m_RopeLen - RDZ_Move.SWING_REEL_SPEED * dt);

		m_Vel = m_Vel + accel * dt;
		vector next = m_Pos + m_Vel * dt;

		// Rope constraint on the hands.
		vector hands = next + HAND_OFFSET;
		vector fromAnchor = hands - m_Anchor;
		float len = fromAnchor.Length();
		if (len > m_RopeLen && len > 0.01)
		{
			vector n = fromAnchor.Normalized();
			hands = m_Anchor + n * m_RopeLen;
			next = hands - HAND_OFFSET;
			float radial = vector.Dot(m_Vel, n);
			if (radial > 0)
				m_Vel = m_Vel - n * radial;
		}
		ClampSpeed();
		MoveWithCollision(next);
	}

	protected void UpdateAir(float dt)
	{
		float spd;
		vector local;
		m_Input.GetMovement(spd, local);
		float heading = m_Input.GetHeadingAngle();
		vector steer = RDZ.HeadingToDir(heading) * local[2] + RDZ.HeadingToRight(heading) * local[0];
		m_Vel = m_Vel + (steer * RDZ_Move.AIR_STEER_ACCEL + Vector(0, -RDZ_Move.AIR_GRAVITY, 0)) * dt;
		ClampSpeed();
		MoveWithCollision(m_Pos + m_Vel * dt);
	}

	protected void UpdateZip(float dt)
	{
		vector toTarget = m_Target - m_Pos;
		float dist = toTarget.Length();
		if (dist <= RDZ_Move.ZIP_ARRIVE_DIST)
		{
			ArriveFromZip(toTarget);
			return;
		}
		vector dir = toTarget.Normalized();
		m_Vel = dir * m_Speed;
		float stepLen = m_Speed * dt;
		if (stepLen >= dist - RDZ_Move.ZIP_ARRIVE_DIST)
		{
			MoveWithCollision(m_Target - dir * RDZ_Move.ZIP_ARRIVE_DIST);
			if (m_State == ST_ZIP)
				ArriveFromZip(toTarget);
			return;
		}
		MoveWithCollision(m_Pos + m_Vel * dt);
	}

	protected void ArriveFromZip(vector dir)
	{
		if (m_CanCrawl && RDZ_Geo.IsSteep(m_Normal) && m_Normal.Length() > 0.5)
		{
			EnterCrawl(m_Target, m_Normal);
			return;
		}
		m_Vel = dir.Normalized() * (m_Speed * 0.25);
		m_State = ST_AIR;
	}

	protected void EnterCrawl(vector wallPoint, vector normal)
	{
		m_State = ST_CRAWL;
		m_Normal = RDZ.Flat(normal).Normalized();
		m_Vel = vector.Zero;
		m_Pos = wallPoint + m_Normal * RDZ_Move.CRAWL_WALL_OFFSET - CHEST_OFFSET;
	}

	protected void UpdateCrawl(float dt, bool jumpPressed)
	{
		if (jumpPressed)
		{
			m_Vel = m_Normal * RDZ_Move.CRAWL_JUMP_OFF_SPEED + Vector(0, RDZ_Move.CRAWL_JUMP_OFF_SPEED * 0.6, 0);
			m_State = ST_AIR;
			return;
		}

		float spd;
		vector local;
		m_Input.GetMovement(spd, local);
		vector up = "0 1 0";
		vector side = Vector(m_Normal[2], 0, -m_Normal[0]);	// up x normal: horizontal along the wall (points to our left)
		vector move = (up * local[2] - side * local[0]) * (m_CrawlSpeed * dt);
		vector next = m_Pos + move;

		// Stay stuck: find the wall again from in front of the chest.
		vector chest = next + CHEST_OFFSET;
		vector hit, n;
		Object o;
		if (RDZ_Geo.Ray(chest + m_Normal * 0.4, chest - m_Normal * 1.6, m_Player, hit, n, o) && RDZ_Geo.IsSteep(n))
		{
			m_Normal = RDZ.Flat(n).Normalized();
			m_Pos = hit + m_Normal * RDZ_Move.CRAWL_WALL_OFFSET - CHEST_OFFSET;
			if (local[2] < -0.1 && m_Pos[1] - RDZ_Geo.GroundY(m_Pos, m_Player) < 0.2)
				Finish();	// crawled down to the ground
			return;
		}

		if (local[2] > 0.1)
		{
			// Ran out of wall going up: climb over the top edge if there is a roof.
			vector over = next + CHEST_OFFSET - m_Normal * 0.8 + "0 1.5 0";
			if (RDZ_Geo.Ray(over, over - "0 3 0", m_Player, hit, n, o) && RDZ_Geo.IsFloor(n))
			{
				m_Pos = hit + "0 0.05 0";
				Finish();
				return;
			}
		}
		// Lost the wall: let go.
		m_Vel = vector.Zero;
		m_State = ST_AIR;
	}

	// --- helpers -------------------------------------------------------------

	protected void ClampSpeed()
	{
		float s = m_Vel.Length();
		if (s > RDZ_Move.SWING_MAX_SPEED)
			m_Vel = m_Vel * (RDZ_Move.SWING_MAX_SPEED / s);
	}

	//! Move the body toward next, stopping at walls (crawl onto them if we can) and floors (land).
	protected void MoveWithCollision(vector next)
	{
		vector from = m_Pos + CHEST_OFFSET;
		vector to = next + CHEST_OFFSET;
		vector delta = to - from;
		float dist = delta.Length();
		vector hit, n;
		Object o;
		if (dist > 0.001)
		{
			vector dir = delta * (1.0 / dist);
			if (RDZ_Geo.Ray(from, to + dir * RDZ_Move.BODY_RADIUS, m_Player, hit, n, o) && !RDZ_Geo.IsCreature(o))
			{
				if (RDZ_Geo.IsSteep(n))
				{
					if (m_CanCrawl && m_State != ST_ZIP)
					{
						EnterCrawl(hit, n);
						return;
					}
					// Slide along the wall.
					vector nf = RDZ.Flat(n).Normalized();
					m_Vel = m_Vel - nf * vector.Dot(m_Vel, nf);
					next = hit + nf * RDZ_Move.BODY_RADIUS - CHEST_OFFSET;
				}
				else if (n[1] < 0)
				{
					// Ceiling.
					if (m_Vel[1] > 0)
						m_Vel[1] = 0;
					next = m_Pos;
				}
			}
		}

		// Feet: land on floors and terrain.
		float groundY = RDZ_Geo.GroundY(next + "0 0.9 0", m_Player);
		if (next[1] <= groundY + 0.02 && m_Vel[1] <= 0 && m_State != ST_SWING)
		{
			next[1] = groundY;
			m_Pos = next;
			Finish();
			return;
		}
		if (next[1] < groundY)
		{
			next[1] = groundY;
			if (m_Vel[1] < 0)
				m_Vel[1] = 0;
		}
		m_Pos = next;
	}

	protected void GoAirOrFinish()
	{
		if (m_Pos[1] - RDZ_Geo.GroundY(m_Pos, m_Player) > 0.3)
		{
			m_State = ST_AIR;
			return;
		}
		Finish();
	}

	protected void Finish()
	{
		m_Finish = true;
		m_Vel = vector.Zero;
	}
}
