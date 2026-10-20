# Third-party notices

The Richman 4 (大富翁4) engine in this repository is GPL-3.0-or-later (see [`LICENSE`](LICENSE)). Game
content is **not** included and is not covered by any license here; see [`NOTICE.md`](NOTICE.md).

The only third-party code committed to this repository is TinySoundFont (two MIT-licensed headers in
`third_party/TinySoundFont/`, with its `LICENSE`) and stb_vorbis (`third_party/stb/stb_vorbis.c`, its
license at the end of the file). No third-party binaries, fonts or sound banks are
committed. The build scripts and CMake download the other components below into gitignored directories
(`android/third_party/`, `ios/third_party/`, the CMake build directory) and verify each download by
SHA-256, or the Mac build links the Homebrew copies.

## Source and format knowledge

| Component | Use | License |
| --- | --- | --- |
| [mytbk/rich4](https://github.com/mytbk/rich4) @ `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e` | Disassembly used to derive file formats, rules and UI behaviour. Upstream C sources carry copyright notices for Iru Cai (2018 onward). Each record in `docs/migrations/` and `docs/ui/` names its source files and original addresses. | GPL-3.0-or-later |

## Linked into the game

| Component | Version | Platforms | License |
| --- | --- | --- | --- |
| [SDL](https://libsdl.org) | 3.4.x (3.4.16 on Android/iOS) | all | zlib |
| [FreeType](https://freetype.org) | 2.14.x (2.14.3 on Android/iOS) | all | FreeType License (FTL), used under the FTL option |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) (`tsf.h`, `tml.h`) @ `853a0a1` | SF2 synthesizer and MIDI loader for the original music | all | MIT |
| [stb_vorbis](https://github.com/nothings/stb) (`stb_vorbis.c`) v1.22 @ `a7d4c21` | Ogg Vorbis decoder for the Steam release's CD audio tracks | all | MIT or public domain (Unlicense), used under MIT |
| [GNU libiconv](https://www.gnu.org/software/libiconv/) | 1.18 | Android (statically linked; Android's own iconv has no Big5) | LGPL-2.1-or-later |
| System iconv | — | macOS, iOS | part of the OS |

FreeType requires this credit: *Portions of this software are copyright © The FreeType Project
(https://freetype.org). All rights reserved.*

GNU libiconv is LGPL-licensed. The combined program is distributed under the GPL-3.0, whose terms
satisfy the LGPL; the complete corresponding source is this repository plus the pinned upstream releases
fetched by `scripts/build_android.sh` and `scripts/build_ios.sh`.

## Bundled data

| Component | Use | License |
| --- | --- | --- |
| TimGM6mb (`TimGM6mb.sf2`, 5.7 MB) by Tim Brechbill, 2010 revision by David Bolton; fetched from [Debian's pristine upstream tarball](https://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/) `timgm6mb-soundfont_1.3.orig.tar.gz` | General MIDI sound bank for the original MIDI music on every platform (macOS: the app's `Resources`; Android/iOS: the app package). Loaded at run time as a separate data file. | GPL-2.0 (per Debian's `copyright`: samples are the author's own, public domain, or GPL) |
| [Noto Serif CJK TC](https://github.com/notofonts/noto-cjk) | Traditional Chinese text on Android/iOS, and the fallback when the original 細明體 is unavailable. | SIL Open Font License 1.1 |

The Mac build can use 細明體 (`mingliu.ttc`) from a locally installed Microsoft Office; that font is
proprietary, is never copied into this repository or any build, and is used only from its installed
location.

## Reference and test infrastructure (not part of the game)

| Component | Use | License |
| --- | --- | --- |
| [Boxedwine](https://github.com/danoon2/Boxedwine) 26R1.0 + Wine 11.0 | Runs the user's own original executable on macOS to capture comparison data (oracle tests) and as a reference player. | Boxedwine: GPL-2.0; Wine: LGPL-2.1-or-later |
| mingw-w64, NASM | Rebuild the upstream disassembly for reference (`scripts/build_upstream.sh`). | various open-source licenses |

The oracle tests invoke the user's local original executable; neither that binary nor its machine code
is embedded in this repository. Oracle outputs and screenshots stay in gitignored local directories.

## Game content

All Richman 4 artwork, music, voices, text, maps and executables remain © 1998–2022 SOFTSTAR
ENTERTAINMENT INC. Game files are not included; they are read at run time (and packaged into local
builds) from the user's own legally obtained copy. Text excerpts transcribed from the original program
(dialogue tables, company names, short labels and prompts) are present in the source for
interoperability; they are not covered by the GPL (see [`NOTICE.md`](NOTICE.md) §2). Buy the game on Steam: <https://store.steampowered.com/app/2059810/Richman_4/?l=tchinese>
