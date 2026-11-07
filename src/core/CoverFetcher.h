#pragma once

#include <QByteArray>
#include <QNetworkRequest>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

struct CoverCandidate
{
    QString gameId;
    QString gameName;
    int score = 0;
};

// One artwork option offered for a resolved game.
struct CoverArt
{
    QString gridId;
    QString fullUrl;
    QString thumbUrl;
    QString author;
    int width = 0;
    int height = 0;
    int upvotes = 0;
};

// Resolves a game name to SteamGridDB, lists the available artwork and downloads
// the one the user picks.
//
// Name resolution is deliberately forgiving: an exact case-insensitive match wins
// outright, otherwise the closest candidate is chosen by token-aligned similarity,
// so a typo such as "Testris" still finds "Tetris".
//
// Two entry points are offered:
//   fetchCover()    - resolve, take the first artwork and save it in one go.
//   listCoverArt()  - resolve, then report every artwork so a UI can offer a choice.
// The artwork stage always reports the *resolved* SteamGridDB game id, which is
// not the same thing as the Vanta game id carried through the signals.
class CoverFetcher : public QObject
{
    Q_OBJECT

public:
    explicit CoverFetcher(QObject *parent = nullptr);
    ~CoverFetcher() override;

    static QString defaultApiKey();
    static QString coversDirectory();
    static QString suggestedCoverPath(const QString &gameId, const QString &extension = QStringLiteral(".jpg"));
    static QString detectImageExtension(const QByteArray &payload);

    // Similarity in the range 0..100. Exposed for the fuzzy-match unit checks.
    static int similarity(const QString &query, const QString &candidate);

    void setApiKey(const QString &apiKey);
    bool hasApiKey() const;

    void fetchCover(const QString &gameId, const QString &gameName);
    void listCoverArt(const QString &gameId, const QString &gameName);
    void downloadCoverArt(const QString &gameId, const CoverArt &art);
    void downloadThumbnail(const QString &gridId, const QString &thumbUrl);
    void cancel();

signals:
    void started(const QString &gameId);
    void resolved(const QString &gameId, const QString &matchedName, int score);
    void artworksListed(const QString &gameId, const QString &matchedName, const QList<CoverArt> &artworks);
    void thumbnailReady(const QString &gridId, const QByteArray &payload);
    void finished(const QString &gameId, const QString &coverPath);
    void failed(const QString &gameId, const QString &message);

private:
    enum class Mode { SaveFirst, ListArtworks };

    void searchByName(const QString &gameId, const QString &gameName);
    void requestGrids(const QString &gameId, const QString &steamGridId, const QString &matchedName);
    void downloadImage(const QString &gameId, const QString &imageUrl);
    QNetworkRequest makeRequest(const QUrl &url) const;
    void cleanupReply();
    void cleanupThumbReplies();
    static QString sanitizeFileName(const QString &value);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QList<QNetworkReply *> m_thumbReplies;
    QString m_apiKey;
    QString m_pendingGameId;
    QString m_pendingGameName;
    Mode m_mode = Mode::SaveFirst;
};
