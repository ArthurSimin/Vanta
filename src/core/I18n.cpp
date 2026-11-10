#include "I18n.h"

#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

QString matchLocale(const QLocale &locale)
{
    const QString name = locale.name();
    const QList<I18n::Language> &available = I18n::languages();
    // An exact region match (pt_BR) wins over a language-only match (pt).
    for (const I18n::Language &language : available) {
        if (name.compare(language.code, Qt::CaseInsensitive) == 0)
            return language.code;
    }
    const QString base = name.section(QLatin1Char('_'), 0, 0);
    for (const I18n::Language &language : available) {
        if (language.code.compare(base, Qt::CaseInsensitive) == 0)
            return language.code;
    }
    return QStringLiteral("en");
}

QString catalogPath(const QString &code)
{
    return QStringLiteral(":/i18n/vanta_%1.qm").arg(code);
}

// Tracked so Storage can invalidate a scan cache whose report text was written
// in another language.
QString g_currentCode = QStringLiteral("en");

} // namespace

const QList<I18n::Language> &I18n::languages()
{
    // Portuguese (Brazil) is listed before Portuguese so an exact pt_BR locale
    // resolves to the Brazilian catalog, and a bare pt locale to the European one.
    static const QList<Language> list = {
        { QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)"), QStringLiteral("Portuguese (Brazil)") },
        { QStringLiteral("pt"), QStringLiteral("Português"), QStringLiteral("Portuguese") },
        { QStringLiteral("es"), QStringLiteral("Español"), QStringLiteral("Spanish") },
        { QStringLiteral("ru"), QStringLiteral("Русский"), QStringLiteral("Russian") },
    };
    return list;
}

bool I18n::hasCatalog(const QString &code)
{
    if (code == QStringLiteral("en") || code.isEmpty())
        return false;
    return QFile::exists(catalogPath(code));
}

QString I18n::resolveCode(const QString &code)
{
    if (code.isEmpty() || code == QStringLiteral("system"))
        return matchLocale(QLocale::system());
    return code;
}

bool I18n::install(const QString &code)
{
    static QTranslator appTranslator;
    static QTranslator qtTranslator;
    QCoreApplication::removeTranslator(&appTranslator);
    QCoreApplication::removeTranslator(&qtTranslator);

    const QString resolved = resolveCode(code);
    g_currentCode = QStringLiteral("en");
    if (resolved == QStringLiteral("en"))
        return true;

    bool installed = false;
    const QString path = catalogPath(resolved);
    if (QFile::exists(path) && appTranslator.load(path))
        installed = QCoreApplication::installTranslator(&appTranslator);
    if (installed)
        g_currentCode = resolved;

    // Qt ships translations for its own strings (standard dialog buttons, the
    // line-edit context menu). They come from the system Qt package, so a
    // missing file is not an error; the English text stays as the fallback.
    const QLocale locale(resolved);
    const QString qtDirectory = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), qtDirectory))
        QCoreApplication::installTranslator(&qtTranslator);

    return installed;
}

QString I18n::currentCode()
{
    return g_currentCode;
}
