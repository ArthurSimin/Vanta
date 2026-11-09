#pragma once

#include "Types.h"

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

struct PackageManagerInfo
{
    QString id;
    QString displayName;
    QString executable;
    // Removal is not always the install tool: Void installs with xbps-install
    // and removes with xbps-remove. Empty means "same as executable".
    QString uninstallExecutable;
    // The name of that removal tool, kept so a missing one can be named.
    QString uninstallToolName;
    // True when the distribution needs a separate removal tool that is missing.
    // Reporting a missing remover is better than running the installer again in
    // the hope that it removes something.
    bool removalUnsupported = false;
    QString packageName = QStringLiteral("wine");
    bool available = false;

    QString removalExecutable() const
    {
        if (removalUnsupported)
            return {};
        return uninstallExecutable.isEmpty() ? executable : uninstallExecutable;
    }
};

class RunnerInstaller : public QObject
{
    Q_OBJECT

public:
    explicit RunnerInstaller(QObject *parent = nullptr);
    ~RunnerInstaller() override;

    bool busy() const { return m_operation != Operation::None; }

    static PackageManagerInfo detectPackageManager();
    static QStringList installArguments(const PackageManagerInfo &manager);
    static QString installCommandLine(const PackageManagerInfo &manager);
    static QStringList uninstallArguments(const PackageManagerInfo &manager);
    static QString uninstallCommandLine(const PackageManagerInfo &manager);
    static QString managedRunnerRoot(RunnerType type);

    static bool isSystemWineInstalled();
    static bool isPortableWineInstalled();
    static QList<QPair<QString, QString>> installedProtonInstallations();

    void installSystemWine();
    void uninstallSystemWine();
    void downloadPortableWine();
    void uninstallPortableWine();
    void downloadProton();
    void uninstallProton(const QString &directoryPath);
    void cancel();

signals:
    void busyChanged(bool busy);
    void progress(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void finished(const QString &message);
    void failed(const QString &message);
    void cancelled();

private:
    enum class Operation { None, FetchingRelease, Downloading, Extracting, InstallingPackage, UninstallingPackage };

    void fetchLatestRelease(RunnerType type);
    void handleReleaseReply(QNetworkReply *reply);
    void startDownload(const QString &url, const QString &name, qint64 expectedSize);
    void handleDownloadFinished(QNetworkReply *reply);
    void beginExtraction();
    void handleExtractionFinished(int exitCode, QProcess::ExitStatus status);
    void startPackageInstall();
    void handlePackageFinished(int exitCode, QProcess::ExitStatus status);
    void handlePackageUninstallFinished(int exitCode, QProcess::ExitStatus status);
    void validateManagedRunner();
    void beginOperation(Operation operation, const QString &message);
    void resetOperation(bool removeTemporaryFile);
    void failOperation(const QString &message);
    void completeOperation(const QString &message);
    void cancelOperation();

    Operation m_operation = Operation::None;
    RunnerType m_requestedType = RunnerType::Wine;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QPointer<QProcess> m_process;
    QFile m_file;
    QString m_temporaryFile;
    QString m_downloadName;
    QString m_downloadUrl;
    QString m_releaseTag;
    QString m_extractionRoot;
    QString m_processOutput;
    qint64 m_expectedSize = -1;
    bool m_cancelRequested = false;
};
