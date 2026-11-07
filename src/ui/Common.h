#pragma once

#include <QDialog>
#include <QLayout>
#include <QStyle>
#include <QLabel>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAbstractButton;
class QBoxLayout;
class QFrame;
class QMouseEvent;
class QPushButton;
class QVBoxLayout;
class ThemeManager;

class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int margin = 0, int horizontalSpacing = 16, int verticalSpacing = 16);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    Qt::Orientations expandingDirections() const override;
    void setGeometry(const QRect &rect) override;

    int horizontalSpacing() const;
    int verticalSpacing() const;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> m_items;
    int m_horizontalSpacing;
    int m_verticalSpacing;
};

class VantaDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VantaDialog(const QString &title, ThemeManager *theme, QWidget *parent = nullptr);
    ~VantaDialog() override;

    void setDialogTitle(const QString &title);
    void applyPalette();

protected:
    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    QWidget *headerWidget() const { return m_header; }
    ThemeManager *themeManager() const { return m_theme; }
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ThemeManager *m_theme = nullptr;
    QFrame *m_frame = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_titleLabel = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

namespace UiHelp
{
QLabel *helpLabel(const QString &text, QWidget *parent = nullptr);
QLabel *sectionLabel(const QString &text, QWidget *parent = nullptr);
QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr);
QFrame *separator(QWidget *parent = nullptr);
void makePrimary(QAbstractButton *button);
void makeDanger(QAbstractButton *button);
void makeFlat(QAbstractButton *button);
bool validateDllOverrides(const QString &text, QString *message = nullptr);
bool parseEnvironment(const QString &text, QMap<QString, QString> *environment, QString *message = nullptr);
QString environmentToText(const QMap<QString, QString> &environment);
QString dataSize(qint64 bytes);
void openContainingDirectory(const QString &path);
}
