#include "SteamPrecondition.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVariantMap>
#include <QDateTime>

namespace {
class VdfParser
{
public:
    explicit VdfParser(const QString &text) : m_text(text) {}

    QVariantMap parse()
    {
        skipSpace();
        return parseObject(false);
    }

private:
    QVariantMap parseObject(bool expectClose)
    {
        QVariantMap map;
        while (true) {
            skipSpace();
            if (m_pos >= m_text.size())
                break;
            if (m_text.at(m_pos) == QLatin1Char('}') && expectClose) {
                ++m_pos;
                break;
            }
            const QString key = parseString();
            if (key.isEmpty())
                break;
            skipSpace();
            if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char('{')) {
                ++m_pos;
                map.insert(key, parseObject(true));
            } else {
                map.insert(key, parseString());
            }
        }
        return map;
    }

    QString parseString()
    {
        skipSpace();
        if (m_pos >= m_text.size())
            return {};
        if (m_text.at(m_pos) == QLatin1Char('"')) {
            ++m_pos;
            QString result;
            while (m_pos < m_text.size()) {
                const QChar chr = m_text.at(m_pos++);
                if (chr == QLatin1Char('"'))
                    break;
                if (chr == QLatin1Char('\\') && m_pos < m_text.size()) {
                    const QChar escaped = m_text.at(m_pos++);
                    switch (escaped.unicode()) {
                    case 'n': result.append(QLatin1Char('\n')); break;
                    case 't': result.append(QLatin1Char('\t')); break;
                    case '\\': result.append(QLatin1Char('\\')); break;
                    case '"': result.append(QLatin1Char('"')); break;
                    default: result.append(escaped); break;
                    }
                } else {
                    result.append(chr);
                }
            }
            return result;
        }
        const int start = m_pos;
        while (m_pos < m_text.size() && !m_text.at(m_pos).isSpace()
               && m_text.at(m_pos) != QLatin1Char('{') && m_text.at(m_pos) != QLatin1Char('}'))
            ++m_pos;
        return m_text.mid(start, m_pos - start);
    }

    void skipSpace()
    {
        while (m_pos < m_text.size()) {
            if (m_text.at(m_pos).isSpace()) {
                ++m_pos;
                continue;
            }
            if (m_text.mid(m_pos, 2) == QStringLiteral("//")) {
                const int newline = m_text.indexOf(QLatin1Char('\n'), m_pos);
                m_pos = newline < 0 ? m_text.size() : newline + 1;
                continue;
            }
            break;
        }
    }

    const QString &m_text;
    int m_pos = 0;
};

QString libraryFoldersPath(const QString &root)
{
    return root + QStringLiteral("/steamapps/libraryfolders.vdf");
}

void appendUniquePath(QStringList &paths, const QString &path)
{
    if (path.trimmed().isEmpty())
        return;
    const QFileInfo info(path);
    const QString clean = info.exists() ? info.canonicalFilePath() : QDir::cleanPath(path);
    if (clean.isEmpty() || paths.contains(clean))
        return;
    paths.append(clean);
}

QStringList steamRootCandidates()
{
    const QString home = QDir::homePath();
    return {
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
        home + QStringLiteral("/.steam/root")
    };
}
}

QString SteamIntegration::steamInstallRoot()
{
    const QStringList candidates = steamRootCandidates();
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isDir())
            return QFileInfo(candidate).canonicalFilePath();
    }
    return QDir::cleanPath(candidates.first());
}

QStringList SteamIntegration::steamLibraryRoots()
{
    QStringList roots;
    appendUniquePath(roots, steamInstallRoot());

    for (const QString &candidate : steamRootCandidates()) {
        QFile file(libraryFoldersPath(candidate));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QVariantMap root = VdfParser(QString::fromUtf8(file.readAll())).parse();
        const QVariantMap folders = root.value(QStringLiteral("libraryfolders")).toMap();
        for (auto it = folders.constBegin(); it != folders.constEnd(); ++it) {
            if (!it.value().canConvert<QVariantMap>())
                continue;
            const QString path = it.value().toMap().value(QStringLiteral("path")).toString();
            if (path.isEmpty())
                continue;
            if (QFileInfo(QDir(path).filePath(QStringLiteral("steamapps"))).isDir())
                appendUniquePath(roots, path);
        }
    }
    return roots;
}

QString SteamIntegration::loginUsersPath()
{
    return steamInstallRoot() + QStringLiteral("/config/loginusers.vdf");
}

bool SteamIntegration::steamBinaryAvailable(QString *path)
{
    const QString found = QStandardPaths::findExecutable(QStringLiteral("steam"));
    if (path)
        *path = found;
    return !found.isEmpty();
}

bool SteamIntegration::steamIsRunning(QString *diagnostic)
{
    QProcess process;
    process.start(QStringLiteral("pidof"), {QStringLiteral("steam")});
    if (!process.waitForStarted(1000)) {
        if (diagnostic)
            *diagnostic = QStringLiteral("pidof is not available; checking /proc instead.");
    } else if (process.waitForFinished(1500)) {
        if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
            return true;
        return false;
    } else {
        process.kill();
        process.waitForFinished(200);
    }

    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        entry.toLongLong(&numeric);
        if (!numeric)
            continue;
        QFile comm(QStringLiteral("/proc/") + entry + QStringLiteral("/comm"));
        if (!comm.open(QIODevice::ReadOnly))
            continue;
        if (QString::fromUtf8(comm.readAll()).trimmed() == QStringLiteral("steam"))
            return true;
    }
    return false;
}

QString SteamIntegration::steamRuntimeAppIdForProton(const QString &protonPath)
{
    const QFileInfo info(protonPath);
    QString base;
    if (info.isDir())
        base = info.absoluteFilePath();
    else
        base = info.absolutePath();
    if (QFileInfo(base).fileName() == QStringLiteral("bin"))
        base = QFileInfo(base).absolutePath();

    int version = 11;
    QFile versionFile(QDir(base).filePath(QStringLiteral("version")));
    if (versionFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(versionFile.readAll()).trimmed();
        const QRegularExpression expression(QStringLiteral("(\\d{1,2})"));
        const QRegularExpressionMatch match = expression.match(text);
        if (match.hasMatch()) {
            bool ok = false;
            const int parsed = match.captured(1).toInt(&ok);
            if (ok)
                version = parsed;
        }
    }

    if (version >= 11)
        return QSysInfo::currentCpuArchitecture().contains(QStringLiteral("arm64"), Qt::CaseInsensitive)
            || QSysInfo::currentCpuArchitecture().contains(QStringLiteral("aarch64"), Qt::CaseInsensitive)
            ? QStringLiteral("4185400") : QStringLiteral("4183110");
    if (version >= 8)
        return QStringLiteral("1628350");
    return QStringLiteral("1391110");
}

QString SteamIntegration::steamRuntimeDirectoryForProton(const QString &protonPath)
{
    const QString app = steamRuntimeAppIdForProton(protonPath);
    if (app == QStringLiteral("4183110"))
        return QStringLiteral("SteamLinuxRuntime_4");
    if (app == QStringLiteral("4185400"))
        return QStringLiteral("SteamLinuxRuntime_4-arm64");
    if (app == QStringLiteral("1628350"))
        return QStringLiteral("SteamLinuxRuntime_sniper");
    return QStringLiteral("SteamLinuxRuntime_soldier");
}

QString SteamIntegration::findSteamRuntime(const QString &protonPath)
{
    if (protonPath.isEmpty())
        return {};
    const QString runtimeDir = steamRuntimeDirectoryForProton(protonPath);
    const QStringList roots = steamLibraryRoots();
    for (const QString &root : roots) {
        const QString run = QDir(root).filePath(QStringLiteral("steamapps/common/") + runtimeDir
                                               + QStringLiteral("/run"));
        if (QFileInfo(run).isFile())
            return run;
    }
    return {};
}

SteamPrecondition::SteamPrecondition(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &SteamPrecondition::pollLoginUsers);
}

void SteamPrecondition::ensureRunning()
{
    m_finished = false;
    m_attempts = 0;
    QString steamPath;
    if (!SteamIntegration::steamBinaryAvailable(&steamPath)) {
        fail(QStringLiteral("Steam is required for an OnlineFix launch, but the steam command was not found on PATH. Disable the Steam precondition in Settings if you do not need the Steam client."));
        return;
    }
    QFileInfo loginInfo(SteamIntegration::loginUsersPath());
    m_initialLoginExisted = loginInfo.exists();
    m_initialLoginMtime = loginInfo.exists() ? loginInfo.lastModified().toMSecsSinceEpoch() : 0;
    checkPid();
}

void SteamPrecondition::checkPid()
{
    if (m_finished)
        return;
    if (SteamIntegration::steamIsRunning()) {
        m_finished = true;
        emit ready();
        return;
    }
    startSteam();
}

void SteamPrecondition::startSteam()
{
    if (m_finished)
        return;
    emit progress(QStringLiteral("Starting Steam and waiting for the client to become ready…"));
    const bool started = QProcess::startDetached(QStringLiteral("steam"), {QStringLiteral("-silent")});
    if (!started) {
        fail(QStringLiteral("Steam could not be started. Start Steam manually and try again."));
        return;
    }
    m_pollTimer->start();
}

void SteamPrecondition::pollLoginUsers()
{
    if (m_finished)
        return;
    ++m_attempts;
    const QFileInfo info(SteamIntegration::loginUsersPath());
    if (info.exists()) {
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        if (!m_initialLoginExisted || modified != m_initialLoginMtime) {
            m_finished = true;
            m_pollTimer->stop();
            emit ready();
            return;
        }
    }
    if (m_attempts >= 420) {
        fail(QStringLiteral("Steam did not finish starting within seven minutes. Start Steam manually, verify that you are signed in, and try again."));
        return;
    }
    emit progress(QStringLiteral("Waiting for Steam… (%1/420)").arg(m_attempts));
}

void SteamPrecondition::cancel()
{
    if (m_finished)
        return;
    m_finished = true;
    m_pollTimer->stop();
}

void SteamPrecondition::fail(const QString &message)
{
    if (m_finished)
        return;
    m_finished = true;
    m_pollTimer->stop();
    emit failed(message);
}
