// Rivals x DayZ - hosted game mission (copied over the vanilla dayzOffline.chernarusplus
// mission by the launcher into mpmissions\rivalsdayz.chernarusplus). The mashup itself
// lives in the @RivalsDayZ mod (modded MissionServer: rooftop spawns, AI heroes, invites).

void main()
{
	// Central economy (loot and infected across the map) from the vanilla mission files.
	Hive ce = CreateHive();
	if (ce)
		ce.InitOffline();

	// Late afternoon light for the hunt.
	GetGame().GetWorld().SetDate(2026, 7, 14, 17, 30);
}

class CustomMission : MissionServer
{
}

Mission CreateCustomMission(string path)
{
	return new CustomMission();
}
