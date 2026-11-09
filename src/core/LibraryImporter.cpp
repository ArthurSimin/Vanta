#include "LibraryImporter.h"
#include "Library.h"
#include "RunnerDetector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString homePath()
{
    return QDir::homePath();
}

QString canonicalOrClean(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    if (info.exists())
        return info.canonicalFilePath();
    return QDir::cleanPath(path);
}

int leadingSpaces(const QString &line)
{
    int count = 0;
    while (count < line.length() && line.at(count) == QLatin1Char(' '))
        ++count;
    return count;
}

// Value of a direct child `key` under a top-level `section`. Unlike the
// single-key lookup above this honours indentation, so a key nested one level
// deeper (Lutris writes wine.overrides.version as well as wine.version) is not
// mistaken for the section's own key.
QString parseYamlSectionValue(const QString &yamlContent, const QString &section, const QString &key)
{
    const QString sectionKey = section + QStringLiteral(":");
    const QString targetKey = key + QStringLiteral(":");
    const QStringList lines = yamlContent.split(QLatin1Char('\n'));
    int sectionIndent = -1;
    int childIndent = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &raw = lines.at(i);
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        const int indent = leadingSpaces(raw);
        if (sectionIndent < 0) {
            if (indent == 0 && trimmed == sectionKey)
                sectionIndent = 0;
            continue;
        }
        if (indent <= sectionIndent)
            break;
        if (childIndent < 0)
            childIndent = indent;
        if (indent != childIndent || !trimmed.startsWith(targetKey))
            continue;

        QString value = trimmed.mid(targetKey.length()).trimmed();
        // Fold a wrapped plain scalar, the shape Lutris uses for long paths.
        while (i + 1 < lines.size()) {
            const QString &nextRaw = lines.at(i + 1);
            if (nextRaw.startsWith(QStringLiteral("  ")) && !nextRaw.trimmed().contains(QLatin1Char(':'))) {
                value += QLatin1Char(' ') + nextRaw.trimmed();
                ++i;
            } else {
                break;
            }
        }
        if (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')) && value.length() >= 2)
            value = value.mid(1, value.length() - 2);
        else if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) && value.length() >= 2)
            value = value.mid(1, value.length() - 2);
        return value.trimmed();
    }
    return {};
}

// Lutris stores the runner as "wine" even when the build is Proton; the version
// string under wine: is the part that tells the truth. A version naming Proton
// selects Proton, one naming Wine selects Wine, and the pga.db runner is only
// the fallback for a configuration with no version recorded.
RunnerType lutrisRunnerFor(const QString &dbRunner, const QString &version, bool windowsExecutable)
{
    const QString runner = dbRunner.trimmed().toLower();
    const QString build = version.trimmed().toLower();
    if (runner == QStringLiteral("proton") || build.contains(QStringLiteral("proton")))
        return RunnerType::Proton;
    if (runner == QStringLiteral("wine") || build.contains(QStringLiteral("wine")) || windowsExecutable)
        return RunnerType::Wine;
    return RunnerType::Native;
}

// Best effort: point the imported game at a detected build whose name matches
// the recorded version so the launch reuses it. An unmatched version leaves the
// path blank and the launch plan resolves a runner at start time, which is
// safer than guessing a path that may not be the build Lutris used. The
// detected entries are passed in because rescanning the runner directories for
// every game would be wasteful.
QString matchRunnerPath(const QString &version, RunnerType runner,
                        const QList<RunnerEntry> &wineEntries,
                        const QList<RunnerEntry> &protonEntries)
{
    const QString needle = version.trimmed();
    if (needle.isEmpty() || !runnerIsWineLike(runner))
        return {};
    const QList<RunnerEntry> &entries = runner == RunnerType::Proton ? protonEntries : wineEntries;
    for (const RunnerEntry &entry : entries) {
        // A recorded version names the build alone, so it is matched against the
        // unsuffixed build name rather than the "build (source)" display label.
        if (entry.buildName.compare(needle, Qt::CaseInsensitive) == 0)
            return entry.path;
    }
    return {};
}

QString findCandidateExecutableInDirectory(const QString &directory, const QString &preferredName = {})
{
    const QDir dir(directory);
    if (!dir.exists())
        return {};

    // Search top-level and 1 subdirectory down
    const QStringList ignorePatterns = {
        QStringLiteral("unitycrashhandler*"),
        QStringLiteral("crashreport*"),
        QStringLiteral("dxsetup*"),
        QStringLiteral("unins*"),
        QStringLiteral("uninstall*"),
        QStringLiteral("setup*")
    };

    struct Candidate {
        QString path;
        qint64 size = 0;
        int score = 0;
    };
    QList<Candidate> candidates;

    auto inspectDir = [&](const QDir &d, int depth) {
        const QFileInfoList entries = d.entryInfoList(QDir::Files | QDir::Executable, QDir::Size);
        for (const QFileInfo &entry : entries) {
            const QString name = entry.fileName().toLower();
            bool ignore = false;
            for (const QString &pattern : ignorePatterns) {
                if (QDir::match(pattern, name)) {
                    ignore = true;
                    break;
                }
            }
            if (ignore)
                continue;

            int score = 0;
            if (name.endsWith(QStringLiteral(".exe")))
                score += 50;
            else if (entry.isExecutable() && !name.contains(QLatin1Char('.')))
                score += 40;

            if (!preferredName.isEmpty() && name.contains(preferredName.toLower()))
                score += 100;
            if (depth == 0)
                score += 20;

            Candidate c;
            c.path = entry.absoluteFilePath();
            c.size = entry.size();
            c.score = score;
            candidates.append(c);
        }
    };

    inspectDir(dir, 0);
    const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &sub : subdirs) {
        inspectDir(QDir(sub.absoluteFilePath()), 1);
    }

    if (candidates.isEmpty())
        return {};

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.size > b.size;
    });

    return candidates.first().path;
}

} // namespace

bool LibraryImporter::isGameInLibrary(const Game &candidate, const GameLibrary *library)
{
    if (!library)
        return false;

    const QString candidateExe = canonicalOrClean(candidate.executable);
    const QString candidateDir = canonicalOrClean(candidate.gameDirectory);

    for (const Game &existing : library->games()) {
        const QString existingExe = canonicalOrClean(existing.executable);
        if (!candidateExe.isEmpty() && !existingExe.isEmpty() && candidateExe == existingExe)
            return true;
        const QString existingDir = canonicalOrClean(existing.gameDirectory);
        if (!candidateDir.isEmpty() && !existingDir.isEmpty() && candidateDir == existingDir)
            return true;
        if (existing.name.compare(candidate.name, Qt::CaseInsensitive) == 0 && existing.source == candidate.source)
            return true;
    }
    return false;
}

QList<ImportedGameCandidate> LibraryImporter::scanAll(const GameLibrary *existingLibrary)
{
    QList<ImportedGameCandidate> all;
    all.append(scanLutris());
    all.append(scanHeroic());

    // Deduplicate against each other
    QList<ImportedGameCandidate> unique;
    QSet<QString> seenExecutables;
    QSet<QString> seenNames;

    for (ImportedGameCandidate &item : all) {
        const QString exeKey = canonicalOrClean(item.game.executable);
        const QString nameKey = item.game.name.toLower() + QLatin1Char('|') + item.sourceLauncher;
        if (!exeKey.isEmpty() && seenExecutables.contains(exeKey))
            continue;
        if (seenNames.contains(nameKey))
            continue;

        if (!exeKey.isEmpty())
            seenExecutables.insert(exeKey);
        seenNames.insert(nameKey);

        if (existingLibrary)
            item.alreadyInLibrary = isGameInLibrary(item.game, existingLibrary);

        unique.append(item);
    }

    return unique;
}

QList<ImportedGameCandidate> LibraryImporter::scanLutris()
{
    QList<ImportedGameCandidate> results;
    const QString home = homePath();

    // Detected once for the whole scan. A version is matched against these for
    // every game, so rescanning the runner directories per game would be
    // wasteful. AppSettings defaults are used because the importer has no
    // settings of its own; this matches the builds Vanta detects out of the box.
    const AppSettings detectionSettings;
    const QList<RunnerEntry> detectedWine = RunnerDetector::wineEntries(detectionSettings);
    const QList<RunnerEntry> detectedProton = RunnerDetector::protonEntries(detectionSettings);
    const QStringList lutrisDirs = {
        home + QStringLiteral("/.local/share/lutris"),
        home + QStringLiteral("/.var/app/net.lutris.Lutris/data/lutris")
    };

    struct LutrisDbRow {
        QString name;
        QString slug;
        QString runner;
        QString platform;
        QString directory;
        QString configpath;
    };

    for (const QString &lutrisDir : lutrisDirs) {
        if (!QDir(lutrisDir).exists())
            continue;

        QList<LutrisDbRow> dbRows;
        const QString dbPath = QDir(lutrisDir).filePath(QStringLiteral("pga.db"));

        // Query pga.db if sqlite3 CLI is available
        const QString sqlite3 = QStandardPaths::findExecutable(QStringLiteral("sqlite3"));
        if (!sqlite3.isEmpty() && QFile::exists(dbPath)) {
            // A game title may contain a pipe, so the columns are separated with
            // a control character instead of the default "|".
            const QString separator = QString(QChar(0x1f));
            QProcess process;
            process.start(sqlite3, {QStringLiteral("-separator"), separator, dbPath,
                                    QStringLiteral("SELECT name, slug, runner, platform, directory, "
                                                   "configpath FROM games WHERE installed = 1;")});
            if (process.waitForFinished(5000)) {
                const QString output = QString::fromUtf8(process.readAllStandardOutput());
                const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    const QStringList parts = line.split(separator);
                    if (parts.size() >= 6) {
                        LutrisDbRow row;
                        row.name = parts.at(0).trimmed();
                        row.slug = parts.at(1).trimmed();
                        row.runner = parts.at(2).trimmed();
                        row.platform = parts.at(3).trimmed();
                        row.directory = parts.at(4).trimmed();
                        row.configpath = parts.at(5).trimmed();
                        dbRows.append(row);
                    }
                }
            }
        }

        const QString gamesDir = QDir(lutrisDir).filePath(QStringLiteral("games"));
        if (dbRows.isEmpty() && QDir(gamesDir).exists()) {
            // Fallback: discover games directly from YAML files in games/
            const QFileInfoList ymlFiles = QDir(gamesDir).entryInfoList({QStringLiteral("*.yml")}, QDir::Files);
            for (const QFileInfo &fileInfo : ymlFiles) {
                LutrisDbRow row;
                row.slug = fileInfo.completeBaseName();
                row.configpath = row.slug;
                row.name = fileInfo.baseName().replace(QLatin1Char('-'), QLatin1Char(' '));
                dbRows.append(row);
            }
        }

        for (const LutrisDbRow &row : dbRows) {
            QString yamlPath;
            if (!row.configpath.isEmpty()) {
                const QString candidate = QDir(gamesDir).filePath(row.configpath + QStringLiteral(".yml"));
                if (QFile::exists(candidate))
                    yamlPath = candidate;
            }
            if (yamlPath.isEmpty() && !row.slug.isEmpty()) {
                const QString candidate = QDir(gamesDir).filePath(row.slug + QStringLiteral(".yml"));
                if (QFile::exists(candidate))
                    yamlPath = candidate;
            }

            QString exe;
            QString prefix;
            QString args;
            QString wineVersion;
            if (!yamlPath.isEmpty()) {
                QFile yFile(yamlPath);
                if (yFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    const QString content = QString::fromUtf8(yFile.readAll());
                    exe = parseYamlSectionValue(content, QStringLiteral("game"), QStringLiteral("exe"));
                    prefix = parseYamlSectionValue(content, QStringLiteral("game"), QStringLiteral("prefix"));
                    args = parseYamlSectionValue(content, QStringLiteral("game"), QStringLiteral("args"));
                    wineVersion = parseYamlSectionValue(content, QStringLiteral("wine"), QStringLiteral("version"));
                }
            }

            if (exe.isEmpty() && !row.directory.isEmpty() && QDir(row.directory).exists()) {
                exe = findCandidateExecutableInDirectory(row.directory, row.name);
            }

            if (exe.isEmpty() && row.directory.isEmpty())
                continue;

            Game game = Game::create();
            game.name = row.name.isEmpty() ? (row.slug.isEmpty() ? QStringLiteral("Lutris Game") : row.slug) : row.name;
            game.source = QStringLiteral("Lutris");
            game.executable = exe;
            game.gameDirectory = !row.directory.isEmpty() ? row.directory : QFileInfo(exe).absolutePath();
            game.workingDirectory = game.gameDirectory;
            game.winePrefix = prefix;
            game.launchArguments = args;

            const bool isWindows = exe.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
                || row.platform.compare(QStringLiteral("Windows"), Qt::CaseInsensitive) == 0;
            game.platform = isWindows ? Platform::Windows : Platform::Linux;
            game.runner = lutrisRunnerFor(row.runner, wineVersion, isWindows);
            game.runnerPath = matchRunnerPath(wineVersion, game.runner, detectedWine, detectedProton);

            if (!row.slug.isEmpty()) {
                const QStringList coverCandidates = {
                    QDir(lutrisDir).filePath(QStringLiteral("coverart/%1.jpg").arg(row.slug)),
                    QDir(lutrisDir).filePath(QStringLiteral("coverart/%1.png").arg(row.slug)),
                    QDir(lutrisDir).filePath(QStringLiteral("banners/%1.jpg").arg(row.slug)),
                    QDir(lutrisDir).filePath(QStringLiteral("banners/%1.png").arg(row.slug))
                };
                for (const QString &cPath : coverCandidates) {
                    if (QFile::exists(cPath)) {
                        game.coverPath = cPath;
                        break;
                    }
                }
            }

            ImportedGameCandidate candidate;
            candidate.game = game;
            candidate.sourceLauncher = QStringLiteral("Lutris");
            candidate.originalId = row.slug;
            results.append(candidate);
        }
    }
    return results;
}

QList<ImportedGameCandidate> LibraryImporter::scanHeroic()
{
    QList<ImportedGameCandidate> results;
    const QString home = homePath();
    const QStringList heroicRoots = {
        home + QStringLiteral("/.config/heroic"),
        home + QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/config/heroic")
    };

    // Detected once so a per-game Proton selection can be pointed at a real
    // build rather than relying on a "proton" that may not be on PATH. The
    // importer has no settings of its own, so the defaults are used, which
    // matches the builds Vanta detects out of the box.
    const AppSettings detectionSettings;
    const QList<RunnerEntry> detectedProton = RunnerDetector::protonEntries(detectionSettings);

    for (const QString &root : heroicRoots) {
        if (!QDir(root).exists())
            continue;

        // 1. Legendary (Epic Games)
        const QStringList legendaryPaths = {
            QDir(root).filePath(QStringLiteral("legendaryConfig/legendary/installed.json")),
            home + QStringLiteral("/.config/legendary/installed.json")
        };
        for (const QString &legendaryPath : legendaryPaths) {
            QFile file(legendaryPath);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isObject())
                continue;
            const QJsonObject rootObj = doc.object();
            for (auto it = rootObj.begin(); it != rootObj.end(); ++it) {
                if (!it.value().isObject())
                    continue;
                const QJsonObject gameObj = it.value().toObject();
                const QString title = gameObj.value(QStringLiteral("title")).toString();
                const QString installPath = gameObj.value(QStringLiteral("install_path")).toString();
                QString exe = gameObj.value(QStringLiteral("executable")).toString();
                if (title.isEmpty() || installPath.isEmpty())
                    continue;
                if (!QFileInfo(exe).isAbsolute())
                    exe = QDir(installPath).filePath(exe);

                // Check per-game config
                QString prefix;
                QString heroicVersionName;
                QString heroicVersionBin;
                RunnerType heroicRunner = RunnerType::Wine;
                const QString gameConfigPath = QDir(root).filePath(
                    QStringLiteral("GamesConfig/%1.json").arg(it.key()));
                QFile cfgFile(gameConfigPath);
                if (cfgFile.open(QIODevice::ReadOnly)) {
                    const QJsonDocument cfgDoc = QJsonDocument::fromJson(cfgFile.readAll());
                    if (cfgDoc.isObject()) {
                        const QJsonObject cfg = cfgDoc.object();
                        prefix = cfg.value(QStringLiteral("winePrefix")).toString();
                        // Heroic records the chosen build; a Proton name or
                        // type means the truer runner is Proton, not Wine.
                        const QJsonObject wineVersion = cfg.value(QStringLiteral("wineVersion")).toObject();
                        heroicVersionName = wineVersion.value(QStringLiteral("name")).toString();
                        heroicVersionBin = wineVersion.value(QStringLiteral("bin")).toString();
                        const QString versionType = wineVersion.value(QStringLiteral("type")).toString();
                        if (heroicVersionName.contains(QStringLiteral("proton"), Qt::CaseInsensitive)
                            || versionType.contains(QStringLiteral("proton"), Qt::CaseInsensitive))
                            heroicRunner = RunnerType::Proton;
                    }
                }

                Game game = Game::create();
                game.name = title;
                game.source = QStringLiteral("Epic Games Store");
                game.gameDirectory = installPath;
                game.executable = exe;
                game.workingDirectory = installPath;
                game.winePrefix = prefix;
                game.platform = Platform::Windows;
                game.runner = heroicRunner;
                if (heroicRunner == RunnerType::Proton) {
                    // Prefer a build Vanta detected; fall back to the path Heroic
                    // recorded so a launch does not need "proton" on PATH.
                    game.runnerPath = matchRunnerPath(heroicVersionName, RunnerType::Proton, {},
                                                      detectedProton);
                    if (game.runnerPath.isEmpty())
                        game.runnerPath = heroicVersionBin.trimmed();
                }

                const QStringList coverCandidates = {
                    QDir(root).filePath(QStringLiteral("icons/%1.jpg").arg(title)),
                    QDir(root).filePath(QStringLiteral("icons/%1.png").arg(title)),
                    QDir(root).filePath(QStringLiteral("icons/%1.jpg").arg(it.key())),
                    QDir(root).filePath(QStringLiteral("icons/%1.png").arg(it.key()))
                };
                for (const QString &cPath : coverCandidates) {
                    if (QFile::exists(cPath)) {
                        game.coverPath = cPath;
                        break;
                    }
                }

                ImportedGameCandidate candidate;
                candidate.game = game;
                candidate.sourceLauncher = QStringLiteral("Heroic");
                candidate.originalId = it.key();
                results.append(candidate);
            }
        }

        // 2. GOG
        const QString gogPath = QDir(root).filePath(QStringLiteral("gog_store/installed.json"));
        QFile gogFile(gogPath);
        if (gogFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(gogFile.readAll());
            QJsonArray gamesArray;
            if (doc.isObject() && doc.object().contains(QStringLiteral("installed")))
                gamesArray = doc.object().value(QStringLiteral("installed")).toArray();
            else if (doc.isArray())
                gamesArray = doc.array();

            for (const QJsonValue &val : gamesArray) {
                if (!val.isObject())
                    continue;
                const QJsonObject obj = val.toObject();
                const QString title = obj.value(QStringLiteral("title")).toString();
                const QString installPath = obj.value(QStringLiteral("install_path")).toString();
                QString exe = obj.value(QStringLiteral("executable")).toString();
                if (title.isEmpty() || installPath.isEmpty())
                    continue;
                if (!QFileInfo(exe).isAbsolute())
                    exe = QDir(installPath).filePath(exe);

                Game game = Game::create();
                game.name = title;
                game.source = QStringLiteral("GOG");
                game.gameDirectory = installPath;
                game.executable = exe;
                game.workingDirectory = installPath;
                const bool isWindows = exe.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
                game.platform = isWindows ? Platform::Windows : Platform::Linux;
                game.runner = isWindows ? RunnerType::Wine : RunnerType::Native;

                const QString appName = obj.value(QStringLiteral("appName")).toString();
                const QStringList coverCandidates = {
                    QDir(root).filePath(QStringLiteral("icons/%1.jpg").arg(title)),
                    QDir(root).filePath(QStringLiteral("icons/%1.png").arg(title)),
                    QDir(root).filePath(QStringLiteral("icons/%1.jpg").arg(appName)),
                    QDir(root).filePath(QStringLiteral("icons/%1.png").arg(appName))
                };
                for (const QString &cPath : coverCandidates) {
                    if (QFile::exists(cPath)) {
                        game.coverPath = cPath;
                        break;
                    }
                }

                ImportedGameCandidate candidate;
                candidate.game = game;
                candidate.sourceLauncher = QStringLiteral("Heroic");
                candidate.originalId = obj.value(QStringLiteral("appName")).toString();
                results.append(candidate);
            }
        }

        // 3. Sideloaded apps
        const QString sideloadDir = QDir(root).filePath(QStringLiteral("sideload_apps"));
        if (QDir(sideloadDir).exists()) {
            const QFileInfoList configs = QDir(sideloadDir).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
            for (const QFileInfo &cfile : configs) {
                QFile sFile(cfile.absoluteFilePath());
                if (!sFile.open(QIODevice::ReadOnly))
                    continue;
                const QJsonDocument doc = QJsonDocument::fromJson(sFile.readAll());
                if (!doc.isObject())
                    continue;
                const QJsonObject obj = doc.object();
                const QString title = obj.value(QStringLiteral("title")).toString();
                const QString targetExe = obj.value(QStringLiteral("targetExe")).toString();
                if (title.isEmpty() || targetExe.isEmpty())
                    continue;

                Game game = Game::create();
                game.name = title;
                game.source = QStringLiteral("Heroic");
                game.executable = targetExe;
                game.gameDirectory = QFileInfo(targetExe).absolutePath();
                game.workingDirectory = game.gameDirectory;
                game.winePrefix = obj.value(QStringLiteral("winePrefix")).toString();
                const bool isWindows = targetExe.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
                game.platform = isWindows ? Platform::Windows : Platform::Linux;
                game.runner = isWindows ? RunnerType::Wine : RunnerType::Native;

                const QStringList coverCandidates = {
                    QDir(root).filePath(QStringLiteral("icons/%1.jpg").arg(title)),
                    QDir(root).filePath(QStringLiteral("icons/%1.png").arg(title)),
                    QDir(root).filePath(QStringLiteral("icons/%1.jpg").arg(cfile.baseName())),
                    QDir(root).filePath(QStringLiteral("icons/%1.png").arg(cfile.baseName()))
                };
                for (const QString &cPath : coverCandidates) {
                    if (QFile::exists(cPath)) {
                        game.coverPath = cPath;
                        break;
                    }
                }

                ImportedGameCandidate candidate;
                candidate.game = game;
                candidate.sourceLauncher = QStringLiteral("Heroic");
                candidate.originalId = cfile.baseName();
                results.append(candidate);
            }
        }
    }
    return results;
}
