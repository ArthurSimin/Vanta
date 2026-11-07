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
    QString packageName = QStringLiteral("wine");
    bool available = false;
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
    static QString managedRunnerRoot(RunnerType type);

    void installSystemWine();
    void downloadPortableWine();
    void downloadProton();
    void cancel();

signals:
    void busyChanged(bool busy);
    void progress(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void finished(const QString &message);
    void failed(const QString &message);
    void cancelled();

private:
    enum class Operation { None, FetchingRelease, Downloading, Extracting, InstallingPackage };

    void fetchLatestRelease(RunnerType type);
    void handleReleaseReply(QNetworkReply *reply);
    void startDownload(const QString &url, const QString &name, qint64 expectedSize);
    void handleDownloadFinished(QNetworkReply *reply);
    void beginExtraction();
    void handleExtractionFinished(int exitCode, QProcess::ExitStatus status);
    void startPackageInstall();
    void handlePackageFinished(int exitCode, QProcess::ExitStatus status);
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
