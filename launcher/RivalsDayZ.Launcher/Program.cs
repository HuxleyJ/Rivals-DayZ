using System.Diagnostics;

namespace RivalsDayZ.Launcher;

internal enum Mode { Solo, Host, Join, Report }

internal sealed class Options
{
    public Mode Mode = Mode.Solo;
    public string? DayZ;
    public string? Rivals;
    public string? Join;
    public int Port = Sheets.DEFAULT_PORT;
    public bool SkipRivals;
    public bool ForceRivals;
    public string? AesKey;
    public string? Usmap;
    public string Language = "en";

    // Melty passes folders as arguments and multiplayer switches as environment variables
    // (see design/sheets/multiplayer.json and launcher.json).
    public static Options Parse(string[] args)
    {
        var o = new Options();
        var hostEnv = Sheets.HOST_ENV.Split('=');
        if (Environment.GetEnvironmentVariable(hostEnv[0]) is { } m && m.Equals(hostEnv[1], StringComparison.OrdinalIgnoreCase))
            o.Mode = Mode.Host;
        if (int.TryParse(Environment.GetEnvironmentVariable(Sheets.PORT_ENV), out var envPort) && envPort > 0)
            o.Port = envPort;
        if (Environment.GetEnvironmentVariable(Sheets.JOIN_ENV) is { Length: > 0 } join)
        {
            o.Mode = Mode.Join;
            o.Join = join;
        }
        o.AesKey = Environment.GetEnvironmentVariable("RDZ_RIVALS_AES");
        o.Language = Environment.GetEnvironmentVariable("RDZ_LANG") ?? "en";

        for (var i = 0; i < args.Length; i++)
        {
            string Next() => i + 1 < args.Length ? args[++i] : throw new ArgumentException($"{args[i]} needs a value");
            switch (args[i].ToLowerInvariant())
            {
                case "--dayz": o.DayZ = Next(); break;
                case "--rivals": o.Rivals = Next(); break;
                case "--host": o.Mode = Mode.Host; break;
                case "--join": o.Mode = Mode.Join; o.Join = Next(); break;
                case "--port": o.Port = int.Parse(Next()); break;
                case "--report": o.Mode = Mode.Report; o.ForceRivals = true; break;
                case "--no-rivals": o.SkipRivals = true; break;
                case "--refresh-rivals": o.ForceRivals = true; break;
                case "--aes": o.AesKey = Next(); break;
                case "--usmap": o.Usmap = Next(); break;
                case "--lang": o.Language = Next(); break;
                default: Log.Warn($"Ignoring unknown argument {args[i]}"); break;
            }
        }
        return o;
    }
}

internal static class Program
{
    public static string ModDir => AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

    private static int Main(string[] args)
    {
        Log.Init();
        Console.Title = "Rivals x DayZ";
        Log.Info("Rivals x DayZ launcher " + typeof(Program).Assembly.GetName().Version);
        try
        {
            var opt = Options.Parse(args);
            var paths = GamePaths.Resolve(opt);
            if (paths.DayZ is null)
            {
                Log.Error("Could not find DayZ. Start Rivals x DayZ from Melty, or pass --dayz \"<DayZ folder>\".");
                return Fail(2);
            }
            Log.Info($"DayZ: {paths.DayZ}");
            Log.Info($"Marvel Rivals: {paths.Rivals ?? "not found"}");

            if (!opt.SkipRivals)
                RivalsContent.Prepare(paths, opt);

            return opt.Mode switch
            {
                Mode.Host => Hosting.Run(paths, opt),
                Mode.Join => Joining.Run(paths, opt),
                Mode.Report => 0,
                _ => Solo.Run(paths),
            };
        }
        catch (Exception e)
        {
            Log.Error(e.ToString());
            return Fail(1);
        }
    }

    public static int Fail(int code)
    {
        Log.Info("Press Enter to close this window.");
        try { Console.ReadLine(); } catch { /* no console */ }
        return code;
    }

    //! Name of the mod folder relative to the DayZ folder (where this exe lives), e.g. "@RivalsDayZ".
    public static string ModArg(GamePaths paths)
    {
        var rel = Path.GetRelativePath(paths.DayZ!, ModDir);
        return rel.StartsWith("..") ? ModDir : rel;
    }

    public static Process StartGame(string exe, string args, string workDir)
    {
        Log.Info($"Starting {Path.GetFileName(exe)} {args}");
        var psi = new ProcessStartInfo(exe, args) { WorkingDirectory = workDir, UseShellExecute = false };
        return Process.Start(psi) ?? throw new InvalidOperationException($"Could not start {exe}");
    }
}

internal static class Solo
{
    public static int Run(GamePaths paths)
    {
        var mod = Program.ModArg(paths);
        var mission = Path.Combine(".", mod, "Missions", "RivalsDayZ_Hunt.chernarusplus");
        var args = $"\"-mission={mission}\" \"-mod={mod}\" -filePatching -nosplash -nopause -noBenchmark -doLogs";
        using var game = Program.StartGame(Path.Combine(paths.DayZ!, "DayZ_x64.exe"), args, paths.DayZ!);
        game.WaitForExit();
        Log.Info("DayZ closed.");
        return 0;
    }
}

internal static class Joining
{
    public static int Run(GamePaths paths, Options opt)
    {
        if (!Invite.TryParseAddress(opt.Join!, out var host, out var port))
        {
            Log.Error($"'{opt.Join}' is not an invite code or address. Invite codes look like {Sheets.INVITE_PREFIX}-XXXX-XXXX-XX.");
            return Program.Fail(3);
        }
        Log.Info($"Joining {host}:{port}");
        var mod = Program.ModArg(paths);
        var args = $"-connect={host} -port={port} \"-mod={mod}\" -filePatching -nosplash -nopause";
        using var game = Program.StartGame(Path.Combine(paths.DayZ!, "DayZ_x64.exe"), args, paths.DayZ!);
        game.WaitForExit();
        return 0;
    }
}
