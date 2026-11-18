#pragma once
#include <QtWidgets>

class DownwardCombo : public QComboBox
{
  public:
    DownwardCombo()
    {
        setMaxVisibleItems(12);
    }
    void showPopup() override
    {
        if (!count())
            return;
        auto anchor = [this] { return mapToGlobal(QPoint(0, height())); };
        auto *display = QGuiApplication::screenAt(mapToGlobal(rect().center()));
        if (!display)
            display = screen();
        const auto bounds = display->availableGeometry();
        const int rowHeight = qMax(fontMetrics().height() + 4, view()->sizeHintForRow(0));
        const int minimumRoom = rowHeight + 4;
        auto room = [&] { return bounds.bottom() + 1 - anchor().y(); };
        // A field clipped by a scroll pane can be revealed without moving the workspace.
        if (room() < minimumRoom)
        {
            for (QWidget *p = parentWidget(); p && room() < minimumRoom; p = p->parentWidget())
                if (auto *scroll = qobject_cast<QAbstractScrollArea *>(p))
                {
                    auto *bar = scroll->verticalScrollBar();
                    bar->setValue(bar->value() + minimumRoom - room());
                }
            // A floating window may itself have been dragged partly off the desktop.
            auto *host = window();
            if (room() < minimumRoom && !host->isMaximized() && !host->isFullScreen())
                host->move(host->x(), host->y() - (minimumRoom - room()));
        }
        const int limit = maxVisibleItems();
        const int rows = qBound(1, (room() - 4) / rowHeight, limit);
        setMaxVisibleItems(rows);
        QComboBox::showPopup();
        setMaxVisibleItems(limit);
        auto *popup = view()->window();
        if (!popup->isVisible() || popup == window())
            return;
        const int width = qMin(popup->width(), bounds.width());
        const int height = qBound(1, popup->height(), qMax(1, room()));
        const auto point = anchor();
        const int x = qBound(bounds.left(), point.x(), bounds.right() - width + 1);
        popup->setGeometry(x, point.y(), width, height);
        view()->scrollTo(view()->currentIndex(), QAbstractItemView::EnsureVisible);
    }
};
