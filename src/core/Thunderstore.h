#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// One downloadable release of a Thunderstore package. Dependencies use the
// "Owner-Name-Version" form declared by the package author.
struct ThunderstoreVersion
{
    QString name;
    QString fullName;
    QString versionNumber;
    QString description;
    QString iconUrl;
    QString downloadUrl;
    QString websiteUrl;
    QStringList dependencies;
    qint64 downloads = 0;
    qint64 fileSize = 0;
    QDateTime dateCreated;

    bool isValid() const { return !name.isEmpty() || !fullName.isEmpty(); }
};

struct ThunderstorePackage
{
    QString name;
    QString fullName;
    QString owner;
    QString packageUrl;
    QString description;
    QString iconUrl;
    QStringList categories;
    bool deprecated = false;
    QList<ThunderstoreVersion> versions;

    bool isValid() const { return !fullName.isEmpty() && !versions.isEmpty(); }
    ThunderstoreVersion latest() const { return versions.value(0); }
};

struct ThunderstoreDependency
{
    QString owner;
    QString name;
    QString version;

    QString key() const { return (owner + QLatin1Char('-') + name).toLower(); }
    QString text() const
    {
        if (version.isEmpty())
            return owner + QLatin1Char('-') + name;
        return owner + QLatin1Char('-') + name + QLatin1Char('-') + version;
    }
};

// Read-only client for the public Thunderstore API. It loads one community's
// package list, can fetch a single package that is not listed in that community,
// and downloads release archives. Nothing is written to disk here; the caller
// decides what to install.
class Thunderstore : public QObject
{
    Q_OBJECT

public:
    explicit Thunderstore(QObject *parent = nullptr);
    ~Thunderstore() override;

    static QUrl packagesUrl(const QString &community);
    static QString communitySlug(const QString &input);
    static QStringList communityCandidates(const QString &gameName);
    static bool packageFromLink(const QString &input, QString *community, QString *owner, QString *name);
    static ThunderstoreDependency parseDependency(const QString &text);

    // Resolves dependencies depth-first and returns them in install order, with
    // the deepest dependency first. `missing` receives references that are not
    // present in the supplied community list. The root package is not included.
    static QList<ThunderstoreVersion> resolveDependencies(const ThunderstorePackage &root,
                                                          const QList<ThunderstorePackage> &packages,
                                                          QStringList *missing);

    void setCommunity(const QString &community);
    QString community() const { return m_community; }

    void loadPackages();
    void loadPackage(const QString &owner, const QString &name);
    void download(const ThunderstoreVersion &version, const QString &context);
    void cancel();

signals:
    void packagesLoaded(const QString &community, const QList<ThunderstorePackage> &packages);
    void packageLoaded(const ThunderstorePackage &package);
    void downloadProgress(const QString &context, qint64 received, qint64 total);
    void downloadFinished(const QString &context, const QByteArray &payload);
    void failed(const QString &context, const QString &message);

private:
    void get(const QUrl &url, const QString &context);
    void handleReply(QNetworkReply *reply);
    void fail(const QString &context, const QString &message);
    void clearReply();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_context;
    QString m_community;
};
