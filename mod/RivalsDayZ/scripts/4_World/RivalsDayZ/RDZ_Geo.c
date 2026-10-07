// World queries: raycasts for webs, walls and landings; rooftop finder for spawns.

class RDZ_Geo
{
	static bool Ray(vector from, vector to, Object ignore, out vector hitPos, out vector hitNormal, out Object hitObj, float radius = 0)
	{
		int component;
		set<Object> results = new set<Object>;
		hitObj = null;
		if (DayZPhysics.RaycastRV(from, to, hitPos, hitNormal, component, results, null, ignore, true, false, ObjIntersectGeom, radius))
		{
			if (results.Count() > 0)
				hitObj = results.Get(0);
			return true;
		}
		return false;
	}

	static bool IsCreature(Object o)
	{
		if (!o)
			return false;
		return o.IsInherited(ZombieBase) || o.IsInherited(PlayerBase) || o.IsInherited(AnimalBase);
	}

	static bool IsSteep(vector normal)
	{
		return Math.AbsFloat(normal[1]) < 0.5;
	}

	static bool IsFloor(vector normal)
	{
		return normal[1] > 0.7;
	}

	//! Height of whatever is under p (building floor or terrain).
	static float GroundY(vector p, Object ignore)
	{
		vector hit, n;
		Object o;
		if (Ray(p + "0 0.5 0", p - "0 60 0", ignore, hit, n, o))
			return hit[1];
		return GetGame().SurfaceY(p[0], p[2]);
	}

	//! Position of a named settlement from CfgWorlds <world> Names, or the fallback.
	static vector SettlementPos(string settlement, float fx, float fz)
	{
		string world = GetGame().GetWorldName();
		string path = "CfgWorlds " + world + " Names";
		int count = GetGame().ConfigGetChildrenCount(path);
		for (int i = 0; i < count; i++)
		{
			string child;
			GetGame().ConfigGetChildName(path, i, child);
			string label;
			GetGame().ConfigGetText(path + " " + child + " name", label);
			if (label == settlement)
			{
				TFloatArray xy = new TFloatArray;
				GetGame().ConfigGetFloatArray(path + " " + child + " position", xy);
				if (xy.Count() >= 2)
				{
					RDZ.Log("Found " + settlement + " in CfgWorlds at " + xy[0] + ", " + xy[1]);
					return Vector(xy[0], 0, xy[1]);
				}
			}
		}
		RDZ.Log(settlement + " not found in " + path + "; using sheet fallback " + fx + ", " + fz);
		return Vector(fx, 0, fz);
	}

	//! Highest flat roof with headroom within [rMin, rMax] of center. Falls back to the ground at center.
	static vector FindRooftop(vector center, float rMin, float rMax, Object ignore = null)
	{
		vector best;
		bool found = false;
		float step = 7.0;
		for (float dx = -rMax; dx <= rMax; dx += step)
		{
			for (float dz = -rMax; dz <= rMax; dz += step)
			{
				float d = Math.Sqrt(dx * dx + dz * dz);
				if (d > rMax || d < rMin)
					continue;
				float x = center[0] + dx;
				float z = center[2] + dz;
				vector hit, n;
				Object o;
				if (!Ray(Vector(x, 700, z), Vector(x, -10, z), ignore, hit, n, o))
					continue;
				if (!o || !o.IsInherited(Building) || !IsFloor(n))
					continue;
				if (hit[1] - GetGame().SurfaceY(x, z) < 5)
					continue;	// not really a rooftop
				vector h2, n2;
				Object o2;
				if (Ray(hit + "0 0.2 0", hit + "0 2.6 0", ignore, h2, n2, o2))
					continue;	// no headroom
				if (!found || hit[1] > best[1])
				{
					best = hit;
					found = true;
				}
			}
		}
		if (found)
			return best + "0 0.05 0";
		RDZ.Log("No rooftop found near " + center.ToString() + "; spawning on the ground.");
		return Vector(center[0], GetGame().SurfaceY(center[0], center[2]), center[2]);
	}

	static vector SpawnPointPos(string spawnId, vector nearPos)
	{
		RDZ_Data.Init();
		RDZ_SpawnPointDef sp = RDZ_Data.SpawnPoints.Get(spawnId);
		if (!sp)
			return nearPos;
		vector center = nearPos;
		if (!sp.NearPlayer)
			center = SettlementPos(sp.Settlement, sp.FallbackX, sp.FallbackZ);
		return FindRooftop(center, sp.RadiusMin, sp.RadiusMax);
	}
}
