# Source and asset provenance

The migrated Rich4 engine is GPL-3.0-or-later. See `LICENSE`.

Algorithms and format knowledge are derived from [mytbk/rich4](https://github.com/mytbk/rich4),
commit `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`.
Upstream C sources include copyright notices for Iru Cai (2018 onward) under GPL-3.0-or-later.
Each migration record names its source files and, where known, original executable addresses.

SDL3 is linked as a separately installed dependency under the zlib license.
No SDL source, game executable, MKF archive, music, cutscene, font, or save file is committed here.
The original game and locally installed fonts remain separate user-provided inputs.

The Windows-only oracle is test infrastructure. It invokes the user's local original executable
to produce comparison data; neither that binary nor its machine code is embedded in this repository.
Oracle outputs and screenshots remain local artifacts. They are not engine dependencies.
