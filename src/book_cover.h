#pragma once
#include <QtWidgets>

class BookCover : public QPushButton
{
    QString title;
    QColor cloth;
    QPixmap artwork;
    QToolButton *more = new QToolButton(this);
    int captionHeight() const
    {
        return 2 * (fontMetrics().height() + 2) + 6;
    }

    static void drawTitle(QPainter &p, const QString &title, const QRectF &area, int rows)
    {
        QTextLayout lines(title, p.font());
        QTextOption options;
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        lines.setTextOption(options);
        lines.beginLayout();
        qreal y = area.top();
        for (int row = 0; row < rows; ++row)
        {
            auto line = lines.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(area.width());
            if (row == rows - 1 && line.textStart() + line.textLength() < title.size())
            {
                p.drawText(QPointF(area.x(), y + line.ascent()),
                           p.fontMetrics().elidedText(title.mid(line.textStart()), Qt::ElideRight, int(area.width())));
                break;
            }
            line.draw(&p, QPointF(area.x(), y));
            y += line.height() + 2;
        }
        lines.endLayout();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(font());
        p.setPen(QColor("#e0f1e6"));
        const QRectF cover = QRectF(rect()).adjusted(4, 3, -7, -8 - captionHeight());
        drawTitle(p, title, QRectF(cover.left(), cover.bottom() + 10, cover.width(), captionHeight()), 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#899886"));
        p.drawRoundedRect(cover.translated(2, 4), 4, 4);
        p.setBrush(QColor("#ced0b6"));
        p.drawRoundedRect(cover.translated(1, 2), 4, 4);
        QLinearGradient clothLight(cover.topLeft(), cover.bottomRight());
        clothLight.setColorAt(0, cloth.lighter(135));
        clothLight.setColorAt(1, cloth.darker(135));
        p.setBrush(clothLight);
        p.setPen(underMouse() || hasFocus() ? QColor("#b7dec5") : cloth.lighter(145));
        p.drawRoundedRect(cover, 4, 4);
        if (!artwork.isNull())
        {
            p.save();
            QPainterPath outline;
            outline.addRoundedRect(cover, 4, 4);
            p.setClipPath(outline);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            QSizeF crop = cover.size();
            crop.scale(artwork.size(), Qt::KeepAspectRatio);
            QRectF source(QPointF(), crop);
            source.moveCenter(QRectF(artwork.rect()).center());
            p.drawPixmap(cover, artwork, source);
            p.restore();
        }
        p.setPen(Qt::NoPen);
        p.fillRect(QRectF(cover.x() + 7, cover.y() + 1, 3, cover.height() - 2), QColor(0, 0, 0, 65));
        p.fillRect(QRectF(cover.x() + 10, cover.y() + 1, 1, cover.height() - 2), QColor(255, 255, 255, 42));
        if (!artwork.isNull())
            return;
        p.setClipRect(cover.adjusted(12, 1, -1, -1));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(207, 218, 172, 47), 1));
        for (int radius = 28; radius <= 124; radius += 12)
            p.drawEllipse(cover.bottomRight() + QPointF(-12, 24), radius, radius);
        const QRectF area = cover.adjusted(23, 45, -15, -27);
        QFont face("Microsoft YaHei UI", 14, QFont::DemiBold);
        p.setFont(face);
        p.setPen(QColor("#f0edda"));
        drawTitle(p, title, area, 3);
        p.setPen(QPen(QColor("#c4c4a2"), 2));
        p.drawLine(area.topLeft() - QPointF(0, 10), area.topLeft() + QPointF(22, -10));
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        more->move(width() - 43, 9);
    }

  public:
    QSize sizeHint() const override
    {
        return QSize(161, 209 + captionHeight());
    }
    BookCover(const QString &name, const QString &path) : title(name)
    {
        static const QColor colors[] = {QColor("#315344"), QColor("#364957"), QColor("#64513f"), QColor("#514454")};
        cloth = colors[qChecksum(path.toUtf8()) % 4];
        setAccessibleName(name);
        setToolTip(name + "\n" + QDir::toNativeSeparators(path));
        setCursor(Qt::PointingHandCursor);
        more->setText("⋯");
        more->setAccessibleName(name + "的更多操作");
        more->setToolTip("更多操作");
        more->setFixedSize(29, 26);
        more->setStyleSheet("QToolButton "
                            "{color:#eeeeda;background:rgba(0,0,0,38);border:none;border-radius:4px;padding:0;font-"
                            "size:18px;} QToolButton:hover {background:#264536;}");
    }
    QMenu *actionsMenu()
    {
        auto *menu = new QMenu(more);
        connect(more, &QToolButton::clicked, menu,
                [this, menu] { menu->popup(more->mapToGlobal(QPoint(0, more->height()))); });
        return menu;
    }
    void setArtwork(const QPixmap &pixmap)
    {
        artwork = pixmap;
        update();
    }
    const QPixmap &coverImage() const
    {
        return artwork;
    }
    void useAsPreview()
    {
        more->hide();
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
};
