# Language packs / 语言包说明

English is the default language, compiled into the C++ sources. Every other language is a pack:
**a plain UTF-8 file in this folder (`lang\`) that is compiled into the exe** as an `RT_RCDATA`
resource (see `src\resource\app.rc`). Nothing is read from disk at runtime, so the exe carries all
of its translations and runs as a single self-contained file.

英语是写在 C++ 源码里的默认语言；其他语言都是**本目录下的源码文件**，由构建脚本**编译进 exe**
（在 `src\resource\app.rc` 里以 `RT_RCDATA` 资源嵌入）。运行时不读任何外部文件，exe 自带全部
翻译、单文件即用。

> 这里的“外置”是**源码层面**的外置：翻译内容独立成文件，不混进 C++ 代码；**最终交付的只有
> exe 一个文件**，语言包不需要随 exe 分发。

## Included packs / 已附带的语言包

| Source file (this folder) | Code / resource id | Language / 语言 |
| --- | --- | --- |
| `zh-CN.ini` | `zh-CN` / 101 | 简体中文 Simplified Chinese |
| `zh-TW.ini` | `zh-TW` / 102 | 繁體中文 Traditional Chinese |
| `fr-FR.ini` | `fr-FR` / 103 | Français French |
| `it-IT.ini` | `it-IT` / 104 | Italiano Italian |
| `ru-RU.ini` | `ru-RU` / 105 | Русский Russian |
| `ja-JP.ini` | `ja-JP` / 106 | 日本語 Japanese |

All six are complete (186 keys each; verified: no missing keys, no placeholder mismatches, and
every screen checked visually for overlap and truncation). 第一次运行按系统语言自动选择，之后由
右上角 **Language** 下拉框切换，选择会被记住；下拉框始终可用，因为语言都在 exe 内部。

## Adding or updating a pack / 新增或修改语言包

1. Copy any pack here as a starting point, e.g. `de-DE.ini`, and translate the value after each `=`.
2. Register it with three one-line edits:
   - `src\resource\resource.h` → add `IDR_LANG_XXX`;
   - `src\resource\app.rc` → add `IDR_LANG_XXX RCDATA "../../lang/de-DE.ini"`;
   - `src\i18n.cpp` → append the code/native name to `kLangs[]` and the id to `kLangIds[]`.
3. Rebuild with `build\build.bat`. The language appears in the combo box automatically.

**Updating an existing pack needs no C++ changes** — edit its `.ini` and rebuild.

## File format / 文件格式

UTF-8 (with or without BOM), one key per line, `key=translation`, `;` or `#` starts a comment:

```
; comments are ignored
ui.btn.auto=一键自动同步
ui.chk.timed=定时自动同步
ntp.timeout=无响应
```

Rules / 规则：

- **Partial packs are fine.** Any key you do not translate falls back to English, so you can
  translate in batches. 未翻译的键自动回退英语，可以分批翻译。
- **`%s` `%d` `%02d` `%lu` `%04d` etc. must stay** in the translation — they are placeholders the
  program fills in. Never translate, drop or reorder them without matching the meaning.
  占位符必须原样保留，不可翻译、删除或改动顺序。
- Keys are dotted and namespaced by area: `ui.*` window/controls, `log.*` log lines, `eng.*` sync
  engine, `ntp.*` NTP protocol messages, `cfg.*` config validation, `as.*` scheduled task,
  `ntpName.*` built-in server names (addresses never change).
  键按模块分前缀，便于分工；`ntpName.*` 是内置服务器名，**地址永远不会被翻译**。
- Keep button and label wording short: the UI measures and sizes controls from the translated
  text, but a very long line can still crowd its row — the program logs a `layoutTight` warning
  when that happens.  按钮/标签请用该语言的通用缩写，避免整行放不下（放不下时程序会记
  `layoutTight` 告警）。

## Getting the list of keys / 如何获得全部键

```
NTPTimeSync.exe --export-strings
```

writes `strings.default.ini` beside the exe: every key with its English text, ready to be copied
and translated.  程序会在 exe 旁生成 `strings.default.ini`，含全部键和英文原文，复制一份改名即可
开始翻译。

## Testing a pack / 测试

1. Edit the `.ini`, rebuild, start the program, pick the language in the top-right combo box.
2. The log window shows `Language switched to <code>`.
3. Missing keys appear in English — that is expected, not an error.
   未翻译的键显示英文是正常现象。
