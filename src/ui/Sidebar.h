#pragma once

#include "Game.h"

#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class ThemeManager;
class SidebarItem;

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(ThemeManager *theme, QWidget *parent = nullptr);

    void setGames(const QList<Game> &games);
    void setCurrentItem(const QString &id);
    void setCollapsedGroups(const QJsonObject &collapsed);
    void refreshTheme();

    QString currentItem() const { return m_currentId; }
    QString labelFor(const QString &id) const;
    QWidget *targetFor(const QString &id) const;

signals:
    void filterSelected(const QString &id);
    void collapsedGroupsChanged(const QJsonObject &collapsed);
    void settingsRequested();
    void tutorialRequested();
    void aboutRequested();

private:
    void rebuild();
    void setGroupCollapsed(const QString &groupId, bool collapsed);
    void updateGroupHeaders();
    void addGroup(const QString &id, const QString &title);
    void addItem(const QString &groupId, const QString &id, const QString &label,
                 const QIcon &icon, const QIcon &selectedIcon, int count);
    QJsonObject collapsedObject() const;

    ThemeManager *m_theme = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_groupsLayout = nullptr;
    QLabel *m_versionLabel = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QPushButton *m_tutorialButton = nullptr;
    QPushButton *m_aboutButton = nullptr;

    QList<Game> m_games;
    QString m_currentId = QStringLiteral("library.games");
    QHash<QString, bool> m_collapsed;
    QHash<QString, QWidget *> m_targets;
    QHash<QString, QString> m_labels;
    QHash<QString, QToolButton *> m_groupHeaders;
    QHash<QString, QList<QWidget *>> m_groupRows;
    QList<SidebarItem *> m_items;
    QStringList m_groupOrder;
    bool m_rebuilding = false;
};
