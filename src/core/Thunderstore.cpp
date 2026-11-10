#include "Thunderstore.h"

#include "I18n.h"
#include "Version.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <functional>

namespace {
constexpr int kRequestTimeoutMs = 25000;
constexpr int kDownloadTimeoutMs = 180000;

// QStringList::indexOf() lost its case-sensitivity argument in Qt 6, so the
// lookup is done by hand and stays available on every supported Qt release.
int indexOfNoCase(const QStringList &values, const QString &needle, int from = 0)
{
    for (int index = qMax(0, from); index < values.size(); ++index) {
        if (values.at(index).compare(needle, Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

QDateTime parseDate(const QString &text)
{
    if (text.isEmpty())
        return {};
    QDateTime value = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!value.isValid())
        value = QDateTime::fromString(text, Qt::ISODate);
    return value;
}

ThunderstoreVersion versionFromJson(const QJsonObject &object)
{
    ThunderstoreVersion version;
    version.name = object.value(QStringLiteral("name")).toString();
    version.fullName = object.value(QStringLiteral("full_name")).toString();
    version.versionNumber = object.value(QStringLiteral("version_number")).toString();
    version.description = object.value(QStringLiteral("description")).toString();
    version.iconUrl = object.value(QStringLiteral("icon")).toString();
    version.downloadUrl = object.value(QStringLiteral("download_url")).toString();
    version.websiteUrl = object.value(QStringLiteral("website_url")).toString();
    version.downloads = static_cast<qint64>(object.value(QStringLiteral("downloads")).toDouble());
    version.fileSize = static_cast<qint64>(object.value(QStringLiteral("file_size")).toDouble());
    version.dateCreated = parseDate(object.value(QStringLiteral("date_created")).toString());

    const QJsonArray dependencies = object.value(QStringLiteral("dependencies")).toArray();
    for (const QJsonValue &value : dependencies) {
        if (value.isString()) {
            const QString dependency = value.toString().trimmed();
            if (!dependency.isEmpty())
                version.dependencies.append(dependency);
            continue;
        }
        if (!value.isObject())
            continue;
        const QJsonObject dependency = value.toObject();
        const QString owner = dependency.value(QStringLiteral("namespace")).toString(
            dependency.value(QStringLiteral("owner")).toString());
        const QString name = dependency.value(QStringLiteral("name")).toString();
        const QString number = dependency.value(QStringLiteral("version_number")).toString();
        if (owner.isEmpty() || name.isEmpty())
            continue;
        QString text = owner + QLatin1Char('-') + name;
        if (!number.isEmpty())
            text += QLatin1Char('-') + number;
        version.dependencies.append(text);
    }
    return version;
}

ThunderstorePackage packageFromJson(const QJsonObject &object)
{
    ThunderstorePackage package;
    package.name = object.value(QStringLiteral("name")).toString();
    package.fullName = object.value(QStringLiteral("full_name")).toString();
    package.owner = object.value(QStringLiteral("owner")).toString();
    if (package.owner.isEmpty())
        package.owner = object.value(QStringLiteral("namespace")).toString();
    if (package.fullName.isEmpty() && !package.owner.isEmpty() && !package.name.isEmpty())
        package.fullName = package.owner + QLatin1Char('-') + package.name;
    if (package.owner.isEmpty() && package.fullName.contains(QLatin1Char('-'))) {
        const int separator = package.fullName.indexOf(QLatin1Char('-'));
        package.owner = package.fullName.left(separator);
    }
    package.packageUrl = object.value(QStringLiteral("package_url")).toString();
    package.description = object.value(QStringLiteral("description")).toString();
    package.iconUrl = object.value(QStringLiteral("icon")).toString();
    package.deprecated = object.value(QStringLiteral("is_deprecated")).toBool(false);
    const QJsonArray categories = object.value(QStringLiteral("categories")).toArray();
    for (const QJsonValue &category : categories) {
        const QString name = category.toString().trimmed();
        if (!name.isEmpty())
            package.categories.append(name);
    }

    const QJsonArray versions = object.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue &value : versions) {
        if (value.isObject())
            package.versions.append(versionFromJson(value.toObject()));
    }
    if (package.versions.isEmpty() && object.value(QStringLiteral("latest")).isObject())
        package.versions.append(versionFromJson(object.value(QStringLiteral("latest")).toObject()));
    if (package.versions.isEmpty())
        return package;

    if (package.name.isEmpty())
        package.name = package.versions.first().name;
    if (package.fullName.isEmpty())
        package.fullName = package.versions.first().fullName;
    if (package.description.isEmpty())
        package.description = package.versions.first().description;
    if (package.iconUrl.isEmpty())
        package.iconUrl = package.versions.first().iconUrl;
    return package;
}

QString describeFailure(int status, const QByteArray &payload, const QString &errorText,
                        const QString &context)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    QString serverMessage;
    if (parseError.error == QJsonParseError::NoError && document.isObject())
        serverMessage = document.object().value(QStringLiteral("error")).toString().trimmed();

    if (status == 404) {
        if (context == QStringLiteral("packages"))
            return Vanta::tr("No Thunderstore community was found at that address.");
        if (context == QStringLiteral("package"))
            return Vanta::tr("Thunderstore has no package at that address.");
        return Vanta::tr("The package download was not found.");
    }
    if (status == 403 || status == 429)
        return Vanta::tr("Thunderstore is rate limiting requests. Try again in a minute.");
    if (serverMessage.contains(QStringLiteral("cache"), Qt::CaseInsensitive)) {
        return Vanta::tr("Thunderstore did not return a package list for this community. "
                         "Check the community slug, or try again in a moment.");
    }
    if (status >= 500)
        return Vanta::tr("Thunderstore is having server trouble. Try again later.");
    if (!errorText.isEmpty())
        return Vanta::tr("Thunderstore could not be reached: %1").arg(errorText);
    return Vanta::tr("Thunderstore could not be reached. Check your internet connection.");
}

QString normalisedGameName(const QString &gameName)
{
    QString value = gameName.toLower();
    value.replace(QLatin1Char('&'), QStringLiteral(" and "));
    value.remove(QLatin1Char('\''));
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return value.simplified();
}

QString slugWithSeparator(const QString &words, const QString &separator)
{
    QStringList parts = words.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    return parts.join(separator);
}
}

Thunderstore::Thunderstore(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

Thunderstore::~Thunderstore()
{
    cancel();
}

QUrl Thunderstore::packagesUrl(const QString &community)
{
    if (community.trimmed().isEmpty())
        return {};
    return QUrl(QStringLiteral("https://thunderstore.io/c/%1/api/v1/package/").arg(community.trimmed()));
}

QString Thunderstore::communitySlug(const QString &input)
{
    QString value = input.trimmed();
    if (value.isEmpty())
        return {};

    if (value.contains(QStringLiteral("thunderstore.io")) || value.startsWith(QStringLiteral("/c/"))
        || value.startsWith(QStringLiteral("c/"))) {
        const QUrl url = QUrl::fromUserInput(value.startsWith(QLatin1Char('/'))
                                                 ? QStringLiteral("https://thunderstore.io") + value
                                                 : value);
        QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int index = indexOfNoCase(parts, QStringLiteral("c"));
        if (index >= 0 && index + 1 < parts.size())
            return parts.at(index + 1).toLower();
        if (parts.size() == 1 && !parts.first().contains(QLatin1Char('.')))
            return parts.first().toLower();
        return {};
    }

    if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char(' '))) {
        value = value.toLower();
        value.replace(QRegularExpression(QStringLiteral("[^a-z0-9-]+")), QStringLiteral("-"));
        while (value.contains(QStringLiteral("--")))
            value.replace(QStringLiteral("--"), QStringLiteral("-"));
        while (value.startsWith(QLatin1Char('-')) || value.endsWith(QLatin1Char('-'))) {
            if (value.startsWith(QLatin1Char('-')))
                value.remove(0, 1);
            if (value.endsWith(QLatin1Char('-')))
                value.chop(1);
        }
    }
    value = value.toLower();
    if (!QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9-]*$")).match(value).hasMatch())
        return {};
    return value;
}

QStringList Thunderstore::communityCandidates(const QString &gameName)
{
    const QString words = normalisedGameName(gameName);
    if (words.isEmpty())
        return {};

    QStringList candidates;
    const auto add = [&candidates](const QString &candidate) {
        if (!candidate.isEmpty() && !candidates.contains(candidate))
            candidates.append(candidate);
    };
    add(slugWithSeparator(words, QStringLiteral("-")));
    add(slugWithSeparator(words, QString()));

    if (words.startsWith(QStringLiteral("the "))) {
        const QString withoutArticle = words.mid(4);
        add(slugWithSeparator(withoutArticle, QStringLiteral("-")));
        add(slugWithSeparator(withoutArticle, QString()));
    }
    return candidates;
}

bool Thunderstore::packageFromLink(const QString &input, QString *community, QString *owner,
                                   QString *name)
{
    if (community)
        community->clear();
    if (owner)
        owner->clear();
    if (name)
        name->clear();

    QString value = input.trimmed();
    if (value.isEmpty())
        return false;

    if (value.contains(QStringLiteral("thunderstore.io")) || value.startsWith(QStringLiteral("/"))) {
        const QUrl url = QUrl::fromUserInput(value.startsWith(QLatin1Char('/'))
                                                 ? QStringLiteral("https://thunderstore.io") + value
                                                 : value);
        QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int communityIndex = indexOfNoCase(parts, QStringLiteral("c"));
        if (communityIndex >= 0 && communityIndex + 1 < parts.size() && community)
            *community = parts.at(communityIndex + 1).toLower();

        int ownerIndex = -1;
        const int packageIndex = indexOfNoCase(parts, QStringLiteral("p"));
        if (packageIndex >= 0 && packageIndex + 2 < parts.size())
            ownerIndex = packageIndex + 1;
        else if (parts.size() >= 3 && parts.first().compare(QStringLiteral("package"), Qt::CaseInsensitive) == 0)
            ownerIndex = parts.at(1).compare(QStringLiteral("download"), Qt::CaseInsensitive) == 0 ? 2 : 1;
        if (ownerIndex < 0 || ownerIndex + 1 >= parts.size())
            return false;
        if (owner)
            *owner = parts.at(ownerIndex);
        if (name)
            *name = parts.at(ownerIndex + 1);
        return !parts.at(ownerIndex).isEmpty() && !parts.at(ownerIndex + 1).isEmpty();
    }

    if (value.contains(QLatin1Char('/'))) {
        const QStringList parts = value.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() != 2)
            return false;
        if (owner)
            *owner = parts.at(0);
        if (name)
            *name = parts.at(1);
        return true;
    }

    const int separator = value.indexOf(QLatin1Char('-'));
    if (separator <= 0 || separator + 1 >= value.size())
        return false;
    if (owner)
        *owner = value.left(separator);
    if (name)
        *name = value.mid(separator + 1);
    return true;
}

ThunderstoreDependency Thunderstore::parseDependency(const QString &text)
{
    ThunderstoreDependency dependency;
    QString value = text.trimmed();
    if (value.isEmpty())
        return dependency;

    int versionSeparator = value.lastIndexOf(QLatin1Char('-'));
    if (versionSeparator > 0 && versionSeparator + 1 < value.size()) {
        const QString suffix = value.mid(versionSeparator + 1);
        if (!suffix.isEmpty() && suffix.at(0).isDigit()) {
            dependency.version = suffix;
            value = value.left(versionSeparator);
        }
    }

    const int ownerSeparator = value.indexOf(QLatin1Char('-'));
    if (ownerSeparator <= 0 || ownerSeparator + 1 >= value.size())
        return {};
    dependency.owner = value.left(ownerSeparator);
    dependency.name = value.mid(ownerSeparator + 1);
    return dependency;
}

QList<ThunderstoreVersion> Thunderstore::resolveDependencies(const ThunderstorePackage &root,
                                                             const QList<ThunderstorePackage> &packages,
                                                             QStringList *missing,
                                                             QStringList *notes)
{
    QHash<QString, const ThunderstorePackage *> byKey;
    for (const ThunderstorePackage &package : packages)
        byKey.insert(package.fullName.toLower(), &package);

    QList<ThunderstoreVersion> ordered;
    QSet<QString> visited;

    std::function<void(const ThunderstoreVersion &)> visit = [&](const ThunderstoreVersion &version) {
        for (const QString &text : version.dependencies) {
            const ThunderstoreDependency dependency = parseDependency(text);
            if (dependency.owner.isEmpty() || dependency.name.isEmpty()) {
                if (missing && !missing->contains(text))
                    missing->append(text);
                continue;
            }
            const QString key = dependency.key();
            if (visited.contains(key))
                continue;
            visited.insert(key);

            const auto found = byKey.constFind(key);
            if (found == byKey.constEnd()) {
                if (missing) {
                    const QString label = dependency.text();
                    if (!missing->contains(label))
                        missing->append(label);
                }
                continue;
            }
            const ThunderstorePackage *package = found.value();
            ThunderstoreVersion selected = package->latest();
            if (!dependency.version.isEmpty()) {
                for (const ThunderstoreVersion &candidate : package->versions) {
                    if (candidate.versionNumber.compare(dependency.version, Qt::CaseInsensitive) == 0) {
                        selected = candidate;
                        break;
                    }
                }
                // A pinned version is a request, not a guarantee: communities
                // keep the newest release in the list and authors often do not
                // publish the exact build a package was tested against. Falling
                // back to the newest release is still useful, but it has to be
                // visible instead of silent.
                if (selected.versionNumber.compare(dependency.version, Qt::CaseInsensitive) != 0
                    && notes) {
                    notes->append(Vanta::tr("%1 %2 is not published here; Vanta installed %3 instead.")
                                      .arg(dependency.owner + QLatin1Char('-') + dependency.name,
                                           dependency.version,
                                           selected.versionNumber));
                }
            }
            if (selected.fullName.isEmpty())
                continue;
            visit(selected);
            ordered.append(selected);
        }
    };

    visit(root.latest());
    return ordered;
}

void Thunderstore::setCommunity(const QString &community)
{
    const QString slug = communitySlug(community);
    m_community = slug.isEmpty() ? community.trimmed().toLower() : slug;
}

void Thunderstore::loadPackages()
{
    const QUrl url = packagesUrl(m_community);
    if (!url.isValid()) {
        fail(QStringLiteral("packages"), Vanta::tr("Enter a Thunderstore community slug first."));
        return;
    }
    get(url, QStringLiteral("packages"));
}

void Thunderstore::loadPackage(const QString &owner, const QString &name)
{
    if (owner.trimmed().isEmpty() || name.trimmed().isEmpty()) {
        fail(QStringLiteral("package"), Vanta::tr("The Thunderstore package address is incomplete."));
        return;
    }
    const QUrl url(QStringLiteral("https://thunderstore.io/api/experimental/package/%1/%2/")
                       .arg(owner.trimmed(), name.trimmed()));
    get(url, QStringLiteral("package"));
}

void Thunderstore::download(const ThunderstoreVersion &version, const QString &context)
{
    const QUrl url(version.downloadUrl);
    if (!url.isValid()) {
        fail(context, Vanta::tr("The package has no download address."));
        return;
    }
    // The address comes from a remote API, so it is restricted to https before a
    // GET is issued with the user's machine as the source.
    if (url.scheme() != QStringLiteral("https")) {
        fail(context, Vanta::tr("The package download address is not an https URL."));
        return;
    }
    get(url, context);
}

void Thunderstore::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    m_context.clear();
    reply->abort();
    reply->deleteLater();
}

void Thunderstore::get(const QUrl &url, const QString &context)
{
    cancel();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Vanta/%1").arg(QStringLiteral(VANTA_VERSION)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(context.startsWith(QStringLiteral("download"))
                                   ? kDownloadTimeoutMs : kRequestTimeoutMs);

    m_context = context;
    m_reply = m_network->get(request);
    QNetworkReply *reply = m_reply;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply, context](qint64 received, qint64 total) {
                if (reply == m_reply)
                    emit downloadProgress(context, received, total);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_reply) {
            reply->deleteLater();
            return;
        }
        handleReply(reply);
    });
}

void Thunderstore::handleReply(QNetworkReply *reply)
{
    const QString context = m_context;
    m_reply = nullptr;
    m_context.clear();

    const QVariant statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusValue.isValid() ? statusValue.toInt() : 0;
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (error == QNetworkReply::OperationCanceledError)
        return;

    if (error != QNetworkReply::NoError || status >= 400) {
        emit failed(context, describeFailure(status, payload, errorText, context));
        return;
    }

    if (context == QStringLiteral("packages")) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit failed(context, Vanta::tr("Thunderstore returned a package list that could not be read."));
            return;
        }
        if (document.isObject()) {
            const QString serverMessage = document.object().value(QStringLiteral("error")).toString();
            if (!serverMessage.isEmpty()) {
                emit failed(context, Vanta::tr("Thunderstore did not return a package list: %1")
                                         .arg(serverMessage));
                return;
            }
        }
        if (!document.isArray()) {
            emit failed(context, Vanta::tr("Thunderstore returned an unexpected package list."));
            return;
        }
        QList<ThunderstorePackage> packages;
        const QJsonArray array = document.array();
        packages.reserve(array.size());
        for (const QJsonValue &value : array) {
            if (!value.isObject())
                continue;
            ThunderstorePackage package = packageFromJson(value.toObject());
            if (package.isValid())
                packages.append(package);
        }
        emit packagesLoaded(m_community, packages);
        return;
    }

    if (context == QStringLiteral("package")) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit failed(context, Vanta::tr("Thunderstore returned a package that could not be read."));
            return;
        }
        const ThunderstorePackage package = packageFromJson(document.object());
        if (!package.isValid()) {
            emit failed(context, Vanta::tr("Thunderstore returned an incomplete package."));
            return;
        }
        emit packageLoaded(package);
        return;
    }

    if (context.startsWith(QStringLiteral("download"))) {
        if (payload.isEmpty()) {
            emit failed(context, Vanta::tr("The downloaded package is empty."));
            return;
        }
        emit downloadFinished(context, payload);
    }
}

void Thunderstore::fail(const QString &context, const QString &message)
{
    emit failed(context, message);
}
