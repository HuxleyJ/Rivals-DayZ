namespace RivalsDayZ.Launcher;

//! Writes DayZ .paa textures (DXT5, uncompressed mip data, power-of-two square) from RGBA.
//! Layout: type 0xFF05, TAGG blocks (average colour, max colour, alpha flag, mip offsets),
//! empty palette, mipmaps (u16 w, u16 h, u24 size, DXT5 blocks), zero terminator.
internal static class Paa
{
    //! CUE4Parse decodes to BGRA or RGBA depending on the source; normalise to RGBA.
    public static byte[]? ToRgba(byte[] data, string pixelFormat)
    {
        if (pixelFormat.Contains("B8G8R8A8"))
        {
            var o = new byte[data.Length];
            for (var i = 0; i + 3 < data.Length; i += 4)
            {
                o[i] = data[i + 2];
                o[i + 1] = data[i + 1];
                o[i + 2] = data[i];
                o[i + 3] = data[i + 3];
            }
            return o;
        }
        if (pixelFormat.Contains("R8G8B8A8"))
            return data;
        return null;
    }

    public static void WriteDxt5(string path, byte[] rgba, int width, int height, int size)
    {
        var img = Resize(rgba, width, height, size);
        var mips = new List<(int Size, byte[] Rgba)>();
        for (var s = size; s >= 4; s /= 2)
        {
            mips.Add((s, img));
            if (s > 4)
                img = Half(img, s);
        }

        long r = 0, g = 0, b = 0, a = 0;
        var hasAlpha = false;
        var top = mips[0].Rgba;
        for (var i = 0; i < top.Length; i += 4)
        {
            r += top[i];
            g += top[i + 1];
            b += top[i + 2];
            a += top[i + 3];
            hasAlpha |= top[i + 3] < 255;
        }
        var n = top.Length / 4;

        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var fs = File.Create(path);
        using var w = new BinaryWriter(fs);
        w.Write((ushort)0xFF05);
        WriteTag(w, "CGVA", BitConverter.GetBytes((uint)((a / n << 24) | (r / n << 16) | (g / n << 8) | (b / n))));
        WriteTag(w, "CXAM", BitConverter.GetBytes(0xFFFFFFFFu));
        WriteTag(w, "GALF", BitConverter.GetBytes(hasAlpha ? 1u : 0u));
        var offsPos = fs.Position + 12; // after "GGAT" + name + u32 length
        WriteTag(w, "SFFO", new byte[64]);
        w.Write((ushort)0); // no palette

        var offsets = new uint[16];
        for (var m = 0; m < mips.Count && m < 16; m++)
        {
            offsets[m] = (uint)fs.Position;
            var blocks = EncodeDxt5(mips[m].Rgba, mips[m].Size);
            w.Write((ushort)mips[m].Size);
            w.Write((ushort)mips[m].Size);
            w.Write((byte)(blocks.Length & 0xFF));
            w.Write((byte)((blocks.Length >> 8) & 0xFF));
            w.Write((byte)((blocks.Length >> 16) & 0xFF));
            w.Write(blocks);
        }
        w.Write(new byte[6]);

        fs.Position = offsPos;
        foreach (var o in offsets)
            w.Write(o);
    }

    private static void WriteTag(BinaryWriter w, string name, byte[] data)
    {
        w.Write("GGAT"u8.ToArray());
        w.Write(System.Text.Encoding.ASCII.GetBytes(name));
        w.Write((uint)data.Length);
        w.Write(data);
    }

    //! Box-filtered resize to a size x size square.
    private static byte[] Resize(byte[] src, int w, int h, int size)
    {
        var dst = new byte[size * size * 4];
        for (var y = 0; y < size; y++)
        {
            var y0 = y * h / size;
            var y1 = Math.Max(y0 + 1, (y + 1) * h / size);
            for (var x = 0; x < size; x++)
            {
                var x0 = x * w / size;
                var x1 = Math.Max(x0 + 1, (x + 1) * w / size);
                int r = 0, g = 0, b = 0, a = 0, c = 0;
                for (var sy = y0; sy < y1 && sy < h; sy++)
                    for (var sx = x0; sx < x1 && sx < w; sx++)
                    {
                        var i = (sy * w + sx) * 4;
                        r += src[i];
                        g += src[i + 1];
                        b += src[i + 2];
                        a += src[i + 3];
                        c++;
                    }
                var d = (y * size + x) * 4;
                c = Math.Max(c, 1);
                dst[d] = (byte)(r / c);
                dst[d + 1] = (byte)(g / c);
                dst[d + 2] = (byte)(b / c);
                dst[d + 3] = (byte)(a / c);
            }
        }
        return dst;
    }

    private static byte[] Half(byte[] src, int size) => Resize(src, size, size, size / 2);

    private static byte[] EncodeDxt5(byte[] rgba, int size)
    {
        var outp = new byte[(size / 4) * (size / 4) * 16];
        var o = 0;
        var block = new byte[64];
        for (var by = 0; by < size; by += 4)
        {
            for (var bx = 0; bx < size; bx += 4)
            {
                for (var y = 0; y < 4; y++)
                    Array.Copy(rgba, ((by + y) * size + bx) * 4, block, y * 16, 16);
                EncodeAlpha(block, outp, o);
                EncodeColor(block, outp, o + 8);
                o += 16;
            }
        }
        return outp;
    }

    private static void EncodeAlpha(byte[] block, byte[] outp, int o)
    {
        byte max = 0, min = 255;
        for (var i = 0; i < 16; i++)
        {
            max = Math.Max(max, block[i * 4 + 3]);
            min = Math.Min(min, block[i * 4 + 3]);
        }
        outp[o] = max;
        outp[o + 1] = min;
        ulong bits = 0;
        for (var i = 0; i < 16; i++)
        {
            int a = block[i * 4 + 3];
            int idx;
            if (max == min)
                idx = 0;
            else
            {
                // 8-value mode: palette 0 = max, 1 = min, 2..7 = interpolated from max toward min.
                var t = (int)Math.Round((max - a) * 7.0 / (max - min));
                idx = t switch { 0 => 0, 7 => 1, _ => t + 1 };
            }
            bits |= (ulong)idx << (3 * i);
        }
        for (var i = 0; i < 6; i++)
            outp[o + 2 + i] = (byte)(bits >> (8 * i));
    }

    private static void EncodeColor(byte[] block, byte[] outp, int o)
    {
        int bestMax = -1, bestMin = int.MaxValue, iMax = 0, iMin = 0;
        for (var i = 0; i < 16; i++)
        {
            var l = block[i * 4] * 299 + block[i * 4 + 1] * 587 + block[i * 4 + 2] * 114;
            if (l > bestMax) { bestMax = l; iMax = i; }
            if (l < bestMin) { bestMin = l; iMin = i; }
        }
        var c0 = To565(block, iMax);
        var c1 = To565(block, iMin);
        if (c0 < c1)
            (c0, c1) = (c1, c0);
        outp[o] = (byte)c0;
        outp[o + 1] = (byte)(c0 >> 8);
        outp[o + 2] = (byte)c1;
        outp[o + 3] = (byte)(c1 >> 8);
        var palette = new int[4][];
        palette[0] = From565(c0);
        palette[1] = From565(c1);
        palette[2] = [(2 * palette[0][0] + palette[1][0]) / 3, (2 * palette[0][1] + palette[1][1]) / 3, (2 * palette[0][2] + palette[1][2]) / 3];
        palette[3] = [(palette[0][0] + 2 * palette[1][0]) / 3, (palette[0][1] + 2 * palette[1][1]) / 3, (palette[0][2] + 2 * palette[1][2]) / 3];
        uint bits = 0;
        for (var i = 0; i < 16; i++)
        {
            var best = 0;
            var bestD = int.MaxValue;
            for (var p = 0; p < 4; p++)
            {
                var dr = block[i * 4] - palette[p][0];
                var dg = block[i * 4 + 1] - palette[p][1];
                var db = block[i * 4 + 2] - palette[p][2];
                var d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = p; }
            }
            if (c0 == c1)
                best = 0;
            bits |= (uint)best << (2 * i);
        }
        outp[o + 4] = (byte)bits;
        outp[o + 5] = (byte)(bits >> 8);
        outp[o + 6] = (byte)(bits >> 16);
        outp[o + 7] = (byte)(bits >> 24);
    }

    private static ushort To565(byte[] block, int i) =>
        (ushort)(((block[i * 4] >> 3) << 11) | ((block[i * 4 + 1] >> 2) << 5) | (block[i * 4 + 2] >> 3));

    private static int[] From565(ushort c) =>
        [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31];
}
