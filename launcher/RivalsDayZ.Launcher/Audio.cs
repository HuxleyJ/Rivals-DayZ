using System.Diagnostics;
using OggVorbisEncoder;

namespace RivalsDayZ.Launcher;

internal static class Wwise
{
    //! First embedded .wem in a Wwise .bnk (DIDX index + DATA section), or null.
    public static byte[]? FirstWem(byte[] bnk)
    {
        int didx = -1, didxLen = 0, data = -1;
        for (var p = 0; p + 8 <= bnk.Length;)
        {
            var tag = System.Text.Encoding.ASCII.GetString(bnk, p, 4);
            var len = BitConverter.ToInt32(bnk, p + 4);
            if (len < 0)
                break;
            if (tag == "DIDX") { didx = p + 8; didxLen = len; }
            if (tag == "DATA") data = p + 8;
            p += 8 + len;
        }
        if (didx < 0 || data < 0 || didxLen < 12)
            return null;
        var offset = BitConverter.ToInt32(bnk, didx + 4);
        var size = BitConverter.ToInt32(bnk, didx + 8);
        if (data + offset + size > bnk.Length)
            return null;
        return bnk.AsSpan(data + offset, size).ToArray();
    }
}

internal static class Audio
{
    //! Decode a Wwise .wem with the bundled vgmstream-cli, then encode Ogg Vorbis for DayZ.
    public static bool WemToOgg(string vgmstream, byte[] wem, string outOgg)
    {
        var tmp = Path.Combine(Path.GetTempPath(), "RivalsDayZ-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tmp);
        try
        {
            var inPath = Path.Combine(tmp, "in.wem");
            var wavPath = Path.Combine(tmp, "out.wav");
            File.WriteAllBytes(inPath, wem);
            var psi = new ProcessStartInfo(vgmstream, $"-o \"{wavPath}\" \"{inPath}\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            using (var p = Process.Start(psi)!)
            {
                p.StandardOutput.ReadToEnd();
                p.WaitForExit(30000);
            }
            if (!File.Exists(wavPath))
                return false;
            var (channels, rate, samples) = ReadWav(File.ReadAllBytes(wavPath));
            if (samples.Length == 0 || samples[0].Length == 0)
                return false;
            Directory.CreateDirectory(Path.GetDirectoryName(outOgg)!);
            using var fs = File.Create(outOgg);
            EncodeVorbis(fs, channels, rate, samples);
            return true;
        }
        finally
        {
            try { Directory.Delete(tmp, true); } catch { /* temp */ }
        }
    }

    //! 16-bit PCM WAV -> per-channel float samples.
    private static (int Channels, int Rate, float[][] Samples) ReadWav(byte[] wav)
    {
        int channels = 0, rate = 0, bits = 0, dataPos = -1, dataLen = 0;
        for (var p = 12; p + 8 <= wav.Length;)
        {
            var tag = System.Text.Encoding.ASCII.GetString(wav, p, 4);
            var len = BitConverter.ToInt32(wav, p + 4);
            if (tag == "fmt ")
            {
                channels = BitConverter.ToInt16(wav, p + 10);
                rate = BitConverter.ToInt32(wav, p + 12);
                bits = BitConverter.ToInt16(wav, p + 22);
            }
            if (tag == "data")
            {
                dataPos = p + 8;
                dataLen = Math.Min(len, wav.Length - dataPos);
                break;
            }
            p += 8 + len + (len & 1);
        }
        if (bits != 16 || channels <= 0 || dataPos < 0)
            return (0, 0, []);
        var frames = dataLen / (2 * channels);
        var samples = new float[channels][];
        for (var c = 0; c < channels; c++)
            samples[c] = new float[frames];
        for (var f = 0; f < frames; f++)
            for (var c = 0; c < channels; c++)
                samples[c][f] = BitConverter.ToInt16(wav, dataPos + (f * channels + c) * 2) / 32768f;
        return (channels, rate, samples);
    }

    private static void EncodeVorbis(Stream output, int channels, int rate, float[][] samples)
    {
        var info = VorbisInfo.InitVariableBitRate(channels, rate, 0.5f);
        var stream = new OggStream(new Random().Next());
        stream.PacketIn(HeaderPacketBuilder.BuildInfoPacket(info));
        stream.PacketIn(HeaderPacketBuilder.BuildCommentsPacket(new Comments()));
        stream.PacketIn(HeaderPacketBuilder.BuildBooksPacket(info));
        Flush(stream, output, true);

        var state = ProcessingState.Create(info);
        const int chunk = 1024;
        var frames = samples[0].Length;
        var buffer = new float[channels][];
        for (var c = 0; c < channels; c++)
            buffer[c] = new float[chunk];
        for (var start = 0; start < frames; start += chunk)
        {
            var n = Math.Min(chunk, frames - start);
            for (var c = 0; c < channels; c++)
                Array.Copy(samples[c], start, buffer[c], 0, n);
            state.WriteData(buffer, n);
            Drain(state, stream, output);
        }
        state.WriteEndOfStream();
        Drain(state, stream, output);
        Flush(stream, output, true);
    }

    private static void Drain(ProcessingState state, OggStream stream, Stream output)
    {
        while (!stream.Finished && state.PacketOut(out var packet))
        {
            stream.PacketIn(packet);
            Flush(stream, output, false);
        }
    }

    private static void Flush(OggStream stream, Stream output, bool force)
    {
        while (stream.PageOut(out var page, force))
        {
            output.Write(page.Header, 0, page.Header.Length);
            output.Write(page.Body, 0, page.Body.Length);
        }
    }
}
