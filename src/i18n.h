#pragma once
#include <string>
#include <map>

// Translations are external and optional.
//
// * Every user-visible string lives in the source under a dotted key, with English as the built-in
//   default (translation units fall back to English, so a partial pack is fine).
// * A pack is one UTF-8 file:  <exe dir>\lang\<code>.ini   e.g. lang\zh-CN.ini, lang\ja-JP.ini
//       [ui]
//       ui.btn.autoSync=一键自动同步
//       ui.hint.onStart=每次启动后自动同步一次
//   The file may be produced by any tool; no recompilation is needed to load a pack.
// * Prepared (but not shipped) language codes: zh-CN, zh-TW, fr-FR, it-IT, ru-RU, ja-JP.
// * `--export-strings` writes lang\strings.default.ini with every key and its English text so a
//   translator can start from a complete file.

namespace i18n {

// Returns the value for `key`: the current language's override, else the English default.
// Returns the key itself when neither exists (so a missing key is visible, never empty).
std::wstring T(const wchar_t* key);
const wchar_t* Tc(const wchar_t* key);           // same, avoids the temporary for plain calls
std::wstring Tf(const wchar_t* key, ...);        // T(key) as a wprintf format string

struct LangEntry {
    wchar_t code[12];       // file stem, e.g. L"zh-CN"
    wchar_t native[48];     // name in that language, as shown in the picker, e.g. L"简体中文"
};

const LangEntry* Languages(int* count);          // all offered languages, index 0 = English
bool PackExists(int index);                      // lang\<code>.ini present (English is always built in)

bool Init(int* detectedIndex);                   // loads the saved language; returns false only on I/O errors
bool Select(int index, std::wstring* err);       // switch language at runtime (may be an unpacked one)
int  Current();

bool ExportDefaults(std::wstring* err);          // writes lang\strings.default.ini for translators
const wchar_t* LangCode(int i);                     // "zh-CN", "fr-FR", ...
const wchar_t* LangNative(int i);                   // name as shown in the picker
int StringCount();                                  // number of translation keys (for a log line)

} // namespace i18n

std::wstring Tf(const wchar_t* key, ...);        // formatting shorthand (same values as i18n::Tf)
