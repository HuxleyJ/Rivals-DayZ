# Credits and third-party software

Rivals x DayZ uses DayZ (Bohemia Interactive) and Marvel Rivals (NetEase Games / Marvel) content **from the player's own installs**. No game files from either are included in this mod.

Bundled with the launcher (`RivalsDayZ.exe`, `tools/vgmstream`):

| Component | Author | License | Used for |
|---|---|---|---|
| [CUE4Parse](https://github.com/FabianFG/CUE4Parse) and CUE4Parse-Conversion | FabianFG and contributors | Apache-2.0 | Reading Unreal Engine files in the player's Marvel Rivals install |
| [Mono.Nat](https://github.com/alanmcgovern/Mono.Nat) | Alan McGovern and contributors | MIT | Opening the host's game port with UPnP / NAT-PMP |
| [OggVorbisEncoder](https://github.com/SteveLillis/.NET-Ogg-Vorbis-Encoder) | Steve Lillis and contributors | MIT | Converting Rivals audio to Ogg Vorbis for DayZ |
| [vgmstream](https://github.com/vgmstream/vgmstream) (vgmstream-cli and its DLLs) | Adam Gashlin, bnnm and contributors | ISC-style (see `tools/vgmstream/COPYING`); the bundled FFmpeg libraries are LGPL | Decoding Wwise audio |
| .NET runtime | Microsoft and the .NET Foundation | MIT | Runs the launcher |
| CUE4Parse's NuGet dependencies (Serilog, Newtonsoft.Json, BouncyCastle, ZstdSharp, K4os LZ4 and others) | their authors | MIT / Apache-2.0 / BSD (see each package) | Used by CUE4Parse |

Technical references used while building this (no code copied):

- Bohemia Interactive's [DayZ-Script-Diff](https://github.com/BohemiaInteractive/DayZ-Script-Diff) and [DayZ-Samples](https://github.com/BohemiaInteractive/DayZ-Samples)
- Arkensor's [DayZCommunityOfflineMode](https://github.com/Arkensor/DayZCommunityOfflineMode), for how to start DayZ offline
- [HEMTT](https://github.com/BrettMayson/HEMTT), for its PBO signing test fixture (used to check `tools/pbo.py`; not shipped)
- [universal-modder](https://github.com/rehan-remade/universal-modder), the modding toolkit and method

Built with Claude Code.
