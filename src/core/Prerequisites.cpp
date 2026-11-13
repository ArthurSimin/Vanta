#include "Prerequisites.h"

#include "I18n.h"
#include "LaunchPlan.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace {

bool mentions(const QString &output, const QString &needle)
{
    return output.contains(needle, Qt::CaseInsensitive);
}

// A managed runtime that cannot be found shows up as an assembly that fails to
// load or as a type that cannot be initialized. Both wordings are matched
// because the exception depends on where the failure is first observed.
bool mentionsAssemblyLoadFailure(const QString &output)
{
    for (const QString &marker : {QStringLiteral("could not load file or assembly"),
                                  QStringLiteral("filenotfoundexception"),
                                  QStringLiteral("reflectiontypeloadexception"),
                                  QStringLiteral("typeinitializationexception")}) {
        if (mentions(output, marker))
            return true;
    }
    return false;
}

QString trimmedTail(const QString &text, int maxCharacters)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= maxCharacters)
        return trimmed;
    return trimmed.right(maxCharacters);
}

}

QString runtimeDisplayName(RuntimeKind kind)
{
    switch (kind) {
    case RuntimeKind::Xna:
        return Vanta::tr("XNA Framework");
    case RuntimeKind::DotNet:
        return Vanta::tr(".NET Framework");
    }
    return QString();
}

QVector<RuntimeKind> Prerequisites::missingRuntimes(const QString &output)
{
    QVector<RuntimeKind> kinds;
    if (output.trimmed().isEmpty())
        return kinds;

    // XNA is only ever loaded by name, so a load failure that names it is the
    // signature. The game's own folder ships the content pipeline assembly, so
    // the failure is normally about the framework itself.
    if (mentionsAssemblyLoadFailure(output) && mentions(output, QStringLiteral("microsoft.xna.framework")))
        kinds.append(RuntimeKind::Xna);

    // Wine's mono loader prints `err:mscoree:` when it cannot start a managed
    // application at all, and a System.* assembly that Wine's mono does not
    // provide fails the same way an XNA assembly would.
    const bool dotNetFailure =
        mentions(output, QStringLiteral("err:mscoree")) ||
        (mentionsAssemblyLoadFailure(output)
         && mentions(output, QStringLiteral("could not load file or assembly 'system.")))
        || mentions(output, QStringLiteral("requires the .net framework"))
        || mentions(output, QStringLiteral("install the .net framework"));
    if (dotNetFailure)
        kinds.append(RuntimeKind::DotNet);

    return kinds;
}

QVector<RuntimeInstaller> Prerequisites::installersFor(const QString &directory,
                                                       const QVector<RuntimeKind> &kinds)
{
    QVector<RuntimeInstaller> result;
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
        return result;

    const QFileInfoList entries =
        QDir(directory).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);

    // Every runtime the folder ships an installer for is offered, so a game that
    // needs a runtime the output did not name can still be fixed without a
    // second trip to the dialog. The detected runtimes come first because they
    // are the ones the failure actually points at.
    QVector<RuntimeKind> ordered = kinds;
    for (RuntimeKind kind : {RuntimeKind::Xna, RuntimeKind::DotNet}) {
        if (!ordered.contains(kind))
            ordered.append(kind);
    }

    for (RuntimeKind kind : ordered) {
        QVector<QFileInfo> candidates;
        for (const QFileInfo &info : entries) {
            const QString name = info.fileName().toLower();
            if (kind == RuntimeKind::Xna) {
                if (name.startsWith(QStringLiteral("xnafx")) && name.endsWith(QStringLiteral(".msi")))
                    candidates.append(info);
            } else if ((name.startsWith(QStringLiteral("dotnetfx")) && name.endsWith(QStringLiteral(".exe")))
                       || (name.startsWith(QStringLiteral("ndp")) && name.endsWith(QStringLiteral(".exe")))) {
                candidates.append(info);
            }
        }
        if (candidates.isEmpty())
            continue;

        // A game that ships both dotNetFx40_Full_setup.exe and
        // dotNetFx40_Full_x86_x64.exe only needs one; the combined package
        // covers both architectures and is preferred when it is there.
        QFileInfo chosen = candidates.first();
        for (const QFileInfo &info : candidates) {
            if (info.fileName().contains(QStringLiteral("x86_x64"), Qt::CaseInsensitive)) {
                chosen = info;
                break;
            }
        }

        RuntimeInstaller installer;
        installer.kind = kind;
        installer.path = chosen.absoluteFilePath();
        installer.msi = chosen.suffix().compare(QStringLiteral("msi"), Qt::CaseInsensitive) == 0;
        installer.arguments = {QStringLiteral("/passive"), QStringLiteral("/norestart")};
        result.append(installer);
    }
    return result;
}

RuntimeInstallerRunner::RuntimeInstallerRunner(QObject *parent)
    : QObject(parent)
{
}

bool RuntimeInstallerRunner::start(const Game &game, const AppSettings &settings,
                                   const RuntimeInstaller &installer, QString *error)
{
    if (m_process) {
        if (error)
            *error = Vanta::tr("An installer is already running.");
        return false;
    }

    QString planError;
    const LaunchPlan plan = LaunchPlanBuilder::buildInstallerRun(
        game, settings, installer.path, installer.msi, installer.arguments, &planError);
    if (!plan.isValid()) {
        if (error)
            *error = planError.isEmpty() ? Vanta::tr("The installer command could not be built.") : planError;
        return false;
    }
    if (!plan.prefixBase.isEmpty() && !QDir().mkpath(plan.prefixBase)) {
        if (error)
            *error = Vanta::tr("Could not create the game prefix: %1").arg(plan.prefixBase);
        return false;
    }

    m_process = new QProcess(this);
    m_process->setProgram(plan.program);
    m_process->setArguments(plan.arguments);
    m_process->setWorkingDirectory(plan.workingDirectory);
    m_process->setProcessEnvironment(plan.environment);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    const RuntimeKind kind = installer.kind;
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, kind](int exitCode, QProcess::ExitStatus status) {
                const QString output = QString::fromLocal8Bit(m_process->readAll());
                m_process->deleteLater();
                m_process = nullptr;
                const bool ok = status == QProcess::NormalExit && exitCode == 0;
                const QString summary = ok
                    ? Vanta::tr("The %1 installer finished.").arg(runtimeDisplayName(kind))
                    : Vanta::tr("The %1 installer exited with code %2.")
                          .arg(runtimeDisplayName(kind))
                          .arg(exitCode);
                emit finished(ok, exitCode, summary, trimmedTail(output, 8000));
            });
    connect(m_process, &QProcess::errorOccurred, this, [this, kind](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart || !m_process)
            return;
        const QString message = m_process->errorString();
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(false, -1,
                      Vanta::tr("The %1 installer could not be started: %2")
                          .arg(runtimeDisplayName(kind), message),
                      QString());
    });

    m_process->start();
    return true;
}

void RuntimeInstallerRunner::cancel()
{
    if (!m_process)
        return;
    m_process->kill();
}
