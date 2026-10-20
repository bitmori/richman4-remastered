# 參與貢獻 / Contributing

本專案目前**可以完整遊玩**，但仍可能缺少部分原版動畫序列、對白或其他遊戲內容，或與原版有細微差異。
已知差異見 [`docs/STATUS.md`](docs/STATUS.md) 與 [`docs/audit/`](docs/audit/)。歡迎提出 Pull Request！

The game is **fully playable**, but some original animation sequences, dialogue or other game content may
still be missing or differ slightly from the original. Known gaps are listed in
[`docs/STATUS.md`](docs/STATUS.md) and [`docs/audit/`](docs/audit/). Pull requests are welcome!

## 合併前的要求 / Before a PR can be merged

1. **以原版為準 / Follow the original.** 行為與畫面須對照原版程式（[mytbk/rich4](https://github.com/mytbk/rich4)
   的反組譯，或在 Wine 下執行原版）。請在程式註解或 `docs/` 中寫出依據（asm 檔名與行號、位址、素材編號）。
   Base behaviour and visuals on the original program and cite the source (asm file/line or address, MKF
   entry/frame) in comments or `docs/`.
2. **實際試玩 / Play-test it.** 在至少一個平台（macOS、Android 或 iOS）實際玩到你修改的情境，並附上截圖或錄影。
   Actually play the affected situation on at least one platform (macOS, Android or iOS) and attach
   screenshots or a recording.
3. **可重現的結果 / Reproducible outcome.** 提供別人能重現的步驟，例如：
   Provide steps anyone can reproduce, for example:
   - 地圖、seed 與指令：`python3 scripts/run_native_playability.py --map 7 --seed 12 --turns 30`
   - 或產生測試存檔的程式／`rich4-native-fixture` 參數，以及 `--load SAVE.DAT --start-loaded` 的步驟；
     請勿附上含有遊戲素材的檔案（存檔本身可以）。
     or the fixture/save generator and the `--load SAVE.DAT --start-loaded` steps; never attach files
     that contain game assets (save files are fine).
   - 修改規則時附上前後的 `report.json`／事件紀錄差異。
     For rule changes, include before/after `report.json` or event-log differences.
4. **測試全部通過 / All checks pass.**

   ```sh
   cmake --build build/native --parallel && ctest --test-dir build/native --output-on-failure
   python3 scripts/run_native_playability.py --turns 30 --seed 42
   python3 scripts/run_native_acceptance.py --binary build/native/rich4-portable.app/Contents/MacOS/rich4-portable \
     --fixture-binary build/native/rich4-native-fixture --workers 2
   ```

   規則或亂數的改動不得破壞原版金標比對（`RICH4_ORACLE_DIR`）。
   Rule or RNG changes must not break the original-EXE comparisons (`RICH4_ORACLE_DIR`).
5. **不要提交遊戲內容 / Never commit game content** — no MKF, images, audio, executables, fonts or builds
   that contain them. See [`NOTICE.md`](NOTICE.md).
6. **觸控優先 / Touch first.** 原版只能用滑鼠懸停或右鍵的操作，須有觸控可用的替代方式。
   Anything the original does on mouse hover or right-click needs a touch equivalent.

## 平台測試狀態 / Platform test status

| 平台 / Platform | 狀態 / Status |
| --- | --- |
| macOS (Apple Silicon) | ✅ 已測試 / tested |
| Android | ✅ 真機（OnePlus 8）與模擬器 / device (OnePlus 8) and emulator |
| iOS | ✅ 模擬器；真機尚未測試 / simulator; device not yet tested |
| Windows | ⚠️ 尚未測試 / not tested |
