using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Mono.Nat;

namespace RivalsDayZ.Launcher;

//! Invite codes: RDZ-XXXX-XXXX-XX = the host's IPv4 address and port in Crockford base32,
//! so they survive being read out loud or typed on a phone.
internal static class Invite
{
    private const string Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    public static string Encode(IPAddress ip, int port)
    {
        var b = ip.GetAddressBytes();
        ulong v = ((ulong)b[0] << 40) | ((ulong)b[1] << 32) | ((ulong)b[2] << 24) | ((ulong)b[3] << 16) | (ushort)port;
        var chars = new char[10];
        for (var i = 9; i >= 0; i--)
        {
            chars[i] = Alphabet[(int)(v & 31)];
            v >>= 5;
        }
        var s = new string(chars);
        return $"{Sheets.INVITE_PREFIX}-{s[..4]}-{s[4..8]}-{s[8..]}";
    }

    public static bool TryDecode(string code, out IPAddress ip, out int port)
    {
        ip = IPAddress.None;
        port = 0;
        var s = code.Trim().ToUpperInvariant().Replace("-", "").Replace(" ", "");
        if (s.StartsWith(Sheets.INVITE_PREFIX))
            s = s[Sheets.INVITE_PREFIX.Length..];
        s = s.Replace('O', '0').Replace('I', '1').Replace('L', '1');
        if (s.Length != 10)
            return false;
        ulong v = 0;
        foreach (var c in s)
        {
            var d = Alphabet.IndexOf(c);
            if (d < 0)
                return false;
            v = (v << 5) | (uint)d;
        }
        ip = new IPAddress(new[] { (byte)(v >> 40), (byte)(v >> 32), (byte)(v >> 24), (byte)(v >> 16) });
        port = (int)(v & 0xFFFF);
        return port > 0;
    }

    //! Accepts an invite code, "host:port", or a bare host (default port).
    public static bool TryParseAddress(string text, out string host, out int port)
    {
        if (TryDecode(text, out var ip, out port))
        {
            host = ip.ToString();
            return true;
        }
        host = text.Trim();
        port = Sheets.DEFAULT_PORT;
        var colon = host.LastIndexOf(':');
        if (colon > 0 && int.TryParse(host[(colon + 1)..], out var p))
        {
            port = p;
            host = host[..colon];
        }
        return host.Length > 0 && Uri.CheckHostName(host) != UriHostNameType.Unknown;
    }
}

//! Opens the game port on the host's router (UPnP or NAT-PMP) and works out the address
//! friends should use. Nobody is asked to touch their router.
internal sealed class PortOpener : IDisposable
{
    private INatDevice? _device;
    private Mapping? _mapping;

    public IPAddress? RouterPublicIp { get; private set; }
    public string Method { get; private set; } = "none";

    public async Task<bool> OpenAsync(int port, TimeSpan timeout)
    {
        var found = new TaskCompletionSource<INatDevice>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnFound(object? sender, DeviceEventArgs e) => found.TrySetResult(e.Device);
        NatUtility.DeviceFound += OnFound;
        try
        {
            NatUtility.StartDiscovery();
            var winner = await Task.WhenAny(found.Task, Task.Delay(timeout));
            if (winner != found.Task)
            {
                Log.Warn("No router answered UPnP or NAT-PMP.");
                return false;
            }
            _device = found.Task.Result;
            Method = _device.NatProtocol.ToString();
            var mapping = new Mapping(Protocol.Udp, port, port, 0, "Rivals x DayZ");
            await _device.CreatePortMapAsync(mapping);
            _mapping = mapping;
            RouterPublicIp = await _device.GetExternalIPAsync();
            Log.Info($"Opened UDP port {port} on the router with {Method}; router says its public address is {RouterPublicIp}.");
            return true;
        }
        catch (Exception e)
        {
            Log.Warn($"The router refused to open port {port}: {e.Message}");
            return false;
        }
        finally
        {
            NatUtility.StopDiscovery();
            NatUtility.DeviceFound -= OnFound;
        }
    }

    public void Dispose()
    {
        if (_device is null || _mapping is null)
            return;
        try
        {
            _device.DeletePortMapAsync(_mapping).Wait(TimeSpan.FromSeconds(3));
            Log.Info("Closed the port on the router again.");
        }
        catch (Exception e)
        {
            Log.Warn("Could not close the router port: " + e.Message);
        }
    }
}

internal static class NetInfo
{
    //! Public IPv4 address as seen from the internet (RFC 5389 STUN binding request).
    public static async Task<IPAddress?> StunPublicIpAsync(string server, TimeSpan timeout)
    {
        try
        {
            var parts = server.Split(':');
            var addrs = await Dns.GetHostAddressesAsync(parts[0]);
            var target = addrs.FirstOrDefault(a => a.AddressFamily == AddressFamily.InterNetwork);
            if (target is null)
                return null;
            using var udp = new UdpClient(AddressFamily.InterNetwork);
            var req = new byte[20];
            req[1] = 0x01; // Binding Request
            req[4] = 0x21; req[5] = 0x12; req[6] = 0xA4; req[7] = 0x42; // magic cookie
            Random.Shared.NextBytes(req.AsSpan(8, 12));
            await udp.SendAsync(req, req.Length, new IPEndPoint(target, int.Parse(parts[1])));
            var recv = udp.ReceiveAsync();
            if (await Task.WhenAny(recv, Task.Delay(timeout)) != recv)
                return null;
            var data = recv.Result.Buffer;
            for (var i = 20; i + 4 <= data.Length;)
            {
                var type = (data[i] << 8) | data[i + 1];
                var len = (data[i + 2] << 8) | data[i + 3];
                var v = i + 4;
                if ((type == 0x0020 || type == 0x0001) && len >= 8 && data[v + 1] == 0x01)
                {
                    var ip = new byte[4];
                    Array.Copy(data, v + 4, ip, 0, 4);
                    if (type == 0x0020)
                    {
                        ip[0] ^= 0x21; ip[1] ^= 0x12; ip[2] ^= 0xA4; ip[3] ^= 0x42;
                    }
                    return new IPAddress(ip);
                }
                i = v + ((len + 3) & ~3);
            }
        }
        catch (Exception e)
        {
            Log.Warn("STUN lookup failed: " + e.Message);
        }
        return null;
    }

    public static IPAddress LanIp()
    {
        try
        {
            using var s = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            s.Connect("8.8.8.8", 53);
            return ((IPEndPoint)s.LocalEndPoint!).Address;
        }
        catch
        {
            return IPAddress.Loopback;
        }
    }

    public static bool IsPublic(IPAddress ip)
    {
        var b = ip.GetAddressBytes();
        if (b.Length != 4)
            return false;
        return !(b[0] == 10 || b[0] == 127 || b[0] == 0
                 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
                 || (b[0] == 192 && b[1] == 168)
                 || (b[0] == 169 && b[1] == 254)
                 || (b[0] == 100 && b[1] >= 64 && b[1] <= 127)); // carrier-grade NAT
    }

    public static void CopyToClipboard(string text)
    {
        try
        {
            var psi = new ProcessStartInfo("clip.exe") { RedirectStandardInput = true, UseShellExecute = false, CreateNoWindow = true };
            using var p = Process.Start(psi);
            if (p is null)
                return;
            p.StandardInput.Write(text);
            p.StandardInput.Close();
            p.WaitForExit(3000);
        }
        catch (Exception e)
        {
            Log.Warn("Could not copy to the clipboard: " + e.Message);
        }
    }
}
