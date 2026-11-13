#pragma once

#include "Game.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;

// A Windows runtime a game links against that a fresh Wine prefix does not
// provide. Steam installs these from the game's own installscript the first
// time it launches the game; a folder copied into a Vanta library by hand never
// gets that step, which is why the game can stop with a missing-assembly error.
enum class RuntimeKind { Xna, DotNet };

// Human-readable name, translated. Used in messages only.
QString runtimeDisplayName(RuntimeKind kind);

// One installer shipped inside a game folder that can provide a runtime.
struct RuntimeInstaller
{
    RuntimeKind kind = RuntimeKind::Xna;
    QString path;          // absolute path to the .msi or .exe
    bool msi = false;      // an MSI runs through msiexec, an EXE does not
    QStringList arguments; // arguments passed after the installer
};

namespace Prerequisites
{
// Runtimes whose absence is consistent with what the process printed. This is a
// signature match, not proof, so callers report it as a possibility rather than
// a diagnosis.
QVector<RuntimeKind> missingRuntimes(const QString &output);

// Installers found directly inside `directory`, one per runtime. The runtimes
// in `kinds` come first; any other runtime the folder also ships an installer
// for follows, because a game can be missing more than the output names. At
// most one installer is returned per runtime, preferring the combined 32/64-bit
// .NET package when a game ships both.
QVector<RuntimeInstaller> installersFor(const QString &directory, const QVector<RuntimeKind> &kinds);
}

// Runs one installer through the game's runner inside the game's own prefix.
// This lives in core because the UI layer never spawns a process.
class RuntimeInstallerRunner : public QObject
{
    Q_OBJECT

public:
    explicit RuntimeInstallerRunner(QObject *parent = nullptr);

    bool start(const Game &game, const AppSettings &settings, const RuntimeInstaller &installer,
               QString *error = nullptr);
    void cancel();

signals:
    // `output` is what the installer printed, kept so the caller can show the
    // evidence instead of restating it as a cause.
    void finished(bool ok, int exitCode, const QString &summary, const QString &output);

private:
    QProcess *m_process = nullptr;
};
