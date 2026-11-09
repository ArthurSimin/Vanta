#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// One release as reported by the GitHub releases API.
struct UpdateRelease
{
    QString tagName;
    QString name;
    QString htmlUrl;
    QString body;
    QString publishedAt;
    QString tarballUrl;
    QString debUrl;
    QString rpmUrl;
    QString pacmanUrl;

    bool isValid() const { return !tagName.isEmpty() && !htmlUrl.isEmpty(); }
};

// Asks the GitHub releases API for the newest published Vanta release and
// reports whether it is newer than the running build.
//
// The check is deliberately plain: it reads one public endpoint, keeps no
// state, writes nothing and never downloads a package. Installing an update is
// left to the package manager, and the UI only offers to open the release page.
//
// A failed check is not an error the user has to fix, so callers should treat
// it as "no update known" rather than surfacing a dialog.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    // Compares dotted version strings numerically segment by segment, so
    // "1.0.10" is newer than "1.0.9" and a trailing suffix ("1.0.3-beta",
    // "v1.0.3") neither breaks the comparison nor outranks a plain release.
    // Returns a positive number when `a` is newer, negative when `b` is newer
    // and 0 when they are equivalent.
    static int compareVersions(const QString &a, const QString &b);

    // Normalises "v1.0.3" to "1.0.3". Any leading "v"/"V" is dropped.
    static QString normaliseVersion(const QString &version);

    // Extracts a release from the JSON body of the GitHub releases endpoint.
    // Returns an invalid release when the payload cannot be read.
    static UpdateRelease parseRelease(const QByteArray &payload);

    static QString releasesApiUrl();

    // True when `candidate` is newer than `current`.
    static bool isNewer(const QString &candidate, const QString &current);

    void checkForUpdates(const QString &currentVersion);
    void cancel();

signals:
    void updateAvailable(const UpdateRelease &release, const QString &currentVersion);
    void upToDate(const QString &currentVersion);
    void checkSkipped(const QString &currentVersion);
    void checkFailed(const QString &message);

private:
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_currentVersion;
};
