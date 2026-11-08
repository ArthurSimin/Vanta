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
    void shutdown();

signals:
    void gameStarted(const QString &gameId, const QDateTime &startedAt);
    void gameFinished(const QString &gameId, qint64 elapsedSeconds, int exitCode,
                      bool crashed, bool stopped, const QString &diagnostic,
                      const QString &logPath);
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
        bool failureEmitted = false;
        QByteArray output;
        QByteArray errorOutput;
        LaunchPlan plan;
        QString gameName;
        QString executableBaseName;
        QDateTime startedAt;
    };

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
