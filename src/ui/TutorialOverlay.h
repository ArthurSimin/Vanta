#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class ThemeManager;

class TutorialOverlay : public QWidget
{
    Q_OBJECT

public:
    struct Step
    {
        QPointer<QWidget> target;
        QString title;
        QString body;
        bool showLogo = false;
        QString targetHint;
    };

    TutorialOverlay(ThemeManager *theme, QWidget *parent = nullptr);

    void setSteps(const QList<Step> &steps);
    void start(int index = 0);
    void finish();
    void retranslate();
    int currentStep() const { return m_index; }

signals:
    void finished();
    void skipped();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void showStep(int index);
    void positionBubble();
    QRect targetRect() const;
    void refreshIcons();

    ThemeManager *m_theme = nullptr;
    QList<Step> m_steps;
    int m_index = 0;
    bool m_active = false;

    QFrame *m_bubble = nullptr;
    QLabel *m_logo = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
    QLabel *m_counter = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_skipButton = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_nextButton = nullptr;
};
