using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace RivalsDayZ.Launcher;

//! Finds DayZ, the DayZ Server tool and Marvel Rivals. Melty passes DayZ and Rivals folders;
//! Steam's library folders are the fallback (and the only source for DayZ Server).
internal sealed partial class GamePaths
{
    public const int RivalsAppId = 2767030;

    public string? DayZ;
    public string? DayZServer;
    public string? Rivals;
    public List<string> SteamLibraries = [];

    public static GamePaths Resolve(Options opt)
    {
        var p = new GamePaths { SteamLibraries = FindSteamLibraries() };
        p.DayZ = FirstExisting(opt.DayZ, ParentWith(Program.ModDir, "DayZ_x64.exe"), p.FindApp(Sheets.DAYZ_APPID), "DayZ_x64.exe");
        p.Rivals = FirstDir(opt.Rivals, p.FindApp(RivalsAppId));
        p.DayZServer = FirstExisting(null, null, p.FindApp(Sheets.DAYZ_SERVER_APPID), "DayZServer_x64.exe");
        return p;
    }

    public void RefreshServer() => DayZServer = FirstExisting(null, null, FindApp(Sheets.DAYZ_SERVER_APPID), "DayZServer_x64.exe");

    private static string? FirstExisting(string? a, string? b, string? c, string exe)
    {
        foreach (var d in new[] { a, b, c })
            if (d is not null && File.Exists(Path.Combine(d, exe)))
                return Path.GetFullPath(d);
        return null;
    }

    private static string? FirstDir(params string?[] dirs) => dirs.FirstOrDefault(d => d is not null && Directory.Exists(d));

    private static string? ParentWith(string dir, string file)
    {
        var parent = Directory.GetParent(dir)?.FullName;
        return parent is not null && File.Exists(Path.Combine(parent, file)) ? parent : null;
    }

    //! steamapps/common/<installdir> for an installed app, from its appmanifest.
    public string? FindApp(int appId)
    {
        foreach (var lib in SteamLibraries)
        {
            var manifest = Path.Combine(lib, "steamapps", $"appmanifest_{appId}.acf");
            if (!File.Exists(manifest))
                continue;
            var m = InstallDirRegex().Match(File.ReadAllText(manifest));
            if (m.Success)
            {
                var dir = Path.Combine(lib, "steamapps", "common", m.Groups[1].Value);
                if (Directory.Exists(dir))
                    return dir;
            }
        }
        return null;
    }

    public string? BuildId(int appId)
    {
        foreach (var lib in SteamLibraries)
        {
            var manifest = Path.Combine(lib, "steamapps", $"appmanifest_{appId}.acf");
            if (File.Exists(manifest) && BuildIdRegex().Match(File.ReadAllText(manifest)) is { Success: true } m)
                return m.Groups[1].Value;
        }
        return null;
    }

    private static List<string> FindSteamLibraries()
    {
        var libs = new List<string>();
        string? steam = null;
        if (OperatingSystem.IsWindows())
        {
            steam = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string
                    ?? Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath", null) as string;
        }
        if (steam is null)
            return libs;
        steam = steam.Replace('/', Path.DirectorySeparatorChar);
        libs.Add(steam);
        var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (File.Exists(vdf))
        {
            foreach (Match m in LibraryPathRegex().Matches(File.ReadAllText(vdf)))
            {
                var path = m.Groups[1].Value.Replace(@"\\", @"\");
                if (!libs.Contains(path, StringComparer.OrdinalIgnoreCase))
                    libs.Add(path);
            }
        }
        return libs;
    }

    [GeneratedRegex("\"installdir\"\\s+\"([^\"]+)\"")]
    private static partial Regex InstallDirRegex();

    [GeneratedRegex("\"buildid\"\\s+\"([^\"]+)\"")]
    private static partial Regex BuildIdRegex();

    [GeneratedRegex("\"path\"\\s+\"([^\"]+)\"")]
    private static partial Regex LibraryPathRegex();
}
