namespace RivalsDayZ.Launcher;

//! Console + %LOCALAPPDATA%\RivalsDayZ\logs\latest.log (Melty reads the "Hosting at " line from it).
internal static class Log
{
    private static StreamWriter? _file;
    private static readonly object Gate = new();

    public static string Dir => Path.Combine(LocalAppData, "RivalsDayZ");

    //! %LOCALAPPDATA%, created if missing (GetFolderPath returns "" for a missing folder).
    public static string LocalAppData
    {
        get
        {
            var p = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData, Environment.SpecialFolderOption.Create);
            return string.IsNullOrEmpty(p) ? Path.GetTempPath() : p;
        }
    }

    public static void Init()
    {
        try
        {
            var logs = Path.Combine(Dir, "logs");
            Directory.CreateDirectory(logs);
            _file = new StreamWriter(Path.Combine(logs, "latest.log"), append: false) { AutoFlush = true };
        }
        catch
        {
            _file = null;
        }
    }

    public static void Info(string msg) => Write("INFO", msg);
    public static void Warn(string msg) => Write("WARN", msg);
    public static void Error(string msg) => Write("ERROR", msg);

    //! The exact line Melty looks for: "<marker><address>".
    public static void HostingAt(string address) => Write("INFO", Sheets.ADDRESS_MARKER + address);

    private static void Write(string level, string msg)
    {
        var line = $"[{DateTime.Now:HH:mm:ss}] {level} {msg}";
        lock (Gate)
        {
            Console.WriteLine(line);
            _file?.WriteLine(line);
        }
    }
}
