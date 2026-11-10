#include "I18n.h"
#include "MainWindow.h"
#include "Storage.h"
#include "Version.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--version") || arg == QStringLiteral("-v")) {
            fprintf(stdout, "Vanta %s\n", VANTA_VERSION);
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Vanta"));
    QApplication::setApplicationDisplayName(QStringLiteral("Vanta"));
    QApplication::setApplicationVersion(QStringLiteral(VANTA_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Vanta"));
    QApplication::setOrganizationDomain(QStringLiteral("github.com"));
    QApplication::setDesktopFileName(QStringLiteral(VANTA_APP_ID));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/vanta.svg")));

    QLocale::setDefault(QLocale::system());

    // The catalog is installed before the first window exists, so the first
    // paint is already translated. Storage reads settings.json without needing
    // the library, so a saved language is known this early.
    Storage storage;
    QStringList loadWarnings;
    I18n::install(storage.loadSettings(&loadWarnings).language);

    MainWindow window(loadWarnings);
    window.show();
    return app.exec();
}
