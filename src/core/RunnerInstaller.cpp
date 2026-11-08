#include "RunnerInstaller.h"
#include "RunnerDetector.h"
#include "Version.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
struct AssetChoice
{
    QString name;
    QString url;
    qint64 size = -1;
};

struct PackageManagerSpec
{
    QString id;
    QString displayName;
    QString executableName;
    QStringList distributions;
    int priority = 0;
};

QString packageManagerDisplay(const QString &id)
{
    if (id == QStringLiteral("apt-get"))
        return QStringLiteral("APT (Debian/Ubuntu)");
    if (id == QStringLiteral("dnf"))
        return QStringLiteral("DNF (Fedora/RHEL)");
    if (id == QStringLiteral("pacman"))
        return QStringLiteral("Pacman (Arch)");
    if (id == QStringLiteral("zypper"))
        return QStringLiteral("Zypper (openSUSE)");
    if (id == QStringLiteral("apk"))
        return QStringLiteral("APK (Alpine)");
    if (id == QStringLiteral("eopkg"))
        return QStringLiteral("eopkg (Solus)");
    if (id == QStringLiteral("xbps-install"))
        return QStringLiteral("XBPS (Void)");
    return id;
}

QString osReleaseValue(const QString &key)
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        if (line.left(equals).trimmed() != key)
            continue;
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        return value;
    }
    return {};
}

QList<PackageManagerSpec> packageManagerSpecs()
{
    return {
        {QStringLiteral("apt-get"), packageManagerDisplay(QStringLiteral("apt-get")), QStringLiteral("apt-get"),
         {QStringLiteral("debian"), QStringLiteral("ubuntu"), QStringLiteral("linuxmint"), QStringLiteral("pop"),
          QStringLiteral("elementary"), QStringLiteral("kali"), QStringLiteral("raspbian"), QStringLiteral("neon")}, 10},
        {QStringLiteral("dnf"), packageManagerDisplay(QStringLiteral("dnf")), QStringLiteral("dnf"),
         {QStringLiteral("fedora"), QStringLiteral("rhel"), QStringLiteral("centos"), QStringLiteral("rocky"),
          QStringLiteral("almalinux"), QStringLiteral("nobara"), QStringLiteral("bazzite")}, 9},
        {QStringLiteral("pacman"), packageManagerDisplay(QStringLiteral("pacman")), QStringLiteral("pacman"),
         {QStringLiteral("arch"), QStringLiteral("manjaro"), QStringLiteral("endeavouros"),
          QStringLiteral("garuda"), QStringLiteral("cachyos")}, 8},
        {QStringLiteral("zypper"), packageManagerDisplay(QStringLiteral("zypper")), QStringLiteral("zypper"),
         {QStringLiteral("opensuse"), QStringLiteral("suse"), QStringLiteral("sles"), QStringLiteral("sled")}, 7},
        {QStringLiteral("apk"), packageManagerDisplay(QStringLiteral("apk")), QStringLiteral("apk"),
         {QStringLiteral("alpine"), QStringLiteral("postmarketos")}, 6},
        {QStringLiteral("eopkg"), packageManagerDisplay(QStringLiteral("eopkg")), QStringLiteral("eopkg"),
         {QStringLiteral("solus")}, 5},
        {QStringLiteral("xbps-install"), packageManagerDisplay(QStringLiteral("xbps-install")), QStringLiteral("xbps-install"),
         {QStringLiteral("void")}, 4}
    };
}

bool isRoot()
{
#ifdef Q_OS_UNIX
    return geteuid() == 0;
#else
    return false;
#endif
}

QString shellQuote(const QString &value)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./+=:,-]+$"));
    if (safe.match(value).hasMatch())
        return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString commandLine(const QStringList &parts)
{
    QStringList quoted;
    for (const QString &part : parts)
        quoted.append(shellQuote(part));
    return quoted.join(QLatin1Char(' '));
}

QString elevationTool()
{
    if (isRoot())
        return {};
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (!pkexec.isEmpty())
        return pkexec;
    const QString sudo = QStandardPaths::findExecutable(QStringLiteral("sudo"));
    if (!sudo.isEmpty())
        return sudo;
    return {};
}

QStringList elevatedCommand(const PackageManagerInfo &manager)
{
    QStringList command;
    const QString tool = elevationTool();
    if (!tool.isEmpty())
        command.append(tool);
    command.append(manager.executable);
    command.append(RunnerInstaller::installArguments(manager));
    return command;
}

QStringList elevatedUninstallCommand(const PackageManagerInfo &manager)
{
    QStringList command;
    const QString tool = elevationTool();
    if (!tool.isEmpty())
        command.append(tool);
    command.append(manager.executable);
    command.append(RunnerInstaller::uninstallArguments(manager));
    return command;
}

QString firstExistingRunnerInRoot(const QString &root, RunnerType type)
{
    const QDir directory(root);
    if (!directory.exists())
        return {};
    if (type == RunnerType::Wine) {
        const QString direct = directory.filePath(QStringLiteral("bin/wine"));
        if (QFileInfo(direct).isFile())
            return QFileInfo(direct).absoluteFilePath();
    } else {
        const QString direct = RunnerDetector::protonExecutable(root);
        if (!direct.isEmpty())
            return direct;
    }
    const QFileInfoList children = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                           QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &child : children) {
        if (type == RunnerType::Wine) {
            const QString candidate = QDir(child.absoluteFilePath()).filePath(QStringLiteral("bin/wine"));
            if (QFileInfo(candidate).isFile())
                return QFileInfo(candidate).absoluteFilePath();
        } else {
            const QString candidate = RunnerDetector::protonExecutable(child.absoluteFilePath());
            if (!candidate.isEmpty())
                return candidate;
        }
    }
    return {};
}

AssetChoice chooseAsset(const QJsonArray &assets, RunnerType type)
{
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    const bool arm = architecture.contains(QStringLiteral("arm"))
        || architecture.contains(QStringLiteral("aarch64"));
    const QString wantedArch = arm ? QStringLiteral("aarch64") : QStringLiteral("x86_64");

    AssetChoice best;
    int bestScore = -100000;
    for (const QJsonValue &value : assets) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString url = object.value(QStringLiteral("browser_download_url")).toString();
        if (name.isEmpty() || url.isEmpty())
            continue;
        int score = -100000;
        if (type == RunnerType::Proton) {
            if (!name.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive))
                continue;
            score = 10;
            if (name.contains(wantedArch, Qt::CaseInsensitive))
                score += 100;
            if (name.contains(QStringLiteral("sha"), Qt::CaseInsensitive))
                score -= 1000;
            if (name.contains(QStringLiteral("x86_64"), Qt::CaseInsensitive)
                || name.contains(QStringLiteral("aarch64"), Qt::CaseInsensitive)) {
                if (!name.contains(wantedArch, Qt::CaseInsensitive))
                    score -= 200;
            }
        } else {
            if (!name.endsWith(QStringLiteral(".tar.xz"), Qt::CaseInsensitive))
                continue;
            score = 10;
            if (!arm && name.contains(QStringLiteral("amd64"), Qt::CaseInsensitive))
                score += 100;
            if (name.contains(QStringLiteral("wow64"), Qt::CaseInsensitive))
                score += 25;
            if (name.contains(QStringLiteral("staging"), Qt::CaseInsensitive))
                score -= 20;
            if (name.contains(QStringLiteral("tkg"), Qt::CaseInsensitive))
                score -= 30;
            if (name.contains(QStringLiteral("-x86.tar"), Qt::CaseInsensitive))
                score -= 200;
            if (arm)
                score -= 150;
        }
        if (score > bestScore) {
            bestScore = score;
            best.name = name;
            best.url = url;
            best.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        }
    }
    return best;
}
}

RunnerInstaller::RunnerInstaller(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setTransferTimeout(60000);
}

RunnerInstaller::~RunnerInstaller()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }
    if (m_file.isOpen())
        m_file.close();
    if (!m_temporaryFile.isEmpty())
        QFile::remove(m_temporaryFile);
}

PackageManagerInfo RunnerInstaller::detectPackageManager()
{
    PackageManagerInfo detected;
    const QString osId = osReleaseValue(QStringLiteral("ID")).toLower();
    const QStringList likeIds = osReleaseValue(QStringLiteral("ID_LIKE")).toLower()
        .split(QLatin1Char(' '), Qt::SkipEmptyParts);

    const QList<PackageManagerSpec> specs = packageManagerSpecs();
    const PackageManagerSpec *fallback = nullptr;
    for (const PackageManagerSpec &spec : specs) {
        const QString executable = QStandardPaths::findExecutable(spec.executableName);
        if (executable.isEmpty())
            continue;
        bool familyMatch = spec.distributions.contains(osId);
        for (const QString &likeId : likeIds) {
            if (spec.distributions.contains(likeId))
                familyMatch = true;
        }
        if (familyMatch) {
            detected.id = spec.id;
            detected.displayName = spec.displayName;
            detected.executable = executable;
            detected.packageName = QStringLiteral("wine");
            detected.available = true;
            return detected;
        }
        if (!fallback)
            fallback = &spec;
    }

    if (fallback) {
        const QString executable = QStandardPaths::findExecutable(fallback->executableName);
        if (!executable.isEmpty()) {
            detected.id = fallback->id;
            detected.displayName = fallback->displayName;
            detected.executable = executable;
            detected.packageName = QStringLiteral("wine");
            detected.available = true;
        }
    }
    return detected;
}

QStringList RunnerInstaller::installArguments(const PackageManagerInfo &manager)
{
    if (manager.id == QStringLiteral("pacman"))
        return {QStringLiteral("-S"), QStringLiteral("--noconfirm"), manager.packageName};
    if (manager.id == QStringLiteral("zypper"))
        return {QStringLiteral("--non-interactive"), QStringLiteral("install"), manager.packageName};
    if (manager.id == QStringLiteral("apk"))
        return {QStringLiteral("add"), manager.packageName};
    if (manager.id == QStringLiteral("eopkg"))
        return {QStringLiteral("install"), QStringLiteral("-y"), manager.packageName};
    if (manager.id == QStringLiteral("xbps-install"))
        return {QStringLiteral("-y"), manager.packageName};
    return {QStringLiteral("install"), QStringLiteral("-y"), manager.packageName};
}

QString RunnerInstaller::installCommandLine(const PackageManagerInfo &manager)
{
    if (!manager.available)
        return QStringLiteral("No supported package manager was detected.");
    if (!isRoot() && elevationTool().isEmpty()) {
        QStringList command = {QStringLiteral("sudo"), manager.executable};
        command.append(installArguments(manager));
        return commandLine(command);
    }
    return commandLine(elevatedCommand(manager));
}

QStringList RunnerInstaller::uninstallArguments(const PackageManagerInfo &manager)
{
    if (manager.id == QStringLiteral("pacman"))
        return {QStringLiteral("-R"), QStringLiteral("--noconfirm"), manager.packageName};
    if (manager.id == QStringLiteral("zypper"))
        return {QStringLiteral("--non-interactive"), QStringLiteral("remove"), manager.packageName};
    if (manager.id == QStringLiteral("apk"))
        return {QStringLiteral("del"), manager.packageName};
    if (manager.id == QStringLiteral("eopkg"))
        return {QStringLiteral("remove"), QStringLiteral("-y"), manager.packageName};
    if (manager.id == QStringLiteral("xbps-install"))
        return {QStringLiteral("-y"), manager.packageName};
    return {QStringLiteral("remove"), QStringLiteral("-y"), manager.packageName};
}

QString RunnerInstaller::uninstallCommandLine(const PackageManagerInfo &manager)
{
    if (!manager.available)
        return QStringLiteral("No supported package manager was detected.");
    if (!isRoot() && elevationTool().isEmpty()) {
        QStringList command = {QStringLiteral("sudo"), manager.executable};
        command.append(uninstallArguments(manager));
        return commandLine(command);
    }
    return commandLine(elevatedUninstallCommand(manager));
}

bool RunnerInstaller::isSystemWineInstalled()
{
    const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
    if (!wine.isEmpty())
        return true;
    const QString wine64 = QStandardPaths::findExecutable(QStringLiteral("wine64"));
    if (!wine64.isEmpty())
        return true;
    return QFile::exists(QStringLiteral("/usr/bin/wine"));
}

bool RunnerInstaller::isPortableWineInstalled()
{
    const QString root = managedRunnerRoot(RunnerType::Wine);
    if (!QDir(root).exists())
        return false;
    const QString wine = firstExistingRunnerInRoot(root, RunnerType::Wine);
    return !wine.isEmpty();
}

QList<QPair<QString, QString>> RunnerInstaller::installedProtonInstallations()
{
    QList<QPair<QString, QString>> results;
    QSet<QString> seen;

    const QString managed = managedRunnerRoot(RunnerType::Proton);
    if (QDir(managed).exists()) {
        const QFileInfoList children = QDir(managed).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children) {
            const QString candidate = RunnerDetector::protonExecutable(child.absoluteFilePath());
            if (!candidate.isEmpty()) {
                const QString base = RunnerDetector::protonBaseDirectory(candidate);
                if (!seen.contains(base)) {
                    seen.insert(base);
                    results.append({child.fileName(), base});
                }
            }
        }
        if (results.isEmpty()) {
            const QString candidate = RunnerDetector::protonExecutable(managed);
            if (!candidate.isEmpty()) {
                results.append({QStringLiteral("Managed Proton"), managed});
                seen.insert(managed);
            }
        }
    }

    for (const QString &searchDir : RunnerDetector::protonSearchDirectories()) {
        const QDir dir(searchDir);
        if (!dir.exists())
            continue;
        const QFileInfoList children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children) {
            const QString candidate = RunnerDetector::protonExecutable(child.absoluteFilePath());
            if (!candidate.isEmpty()) {
                const QString base = RunnerDetector::protonBaseDirectory(candidate);
                if (!seen.contains(base)) {
                    seen.insert(base);
                    results.append({child.fileName(), base});
                }
            }
        }
    }

    return results;
}

QString RunnerInstaller::managedRunnerRoot(RunnerType type)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/vanta/runners/");
    if (type == RunnerType::Proton)
        return root + QStringLiteral("proton");
    return root + QStringLiteral("wine");
}

void RunnerInstaller::installSystemWine()
{
    if (busy())
        return;
    const PackageManagerInfo manager = detectPackageManager();
    if (!manager.available) {
        emit failed(QStringLiteral("No supported package manager was detected on this system. Use the portable Wine download instead."));
        return;
    }
    if (!isRoot() && elevationTool().isEmpty()) {
        emit failed(QStringLiteral("No graphical elevation tool (pkexec) or sudo was found. Run this command in a terminal:\n%1")
                        .arg(installCommandLine(manager)));
        return;
    }

    QStringList command = elevatedCommand(manager);
    if (command.isEmpty())
        return;
    const QString program = command.takeFirst();
    beginOperation(Operation::InstallingPackage,
                   QStringLiteral("Installing Wine with %1…").arg(manager.displayName));
    m_processOutput.clear();

    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        m_processOutput = QString::fromLocal8Bit(process->readAll());
        if (m_process == process)
            m_process = nullptr;
        handlePackageFinished(exitCode, status);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, manager](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_operation != Operation::InstallingPackage)
            return;
        if (m_process == process)
            m_process = nullptr;
        failOperation(QStringLiteral("The package manager could not be started. Run this command manually:\n%1")
                          .arg(installCommandLine(manager)));
        process->deleteLater();
    });
    process->start(program, command);
}

void RunnerInstaller::uninstallSystemWine()
{
    if (busy())
        return;
    const PackageManagerInfo manager = detectPackageManager();
    if (!manager.available) {
        emit failed(QStringLiteral("No supported package manager was detected on this system."));
        return;
    }
    if (!isRoot() && elevationTool().isEmpty()) {
        emit failed(QStringLiteral("No graphical elevation tool (pkexec) or sudo was found. Run this command in a terminal:\n%1")
                        .arg(uninstallCommandLine(manager)));
        return;
    }

    QStringList command = elevatedUninstallCommand(manager);
    if (command.isEmpty())
        return;
    const QString program = command.takeFirst();
    beginOperation(Operation::UninstallingPackage,
                   QStringLiteral("Uninstalling Wine with %1…").arg(manager.displayName));
    m_processOutput.clear();

    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        m_processOutput = QString::fromLocal8Bit(process->readAll());
        if (m_process == process)
            m_process = nullptr;
        handlePackageUninstallFinished(exitCode, status);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, manager](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_operation != Operation::UninstallingPackage)
            return;
        if (m_process == process)
            m_process = nullptr;
        failOperation(QStringLiteral("The package manager could not be started. Run this command manually:\n%1")
                          .arg(uninstallCommandLine(manager)));
        process->deleteLater();
    });
    process->start(program, command);
}

void RunnerInstaller::uninstallPortableWine()
{
    if (busy())
        return;
    const QString root = managedRunnerRoot(RunnerType::Wine);
    if (!QDir(root).exists()) {
        failOperation(QStringLiteral("Portable Wine is not installed."));
        return;
    }
    beginOperation(Operation::Extracting, QStringLiteral("Uninstalling portable Wine…"));
    QDir dir(root);
    if (dir.removeRecursively()) {
        completeOperation(QStringLiteral("Portable Wine was uninstalled successfully."));
    } else {
        failOperation(QStringLiteral("Could not remove the folder %1.").arg(root));
    }
}

void RunnerInstaller::uninstallProton(const QString &directoryPath)
{
    if (busy())
        return;
    if (directoryPath.isEmpty() || !QDir(directoryPath).exists()) {
        failOperation(QStringLiteral("The specified Proton directory was not found."));
        return;
    }
    const QString name = QFileInfo(directoryPath).fileName();
    beginOperation(Operation::Extracting, QStringLiteral("Uninstalling %1…").arg(name));
    QDir dir(directoryPath);
    if (dir.removeRecursively()) {
        completeOperation(QStringLiteral("Uninstalled %1 successfully.").arg(name));
    } else {
        failOperation(QStringLiteral("Could not remove the folder %1.").arg(directoryPath));
    }
}

void RunnerInstaller::downloadPortableWine()
{
    fetchLatestRelease(RunnerType::Wine);
}

void RunnerInstaller::downloadProton()
{
    fetchLatestRelease(RunnerType::Proton);
}

void RunnerInstaller::fetchLatestRelease(RunnerType type)
{
    if (busy())
        return;
    m_requestedType = type;
    m_releaseTag.clear();
    const QString repository = type == RunnerType::Proton
        ? QStringLiteral("GloriousEggroll/proton-ge-custom")
        : QStringLiteral("Kron4ek/Wine-Builds");
    beginOperation(Operation::FetchingRelease,
                   QStringLiteral("Checking GitHub for the latest %1 release…")
                       .arg(type == RunnerType::Proton ? QStringLiteral("GE-Proton") : QStringLiteral("Wine build")));

    QNetworkRequest request{QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repository))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vanta/%1").arg(QStringLiteral(VANTA_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleReleaseReply(reply);
    });
}

void RunnerInstaller::handleReleaseReply(QNetworkReply *reply)
{
    if (m_operation != Operation::FetchingRelease || m_reply != reply) {
        reply->deleteLater();
        return;
    }
    m_reply.clear();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        failOperation(QStringLiteral("Could not reach GitHub: %1").arg(reply->errorString()));
        return;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (status.isValid() && status.toInt() != 200) {
        failOperation(QStringLiteral("GitHub returned HTTP %1 while looking up the latest release.")
                          .arg(status.toInt()));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failOperation(QStringLiteral("GitHub returned an unreadable release description: %1")
                          .arg(parseError.errorString()));
        return;
    }
    const QJsonObject release = document.object();
    m_releaseTag = release.value(QStringLiteral("tag_name")).toString();
    const AssetChoice asset = chooseAsset(release.value(QStringLiteral("assets")).toArray(), m_requestedType);
    if (asset.name.isEmpty() || asset.url.isEmpty()) {
        failOperation(m_requestedType == RunnerType::Proton
            ? QStringLiteral("No suitable %1 GE-Proton archive was published in the latest release.").arg(
                  QSysInfo::currentCpuArchitecture())
            : QStringLiteral("No suitable portable Wine archive was published in the latest release. Use the package manager option instead."));
        return;
    }

    m_extractionRoot = managedRunnerRoot(m_requestedType);
    if (!QDir().mkpath(m_extractionRoot)) {
        failOperation(QStringLiteral("Could not create the runner folder: %1").arg(m_extractionRoot));
        return;
    }
    startDownload(asset.url, asset.name, asset.size);
}

void RunnerInstaller::startDownload(const QString &url, const QString &name, qint64 expectedSize)
{
    const QString safeName = QFileInfo(name).fileName();
    if (safeName.isEmpty()) {
        failOperation(QStringLiteral("The release asset has an invalid file name."));
        return;
    }
    const QString downloadDirectory = QDir(m_extractionRoot).filePath(QStringLiteral(".downloads"));
    if (!QDir().mkpath(downloadDirectory)) {
        failOperation(QStringLiteral("Could not create the download folder: %1").arg(downloadDirectory));
        return;
    }
    m_temporaryFile = QDir(downloadDirectory).filePath(safeName);
    QFile::remove(m_temporaryFile);
    m_file.setFileName(m_temporaryFile);
    if (!m_file.open(QIODevice::WriteOnly)) {
        failOperation(QStringLiteral("Could not write to %1: %2").arg(m_temporaryFile, m_file.errorString()));
        return;
    }
    m_downloadName = safeName;
    m_downloadUrl = url;
    m_expectedSize = expectedSize;
    beginOperation(Operation::Downloading,
                   QStringLiteral("Downloading %1…").arg(m_releaseTag.isEmpty() ? safeName : m_releaseTag));

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vanta/%1").arg(QStringLiteral(VANTA_VERSION)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_network->setTransferTimeout(120000);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_operation != Operation::Downloading || m_reply != reply || !m_file.isOpen())
            return;
        m_file.write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (m_operation != Operation::Downloading)
            return;
        if (total > 0)
            m_expectedSize = total;
        emit downloadProgress(received, total > 0 ? total : m_expectedSize);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleDownloadFinished(reply);
    });
}

void RunnerInstaller::handleDownloadFinished(QNetworkReply *reply)
{
    if (m_operation != Operation::Downloading || m_reply != reply) {
        reply->deleteLater();
        return;
    }
    if (m_file.isOpen()) {
        m_file.write(reply->readAll());
        m_file.close();
    }
    m_reply.clear();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        if (error == QNetworkReply::OperationCanceledError)
            failOperation(QStringLiteral("The download was cancelled."));
        else
            failOperation(QStringLiteral("The download failed: %1").arg(errorText));
        return;
    }
    if (status.isValid() && status.toInt() != 200) {
        failOperation(QStringLiteral("The download server returned HTTP %1.").arg(status.toInt()));
        return;
    }
    const QFileInfo fileInfo(m_temporaryFile);
    if (!fileInfo.isFile() || fileInfo.size() <= 0) {
        failOperation(QStringLiteral("The downloaded archive is empty."));
        return;
    }
    if (m_expectedSize > 0 && fileInfo.size() != m_expectedSize) {
        failOperation(QStringLiteral("The downloaded archive is incomplete (%1 of %2 bytes).")
                          .arg(fileInfo.size()).arg(m_expectedSize));
        return;
    }
    beginExtraction();
}

void RunnerInstaller::beginExtraction()
{
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        failOperation(QStringLiteral("The tar command was not found; the downloaded archive cannot be extracted."));
        return;
    }
    beginOperation(Operation::Extracting,
                   QStringLiteral("Extracting %1…").arg(m_releaseTag.isEmpty() ? m_downloadName : m_releaseTag));
    m_processOutput.clear();

    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        m_processOutput = QString::fromLocal8Bit(process->readAll());
        if (m_process == process)
            m_process = nullptr;
        handleExtractionFinished(exitCode, status);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_operation != Operation::Extracting)
            return;
        if (m_process == process)
            m_process = nullptr;
        failOperation(QStringLiteral("The tar command could not be started."));
        process->deleteLater();
    });
    process->start(tar, {QStringLiteral("-xf"), m_temporaryFile, QStringLiteral("-C"), m_extractionRoot,
                         QStringLiteral("--no-same-owner")});
}

void RunnerInstaller::handleExtractionFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_operation != Operation::Extracting)
        return;
    if (status != QProcess::NormalExit || exitCode != 0) {
        QString detail = m_processOutput.trimmed();
        if (detail.size() > 4000)
            detail = detail.right(4000);
        failOperation(detail.isEmpty()
            ? QStringLiteral("The archive could not be extracted.")
            : QStringLiteral("The archive could not be extracted:\n%1").arg(detail));
        return;
    }
    if (!m_temporaryFile.isEmpty()) {
        QFile::remove(m_temporaryFile);
        m_temporaryFile.clear();
    }
    validateManagedRunner();
}

void RunnerInstaller::validateManagedRunner()
{
    const QString runner = firstExistingRunnerInRoot(m_extractionRoot, m_requestedType);
    if (runner.isEmpty()) {
        failOperation(m_requestedType == RunnerType::Proton
            ? QStringLiteral("The archive was extracted, but no Proton launcher was found in %1.").arg(m_extractionRoot)
            : QStringLiteral("The archive was extracted, but no Wine executable was found in %1.").arg(m_extractionRoot));
        return;
    }
    const QString label = m_requestedType == RunnerType::Proton
        ? QStringLiteral("GE-Proton") : QStringLiteral("portable Wine");
    completeOperation(QStringLiteral("Downloaded %1 %2 to %3.")
                          .arg(label, m_releaseTag.isEmpty() ? m_downloadName : m_releaseTag,
                               QFileInfo(runner).absolutePath()));
}

void RunnerInstaller::handlePackageFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_operation != Operation::InstallingPackage)
        return;
    if (status != QProcess::NormalExit || exitCode != 0) {
        QString detail = m_processOutput.trimmed();
        if (detail.size() > 4000)
            detail = detail.right(4000);
        const PackageManagerInfo manager = detectPackageManager();
        QString message = QStringLiteral("The package manager did not install Wine successfully.");
        if (!detail.isEmpty())
            message += QStringLiteral("\n\n%1").arg(detail);
        if (manager.available)
            message += QStringLiteral("\n\nYou can run this command manually:\n%1").arg(installCommandLine(manager));
        message += QStringLiteral("\n\nAlternatively install the portable Wine build from this dialog.");
        failOperation(message);
        return;
    }
    const QString wine = RunnerDetector::wineExecutable(AppSettings());
    if (wine.isEmpty()) {
        failOperation(QStringLiteral("The package manager finished, but the wine command is still not available. Log out and back in, or use the portable Wine download below."));
        return;
    }
    completeOperation(QStringLiteral("Wine was installed successfully: %1").arg(wine));
}

void RunnerInstaller::handlePackageUninstallFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_operation != Operation::UninstallingPackage)
        return;
    if (status != QProcess::NormalExit || exitCode != 0) {
        QString detail = m_processOutput.trimmed();
        if (detail.size() > 4000)
            detail = detail.right(4000);
        failOperation(detail.isEmpty()
            ? QStringLiteral("The package manager did not uninstall Wine successfully.")
            : QStringLiteral("The package manager did not uninstall Wine successfully:\n\n%1").arg(detail));
        return;
    }
    completeOperation(QStringLiteral("System Wine was uninstalled."));
}

void RunnerInstaller::beginOperation(Operation operation, const QString &message)
{
    const bool wasBusy = busy();
    m_operation = operation;
    m_cancelRequested = false;
    if (!wasBusy)
        emit busyChanged(true);
    if (!message.isEmpty())
        emit progress(message);
}

void RunnerInstaller::resetOperation(bool removeTemporaryFile)
{
    if (m_file.isOpen())
        m_file.close();
    if (m_file.fileName() == m_temporaryFile)
        m_file.setFileName(QString());
    if (removeTemporaryFile && !m_temporaryFile.isEmpty())
        QFile::remove(m_temporaryFile);
    m_temporaryFile.clear();
    m_downloadName.clear();
    m_downloadUrl.clear();
    m_expectedSize = -1;
    m_operation = Operation::None;
}

void RunnerInstaller::failOperation(const QString &message)
{
    const bool wasBusy = busy();
    resetOperation(true);
    if (wasBusy)
        emit busyChanged(false);
    emit failed(message);
}

void RunnerInstaller::completeOperation(const QString &message)
{
    const bool wasBusy = busy();
    resetOperation(false);
    if (wasBusy)
        emit busyChanged(false);
    emit finished(message);
}

void RunnerInstaller::cancelOperation()
{
    if (!busy())
        return;
    m_cancelRequested = true;
    if (m_reply) {
        QNetworkReply *reply = m_reply.data();
        m_reply.clear();
        reply->abort();
        reply->deleteLater();
    }
    if (m_process) {
        QProcess *process = m_process.data();
        m_process.clear();
        process->kill();
        process->deleteLater();
    }
    resetOperation(true);
    emit busyChanged(false);
    emit cancelled();
}

void RunnerInstaller::cancel()
{
    cancelOperation();
}
