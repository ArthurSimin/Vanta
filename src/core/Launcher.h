#pragma once

#include "Game.h"
#include "LaunchPlan.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>

class GameLauncher : public QObject
{
    Q_OBJECT

public:
    explicit GameLauncher(QObject *parent = nullptr);
    ~GameLauncher() override;

    bool launch(const Game &game, const LaunchPlan &plan, QString *error = nullptr);
    void stopGame(const QString &gameId);
    bool isRunning(const QString &gameId) const;
    bool isStopping(const QString &gameId) const;
    QStringList runningGameIds() const;
    QString diagnosticLog(const QString &gameId) const;
    // Stops every running game. `reportStopped` forwards each session to
    // gameFinished so playtime and the launch log survive the quit; the
    // destructor passes false because no receiver should run that late.
    void shutdown(bool reportStopped = true);

signals:
    void gameStarted(const QString &gameId, const QDateTime &startedAt);
    void gameFinished(const QString &gameId, qint64 elapsedSeconds, int exitCode,
                      bool crashed, bool stopped, const QString &diagnostic,
                      const QString &logPath);
    // Emitted the moment the game process exits, before the prefix teardown
    // finishes. The UI clears its running state from this so a game that exits
    // on its own stops showing Stop without waiting on wineserver.
    void gameExited(const QString &gameId);
    void launchFailed(const QString &gameId, const QString &message, const QString &diagnostic,
                      const QString &logPath);
    void gameStopping(const QString &gameId);

private:
    struct Instance
    {
        QPointer<QProcess> process;
        QElapsedTimer clock;
        qint64 elapsedAtExitMs = 0;
        bool stopped = false;
        bool stopInProgress = false;
        bool started = false;
        bool running = false;
        bool failureEmitted = false;
        QByteArray output;
        QByteArray errorOutput;
        QString liveLogPath;
        LaunchPlan plan;
        QString gameName;
        QString executableBaseName;
        QDateTime startedAt;
    };

    // Signals the game process group so launcher-script children stop too. A
    // group that cannot be resolved falls back to the QProcess itself.
    static void signalProcessGroup(const Instance &instance, int sig);
    void collectOutput(const QString &gameId);
    void runTerminationFallback(const QString &gameId);
    void beginTeardown(const QString &gameId);
    void completeInstance(const QString &gameId, int exitCode, QProcess::ExitStatus exitStatus);
    void emitFailure(const QString &gameId, const QString &message);
    QString outputLog(const Instance &instance) const;
    QString persistLog(const QString &gameId, const Instance &instance, int exitCode, bool crashed,
                       bool stopped, const QString &output) const;

    QHash<QString, Instance> m_instances;
};
