// Shared constants, helpers and the Rivals content loaded from the player's own install.

class RDZ_Net
{
	// Client -> server ability request (ScriptInputUserData). Vanilla INPUT_UDT_* stop at ~20.
	static const int UDT_ABILITY = 7738;
	// Sync juncture: server starts a web movement on the server and the owning client together.
	static const int SJ_WEB = 7739;

	static const int RPC_COOLDOWN = 7738101;	// server -> owner: Param2<int bit, float secondsLeft>
	static const int RPC_FX = 7738102;			// server -> clients: Param1<string assetId> on an object
	static const int RPC_MENU = 7738103;		// client -> server: Param1<string menuId>
	static const int RPC_TOAST = 7738104;		// server -> owner: Param1<string text>
	static const int RPC_INVITE = 7738105;		// server -> owner: Param1<string inviteText>

	// Web movement modes carried in SJ_WEB.
	static const int MODE_SWING = 1;
	static const int MODE_ZIP = 2;
	static const int MODE_CRAWL = 3;
	static const int MODE_HELD = 4;		// webbed by Web-Cluster
	static const int MODE_PULLED = 5;	// yanked by Get Over Here! / area drain
}

class RDZ
{
	static bool IsAuthority()
	{
		return GetGame().IsServer() || !GetGame().IsMultiplayer();
	}

	static bool HasUI()
	{
		return !GetGame().IsDedicatedServer();
	}

	static void Log(string msg)
	{
		Print("[RivalsDayZ] " + msg);
	}

	static float Now()
	{
		return GetGame().GetTickTime();
	}

	//! Facing direction for a HumanInputController heading (radians, negative of yaw).
	static vector HeadingToDir(float heading)
	{
		return Vector(-Math.Sin(heading), 0, Math.Cos(heading));
	}

	static vector HeadingToRight(float heading)
	{
		return Vector(Math.Cos(heading), 0, Math.Sin(heading));
	}

	//! Heading (radians) that faces along dir.
	static float DirToHeading(vector dir)
	{
		float yaw = dir.VectorToAngles()[0];
		float h = -yaw * Math.DEG2RAD;
		while (h > Math.PI)
			h -= Math.PI2;
		while (h < -Math.PI)
			h += Math.PI2;
		return h;
	}

	static vector Flat(vector v)
	{
		return Vector(v[0], 0, v[2]);
	}

	static int BitCount(int mask)
	{
		int n = 0;
		for (int i = 0; i < 31; i++)
		{
			if (mask & (1 << i))
				n++;
		}
		return n;
	}
}

// ---------------------------------------------------------------------------
// content.json is written by the launcher from the player's Marvel Rivals install.
// Missing file or missing entries -> the sheet's fallback names are used.

class RDZ_ContentText
{
	string name;
	string description;
}

class RDZ_ContentFile
{
	string rivalsBuild;
	ref map<string, ref RDZ_ContentText> heroes;
	ref map<string, ref RDZ_ContentText> abilities;
	ref map<string, string> assets;		// asset id -> file path actually written (with extension)
}

class RDZ_Content
{
	static ref RDZ_ContentFile s_File;
	static bool s_Loaded;

	static void Load()
	{
		if (s_Loaded)
			return;
		s_Loaded = true;
		RDZ_Data.Init();

		array<string> paths = {"$CurrentDir:RivalsDayZ_Rivals\\content.json", "$profile:RivalsDayZ\\content.json"};
		foreach (string path : paths)
		{
			if (!FileExist(path))
				continue;
			RDZ_ContentFile data;
			string err;
			if (JsonFileLoader<RDZ_ContentFile>.LoadFile(path, data, err) && data)
			{
				s_File = data;
				RDZ.Log("Loaded Marvel Rivals content from " + path + " (Rivals build " + data.rivalsBuild + ")");
				return;
			}
			RDZ.Log("Could not read " + path + ": " + err);
		}
		RDZ.Log("No Marvel Rivals content found; using built-in names. Start the game from Melty or RivalsDayZ.exe so it can read your Rivals install.");
	}

	static bool HasRivals()
	{
		Load();
		return s_File != null;
	}

	static string HeroName(string heroId)
	{
		Load();
		RDZ_ContentText t;
		if (s_File && s_File.heroes && s_File.heroes.Find(heroId, t) && t && t.name != "")
			return t.name;
		RDZ_HeroDef h = RDZ_Data.Heroes.Get(heroId);
		if (h)
			return h.FallbackName;
		return heroId;
	}

	static string AbilityName(string abilityId)
	{
		Load();
		RDZ_ContentText t;
		if (s_File && s_File.abilities && s_File.abilities.Find(abilityId, t) && t && t.name != "")
			return t.name;
		RDZ_AbilityDef a = RDZ_Data.Abilities.Get(abilityId);
		if (a)
			return a.RivalsMatch;
		return abilityId;
	}

	static string AbilityDescription(string abilityId)
	{
		Load();
		RDZ_ContentText t;
		if (s_File && s_File.abilities && s_File.abilities.Find(abilityId, t) && t)
			return t.description;
		return "";
	}

	//! Path of a converted image/audio file, or "" when the launcher could not produce it.
	static string AssetPath(string assetId)
	{
		Load();
		string p;
		if (s_File && s_File.assets && s_File.assets.Find(assetId, p))
			return p;
		return "";
	}
}

// ---------------------------------------------------------------------------
// Lookups over the generated sheet data.

class RDZ_Lookup
{
	static RDZ_AbilityDef AbilityByBit(int bit)
	{
		RDZ_Data.Init();
		foreach (string id : RDZ_Data.AbilityOrder)
		{
			RDZ_AbilityDef a = RDZ_Data.Abilities.Get(id);
			if (a.Bit == bit)
				return a;
		}
		return null;
	}

	static RDZ_AbilityDef AbilityOfKind(int kind, int mask)
	{
		RDZ_Data.Init();
		foreach (string id : RDZ_Data.AbilityOrder)
		{
			RDZ_AbilityDef a = RDZ_Data.Abilities.Get(id);
			if (a.Kind == kind && (mask & a.Bit))
				return a;
		}
		return null;
	}

	static int StealableMask(int mask)
	{
		RDZ_Data.Init();
		int m = 0;
		foreach (string id : RDZ_Data.AbilityOrder)
		{
			RDZ_AbilityDef a = RDZ_Data.Abilities.Get(id);
			if (a.Stealable && (mask & a.Bit))
				m |= a.Bit;
		}
		return m;
	}

	static int ItemMask(string type)
	{
		RDZ_Data.Init();
		RDZ_ItemDef it = RDZ_Data.Items.Get(type);
		if (it)
			return it.PowerMask;
		return 0;
	}

	static RDZ_HeroDef HeroOfMask(int mask)
	{
		RDZ_Data.Init();
		foreach (string hid : RDZ_Data.HeroOrder)
		{
			RDZ_HeroDef h = RDZ_Data.Heroes.Get(hid);
			if (ItemMask(h.PowerItem) & mask)
				return h;
		}
		return null;
	}

	static RDZ_HunterDef HunterOfHero(string heroId)
	{
		RDZ_Data.Init();
		RDZ_HeroDef h = RDZ_Data.Heroes.Get(heroId);
		if (h)
			return RDZ_Data.Hunters.Get(h.Hunter);
		return null;
	}

	static string SoundSetOf(string assetId)
	{
		RDZ_Data.Init();
		RDZ_AssetDef as = RDZ_Data.Assets.Get(assetId);
		if (as && as.IsAudio)
			return as.SoundSet;
		return "";
	}
}
