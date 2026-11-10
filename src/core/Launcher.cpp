#include "Launcher.h"

#include "I18n.h"
#include "LaunchLogs.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

GameLauncher::GameLauncher(QObject *parent)
    : QObject(parent)
{
}

GameLauncher::~GameLauncher()
{
    shutdown(false);
}

bool GameLauncher::launch(const Game &game, const LaunchPlan &plan, QString *error)
{
    const auto existing = m_instances.constFind(game.id);
    if (existing != m_instances.constEnd()) {
        // The process can be gone while the prefix is still being torn down. The
        // game is not running then, but a relaunch has to wait for the previous
        // instance to finish, so the caller gets a reason it can act on.
        if (error)
            *error = existing->started && !existing->running
                ? Vanta::tr("The previous session is still shutting down. Try again in a moment.")
                : Vanta::tr("This game is already running.");
        return false;
    }
    if (!plan.isValid()) {
        if (error)
            *error = Vanta::tr("The launch plan has no program.");
        return false;
    }
    if (!QFileInfo(plan.workingDirectory).isDir()) {
        if (error)
            *error = Vanta::tr("The working directory does not exist: %1").arg(plan.workingDirectory);
        return false;
    }
    if (runnerIsWineLike(plan.runner)) {
        if (plan.prefixBase.isEmpty() || !QDir().mkpath(plan.prefixBase)) {
            if (error)
                *error = Vanta::tr("Could not create the game prefix: %1").arg(plan.prefixBase);
            return false;
        }
        if (plan.runner == RunnerType::Wine && !QDir().mkpath(plan.winePrefix)) {
            if (error)
                *error = Vanta::tr("Could not create the Wine prefix: %1").arg(plan.winePrefix);
            return false;
        }
    }

    auto process = new QProcess(this);
    process->setProgram(plan.program);
    process->setArguments(plan.arguments);
    process->setWorkingDirectory(plan.workingDirectory);
    process->setProcessEnvironment(plan.environment);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    // A native game is often a launcher script whose real process is a child.
    // Killing only the direct child leaves the game running after Vanta quits,
    // so the game gets its own process group and both stop paths can signal the
    // whole group. Qt offers no way to set a process group portably before 6.2,
    // so this is guarded and simply degrades to killing the direct child.
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    process->setChildProcessModifier([] {
#ifdef Q_OS_UNIX
        ::setpgid(0, 0);
#endif
    });
#endif

    Instance instance;
    instance.process = process;
    instance.plan = plan;
    instance.gameName = game.name;
    instance.executableBaseName = QFileInfo(game.executable).fileName();
    m_instances.insert(game.id, instance);

    const QString gameId = game.id;
    connect(process, &QProcess::started, this, [this, gameId] {
        auto it = m_instances.find(gameId);
        if (it == m_instances.end())
            return;
        it->started = true;
        it->running = true;
        it->clock.start();
        it->startedAt = QDateTime::currentDateTime();
        // The log is created now, and filled with what the process prints, so
        // opening the viewer while the game is running shows evidence instead of
        // an empty "no log yet". The final pass rewrites it with the finished
        // header and the complete output.
        QString logError;
        it->liveLogPath = LaunchLogs::beginLive(gameId, it->gameName, runnerName(it->plan.runner),
                                                it->startedAt, it->plan.summary(), &logError);
        if (it->liveLogPath.isEmpty() && !logError.isEmpty())
            qWarning("%s", qUtf8Printable(logError));
        emit gameStarted(gameId, it->startedAt);
    });

    connect(process, &QProcess::readyReadStandardOutput, this, [this, gameId] {
        collectOutput(gameId);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, gameId] {
        collectOutput(gameId);
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, gameId](int, QProcess::ExitStatus) {
        auto it = m_instances.find(gameId);
        if (it == m_instances.end())
            return;
        collectOutput(gameId);
        it->elapsedAtExitMs = it->clock.isValid() ? it->clock.elapsed() : 0;
        // The process is gone, so the game is no longer running even if the
        // prefix still has to be torn down. Reporting that now keeps the UI from
        // showing Stop after a game the user closed themselves.
        it->running = false;
        emit gameExited(gameId);
        beginTeardown(gameId);
    });

    connect(process, &QProcess::errorOccurred, this, [this, gameId, process](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart) {
            emitFailure(gameId, Vanta::tr("The launch process could not be started: %1").arg(process->errorString()));
        }
    });

    process->start();
    if (error)
        error->clear();
    return true;
}

void GameLauncher::collectOutput(const QString &gameId)
{
    auto it = m_instances.find(gameId);
    if (it == m_instances.end() || !it->process)
        return;
    const QByteArray out = it->process->readAllStandardOutput();
    const QByteArray err = it->process->readAllStandardError();
    it->output += out;
    it->errorOutput += err;
    constexpr int maxLogSize = 65536;
    if (it->output.size() > maxLogSize)
        it->output = it->output.right(maxLogSize);
    if (it->errorOutput.size() > maxLogSize)
        it->errorOutput = it->errorOutput.right(maxLogSize);
    if (!it->liveLogPath.isEmpty()) {
        if (!out.isEmpty())
            LaunchLogs::appendLive(it->liveLogPath, QString::fromLocal8Bit(out));
        if (!err.isEmpty())
            LaunchLogs::appendLive(it->liveLogPath, QString::fromLocal8Bit(err));
    }
}

void GameLauncher::runTerminationFallback(const QString &gameId)
{
    auto it = m_instances.find(gameId);
    if (it == m_instances.end())
        return;
    const QString baseName = it->executableBaseName;
    if (baseName.isEmpty())
        return;
    if (baseName.contains(QLatin1Char('/')))
        return;

    // The fallback is a last resort when wineserver cannot be asked to quit, so
    // it has to match the game executable exactly. An unanchored `pkill -f name`
    // also kills the shell or editor that happens to mention the name.
    auto process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(QStringLiteral("pkill"),
                   {QStringLiteral("-f"), QStringLiteral("(^|/)%1($| )").arg(QRegularExpression::escape(baseName))});
}

void GameLauncher::signalProcessGroup(const Instance &instance, int sig)
{
#ifdef Q_OS_UNIX
    if (instance.process) {
        // The child called setpgid(0, 0), so its pid is the group id. Signalling
        // the group reaches the game behind a wrapper script; a negative pid is
        // not valid for QProcess::kill, hence kill(2) directly.
        const qint64 pid = instance.process->processId();
        if (pid > 0) {
            ::kill(-static_cast<pid_t>(pid), sig);
            return;
        }
    }
#endif
    Q_UNUSED(sig);
    if (instance.process)
        instance.process->kill();
}

void GameLauncher::stopGame(const QString &gameId)
{
    auto it = m_instances.find(gameId);
    if (it == m_instances.end() || !it->process)
        return;
    if (it->stopInProgress)
        return;
    it->stopped = true;
    it->stopInProgress = true;
    emit gameStopping(gameId);

    if (!runnerIsWineLike(it->plan.runner)) {
        // The game owns a process group now, so the whole group has to hear the
        // request: terminating only the direct child leaves a wrapper script's
        // game alive and unparented.
        const Instance instance = *it;
        signalProcessGroup(instance, SIGTERM);
        QTimer::singleShot(2500, this, [this, gameId] {
            auto pending = m_instances.find(gameId);
            if (pending == m_instances.end() || !pending->process)
                return;
            if (pending->process->state() != QProcess::NotRunning)
                signalProcessGroup(*pending, SIGKILL);
        });
        return;
    }

    bool startedKill = false;
    if (!it->plan.wineServer.isEmpty() && QFileInfo(it->plan.wineServer).isExecutable()) {
        auto wineserver = new QProcess(this);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WINEPREFIX"), it->plan.winePrefix);
        wineserver->setProcessEnvironment(environment);
        connect(wineserver, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, gameId, wineserver](int exitCode, QProcess::ExitStatus) {
            if (exitCode != 0)
                runTerminationFallback(gameId);
            wineserver->deleteLater();
        });
        connect(wineserver, &QProcess::errorOccurred, this, [this, gameId, wineserver](QProcess::ProcessError) {
            runTerminationFallback(gameId);
            wineserver->deleteLater();
        });
        wineserver->start(it->plan.wineServer, {QStringLiteral("-k")});
        startedKill = true;
    }
    if (!startedKill)
        runTerminationFallback(gameId);

    const QPointer<QProcess> process = it->process;
    QTimer::singleShot(10000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->terminate();
    });
    QTimer::singleShot(15000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void GameLauncher::beginTeardown(const QString &gameId)
{
    auto it = m_instances.find(gameId);
    if (it == m_instances.end())
        return;
    const LaunchPlan plan = it->plan;
    const int exitCode = it->process ? it->process->exitCode() : -1;
    const QProcess::ExitStatus exitStatus = it->process ? it->process->exitStatus() : QProcess::CrashExit;

    if (!runnerIsWineLike(plan.runner) || plan.winePrefix.isEmpty() || !QFileInfo(plan.wineServer).isExecutable()) {
        completeInstance(gameId, exitCode, exitStatus);
        return;
    }

    auto *waiter = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WINEPREFIX"), plan.winePrefix);
    waiter->setProcessEnvironment(environment);
    connect(waiter, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, gameId, waiter, exitCode, exitStatus](int, QProcess::ExitStatus) {
        waiter->deleteLater();
        completeInstance(gameId, exitCode, exitStatus);
    });
    connect(waiter, &QProcess::errorOccurred, this,
            [this, gameId, waiter, exitCode, exitStatus](QProcess::ProcessError) {
        waiter->deleteLater();
        completeInstance(gameId, exitCode, exitStatus);
    });

    // A prefix can keep a background process alive indefinitely, which used to
    // leave the launch un-finalized forever. The running state already cleared
    // when the process exited, so this only bounds how long the playtime and the
    // final log wait on the prefix; the waiter is killed once the bound passes.
    auto *guard = new QTimer(waiter);
    guard->setSingleShot(true);
    connect(guard, &QTimer::timeout, this, [this, gameId, waiter, exitCode, exitStatus] {
        if (waiter->state() != QProcess::NotRunning)
            waiter->kill();
        completeInstance(gameId, exitCode, exitStatus);
    });
    guard->start(20000);

    waiter->start(plan.wineServer, {QStringLiteral("-w")});
}

void GameLauncher::completeInstance(const QString &gameId, int exitCode, QProcess::ExitStatus exitStatus)
{
    auto it = m_instances.find(gameId);
    if (it == m_instances.end())
        return;
    collectOutput(gameId);
    const bool stopped = it->stopped;
    const qint64 elapsedMs = it->elapsedAtExitMs;
    const bool crashed = !stopped && (exitStatus == QProcess::CrashExit || exitCode != 0);
    const QString diagnostic = outputLog(*it);
    const QString gameName = it->gameName;
    const QString logPath = persistLog(gameId, *it, exitCode, crashed, stopped, diagnostic);
    if (it->process) {
        it->process->disconnect(this);
        it->process->deleteLater();
        it->process = nullptr;
    }
    m_instances.erase(it);

    if (crashed) {
        const QString message = Vanta::tr("%1 exited unexpectedly with code %2.").arg(gameName).arg(exitCode);
        emit gameFinished(gameId, elapsedMs / 1000, exitCode, true, false,
                          message + QStringLiteral("\n\n") + diagnostic, logPath);
    } else {
        emit gameFinished(gameId, elapsedMs / 1000, exitCode, false, stopped, diagnostic, logPath);
    }
}

QString GameLauncher::persistLog(const QString &gameId, const Instance &instance, int exitCode,
                                 bool crashed, bool stopped, const QString &output) const
{
    // The plan summary names the exact command and environment overrides, so
    // the log explains what Vanta ran before the process output begins.
    QString body = instance.plan.summary();
    body += QStringLiteral("\n\nOutput:\n");
    body += output.trimmed().isEmpty() ? QStringLiteral("(the process printed nothing)")
                                      : output.trimmed();

    QString error;
    const QDateTime startedAt = instance.startedAt.isValid() ? instance.startedAt
                                                             : QDateTime::currentDateTime();
    QString path;
    if (!instance.liveLogPath.isEmpty()) {
        path = LaunchLogs::finishLive(instance.liveLogPath, gameId, instance.gameName,
                                      runnerName(instance.plan.runner), startedAt,
                                      QDateTime::currentDateTime(), exitCode, crashed, stopped, body,
                                      &error);
    } else {
        path = LaunchLogs::write(gameId, instance.gameName, runnerName(instance.plan.runner),
                                 startedAt, QDateTime::currentDateTime(), exitCode,
                                 crashed, stopped, body, &error);
    }
    if (path.isEmpty()) {
        // A log that cannot be written must never block the game from closing;
        // the diagnostics are still delivered to the caller below.
        qWarning("%s", qUtf8Printable(error));
    } else {
        LaunchLogs::prune(gameId);
    }
    return path;
}

void GameLauncher::emitFailure(const QString &gameId, const QString &message)
{
    auto it = m_instances.find(gameId);
    if (it == m_instances.end() || it->failureEmitted)
        return;
    it->failureEmitted = true;
    const QString output = outputLog(*it);
    const QString diagnostic = output + QLatin1Char('\n') + it->plan.summary();
    const QString logPath = persistLog(gameId, *it, -1, false, false, output);
    if (it->process) {
        it->process->disconnect(this);
        it->process->deleteLater();
        it->process = nullptr;
    }
    m_instances.erase(it);
    emit launchFailed(gameId, message, diagnostic, logPath);
}

bool GameLauncher::isRunning(const QString &gameId) const
{
    const auto it = m_instances.constFind(gameId);
    return it != m_instances.constEnd() && it->running;
}

bool GameLauncher::isStopping(const QString &gameId) const
{
    const auto it = m_instances.constFind(gameId);
    return it != m_instances.constEnd() && it->stopInProgress;
}

bool GameLauncher::isShuttingDown(const QString &gameId) const
{
    const auto it = m_instances.constFind(gameId);
    return it != m_instances.constEnd() && it->started && !it->running;
}

QStringList GameLauncher::runningGameIds() const
{
    QStringList result;
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it) {
        if (it->running)
            result.append(it.key());
    }
    return result;
}

QString GameLauncher::diagnosticLog(const QString &gameId) const
{
    const auto it = m_instances.constFind(gameId);
    if (it == m_instances.constEnd())
        return {};
    return outputLog(*it);
}

QString GameLauncher::outputLog(const Instance &instance) const
{
    QString log = QString::fromLocal8Bit(instance.output);
    const QString error = QString::fromLocal8Bit(instance.errorOutput);
    if (!error.isEmpty()) {
        if (!log.isEmpty() && !log.endsWith(QLatin1Char('\n')))
            log += QLatin1Char('\n');
        log += QStringLiteral("stderr:\n") + error;
    }
    if (log.size() > 60000)
        log = log.right(60000);
    return log.trimmed();
}

void GameLauncher::shutdown(bool reportStopped)
{
    const QStringList ids = m_instances.keys();
    for (const QString &id : ids) {
        auto it = m_instances.find(id);
        if (it == m_instances.end())
            continue;

        const bool wasRunning = it->running;
        collectOutput(id);
        it->elapsedAtExitMs = it->clock.isValid() ? it->clock.elapsed() : 0;

        // Quitting Vanta must not leave the game running. A Wine game outlives
        // its launcher process, so killing the QProcess alone leaves it alive;
        // the prefix has to be shut down through its own wineserver first.
        if (runnerIsWineLike(it->plan.runner) && !it->plan.winePrefix.isEmpty()
            && QFileInfo(it->plan.wineServer).isExecutable()) {
            QProcess wineserver;
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("WINEPREFIX"), it->plan.winePrefix);
            wineserver.setProcessEnvironment(environment);
            wineserver.start(it->plan.wineServer, {QStringLiteral("-k")});
            wineserver.waitForFinished(3000);
        }

        QString diagnostic;
        int exitCode = -1;
        if (it->process) {
            // The game runs in its own process group, so a plain QProcess::kill
            // would leave the real process behind when the game is a wrapper.
            signalProcessGroup(*it, SIGKILL);
            it->process->disconnect(this);
            it->process->waitForFinished(1500);
            exitCode = it->process->exitCode();
            diagnostic = outputLog(*it);
            it->process->deleteLater();
            it->process = nullptr;
        }

        if (!reportStopped || !it->started) {
            // A live log still marked "running" would claim a session that is
            // already over, so it is finalized on this path too. The destructor
            // takes it, and no receiver should run that late.
            if (!it->liveLogPath.isEmpty())
                persistLog(id, *it, exitCode, false, wasRunning, diagnostic);
            m_instances.erase(it);
            continue;
        }

        // The session is over, but the playtime it earned and the log it
        // produced are still worth keeping, so a clean quit reports the stop the
        // same way an explicit stop does.
        const QString logPath = persistLog(id, *it, exitCode, false, wasRunning, diagnostic);
        const qint64 elapsedMs = it->elapsedAtExitMs;
        m_instances.erase(it);
        emit gameFinished(id, elapsedMs / 1000, exitCode, false, wasRunning, diagnostic, logPath);
    }
}
