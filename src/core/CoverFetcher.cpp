#include "CoverFetcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace {
// SteamGridDB issues one key per account. This is the key Vanta ships with so the
// feature works out of the box; Settings lets anyone override it with their own.
const char kBuiltInApiKey[] = "4bbdc63e99f0af9a46af980f7d05dac7";

QString normalise(const QString &value)
{
    QString result = value.toLower();
    result.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return result.simplified();
}

QStringList wordsOf(const QString &value)
{
    const QStringList words = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Articles carry no identifying information, and keeping them would let
    // "The Witcher 3" look more like "The Witcher 2" than it really is.
    static const QStringList stopWords = {QStringLiteral("the"), QStringLiteral("a"),
                                          QStringLiteral("an"), QStringLiteral("of")};
    QStringList filtered;
    for (const QString &word : words) {
        if (!stopWords.contains(word))
            filtered.append(word);
    }
    return filtered.isEmpty() ? words : filtered;
}

bool isNumberWord(const QString &word)
{
    if (word.isEmpty())
        return false;
    for (const QChar &character : word) {
        if (!character.isDigit())
            return false;
    }
    return true;
}

int editDistance(const QString &a, const QString &b)
{
    const int aLength = a.size();
    const int bLength = b.size();
    if (aLength == 0)
        return bLength;
    if (bLength == 0)
        return aLength;

    QList<int> previous;
    QList<int> current;
    previous.resize(bLength + 1);
    current.resize(bLength + 1);
    for (int j = 0; j <= bLength; ++j)
        previous[j] = j;

    for (int i = 1; i <= aLength; ++i) {
        current[0] = i;
        for (int j = 1; j <= bLength; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            current[j] = qMin(qMin(current[j - 1] + 1, previous[j] + 1), previous[j - 1] + cost);
        }
        previous = current;
    }
    return previous.at(bLength);
}

// Similarity of two individual words, 0..1. Numbers are compared strictly so that
// "Witcher 3" can never drift towards "Witcher 2".
qreal wordSimilarity(const QString &a, const QString &b)
{
    if (a == b)
        return 1.0;
    if (isNumberWord(a) || isNumberWord(b))
        return 0.0;
    const int longest = qMax(a.size(), b.size());
    if (longest == 0)
        return 0.0;
    const qreal distance = editDistance(a, b);
    return qBound(0.0, 1.0 - distance / longest, 1.0);
}

QStringList numberWords(const QStringList &words)
{
    QStringList numbers;
    for (const QString &word : words) {
        if (isNumberWord(word))
            numbers.append(word);
    }
    return numbers;
}

// Network errors stringify as the full request URL, which is useless in a dialog.
// Report the meaningful cause instead.
QString describeFailure(int status, const QString &fallback)
{
    switch (status) {
    case 401:
    case 403:
        return QStringLiteral("SteamGridDB rejected the API key.");
    case 404:
        return QStringLiteral("SteamGridDB has no entry for this game.");
    case 429:
        return QStringLiteral("SteamGridDB is rate limiting this API key. Try again in a minute.");
    case 500:
    case 502:
    case 503:
    case 504:
        return QStringLiteral("SteamGridDB is having server trouble. Try again later.");
    default:
        break;
    }
    if (status > 0)
        return QStringLiteral("%1 (HTTP %2).").arg(fallback).arg(status);
    return QStringLiteral("%1. Check your internet connection.").arg(fallback);
}
}

CoverFetcher::CoverFetcher(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_apiKey(QString::fromLatin1(kBuiltInApiKey))
{
}

CoverFetcher::~CoverFetcher()
{
    cancel();
}

QString CoverFetcher::defaultApiKey()
{
    return QString::fromLatin1(kBuiltInApiKey);
}

QString CoverFetcher::coversDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString path = QDir(base).filePath(QStringLiteral("vanta/covers"));
    QDir().mkpath(path);
    return path;
}

QString CoverFetcher::suggestedCoverPath(const QString &gameId, const QString &extension)
{
    QString suffix = extension.trimmed().toLower();
    if (!suffix.startsWith(QLatin1Char('.')))
        suffix.prepend(QLatin1Char('.'));
    if (suffix.size() < 2 || suffix.size() > 5)
        suffix = QStringLiteral(".jpg");
    return QDir(coversDirectory()).filePath(sanitizeFileName(gameId) + suffix);
}

QString CoverFetcher::detectImageExtension(const QByteArray &payload)
{
    if (payload.startsWith("\x89PNG\r\n\x1a\n"))
        return QStringLiteral(".png");
    if (payload.startsWith("\xff\xd8\xff"))
        return QStringLiteral(".jpg");
    if (payload.startsWith("GIF8"))
        return QStringLiteral(".gif");
    if (payload.startsWith("RIFF") && payload.mid(8, 4) == QByteArray("WEBP"))
        return QStringLiteral(".webp");
    return QStringLiteral(".jpg");
}

QString CoverFetcher::sanitizeFileName(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (result.isEmpty())
        result = QStringLiteral("cover");
    return result;
}

int CoverFetcher::similarity(const QString &query, const QString &candidate)
{
    const QString a = normalise(query);
    const QString b = normalise(candidate);
    if (a.isEmpty() || b.isEmpty())
        return 0;
    if (a == b)
        return 100;

    const QStringList aWords = wordsOf(a);
    const QStringList bWords = wordsOf(b);
    if (aWords.isEmpty() || bWords.isEmpty())
        return 0;

    // Align each query word with its best counterpart in the candidate. Averaging
    // keeps "Skyrim" close to the full title "The Elder Scrolls V: Skyrim".
    qreal queryCoverage = 0.0;
    for (const QString &word : aWords) {
        qreal best = 0.0;
        for (const QString &other : bWords)
            best = qMax(best, wordSimilarity(word, other));
        queryCoverage += best;
    }
    queryCoverage /= aWords.size();

    qreal score = queryCoverage * 100.0;

    // Words the query does not account for make a candidate less likely, but the
    // penalty must not depend on how much the leftover word happens to look like
    // the query: otherwise "Tetris Attack" would beat "Tetris Ultimate" for the
    // typo "Testris" just because "attack" is textually nearer to it.
    int unmatched = 0;
    for (const QString &other : bWords) {
        qreal best = 0.0;
        for (const QString &word : aWords)
            best = qMax(best, wordSimilarity(word, other));
        if (best < 0.72)
            ++unmatched;
    }
    if (unmatched > 0)
        score *= 1.0 - 0.16 * (static_cast<qreal>(unmatched) / bWords.size());

    // Sequel numbers are decisive. "The Witcher 3" must never resolve to
    // "The Witcher 2", and a numbered title should not win for a bare query.
    const QStringList queryNumbers = numberWords(aWords);
    const QStringList candidateNumbers = numberWords(bWords);
    if (!queryNumbers.isEmpty()) {
        bool allPresent = true;
        for (const QString &number : queryNumbers) {
            if (!candidateNumbers.contains(number)) {
                allPresent = false;
                break;
            }
        }
        if (!allPresent)
            score *= 0.55;
    } else if (!candidateNumbers.isEmpty()) {
        score *= 0.92;
    }

    return qBound(0, qRound(score), 100);
}

void CoverFetcher::setApiKey(const QString &apiKey)
{
    const QString trimmed = apiKey.trimmed();
    m_apiKey = trimmed.isEmpty() ? defaultApiKey() : trimmed;
}

bool CoverFetcher::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

QNetworkRequest CoverFetcher::makeRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vanta/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

void CoverFetcher::cleanupReply()
{
    if (!m_reply)
        return;
    m_reply->disconnect(this);
    m_reply->deleteLater();
    m_reply = nullptr;
}

void CoverFetcher::cleanupThumbReplies()
{
    for (QNetworkReply *reply : m_thumbReplies) {
        reply->disconnect(this);
        reply->deleteLater();
    }
    m_thumbReplies.clear();
}

void CoverFetcher::cancel()
{
    cleanupReply();
    cleanupThumbReplies();
    m_pendingGameId.clear();
    m_pendingGameName.clear();
}

void CoverFetcher::fetchCover(const QString &gameId, const QString &gameName)
{
    m_mode = Mode::SaveFirst;
    if (gameId.isEmpty() || gameName.trimmed().isEmpty())
        return;
    if (m_reply) {
        emit failed(gameId, QStringLiteral("A cover lookup is already running."));
        return;
    }
    if (!hasApiKey()) {
        emit failed(gameId, QStringLiteral("No SteamGridDB API key is configured."));
        return;
    }
    m_pendingGameId = gameId;
    m_pendingGameName = gameName.trimmed();
    emit started(gameId);
    searchByName(gameId, m_pendingGameName);
}

void CoverFetcher::listCoverArt(const QString &gameId, const QString &gameName)
{
    m_mode = Mode::ListArtworks;
    if (gameId.isEmpty() || gameName.trimmed().isEmpty())
        return;
    if (m_reply) {
        emit failed(gameId, QStringLiteral("A cover lookup is already running."));
        return;
    }
    if (!hasApiKey()) {
        emit failed(gameId, QStringLiteral("No SteamGridDB API key is configured."));
        return;
    }
    m_pendingGameId = gameId;
    m_pendingGameName = gameName.trimmed();
    emit started(gameId);
    searchByName(gameId, m_pendingGameName);
}

void CoverFetcher::downloadCoverArt(const QString &gameId, const CoverArt &art)
{
    m_mode = Mode::SaveFirst;
    if (gameId.isEmpty() || art.fullUrl.isEmpty())
        return;
    if (m_reply) {
        emit failed(gameId, QStringLiteral("A cover download is already running."));
        return;
    }
    emit started(gameId);
    downloadImage(gameId, art.fullUrl);
}

void CoverFetcher::searchByName(const QString &gameId, const QString &gameName)
{
    QUrl url(QStringLiteral("https://www.steamgriddb.com/api/v2/search/autocomplete/")
             + QString::fromLatin1(QUrl::toPercentEncoding(gameName)));
    m_reply = m_network->get(makeRequest(url));
    connect(m_reply, &QNetworkReply::finished, this, [this, gameId, gameName] {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        cleanupReply();

        if (error != QNetworkReply::NoError) {
            emit failed(gameId, describeFailure(
                status, QStringLiteral("SteamGridDB could not be searched")));
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (!root.value(QStringLiteral("success")).toBool()) {
            emit failed(gameId, QStringLiteral("SteamGridDB returned no results for “%1”.").arg(gameName));
            return;
        }

        const QJsonArray data = root.value(QStringLiteral("data")).toArray();
        CoverCandidate best;
        for (const QJsonValue &value : data) {
            const QJsonObject entry = value.toObject();
            const QString id = QString::number(entry.value(QStringLiteral("id")).toInt());
            const QString name = entry.value(QStringLiteral("name")).toString();
            if (id.isEmpty() || name.isEmpty())
                continue;
            const int score = similarity(gameName, name);
            if (score > best.score) {
                best.gameId = id;
                best.gameName = name;
                best.score = score;
            }
        }

        if (best.gameId.isEmpty() || best.score < 45) {
            emit failed(gameId, QStringLiteral("No SteamGridDB match was close enough to “%1”.").arg(gameName));
            return;
        }

        emit resolved(gameId, best.gameName, best.score);
        requestGrids(gameId, best.gameId, best.gameName);
    });
}

void CoverFetcher::requestGrids(const QString &gameId, const QString &steamGridId,
                                const QString &matchedName)
{
    QUrl url(QStringLiteral("https://www.steamgriddb.com/api/v2/grids/game/") + steamGridId);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("dimensions"), QStringLiteral("600x900,342x482,660x930"));
    query.addQueryItem(QStringLiteral("types"), QStringLiteral("static"));
    query.addQueryItem(QStringLiteral("nsfw"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("humor"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("epilepsy"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("30"));
    url.setQuery(query);

    const Mode mode = m_mode;
    m_reply = m_network->get(makeRequest(url));
    connect(m_reply, &QNetworkReply::finished, this, [this, gameId, matchedName, mode] {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        cleanupReply();

        if (error != QNetworkReply::NoError) {
            emit failed(gameId, describeFailure(
                status, QStringLiteral("SteamGridDB has no grid list for the matched game")));
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const QJsonArray data = root.value(QStringLiteral("data")).toArray();

        QList<CoverArt> artworks;
        for (const QJsonValue &value : data) {
            const QJsonObject entry = value.toObject();
            CoverArt art;
            art.fullUrl = entry.value(QStringLiteral("url")).toString();
            art.thumbUrl = entry.value(QStringLiteral("thumb")).toString();
            art.gridId = QString::number(entry.value(QStringLiteral("id")).toInt());
            art.width = entry.value(QStringLiteral("width")).toInt();
            art.height = entry.value(QStringLiteral("height")).toInt();
            art.upvotes = entry.value(QStringLiteral("upvotes")).toInt();
            art.author = entry.value(QStringLiteral("author")).toObject()
                             .value(QStringLiteral("name")).toString();
            if (art.fullUrl.isEmpty())
                continue;
            artworks.append(art);
        }

        if (artworks.isEmpty()) {
            emit failed(gameId, QStringLiteral("SteamGridDB has no portrait grid for this game yet."));
            return;
        }

        if (mode == Mode::ListArtworks) {
            emit artworksListed(gameId, matchedName, artworks);
            return;
        }

        downloadImage(gameId, artworks.first().fullUrl);
    });
}

void CoverFetcher::downloadThumbnail(const QString &gridId, const QString &thumbUrl)
{
    if (gridId.isEmpty() || thumbUrl.isEmpty())
        return;
    QNetworkRequest request{QUrl(thumbUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vanta/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // Thumbnails are fetched concurrently: the picker asks for every artwork at
    // once, so serialising or cancelling previous requests would starve the grid.
    auto *reply = m_network->get(request);
    m_thumbReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gridId] {
        m_thumbReplies.removeAll(reply);
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || payload.isEmpty())
            return;
        emit thumbnailReady(gridId, payload);
    });
}

void CoverFetcher::downloadImage(const QString &gameId, const QString &imageUrl)
{
    QNetworkRequest request{QUrl(imageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vanta/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, gameId] {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        cleanupReply();

        if (error != QNetworkReply::NoError || payload.isEmpty()) {
            emit failed(gameId, QStringLiteral("The cover image could not be downloaded."));
            return;
        }

        const QString path = suggestedCoverPath(gameId, detectImageExtension(payload));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit failed(gameId, QStringLiteral("The cover image could not be saved to %1.").arg(path));
            return;
        }
        file.write(payload);
        file.close();
        if (QFileInfo(path).size() <= 0) {
            QFile::remove(path);
            emit failed(gameId, QStringLiteral("The downloaded cover image was empty."));
            return;
        }
        // A previous fetch may have saved the same cover under another extension.
        for (const QString &other : {QStringLiteral(".jpg"), QStringLiteral(".png"),
                                     QStringLiteral(".webp"), QStringLiteral(".gif")}) {
            const QString stale = suggestedCoverPath(gameId, other);
            if (stale != path && QFileInfo::exists(stale))
                QFile::remove(stale);
        }
        emit finished(gameId, path);
    });
}
