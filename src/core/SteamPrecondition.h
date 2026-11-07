#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace SteamIntegration
{
QString steamInstallRoot();
QStringList steamLibraryRoots();
QString loginUsersPath();
QString findSteamRuntime(const QString &protonPath);
QString steamRuntimeAppIdForProton(const QString &protonPath);
QString steamRuntimeDirectoryForProton(const QString &protonPath);
bool steamBinaryAvailable(QString *path = nullptr);
bool steamIsRunning(QString *diagnostic = nullptr);
}

class SteamPrecondition : public QObject
{
    Q_OBJECT

public:
    explicit SteamPrecondition(QObject *parent = nullptr);
    void ensureRunning();
    void cancel();

signals:
    void ready();
    void failed(const QString &message);
    void progress(const QString &message);

private:
    void checkPid();
    void startSteam();
    void pollLoginUsers();
    void fail(const QString &message);

    QTimer *m_pollTimer = nullptr;
    qint64 m_initialLoginMtime = 0;
    bool m_initialLoginExisted = false;
    bool m_finished = false;
    int m_attempts = 0;
};
