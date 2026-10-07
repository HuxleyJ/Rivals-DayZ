using System.Net;
using System.Reflection;
using RivalsDayZ.Launcher;

var failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine((ok ? "PASS " : "FAIL ") + what);
    if (!ok) failures++;
}

// --- invite codes ---------------------------------------------------------------
foreach (var (ip, port) in new[] { ("81.2.69.160", 2302), ("1.1.1.1", 1), ("255.255.255.254", 65535), ("192.168.1.20", 2402) })
{
    var code = Invite.Encode(IPAddress.Parse(ip), port);
    var ok = Invite.TryDecode(code, out var dip, out var dport) && dip.ToString() == ip && dport == port;
    Check(ok && code.StartsWith("RDZ-") && code.Length == 16, $"invite round trip {ip}:{port} -> {code}");
    Check(Invite.TryDecode(code.ToLowerInvariant().Replace("-", " "), out _, out _), $"invite accepts lowercase/spaces {code}");
}
Check(Invite.TryParseAddress("example.org:2310", out var h, out var p2) && h == "example.org" && p2 == 2310, "address host:port");
Check(Invite.TryParseAddress("10.0.0.5", out var h2, out var p3) && h2 == "10.0.0.5" && p3 == Sheets.DEFAULT_PORT, "bare address gets default port");
Check(!Invite.TryDecode("RDZ-ZZZZ", out _, out _), "short code rejected");
Check(NetInfo.IsPublic(IPAddress.Parse("81.2.69.160")) && !NetInfo.IsPublic(IPAddress.Parse("100.72.1.1")) && !NetInfo.IsPublic(IPAddress.Parse("192.168.0.2")), "public/private/CGNAT classification");

// --- PAA writer: decode our own DXT5 and compare ---------------------------------
var src = new byte[200 * 120 * 4];
for (var y = 0; y < 120; y++)
    for (var x = 0; x < 200; x++)
    {
        var i = (y * 200 + x) * 4;
        src[i] = (byte)(x * 255 / 199); src[i + 1] = (byte)(y * 255 / 119); src[i + 2] = 80; src[i + 3] = (byte)(x < 100 ? 255 : 128);
    }
var paaPath = Path.Combine(Path.GetTempPath(), "rdz_test.paa");
Paa.WriteDxt5(paaPath, src, 200, 120, 64);
var paa = File.ReadAllBytes(paaPath);
Check(BitConverter.ToUInt16(paa, 0) == 0xFF05, "paa type DXT5");
var pos = 2;
var tags = new Dictionary<string, byte[]>();
while (System.Text.Encoding.ASCII.GetString(paa, pos, 4) == "GGAT")
{
    var name = System.Text.Encoding.ASCII.GetString(paa, pos + 4, 4);
    var len = BitConverter.ToInt32(paa, pos + 8);
    tags[name] = paa[(pos + 12)..(pos + 12 + len)];
    pos += 12 + len;
}
Check(tags.ContainsKey("SFFO") && tags.ContainsKey("CGVA") && tags.ContainsKey("CXAM"), "paa tags present: " + string.Join(",", tags.Keys));
Check(BitConverter.ToUInt16(paa, pos) == 0, "paa empty palette");
pos += 2;
var offs = Enumerable.Range(0, 16).Select(i => BitConverter.ToUInt32(tags["SFFO"], i * 4)).ToArray();
var sizes = new List<int>();
var mip0 = Array.Empty<byte>();
for (var m = 0; ; m++)
{
    var w = BitConverter.ToUInt16(paa, pos);
    var hgt = BitConverter.ToUInt16(paa, pos + 2);
    if (w == 0) break;
    Check(offs[m] == pos, $"mip {m} offset table matches ({offs[m]} == {pos})");
    var size = paa[pos + 4] | (paa[pos + 5] << 8) | (paa[pos + 6] << 16);
    Check(size == (w / 4) * (hgt / 4) * 16, $"mip {m} {w}x{hgt} size {size}");
    if (m == 0) mip0 = paa[(pos + 7)..(pos + 7 + size)];
    sizes.Add(w);
    pos += 7 + size;
}
Check(sizes.SequenceEqual(new[] { 64, 32, 16, 8, 4 }), "mip chain 64..4: " + string.Join(",", sizes));
Check(paa.Length - pos == 6 && paa[pos..].All(b => b == 0), "paa zero terminator");

// Independent DXT5 decode of mip 0 vs a box-downscaled source.
double err = 0; var alphaErr = 0.0;
for (var by = 0; by < 16; by++)
    for (var bx = 0; bx < 16; bx++)
    {
        var o = (by * 16 + bx) * 16;
        int a0 = mip0[o], a1 = mip0[o + 1];
        ulong abits = 0; for (var k = 0; k < 6; k++) abits |= (ulong)mip0[o + 2 + k] << (8 * k);
        var c0 = (ushort)(mip0[o + 8] | (mip0[o + 9] << 8)); var c1 = (ushort)(mip0[o + 10] | (mip0[o + 11] << 8));
        int[] C(ushort c) => [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31];
        var p0 = C(c0); var p1 = C(c1);
        var pal = new[] { p0, p1, new[] { (2 * p0[0] + p1[0]) / 3, (2 * p0[1] + p1[1]) / 3, (2 * p0[2] + p1[2]) / 3 }, new[] { (p0[0] + 2 * p1[0]) / 3, (p0[1] + 2 * p1[1]) / 3, (p0[2] + 2 * p1[2]) / 3 } };
        var cbits = BitConverter.ToUInt32(mip0, o + 12);
        for (var k = 0; k < 16; k++)
        {
            var px = bx * 4 + k % 4; var py = by * 4 + k / 4;
            var sx = px * 200 / 64; var sy = py * 120 / 64;
            var si = (sy * 200 + sx) * 4;
            var col = pal[(cbits >> (2 * k)) & 3];
            err += Math.Abs(col[0] - src[si]) + Math.Abs(col[1] - src[si + 1]) + Math.Abs(col[2] - src[si + 2]);
            var ai = (int)((abits >> (3 * k)) & 7);
            var aval = ai == 0 ? a0 : ai == 1 ? a1 : a0 > a1 ? ((8 - ai) * a0 + (ai - 1) * a1) / 7 : 0;
            alphaErr += Math.Abs(aval - src[si + 3]);
        }
    }
Check(err / (64 * 64 * 3) < 12, $"dxt5 colour error per channel {err / (64 * 64 * 3):F2} < 12");
Check(alphaErr / (64 * 64) < 12, $"dxt5 alpha error {alphaErr / (64 * 64):F2} < 12");

// --- Wwise bank: first embedded wem -------------------------------------------------
var wem = new byte[] { 1, 2, 3, 4, 5 };
using (var ms = new MemoryStream())
using (var bw = new BinaryWriter(ms))
{
    bw.Write("BKHD"u8.ToArray()); bw.Write(4); bw.Write(0);
    bw.Write("DIDX"u8.ToArray()); bw.Write(12); bw.Write(1234); bw.Write(0); bw.Write(wem.Length);
    bw.Write("DATA"u8.ToArray()); bw.Write(wem.Length); bw.Write(wem);
    Check(Wwise.FirstWem(ms.ToArray()) is { } got && got.SequenceEqual(wem), "bnk DIDX/DATA extraction");
}

// --- Ogg Vorbis encoder (private EncodeVorbis via reflection) ----------------------
var enc = typeof(Audio).GetMethod("EncodeVorbis", BindingFlags.NonPublic | BindingFlags.Static)!;
var tone = new float[2][];
for (var c = 0; c < 2; c++) { tone[c] = new float[48000]; for (var i = 0; i < 48000; i++) tone[c][i] = (float)(0.4 * Math.Sin(2 * Math.PI * 440 * i / 48000)); }
var oggPath = Path.Combine(Path.GetTempPath(), "rdz_test.ogg");
using (var fs = File.Create(oggPath)) enc.Invoke(null, new object[] { fs, 2, 48000, tone });
var ogg = File.ReadAllBytes(oggPath);
Check(ogg.Length > 2000 && System.Text.Encoding.ASCII.GetString(ogg, 0, 4) == "OggS", $"ogg written ({ogg.Length} bytes) -> {oggPath}");

Console.WriteLine(failures == 0 ? "ALL PASSED" : $"{failures} FAILED");
return failures == 0 ? 0 : 1;
