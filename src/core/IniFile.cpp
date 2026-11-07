#include "IniFile.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>

QString IniFile::detectNewline(const QByteArray &data)
{
    const int lf = data.indexOf('\n');
    if (lf > 0 && data.at(lf - 1) == '\r')
        return QStringLiteral("\r\n");
    return QStringLiteral("\n");
}

bool IniFile::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return false;
    }

    m_path = path;
    m_newline = detectNewline(data);
    m_finalNewline = data.endsWith('\n');
    m_utf8Bom = data.startsWith("\xEF\xBB\xBF");
    const QByteArray content = m_utf8Bom ? data.mid(3) : data;
    QString text = QString::fromUtf8(content);
    if (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('\r')))
        text.chop(1);
    m_lines = text.split(QLatin1Char('\n'));
    for (QString &line : m_lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
    }
    if (m_lines.size() == 1 && m_lines.first().isEmpty())
        m_lines.clear();
    m_changed = false;
    return true;
}

QString IniFile::trimmedSectionName(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.size() < 3 || !trimmed.startsWith(QLatin1Char('[')))
        return QString();
    const int close = trimmed.indexOf(QLatin1Char(']'));
    if (close <= 1)
        return QString();
    return trimmed.mid(1, close - 1).trimmed();
}

QList<IniFile::SectionInfo> IniFile::parsedSections() const
{
    QList<SectionInfo> result;
    SectionInfo current;
    for (int index = 0; index < m_lines.size(); ++index) {
        const QString name = trimmedSectionName(m_lines.at(index));
        if (name.isEmpty())
            continue;
        if (current.headerLine >= 0) {
            current.endLine = index - 1;
            result.append(current);
        }
        current = SectionInfo{name, index, index + 1, static_cast<int>(m_lines.size()) - 1};
    }
    if (current.headerLine >= 0) {
        current.endLine = m_lines.size() - 1;
        result.append(current);
    }
    return result;
}

QStringList IniFile::sections() const
{
    QStringList result;
    for (const SectionInfo &section : parsedSections())
        result.append(section.name);
    return result;
}

IniFile::SectionInfo IniFile::sectionInfo(const QString &name) const
{
    for (const SectionInfo &section : parsedSections()) {
        if (section.name.compare(name, Qt::CaseInsensitive) == 0)
            return section;
    }
    return {};
}

bool IniFile::hasSection(const QString &name) const
{
    return sectionInfo(name).headerLine >= 0;
}

int IniFile::keyLine(const QString &line, const QString &key, int *equalPos, int *valueStart)
{
    int index = 0;
    while (index < line.size() && line.at(index).isSpace())
        ++index;
    if (index >= line.size() || line.at(index) == QLatin1Char(';') || line.at(index) == QLatin1Char('#'))
        return -1;
    const int equal = line.indexOf(QLatin1Char('='), index);
    if (equal < 0)
        return -1;
    const QString candidate = line.mid(index, equal - index).trimmed();
    if (candidate.compare(key, Qt::CaseInsensitive) != 0)
        return -1;
    if (equalPos)
        *equalPos = equal;
    if (valueStart)
        *valueStart = equal + 1;
    return index;
}

QString IniFile::preserveCommentSuffix(const QString &line, int valueStart)
{
    int comment = -1;
    for (int index = valueStart; index < line.size(); ++index) {
        const QChar chr = line.at(index);
        if ((chr == QLatin1Char(';') || chr == QLatin1Char('#'))
            && (index == valueStart || line.at(index - 1).isSpace())) {
            comment = index;
            break;
        }
    }
    if (comment < 0)
        return QString();
    return line.mid(comment);
}

bool IniFile::contains(const QString &section, const QString &key) const
{
    bool found = false;
    value(section, key, &found);
    return found;
}

QString IniFile::value(const QString &section, const QString &key, bool *found) const
{
    if (found)
        *found = false;
    const SectionInfo info = sectionInfo(section);
    if (info.headerLine < 0)
        return QString();
    for (int index = info.firstLine; index <= info.endLine; ++index) {
        int valueStart = 0;
        if (keyLine(m_lines.at(index), key, nullptr, &valueStart) < 0)
            continue;
        const QString line = m_lines.at(index);
        QString raw = line.mid(valueStart);
        const int comment = raw.indexOf(QRegularExpression(QStringLiteral("\\s[;#]")));
        if (comment >= 0)
            raw = raw.left(comment);
        if (found)
            *found = true;
        return raw.trimmed();
    }
    return QString();
}

bool IniFile::setValue(const QString &sectionName, const QString &key, const QString &value)
{
    SectionInfo section = sectionInfo(sectionName);
    if (section.headerLine < 0) {
        if (!ensureSection(sectionName))
            return false;
        section = sectionInfo(sectionName);
        if (section.headerLine < 0)
            return false;
    }

    for (int index = section.firstLine; index <= section.endLine; ++index) {
        int equal = 0;
        int valueStart = 0;
        if (keyLine(m_lines.at(index), key, &equal, &valueStart) < 0)
            continue;
        const QString oldLine = m_lines.at(index);
        int spacing = equal + 1;
        while (spacing < oldLine.size() && oldLine.at(spacing).isSpace())
            ++spacing;
        const QString prefix = oldLine.left(spacing);
        const QString suffix = preserveCommentSuffix(oldLine, valueStart);
        QString separator = suffix.isEmpty() ? QString() : QStringLiteral(" ");
        const QString newLine = prefix + value + separator + suffix;
        if (newLine == oldLine)
            return false;
        m_lines[index] = newLine;
        m_changed = true;
        return true;
    }

    const QString newLine = QStringLiteral("%1=%2").arg(key, value);
    const int insertAt = section.endLine + 1;
    m_lines.insert(insertAt, newLine);
    m_changed = true;
    return true;
}

bool IniFile::ensureSection(const QString &name)
{
    if (hasSection(name))
        return false;
    if (!m_lines.isEmpty() && !m_lines.last().trimmed().isEmpty())
        m_lines.append(QString());
    m_lines.append(QStringLiteral("[%1]").arg(name));
    m_changed = true;
    return true;
}

bool IniFile::save(QString *error)
{
    return saveAs(m_path, error);
}

bool IniFile::saveAs(const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    QByteArray data;
    if (m_utf8Bom)
        data.append("\xEF\xBB\xBF");
    data.append(m_lines.join(m_newline).toUtf8());
    if (m_finalNewline && !m_lines.isEmpty())
        data.append(m_newline.toUtf8());
    if (file.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Could not finish writing %1: %2").arg(path, file.errorString());
        return false;
    }
    m_changed = false;
    m_path = path;
    return true;
}
