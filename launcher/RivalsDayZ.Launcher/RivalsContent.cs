using System.Text;
using System.Text.Json;
using CUE4Parse.Compression;
using CUE4Parse.Encryption.Aes;
using CUE4Parse.FileProvider;
using CUE4Parse.MappingsProvider.Usmap;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Localization;
using CUE4Parse.UE4.Objects.Core.Misc;
using CUE4Parse.UE4.Versions;
using CUE4Parse_Conversion.Textures;

namespace RivalsDayZ.Launcher;

//! Reads Marvel Rivals content from the player's own install (read only; Rivals is never
//! started or changed) and writes it under <DayZ>\RivalsDayZ_Rivals for the mod:
//! hero/ability names and descriptions, voice lines and sounds, portraits and icons.
//! Every step fails soft: the mod keeps working with the sheet's built-in names.
internal static class RivalsContent
{
    private const string OutFolder = "RivalsDayZ_Rivals";

    public static void Prepare(GamePaths paths, Options opt)
    {
        var report = new StringBuilder();
        var reportPath = Path.Combine(Log.Dir, "rivals-report.txt");
        try
        {
            Run(paths, opt, report);
        }
        catch (Exception e)
        {
            Log.Warn("Could not read Marvel Rivals content (" + e.Message + "). The mashup still runs with built-in names.");
            report.AppendLine("FAILED: " + e);
        }
        finally
        {
            try
            {
                Directory.CreateDirectory(Log.Dir);
                File.WriteAllText(reportPath, report.ToString());
                Log.Info("Rivals content report: " + reportPath);
            }
            catch
            {
                // report is a convenience
            }
        }
    }

    private static void Run(GamePaths paths, Options opt, StringBuilder report)
    {
        report.AppendLine($"Rivals x DayZ content report {DateTime.Now:u}");
        if (paths.Rivals is null)
        {
            Log.Warn("Marvel Rivals is not installed (or Melty did not pass its folder); using built-in names.");
            report.AppendLine("Rivals folder: not found");
            return;
        }
        report.AppendLine("Rivals folder: " + paths.Rivals);
        var outDir = Path.Combine(paths.DayZ!, OutFolder);
        var stamp = paths.BuildId(GamePaths.RivalsAppId) ?? NewestPakTime(paths.Rivals);
        var stampFile = Path.Combine(outDir, "stamp.txt");
        if (!opt.ForceRivals && File.Exists(stampFile) && File.ReadAllText(stampFile).Trim() == stamp && File.Exists(Path.Combine(outDir, "content.json")))
        {
            Log.Info($"Marvel Rivals content is up to date (build {stamp}).");
            report.AppendLine("Up to date; nothing re-read.");
            return;
        }

        var paks = FindPaks(paths.Rivals);
        report.AppendLine("Paks folder: " + (paks ?? "not found"));
        if (paks is null)
        {
            Log.Warn("Could not find Marvel Rivals' Paks folder.");
            return;
        }

        var oodle = FindFile(paths.Rivals, 6, n => n.StartsWith("oo2core", StringComparison.OrdinalIgnoreCase) || n.StartsWith("oodle", StringComparison.OrdinalIgnoreCase), ".dll");
        report.AppendLine("Oodle DLL in the Rivals install: " + (oodle ?? "none found"));
        if (oodle is not null)
            OodleHelper.Initialize(oodle);
        else
            Log.Warn("No Oodle library in the Rivals install; compressed Rivals files cannot be read.");

        Log.Info("Reading Marvel Rivals files (read only)...");
        var provider = new DefaultFileProvider(paks, SearchOption.TopDirectoryOnly, new VersionContainer(EGame.GAME_MarvelRivals), StringComparer.OrdinalIgnoreCase);
        provider.Initialize();
        var aes = opt.AesKey ?? ReadSideFile("rivals.aes.txt");
        if (!string.IsNullOrWhiteSpace(aes))
            provider.SubmitKey(new FGuid(), new FAesKey(aes.Trim()));
        provider.Mount();
        var usmap = opt.Usmap ?? SideFilePath("rivals.usmap");
        if (usmap is not null && File.Exists(usmap))
            provider.MappingsContainer = new FileUsmapTypeMappingsProvider(usmap);
        report.AppendLine($"Containers mounted: {provider.MountedVfs.Count}, still encrypted: {provider.UnloadedVfs.Count}, files: {provider.Files.Count}");
        report.AppendLine("AES key supplied: " + (!string.IsNullOrWhiteSpace(aes)) + ", mappings: " + (usmap ?? "none"));

        Discover(provider, paths.Rivals, report);

        var content = new ContentFile { rivalsBuild = stamp };
        ReadTexts(provider, opt.Language, content, report);
        Directory.CreateDirectory(outDir);
        ReadAudio(provider, paths.DayZ!, content, report);
        if (provider.MappingsContainer is not null)
            ReadImages(provider, paths.DayZ!, content, report);
        else
            report.AppendLine("Images skipped: Rivals textures need a .usmap mappings file to decode.");

        if (content.heroes.Count == 0 && content.abilities.Count == 0 && content.assets.Count == 0)
        {
            // Nothing usable: keep any earlier content, write no stamp, and try again next Play.
            Log.Warn("Nothing could be read from Marvel Rivals this time; see the report. The mashup runs with built-in names.");
            return;
        }
        var json = JsonSerializer.Serialize(content, new JsonSerializerOptions { WriteIndented = true, IncludeFields = true });
        File.WriteAllText(Path.Combine(outDir, "content.json"), json);
        foreach (var profile in DayZProfileDirs())
        {
            try
            {
                Directory.CreateDirectory(Path.Combine(profile, "RivalsDayZ"));
                File.WriteAllText(Path.Combine(profile, "RivalsDayZ", "content.json"), json);
            }
            catch
            {
                // optional second location
            }
        }
        File.WriteAllText(stampFile, stamp);
        Log.Info($"Marvel Rivals content: {content.heroes.Count} heroes, {content.abilities.Count} abilities, {content.assets.Count} sounds/images.");
    }

    // --- discovery report: what is in the install, so the sheets can be checked ---------

    private static void Discover(DefaultFileProvider provider, string rivals, StringBuilder report)
    {
        var all = provider.Files.Keys.ToList();
        report.AppendLine();
        report.AppendLine("File types in the Rivals containers:");
        foreach (var g in all.GroupBy(p => Path.GetExtension(p).ToLowerInvariant()).OrderByDescending(g => g.Count()).Take(25))
            report.AppendLine($"  {g.Key,-10} {g.Count()}");
        report.AppendLine();
        report.AppendLine("Localization files:");
        foreach (var p in all.Where(p => p.EndsWith(".locres", StringComparison.OrdinalIgnoreCase)).Take(60))
            report.AppendLine("  " + p);
        foreach (var kw in new[] { "1036", "spider", "rogue" })
        {
            var hits = all.Where(p => p.Contains(kw, StringComparison.OrdinalIgnoreCase)).ToList();
            report.AppendLine();
            report.AppendLine($"Paths containing '{kw}': {hits.Count} (first 150)");
            foreach (var h in hits.Take(150))
                report.AppendLine("  " + h);
        }
        report.AppendLine();
        report.AppendLine("Loose files on disk outside the Paks folder, by type:");
        try
        {
            var loose = Directory.EnumerateFiles(rivals, "*", SearchOption.AllDirectories)
                .Where(f => !f.Contains(Path.DirectorySeparatorChar + "Paks" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                .GroupBy(f => Path.GetExtension(f).ToLowerInvariant()).OrderByDescending(g => g.Count()).Take(25);
            foreach (var g in loose)
                report.AppendLine($"  {g.Key,-10} {g.Count()}  e.g. {g.First()}");
        }
        catch (Exception e)
        {
            report.AppendLine("  could not list: " + e.Message);
        }
    }

    // --- text: hero and ability names/descriptions from the localization files ---------------

    private static void ReadTexts(DefaultFileProvider provider, string language, ContentFile content, StringBuilder report)
    {
        var english = LoadLocres(provider, "en");
        var local = language == "en" ? english : LoadLocres(provider, language);
        if (local.Count == 0)
            local = english;
        report.AppendLine();
        report.AppendLine($"Localization entries: en {english.Count}, {language} {local.Count}");
        if (english.Count == 0)
            return;

        var byValue = new Dictionary<string, List<string>>();
        foreach (var (key, value) in english)
        {
            var n = Normalize(value);
            if (n.Length == 0)
                continue;
            if (!byValue.TryGetValue(n, out var list))
                byValue[n] = list = [];
            list.Add(key);
        }

        foreach (var (id, match) in Sheets.HeroTexts)
        {
            if (FindNameKey(byValue, match, report, "hero " + id) is { } key)
                content.heroes[id] = new ContentText { name = local.GetValueOrDefault(key, english[key]) };
        }
        foreach (var (id, hero, match) in Sheets.AbilityTexts)
        {
            if (FindNameKey(byValue, match, report, "ability " + id) is not { } key)
                continue;
            var desc = FindDescriptionKey(english, key);
            content.abilities[id] = new ContentText
            {
                name = local.GetValueOrDefault(key, english[key]),
                description = desc is null ? "" : local.GetValueOrDefault(desc, english[desc]),
            };
            report.AppendLine($"    description key: {desc ?? "none found"}");
        }
    }

    private static string? FindNameKey(Dictionary<string, List<string>> byValue, string match, StringBuilder report, string what)
    {
        if (!byValue.TryGetValue(Normalize(match), out var keys) || keys.Count == 0)
        {
            report.AppendLine($"  {what}: '{match}' not found in Rivals text");
            return null;
        }
        // Prefer keys that look like names over chat lines or tooltips that happen to match.
        var key = keys.OrderByDescending(k => k.Contains("name", StringComparison.OrdinalIgnoreCase)).ThenBy(k => k.Length).First();
        report.AppendLine($"  {what}: '{match}' -> {key} (of {keys.Count} matches: {string.Join(", ", keys.Take(5))})");
        return key;
    }

    //! The description usually sits next to the name: same namespace, "Name" swapped for "Desc".
    private static string? FindDescriptionKey(Dictionary<string, string> english, string nameKey)
    {
        foreach (var swap in new[] { ("Name", "Desc"), ("name", "desc"), ("Name", "Description"), ("name", "description"), ("Name", "Tips"), ("_N", "_D") })
        {
            var guess = nameKey.Replace(swap.Item1, swap.Item2);
            if (guess != nameKey && english.ContainsKey(guess))
                return guess;
        }
        return null;
    }

    private static Dictionary<string, string> LoadLocres(DefaultFileProvider provider, string language)
    {
        var result = new Dictionary<string, string>();
        var files = provider.Files.Keys.Where(p => p.EndsWith(".locres", StringComparison.OrdinalIgnoreCase)
                                                   && p.Contains($"/{language}/", StringComparison.OrdinalIgnoreCase));
        foreach (var path in files)
        {
            try
            {
                if (!provider.TryCreateReader(path, out var ar))
                    continue;
                var locres = new FTextLocalizationResource(ar);
                foreach (var (ns, entries) in locres.Entries)
                    foreach (var (key, entry) in entries)
                        result[ns.Str + "::" + key.Str] = entry.LocalizedString;
            }
            catch (Exception e)
            {
                Log.Warn($"Could not read {path}: {e.Message}");
            }
        }
        return result;
    }

    private static string Normalize(string s) => new string(s.Where(char.IsLetterOrDigit).ToArray()).ToLowerInvariant();

    // --- audio: Wwise sounds -> Ogg Vorbis ------------------------------------------------------

    private static void ReadAudio(DefaultFileProvider provider, string dayz, ContentFile content, StringBuilder report)
    {
        report.AppendLine();
        report.AppendLine("Audio:");
        var vgm = Path.Combine(Program.ModDir, "tools", "vgmstream", "vgmstream-cli.exe");
        if (!File.Exists(vgm))
        {
            report.AppendLine("  vgmstream-cli.exe missing from the mod; audio skipped.");
            return;
        }
        var audioFiles = provider.Files.Keys.Where(p => p.EndsWith(".wem", StringComparison.OrdinalIgnoreCase) || p.EndsWith(".bnk", StringComparison.OrdinalIgnoreCase)).ToList();
        report.AppendLine($"  Wwise files in the containers: {audioFiles.Count}");
        foreach (var (id, isAudio, keywords, output) in Sheets.Assets)
        {
            if (!isAudio)
                continue;
            var candidates = audioFiles.Where(p => keywords.All(k => p.Contains(k, StringComparison.OrdinalIgnoreCase))).OrderBy(p => p).ToList();
            report.AppendLine($"  {id}: {candidates.Count} candidates {string.Join(" | ", candidates.Take(5))}");
            foreach (var path in candidates.Take(3))
            {
                try
                {
                    if (!provider.TrySaveAsset(path, out var data))
                        continue;
                    if (path.EndsWith(".bnk", StringComparison.OrdinalIgnoreCase))
                        data = Wwise.FirstWem(data);
                    if (data is null)
                        continue;
                    var rel = output + ".ogg";
                    if (Audio.WemToOgg(vgm, data, Path.Combine(dayz, rel)))
                    {
                        content.assets[id] = rel;
                        report.AppendLine($"    -> {rel} from {path}");
                        break;
                    }
                }
                catch (Exception e)
                {
                    report.AppendLine($"    {path}: {e.Message}");
                }
            }
        }
    }

    // --- images: Texture2D -> PAA (needs mappings) -----------------------------------------------

    private static void ReadImages(DefaultFileProvider provider, string dayz, ContentFile content, StringBuilder report)
    {
        report.AppendLine();
        report.AppendLine("Images:");
        var assets = provider.Files.Keys.Where(p => p.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase)).ToList();
        foreach (var (id, isAudio, keywords, output) in Sheets.Assets)
        {
            if (isAudio)
                continue;
            var candidates = assets.Where(p => keywords.All(k => p.Contains(k, StringComparison.OrdinalIgnoreCase))).OrderBy(p => p.Length).ToList();
            report.AppendLine($"  {id}: {candidates.Count} candidates {string.Join(" | ", candidates.Take(5))}");
            foreach (var path in candidates.Take(5))
            {
                try
                {
                    var tex = provider.LoadPackageObject<UTexture2D>(path[..^".uasset".Length]);
                    var decoded = tex.Decode(256);
                    if (decoded is null)
                        continue;
                    var rgba = Paa.ToRgba(decoded.Data, decoded.PixelFormat.ToString());
                    if (rgba is null)
                        continue;
                    var rel = output + ".paa";
                    Paa.WriteDxt5(Path.Combine(dayz, rel), rgba, decoded.Width, decoded.Height, id.StartsWith("img_") ? 128 : 64);
                    content.assets[id] = rel;
                    report.AppendLine($"    -> {rel} from {path}");
                    break;
                }
                catch (Exception e)
                {
                    report.AppendLine($"    {path}: {e.Message}");
                }
            }
        }
    }

    // --- helpers -----------------------------------------------------------------------------

    private static string? FindPaks(string rivals)
    {
        try
        {
            return Directory.EnumerateDirectories(rivals, "Paks", SearchOption.AllDirectories)
                .FirstOrDefault(d => Directory.EnumerateFiles(d, "*.utoc").Any() || Directory.EnumerateFiles(d, "*.pak").Any());
        }
        catch
        {
            return null;
        }
    }

    private static string NewestPakTime(string rivals)
    {
        var paks = FindPaks(rivals);
        if (paks is null)
            return "unknown";
        return Directory.EnumerateFiles(paks).Select(File.GetLastWriteTimeUtc).DefaultIfEmpty().Max().ToString("yyyyMMddHHmmss");
    }

    private static string? FindFile(string root, int maxDepth, Func<string, bool> nameMatch, string ext)
    {
        var queue = new Queue<(string, int)>();
        queue.Enqueue((root, 0));
        while (queue.Count > 0)
        {
            var (dir, depth) = queue.Dequeue();
            try
            {
                foreach (var f in Directory.EnumerateFiles(dir, "*" + ext))
                    if (nameMatch(Path.GetFileName(f)))
                        return f;
                if (depth < maxDepth)
                    foreach (var d in Directory.EnumerateDirectories(dir))
                        queue.Enqueue((d, depth + 1));
            }
            catch
            {
                // unreadable folder
            }
        }
        return null;
    }

    private static string? SideFilePath(string name)
    {
        foreach (var dir in new[] { Program.ModDir, Log.Dir })
        {
            var p = Path.Combine(dir, name);
            if (File.Exists(p))
                return p;
        }
        return null;
    }

    private static string? ReadSideFile(string name) => SideFilePath(name) is { } p ? File.ReadAllText(p) : null;

    private static IEnumerable<string> DayZProfileDirs()
    {
        yield return Path.Combine(Log.LocalAppData, "DayZ");
        var docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        if (!string.IsNullOrEmpty(docs))
            yield return Path.Combine(docs, "DayZ");
    }
}

// Shape of content.json; field names match RDZ_ContentFile in the mod's 3_Game scripts.
internal sealed class ContentText
{
    public string name = "";
    public string description = "";
}

internal sealed class ContentFile
{
    public string rivalsBuild = "";
    public Dictionary<string, ContentText> heroes = new();
    public Dictionary<string, ContentText> abilities = new();
    public Dictionary<string, string> assets = new();
}
