#include "UpdateChecker.h"
#include "Version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr int kRequestTimeoutMs = 9000;

// A dotted numeric prefix, ignoring any leading "v" and stopping at the first
// non-numeric part. "1.0.3-beta" yields {1, 0, 3}; "v2" yields {2}.
QList<int> numericSegments(const QString &version)
{
    QList<int> segments;
    const QString trimmed = UpdateChecker::normaliseVersion(version);
    const QStringList parts = trimmed.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        static const QRegularExpression digits(QStringLiteral("^(\\d+)"));
        const QRegularExpressionMatch match = digits.match(part);
        if (!match.hasMatch())
            break;
        segments.append(match.captured(1).toInt());
    }
    return segments;
}

// The part after the numeric prefix, which tells a pre-release apart from a
// plain release. "1.0.3" and "1.0.3-1" have none worth ranking, while
// "1.0.3-beta" and "1.0.3-rc1" do.
QString versionTail(const QString &version)
{
    QString tail = UpdateChecker::normaliseVersion(version);
    tail.remove(QRegularExpression(QStringLiteral("^\\d+(\\.\\d+)*")));
    return tail;
}

bool isPreRelease(const QString &version)
{
    const QString tail = versionTail(version);
    if (tail.isEmpty())
        return false;
    // A pure numeric tail is a packaging revision ("1.0.3-1"), which is the same
    // upstream version, so it must not read as older. A tail with letters is a
    // pre-release marker.
    static const QRegularExpression letters(QStringLiteral("[A-Za-z]"));
    return tail.contains(letters);
}

QString assetUrlFor(const QJsonArray &assets, const QString &suffix)
{
    for (const QJsonValue &value : assets) {
        if (!value.isObject())
            continue;
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(suffix, Qt::CaseInsensitive))
            continue;
        const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
        if (!url.isEmpty())
            return url;
    }
    return QString();
}
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

UpdateChecker::~UpdateChecker()
{
    cancel();
}

QString UpdateChecker::normaliseVersion(const QString &version)
{
    QString result = version.trimmed();
    while (!result.isEmpty() && (result.at(0) == QLatin1Char('v') || result.at(0) == QLatin1Char('V')))
        result.remove(0, 1);
    return result;
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const QList<int> left = numericSegments(a);
    const QList<int> right = numericSegments(b);
    const int count = qMax(left.size(), right.size());
    for (int index = 0; index < count; ++index) {
        const int lhs = index < left.size() ? left.at(index) : 0;
        const int rhs = index < right.size() ? right.at(index) : 0;
        if (lhs != rhs)
            return lhs < rhs ? -1 : 1;
    }

    // Equal numerically. A pre-release marker ("1.0.3-beta") sorts below the
    // plain release, and a packaging suffix such as "1.0.3-1" counts as equal so
    // it cannot hide an available update.
    const bool leftPre = isPreRelease(a);
    const bool rightPre = isPreRelease(b);
    if (leftPre != rightPre)
        return leftPre ? -1 : 1;
    return 0;
}

bool UpdateChecker::isNewer(const QString &candidate, const QString &current)
{
    return compareVersions(candidate, current) > 0;
}

QString UpdateChecker::releasesApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/ArthurSimin/Vanta/releases/latest");
}

UpdateRelease UpdateChecker::parseRelease(const QByteArray &payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};

    const QJsonObject object = document.object();
    UpdateRelease release;
    release.tagName = object.value(QStringLiteral("tag_name")).toString();
    release.name = object.value(QStringLiteral("name")).toString();
    release.htmlUrl = object.value(QStringLiteral("html_url")).toString();
    release.body = object.value(QStringLiteral("body")).toString();
    release.publishedAt = object.value(QStringLiteral("published_at")).toString();

    if (release.tagName.isEmpty() || release.htmlUrl.isEmpty())
        return {};

    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    release.debUrl = assetUrlFor(assets, QStringLiteral(".deb"));
    release.rpmUrl = assetUrlFor(assets, QStringLiteral(".rpm"));
    release.pacmanUrl = assetUrlFor(assets, QStringLiteral(".pkg.tar.zst"));
    release.tarballUrl = assetUrlFor(assets, QStringLiteral(".tar.gz"));

    // The tag is the identity that survives a renamed release.
    if (release.name.isEmpty())
        release.name = release.tagName;
    return release;
}

void UpdateChecker::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void UpdateChecker::checkForUpdates(const QString &currentVersion)
{
    cancel();
    m_currentVersion = currentVersion;

    // A local build that is not a release has no meaningful version to compare,
    // so skip the request instead of reporting a permanent "update available".
    // This is a separate signal because the caller must not claim the build is
    // up to date when nothing was asked.
    if (numericSegments(currentVersion).isEmpty()) {
        emit checkSkipped(currentVersion);
        return;
    }

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(releasesApiUrl())};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Vanta/%1").arg(QStringLiteral(VANTA_VERSION)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(kRequestTimeoutMs);

    m_reply = m_network->get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_reply) {
            reply->deleteLater();
            return;
        }
        handleReply(reply);
    });
}

void UpdateChecker::handleReply(QNetworkReply *reply)
{
    m_reply = nullptr;
    const QNetworkReply::NetworkError error = reply->error();
    const QVariant statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusValue.isValid() ? statusValue.toInt() : 0;
    const QString errorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (error == QNetworkReply::OperationCanceledError)
        return;

    if (error != QNetworkReply::NoError || status >= 400) {
        // Rate limiting is the common failure and it is not actionable, so say
        // so plainly instead of leaking the HTTP phrasing.
        if (status == 403 || status == 429)
            emit checkFailed(QStringLiteral("GitHub is rate limiting update checks right now."));
        else if (status == 404)
            emit checkFailed(QStringLiteral("The release feed could not be found."));
        else if (status >= 400)
            emit checkFailed(QStringLiteral("GitHub returned HTTP %1.").arg(status));
        else
            emit checkFailed(QStringLiteral("The update check could not reach GitHub: %1").arg(errorText));
        return;
    }

    const UpdateRelease release = parseRelease(payload);
    if (!release.isValid()) {
        emit checkFailed(QStringLiteral("The release feed could not be read."));
        return;
    }

    if (isNewer(release.tagName, m_currentVersion))
        emit updateAvailable(release, m_currentVersion);
    else
        emit upToDate(m_currentVersion);
}
