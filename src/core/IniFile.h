#pragma once

#include <QString>
#include <QStringList>

class IniFile
{
public:
    bool load(const QString &path, QString *error = nullptr);
    bool save(QString *error = nullptr);
    bool saveAs(const QString &path, QString *error = nullptr);

    QStringList sections() const;
    bool hasSection(const QString &name) const;
    bool contains(const QString &section, const QString &key) const;
    QString value(const QString &section, const QString &key, bool *found = nullptr) const;
    bool setValue(const QString &section, const QString &key, const QString &value);
    bool ensureSection(const QString &name);
    bool changed() const { return m_changed; }
    QString path() const { return m_path; }

private:
    struct SectionInfo {
        QString name;
        int headerLine = -1;
        int firstLine = -1;
        int endLine = -1;
    };

    QList<SectionInfo> parsedSections() const;
    SectionInfo sectionInfo(const QString &name) const;
    static QString trimmedSectionName(const QString &line);
    static int keyLine(const QString &line, const QString &key, int *equalPos, int *valueStart);
    static QString preserveCommentSuffix(const QString &line, int valueStart);
    static QString detectNewline(const QByteArray &data);

    QString m_path;
    QStringList m_lines;
    QString m_newline = QStringLiteral("\n");
    bool m_finalNewline = true;
    bool m_utf8Bom = false;
    // A legacy INI (typically Latin-1/CP1252) is decoded and written back in its
    // own single-byte encoding. Re-encoding it as UTF-8 would replace every
    // non-ASCII character with U+FFFD, and the file is not ours to change.
    bool m_latin1 = false;
    bool m_changed = false;
};
