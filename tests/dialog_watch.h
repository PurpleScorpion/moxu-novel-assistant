#pragma once
#include <QtWidgets>

class DialogWatch : public QObject
{
  public:
    QStringList shown;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
            if (auto *dialog = qobject_cast<QDialog *>(object))
            {
                shown.append(dialog->windowTitle());
                QTimer::singleShot(0, dialog, &QDialog::reject);
            }
        return false;
    }
};
template <class T> T *visibleControl(QWidget &root, const QString &name = {})
{
    QApplication::processEvents();
    for (auto *widget : root.findChildren<T *>(name))
        if (widget->isVisible())
            return widget;
    throw std::runtime_error(("未显示控件 " + name).toStdString());
}
