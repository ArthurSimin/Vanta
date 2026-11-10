#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>

// One translation context for the whole application. Per-class tr() contexts
// are not used because many user-facing strings live in free functions and in
// non-QObject core types; Q_DECLARE_TR_FUNCTIONS gives every one of them a
// static Vanta::tr() that lupdate extracts under the "Vanta" context.
class Vanta
{
    Q_DECLARE_TR_FUNCTIONS(Vanta)
};

namespace I18n {

struct Language
{
    QString code;        // stable value stored in settings.json
    QString nativeName;  // shown in the picker, written in that language
    QString englishName; // used by the translation docs and pull requests
};

// Selectable languages except the source language and "System default", which
// the settings dialog adds itself. Only languages with a compiled catalog are
// listed; an unfinished catalog belongs in translations/ until it is complete.
const QList<Language> &languages();

// True when a catalog for `code` is compiled into the binary.
bool hasCatalog(const QString &code);

// Resolves "system" (or an empty value) against the desktop locale and returns
// the code of the matching catalog, or "en" when none matches.
QString resolveCode(const QString &code);

// Installs the catalog for `code` together with Qt's own qtbase catalog when it
// is present, replacing any previously installed one. Returns false when the
// code has no catalog, so the caller can stay on the source language.
bool install(const QString &code);

// The code of the catalog that is actually active ("en" when the source
// language is shown). The scan cache stores translated report text, so it is
// invalidated when this value changes between runs.
QString currentCode();

} // namespace I18n
