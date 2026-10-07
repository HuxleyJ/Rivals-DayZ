// Row types for the design sheets. RDZ_Data.Init() (generated) fills them.

class RDZ_AbilityDef
{
	string Id;
	string Hero;
	string RivalsMatch;
	int Kind;
	string Input;		// UA input name, empty for vanilla controls
	int Bit;
	float Cooldown;		// -1 = n/a
	float Range;
	float Damage;
	float Duration;
	float Speed;
	float HealPct;
	bool Stealable;
	string Icon;		// RDZ_AssetDef id
	string Sound;		// RDZ_AssetDef id
}

class RDZ_HeroDef
{
	string Id;
	string FallbackName;
	string RivalsMatch;
	int Color;
	string PowerItem;
	string Hunter;
	string Portrait;
	string VoiceSpawn;
}

class RDZ_ItemDef
{
	string Type;
	string Hero;
	string Slot;
	int PowerMask;
}

class RDZ_HunterDef
{
	string Id;
	string Hero;
	float Health;
	bool Swinger;
	int UsesMask;
	float AggroRange;
	float AbilityInterval;
	float Respawn;
	string Taunt;
	string Spawn;
	ref array<string> Classes;
}

class RDZ_MenuDef
{
	string Id;
	string Label;
	string Action;		// give_kit | spawn_hunter | clear_hunters
	string Hero;
	ref array<string> ExtraItems;
}

class RDZ_SpawnPointDef
{
	string Id;
	bool NearPlayer;
	string Settlement;
	float FallbackX;
	float FallbackZ;
	float RadiusMin;
	float RadiusMax;
}

class RDZ_AssetDef
{
	string Id;
	bool IsAudio;
	string Path;		// under the DayZ folder, no extension
	string SoundSet;	// audio only
}
