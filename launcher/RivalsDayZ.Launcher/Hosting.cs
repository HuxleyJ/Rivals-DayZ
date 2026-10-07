using System.Diagnostics;
using System.Net;

namespace RivalsDayZ.Launcher;

//! Host mode: runs a DayZ server with the mashup on this PC, opens it to the internet,
//! publishes the address (Melty's join link + an invite code), then joins it.
internal static class Hosting
{
    private const string MissionName = "rivalsdayz.chernarusplus";

    public static int Run(GamePaths paths, Options opt)
    {
        if (!EnsureServerInstalled(paths))
            return Program.Fail(4);
        var server = paths.DayZServer!;
        var port = opt.Port;
        var profile = Path.Combine(Log.Dir, "server");
        Directory.CreateDirectory(Path.Combine(profile, "RivalsDayZ"));

        if (!PrepareServerFiles(paths, server))
            return Program.Fail(5);
        var cfg = WriteConfig(server, port);

        // Reachability and the address friends use.
        using var opener = new PortOpener();
        var opened = opener.OpenAsync(port, TimeSpan.FromSeconds(8)).GetAwaiter().GetResult();
        var stunIp = NetInfo.StunPublicIpAsync(Sheets.PUBLIC_IP_STUN, TimeSpan.FromSeconds(4)).GetAwaiter().GetResult();
        IPAddress? publicIp = opener.RouterPublicIp is { } rip && NetInfo.IsPublic(rip) ? rip : stunIp;
        string address;
        string reach;
        if (opened && publicIp is not null && NetInfo.IsPublic(publicIp))
        {
            if (opener.RouterPublicIp is not null && stunIp is not null && !opener.RouterPublicIp.Equals(stunIp))
                Log.Warn($"Your router says {opener.RouterPublicIp} but the internet sees {stunIp}: there may be a second router or carrier NAT in the way, so friends outside your home may not get in.");
            address = $"{publicIp}:{port}";
            reach = "internet";
        }
        else
        {
            var lan = NetInfo.LanIp();
            address = $"{lan}:{port}";
            reach = "home network only";
            Log.Warn("Your router did not let Rivals x DayZ open a port automatically, so friends on your home network can join but friends elsewhere cannot yet.");
        }

        var code = Invite.Encode(IPAddress.Parse(address.Split(':')[0]), port);
        var inviteLine = $"Invite code {code}  ({reach}) - friends join from your Melty join link, or with RivalsDayZ.exe --join {code}";
        File.WriteAllText(Path.Combine(profile, "RivalsDayZ", "invite.txt"), inviteLine + Environment.NewLine);
        NetInfo.CopyToClipboard(code);
        Log.HostingAt(address);
        Log.Info(inviteLine);
        Log.Info("The invite code is on your clipboard.");

        // Server.
        var mod = Program.ModDir;
        var serverArgs = $"-config={cfg} -port={port} \"-profiles={profile}\" \"-mod={mod}\" -filePatching -dologs -adminlog -freezecheck";
        using var serverProc = Program.StartGame(Path.Combine(server, "DayZServer_x64.exe"), serverArgs, server);
        if (!WaitForServer(profile, serverProc, TimeSpan.FromMinutes(3)))
            Log.Warn("Did not see the server report ready; joining anyway.");

        // Join our own server.
        var clientArgs = $"-connect=127.0.0.1 -port={port} \"-mod={Program.ModArg(paths)}\" -filePatching -nosplash -nopause";
        using (var client = Program.StartGame(Path.Combine(paths.DayZ!, "DayZ_x64.exe"), clientArgs, paths.DayZ!))
        {
            client.WaitForExit();
        }

        Log.Info("You left the game; stopping your server.");
        try
        {
            if (!serverProc.HasExited)
                serverProc.Kill(entireProcessTree: true);
        }
        catch (Exception e)
        {
            Log.Warn("Could not stop the server: " + e.Message);
        }
        return 0;
    }

    //! DayZ Server is a separate free Steam tool for DayZ owners; Steam installs it on request.
    private static bool EnsureServerInstalled(GamePaths paths)
    {
        if (paths.DayZServer is not null)
            return true;
        Log.Info("Hosting needs the free DayZ Server tool from Steam (one time).");
        Log.Info("Steam will open an install window: click Install. This window waits until it has finished.");
        try
        {
            Process.Start(new ProcessStartInfo($"steam://install/{Sheets.DAYZ_SERVER_APPID}") { UseShellExecute = true });
        }
        catch (Exception e)
        {
            Log.Error("Could not ask Steam to install DayZ Server: " + e.Message);
            return false;
        }
        var until = DateTime.Now.AddMinutes(45);
        while (DateTime.Now < until)
        {
            Thread.Sleep(5000);
            paths.RefreshServer();
            if (paths.DayZServer is not null)
            {
                Log.Info("DayZ Server is installed: " + paths.DayZServer);
                Thread.Sleep(5000); // let Steam finish writing files
                return true;
            }
        }
        Log.Error("DayZ Server did not finish installing. Install it from Steam > Library > Tools > DayZ Server, then press Play again.");
        return false;
    }

    //! Mission folder from the server's own vanilla mission, our init.c on top; our key; Rivals text.
    private static bool PrepareServerFiles(GamePaths paths, string server)
    {
        var vanilla = Path.Combine(server, "mpmissions", "dayzOffline.chernarusplus");
        var ours = Path.Combine(server, "mpmissions", MissionName);
        if (!Directory.Exists(vanilla))
        {
            Log.Error($"The DayZ Server install has no {vanilla}. Verify DayZ Server's files in Steam and try again.");
            return false;
        }
        CopyTree(vanilla, ours, skipDir: "storage_1");
        File.Copy(Path.Combine(Program.ModDir, "server", "init.c"), Path.Combine(ours, "init.c"), overwrite: true);

        var keysDir = Path.Combine(server, "keys");
        Directory.CreateDirectory(keysDir);
        foreach (var key in Directory.GetFiles(Path.Combine(Program.ModDir, "keys"), "*.bikey"))
            File.Copy(key, Path.Combine(keysDir, Path.GetFileName(key)), overwrite: true);

        var content = Path.Combine(paths.DayZ!, "RivalsDayZ_Rivals");
        if (Directory.Exists(content))
            CopyTree(content, Path.Combine(server, "RivalsDayZ_Rivals"), skipDir: null);
        return true;
    }

    private static string WriteConfig(string server, int port)
    {
        var adminPassword = Convert.ToHexString(Guid.NewGuid().ToByteArray())[..16];
        var cfg = $$"""
            // Written by the Rivals x DayZ launcher on every host. Values come from design/sheets/multiplayer.json.
            hostname = "Rivals x DayZ";
            password = "";
            passwordAdmin = "{{adminPassword}}";
            maxPlayers = {{Sheets.MAX_PLAYERS}};
            verifySignatures = {{Sheets.SERVER_VERIFY_SIGNATURES}};
            forceSameBuild = 1;
            BattlEye = {{Sheets.SERVER_BATTLEYE}};
            allowFilePatching = {{Sheets.SERVER_ALLOW_FILEPATCHING}};
            disableVoN = 0;
            vonCodecQuality = 20;
            disable3rdPerson = 0;
            disableCrosshair = 0;
            serverTime = "SystemTime";
            serverTimeAcceleration = 1;
            serverNightTimeAcceleration = 4;
            serverTimePersistent = 0;
            guaranteedUpdates = 1;
            loginQueueConcurrentPlayers = 5;
            loginQueueMaxPlayers = 50;
            instanceId = 1;
            storageAutoFix = 1;
            steamQueryPort = {{port + 2}};
            class Missions
            {
                class DayZ
                {
                    template = "{{MissionName}}";
                };
            };
            """;
        const string name = "serverDZ_rivals.cfg";
        File.WriteAllText(Path.Combine(server, name), cfg);
        return name;
    }

    private static bool WaitForServer(string profile, Process server, TimeSpan timeout)
    {
        var markers = new[] { "Player connect enabled", "Mission read", "Game started" };
        var until = DateTime.Now + timeout;
        var started = DateTime.Now;
        Log.Info("Waiting for your server to start...");
        while (DateTime.Now < until && !server.HasExited)
        {
            Thread.Sleep(2000);
            foreach (var rpt in Directory.GetFiles(profile, "*.RPT"))
            {
                if (File.GetLastWriteTime(rpt) < started.AddSeconds(-5))
                    continue;
                try
                {
                    using var fs = new FileStream(rpt, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var sr = new StreamReader(fs);
                    var text = sr.ReadToEnd();
                    if (markers.Any(m => text.Contains(m, StringComparison.OrdinalIgnoreCase)))
                    {
                        Log.Info("Server is up.");
                        Thread.Sleep(3000);
                        return true;
                    }
                }
                catch (IOException)
                {
                    // still being written
                }
            }
        }
        return false;
    }

    private static void CopyTree(string from, string to, string? skipDir)
    {
        Directory.CreateDirectory(to);
        foreach (var file in Directory.GetFiles(from))
            File.Copy(file, Path.Combine(to, Path.GetFileName(file)), overwrite: true);
        foreach (var dir in Directory.GetDirectories(from))
        {
            var name = Path.GetFileName(dir);
            if (skipDir is not null && name.Equals(skipDir, StringComparison.OrdinalIgnoreCase))
                continue;
            CopyTree(dir, Path.Combine(to, name), skipDir);
        }
    }
}
