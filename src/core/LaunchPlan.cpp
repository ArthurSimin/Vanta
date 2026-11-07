#include "LaunchPlan.h"
#include "OnlineFix.h"
#include "RunnerDetector.h"
#include "SteamPrecondition.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

namespace {
QString quoted(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    if (escaped.contains(QLatin1Char(' ')) || escaped.contains(QLatin1Char('\t')))
        return QStringLiteral("\"%1\"").arg(escaped);
    return escaped;
}


QString resolveCommand(const QString &configured, const QString &fallbackName, bool directoryStyle, QString *error)
{
    if (configured.trimmed().isEmpty()) {
        const QString found = QStandardPaths::findExecutable(fallbackName);
        if (found.isEmpty() && error)
            *error = QStringLiteral("%1 was not found. Set a runner path in the game's settings.").arg(fallbackName);
        return found;
    }
    const QFileInfo info(configured);
    if (directoryStyle) {
        const QString resolved = RunnerDetector::protonExecutable(configured);
        if (!resolved.isEmpty())
            return resolved;
    }
    if (!info.exists()) {
        const QString found = QStandardPaths::findExecutable(configured);
        if (!found.isEmpty())
            return found;
        if (error)
            *error = QStringLiteral("Runner path does not exist: %1").arg(configured);
        return {};
    }
    if (info.isDir()) {
        if (error) {
            *error = directoryStyle
                ? QStringLiteral("No Proton launcher was found in %1").arg(configured)
                : QStringLiteral("The runner path points to a directory, not an executable: %1").arg(configured);
        }
        return {};
    }
    if (!info.isExecutable()) {
        if (error)
            *error = QStringLiteral("The runner is not executable: %1").arg(configured);
        return {};
    }
    return info.absoluteFilePath();
}
}

bool LaunchPlanBuilder::isControlledEnvironmentKey(const QString &key)
{
    static const QSet<QString> controlled = {
        QStringLiteral("WINEPREFIX"),
        QStringLiteral("WINEDLLOVERRIDES"),
        QStringLiteral("WINEDEBUG"),
        QStringLiteral("STEAM_COMPAT_DATA_PATH"),
        QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"),
        QStringLiteral("PROTON_USE_WINED3D"),
        QStringLiteral("PROTON_ENABLE_WAYLAND"),
        QStringLiteral("STEAMAPPID"),
        QStringLiteral("STEAMGAMEID"),
        QStringLiteral("STEAMOVERLAYGAMEID"),
        QStringLiteral("ENABLE_VK_LAYER_VALVE_STEAM_OVERLAY_1"),
        QStringLiteral("LD_PRELOAD")
    };
    return controlled.contains(key.trimmed().toUpper());
}

QString LaunchPlanBuilder::effectivePrefix(const Game &game)
{
    if (!game.winePrefix.trimmed().isEmpty())
        return QDir::cleanPath(game.winePrefix);
    QString base = game.executable.trimmed().isEmpty()
        ? QString() : QFileInfo(game.executable).absolutePath();
    if (base.isEmpty())
        base = game.effectiveDirectory();
    if (base.isEmpty())
        return {};
    return QDir(base).filePath(QStringLiteral("Vanta Prefix"));
}

QString LaunchPlanBuilder::effectiveWinePrefix(const Game &game)
{
    return QDir(effectivePrefix(game)).filePath(QStringLiteral("pfx"));
}

QString LaunchPlanBuilder::wineServerFor(const Game &game, const QString &protonProgram)
{
    if (game.runner == RunnerType::Proton) {
        QString base;
        if (!protonProgram.isEmpty()) {
            QFileInfo protonInfo(protonProgram);
            base = protonInfo.absolutePath();
            if (QFileInfo(base).fileName() == QStringLiteral("bin"))
                base = QFileInfo(base).absolutePath();
            if (QFileInfo(base).fileName() == QStringLiteral("files"))
                base = QFileInfo(base).absolutePath();
        }
        if (!base.isEmpty()) {
            const QFileInfo candidate(QDir(base).filePath(QStringLiteral("files/bin/wineserver")));
            if (candidate.exists())
                return candidate.absoluteFilePath();
        }
        const QString fallback = QStandardPaths::findExecutable(QStringLiteral("wineserver"));
        if (!fallback.isEmpty())
            return fallback;
        return {};
    }
    QString wine;
    if (!game.runnerPath.isEmpty()) {
        const QFileInfo info(game.runnerPath);
        if (info.isFile())
            wine = info.absolutePath();
    }
    if (wine.isEmpty()) {
        const QString found = QStandardPaths::findExecutable(QStringLiteral("wine"));
        if (!found.isEmpty())
            wine = QFileInfo(found).absolutePath();
    }
    if (!wine.isEmpty()) {
        const QFileInfo sibling(QDir(wine).filePath(QStringLiteral("wineserver")));
        if (sibling.exists())
            return sibling.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(QStringLiteral("wineserver"));
}

QStringList LaunchPlanBuilder::splitArguments(const QString &arguments)
{
    if (arguments.trimmed().isEmpty())
        return {};
    return QProcess::splitCommand(arguments);
}

QString LaunchPlanBuilder::overridePreview(const Game &game, const OnlineFixInfo &onlineFixInfo,
                                           bool includeOnlineFix, bool includeBepInEx)
{
    return OnlineFixOverrides::compose(game, onlineFixInfo, includeOnlineFix, includeBepInEx);
}

LaunchPlan LaunchPlanBuilder::build(const Game &game, const AppSettings &settings,
                                    const OnlineFixInfo &onlineFixInfo, bool onlineFixActive,
                                    bool bepinexDetected, QString *error)
{
    LaunchPlan plan;
    plan.runner = game.runner;
    plan.environment = QProcessEnvironment::systemEnvironment();
    auto setEnvironment = [&plan](const QString &key, const QString &value) {
        plan.environment.insert(key, value);
        plan.environmentOverrides.insert(key, value);
    };

    QStringList rejectedEnvironment;
    for (auto it = game.environment.constBegin(); it != game.environment.constEnd(); ++it) {
        if (isControlledEnvironmentKey(it.key())) {
            rejectedEnvironment.append(it.key());
            continue;
        }
        setEnvironment(it.key(), it.value());
    }
    if (!rejectedEnvironment.isEmpty()) {
        rejectedEnvironment.removeDuplicates();
        rejectedEnvironment.sort(Qt::CaseInsensitive);
        plan.diagnostics.append(QStringLiteral("Ignored protected environment variable(s) stored with this game: %1")
                                    .arg(rejectedEnvironment.join(QStringLiteral(", "))));
    }

    plan.onlineFixApplied = onlineFixActive;
    plan.bepinexApplied = game.bepinexCompatibility && bepinexDetected;
    plan.prefixBase = effectivePrefix(game);
    plan.winePrefix = QDir(plan.prefixBase).filePath(QStringLiteral("pfx"));
    plan.realAppId = onlineFixActive ? onlineFixInfo.realAppId : game.realAppId;
    plan.fakeAppId = onlineFixActive ? onlineFixInfo.fakeAppId : game.fakeAppId;
    if (plan.realAppId.isEmpty())
        plan.realAppId = game.realAppId;
    if (plan.fakeAppId.isEmpty())
        plan.fakeAppId = game.fakeAppId;

    const QString executable = QFileInfo(game.executable).absoluteFilePath();
    QStringList command;
    QStringList postArgs = splitArguments(game.launchArguments);
    const QFileInfo executableInfo(executable);

    if (game.runner == RunnerType::Native) {
        if (executable.isEmpty() || !executableInfo.exists() || !executableInfo.isFile()) {
            if (error)
                *error = QStringLiteral("The game executable is missing: %1").arg(game.executable);
            return {};
        }
        if (!executableInfo.isExecutable()) {
            if (error)
                *error = QStringLiteral("The game executable is not executable: %1").arg(executable);
            return {};
        }
        command << executable;
        command << postArgs;
    } else if (game.runner == RunnerType::Wine) {
        if (!executableInfo.exists() || !executableInfo.isFile()) {
            if (error)
                *error = QStringLiteral("The game executable is missing: %1").arg(game.executable);
            return {};
        }
        QString runnerError;
        const QString configuredWine = game.runnerPath.trimmed().isEmpty()
            ? settings.customWinePath : game.runnerPath;
        const QString wine = resolveCommand(configuredWine, QStringLiteral("wine"), false, &runnerError);
        if (wine.isEmpty()) {
            if (error)
                *error = runnerError.isEmpty() ? QStringLiteral("Wine was not found.") : runnerError;
            return {};
        }
        command << wine << executable;
        command << postArgs;

        const QString overrides = OnlineFixOverrides::compose(game, onlineFixInfo, onlineFixActive, plan.bepinexApplied);
        plan.composedOverrides = overrides;
        setEnvironment(QStringLiteral("WINEPREFIX"), plan.winePrefix);
        setEnvironment(QStringLiteral("WINEDLLOVERRIDES"), overrides);
        setEnvironment(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
        plan.wineServer = wineServerFor(game, QString());
    } else if (game.runner == RunnerType::Proton) {
        if (!executableInfo.exists() || !executableInfo.isFile()) {
            if (error)
                *error = QStringLiteral("The game executable is missing: %1").arg(game.executable);
            return {};
        }
        QString runnerError;
        const QString proton = resolveCommand(game.runnerPath, QStringLiteral("proton"), true, &runnerError);
        if (proton.isEmpty()) {
            if (error)
                *error = runnerError.isEmpty() ? QStringLiteral("Proton was not found.") : runnerError;
            return {};
        }
        const QString overrides = OnlineFixOverrides::compose(game, onlineFixInfo, onlineFixActive, plan.bepinexApplied);
        plan.composedOverrides = overrides;
        command << proton << QStringLiteral("run") << executable;
        command << postArgs;
        setEnvironment(QStringLiteral("WINEDLLOVERRIDES"), overrides);
        setEnvironment(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
        setEnvironment(QStringLiteral("STEAM_COMPAT_DATA_PATH"), plan.prefixBase);
        setEnvironment(QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"), SteamIntegration::steamInstallRoot());
        if (game.wineD3D)
            setEnvironment(QStringLiteral("PROTON_USE_WINED3D"), QStringLiteral("1"));
        if (game.nativeWayland)
            setEnvironment(QStringLiteral("PROTON_ENABLE_WAYLAND"), QStringLiteral("1"));
        if (!plan.realAppId.isEmpty()) {
            setEnvironment(QStringLiteral("SteamAppId"), plan.realAppId);
            setEnvironment(QStringLiteral("SteamGameId"), plan.realAppId);
        }

        if (game.steamOverlay) {
            const QString steam = SteamIntegration::steamInstallRoot();
            QStringList overlayPaths;
            for (const QString &relative : {QStringLiteral("ubuntu12_32/gameoverlayrenderer.so"),
                                            QStringLiteral("ubuntu12_64/gameoverlayrenderer.so")}) {
                const QString candidate = QDir(steam).filePath(relative);
                if (QFileInfo(candidate).isFile())
                    overlayPaths.append(candidate);
            }
            if (!overlayPaths.isEmpty()) {
                const QString existing = plan.environment.value(QStringLiteral("LD_PRELOAD"));
                QString preload = QStringLiteral(":") + overlayPaths.join(QLatin1Char(':'));
                if (!existing.isEmpty())
                    preload += QLatin1Char(':') + existing;
                setEnvironment(QStringLiteral("LD_PRELOAD"), preload);
                setEnvironment(QStringLiteral("ENABLE_VK_LAYER_VALVE_steam_overlay_1"), QStringLiteral("1"));
                const QString overlayId = plan.fakeAppId.isEmpty() ? QStringLiteral("480") : plan.fakeAppId;
                setEnvironment(QStringLiteral("SteamOverlayGameId"), overlayId);
            } else {
                plan.diagnostics.append(QStringLiteral("The Steam overlay was requested, but its renderer libraries were not found under %1.").arg(steam));
            }
        }

        plan.steamRuntimeRequested = game.useSteamRuntime;
        if (game.useSteamRuntime) {
            const QString runtime = SteamIntegration::findSteamRuntime(proton);
            if (runtime.isEmpty()) {
                plan.steamRuntimeUnavailable = true;
                plan.diagnostics.append(QStringLiteral("SteamLinuxRuntime was requested but could not be resolved. Launching without the runtime wrapper."));
            } else {
                plan.usedSteamRuntime = true;
                plan.diagnostics.append(QStringLiteral("SteamLinuxRuntime wrapper: %1").arg(runtime));
                QStringList wrapped;
                wrapped << runtime << QStringLiteral("--") << command;
                command = wrapped;
            }
        }
        plan.wineServer = wineServerFor(game, proton);
    } else if (game.runner == RunnerType::Flatpak) {
        const QString flatpak = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
        if (flatpak.isEmpty()) {
            if (error)
                *error = QStringLiteral("The flatpak command was not found.");
            return {};
        }
        if (game.runnerPath.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("Set the Flatpak application ID in the runner path field.");
            return {};
        }
        command << flatpak << QStringLiteral("run") << game.runnerPath;
        command << postArgs;
    } else {
        if (game.runnerPath.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("Set the custom runner command.");
            return {};
        }
        const QStringList custom = splitArguments(game.runnerPath);
        if (custom.isEmpty()) {
            if (error)
                *error = QStringLiteral("The custom runner command is empty.");
            return {};
        }
        const QString executablePath = custom.first();
        const QString resolved = QFileInfo(executablePath).isAbsolute()
            ? executablePath : QStandardPaths::findExecutable(executablePath);
        if (resolved.isEmpty() || !QFileInfo::exists(resolved)) {
            if (error)
                *error = QStringLiteral("Custom runner command was not found: %1").arg(executablePath);
            return {};
        }
        command << resolved;
        command << custom.mid(1);
        if (executableInfo.exists())
            command << executable;
        command << postArgs;
    }

    if (command.isEmpty()) {
        if (error)
            *error = QStringLiteral("The launch command could not be assembled.");
        return {};
    }
    plan.program = command.first();
    command.removeFirst();
    plan.arguments = command;

    plan.workingDirectory = game.effectiveWorkingDirectory();
    if (plan.workingDirectory.isEmpty()) {
        plan.workingDirectory = QFileInfo(executable).absolutePath();
    }
    if (!QFileInfo(plan.workingDirectory).isDir()) {
        if (error)
            *error = QStringLiteral("The working directory does not exist: %1").arg(plan.workingDirectory);
        return {};
    }

    QStringList preArgs = splitArguments(game.preArguments);
    if (!preArgs.isEmpty()) {
        const QString wrapper = preArgs.first();
        QString resolvedWrapper = QFileInfo(wrapper).isAbsolute()
            ? wrapper : QStandardPaths::findExecutable(wrapper);
        if (resolvedWrapper.isEmpty() || !QFileInfo(resolvedWrapper).isFile()) {
            if (error)
                *error = QStringLiteral("The wrapper command was not found: %1").arg(wrapper);
            return {};
        }
        preArgs.first() = QFileInfo(resolvedWrapper).absoluteFilePath();
        QStringList commandWithWrappers = preArgs;
        commandWithWrappers << plan.program << plan.arguments;
        plan.program = commandWithWrappers.first();
        commandWithWrappers.removeFirst();
        plan.arguments = commandWithWrappers;
    }

    if (plan.onlineFixApplied) {
        plan.diagnostics.append(QStringLiteral("OnlineFix launch configuration enabled for this launch only."));
        plan.diagnostics.append(QStringLiteral("WINEDLLOVERRIDES source: forced DXVK chain, stored overrides, detected modules."));
    }
    if (plan.bepinexApplied)
        plan.diagnostics.append(QStringLiteral("The BepInEx compatibility override (winhttp=n,b) is included because the user enabled it and BepInEx files were detected."));
    if (!plan.composedOverrides.isEmpty())
        plan.diagnostics.append(QStringLiteral("Composed WINEDLLOVERRIDES: %1").arg(plan.composedOverrides));
    plan.diagnostics.append(QStringLiteral("Runner: %1").arg(runnerName(game.runner)));
    plan.diagnostics.append(QStringLiteral("Working directory: %1").arg(plan.workingDirectory));
    if (!plan.realAppId.isEmpty())
        plan.diagnostics.append(QStringLiteral("Real app id: %1").arg(plan.realAppId));
    if (!plan.fakeAppId.isEmpty())
        plan.diagnostics.append(QStringLiteral("Fake app id: %1").arg(plan.fakeAppId));
    if (!plan.prefixBase.isEmpty())
        plan.diagnostics.append(QStringLiteral("Prefix base: %1").arg(plan.prefixBase));
    plan.diagnostics.append(QStringLiteral("Command: %1 %2").arg(quoted(plan.program), [&plan] {
        QStringList args;
        for (const QString &arg : plan.arguments)
            args.append(quoted(arg));
        return args.join(QLatin1Char(' '));
    }()));
    return plan;
}

QString LaunchPlan::summary() const
{
    QStringList lines;
    lines.append(QStringLiteral("$ %1 %2").arg(quoted(program), [this] {
        QStringList quotedArgs;
        for (const QString &argument : arguments)
            quotedArgs.append(quoted(argument));
        return quotedArgs.join(QLatin1Char(' '));
    }()));
    lines.append(QStringLiteral("Workdir: %1").arg(workingDirectory));
    if (!diagnostics.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("Diagnostics:"));
        for (const QString &entry : diagnostics)
            lines.append(QStringLiteral("• %1").arg(entry));
    }
    if (!writtenFiles.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("Files written for this launch:"));
        for (const QString &file : writtenFiles)
            lines.append(QStringLiteral("• %1").arg(file));
    }
    if (!backups.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("Backups created:"));
        for (const QString &file : backups)
            lines.append(QStringLiteral("• %1").arg(file));
    }
    lines.append(QString());
    lines.append(environmentSummary());
    return lines.join(QLatin1Char('\n'));
}

QString LaunchPlan::environmentSummary() const
{
    QStringList lines;
    lines.append(QStringLiteral("Environment overrides:"));
    if (environmentOverrides.isEmpty()) {
        lines.append(QStringLiteral("(none)"));
        return lines.join(QLatin1Char('\n'));
    }
    QStringList vars;
    for (auto it = environmentOverrides.constBegin(); it != environmentOverrides.constEnd(); ++it)
        vars.append(QStringLiteral("%1=%2").arg(it.key(), it.value()));
    vars.sort(Qt::CaseInsensitive);
    lines.append(vars);
    return lines.join(QLatin1Char('\n'));
}
