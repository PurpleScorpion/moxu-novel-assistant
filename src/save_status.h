#pragma once
#include <QtWidgets>

class SaveStatus : public QLabel
{
    QGraphicsOpacityEffect *ink = new QGraphicsOpacityEffect(this);
    QPropertyAnimation pulse{ink, "opacity"};

  public:
    QTimer pending;

    SaveStatus()
    {
        setTextFormat(Qt::PlainText);
        setWordWrap(true);
        setObjectName("saveStatus");
        pending.setSingleShot(true);
        pending.setInterval(1800);
        setGraphicsEffect(ink);
        ink->setOpacity(1);
        pulse.setDuration(300);
        pulse.setStartValue(0.25);
        pulse.setEndValue(1.0);
    }
    void edited()
    {
        if (!pending.isActive())
        {
            setText("等待自动保存…");
            pending.start();
        }
    }
    void confirm(const QString &subject)
    {
        setText("✓ " + subject + "已保存  ·  " + QTime::currentTime().toString("HH:mm:ss"));
        pulse.stop();
        pulse.start();
    }
};
