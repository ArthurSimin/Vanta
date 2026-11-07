#include "MainWindow.h"
#include "Version.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Vanta"));
    QApplication::setApplicationDisplayName(QStringLiteral("Vanta"));
    QApplication::setApplicationVersion(QStringLiteral(VANTA_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Vanta"));
    QApplication::setOrganizationDomain(QStringLiteral("github.com"));
    QApplication::setDesktopFileName(QStringLiteral(VANTA_APP_ID));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/vanta.svg")));

    QLocale::setDefault(QLocale::system());

    MainWindow window;
    window.show();
    return app.exec();
}
