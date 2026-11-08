#pragma once

#include "Game.h"

#include <QList>
#include <QString>

class GameLibrary;

struct ImportedGameCandidate
{
    Game game;
    QString sourceLauncher; // "Lutris" or "Heroic"
    QString originalId;
    bool alreadyInLibrary = false;
};

struct ImportSummary
{
    int lutrisFound = 0;
    int heroicFound = 0;
    int totalImported = 0;
    int totalSkipped = 0;
};

class LibraryImporter
{
public:
    static QList<ImportedGameCandidate> scanAll(const GameLibrary *existingLibrary = nullptr);
    static QList<ImportedGameCandidate> scanLutris();
    static QList<ImportedGameCandidate> scanHeroic();

    static bool isGameInLibrary(const Game &candidate, const GameLibrary *library);
};
