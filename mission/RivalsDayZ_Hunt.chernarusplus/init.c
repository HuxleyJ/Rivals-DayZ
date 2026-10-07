// Rivals x DayZ - solo hunt. Everything lives in the @RivalsDayZ mod; this file only
// tells DayZ which mission class to run offline.

void main()
{
}

Mission CreateCustomMission(string path)
{
	return new RDZ_OfflineMission();
}
