#pragma once
#include <QtWidgets>

class MoxuStyle : public QProxyStyle
{
  public:
    MoxuStyle() : QProxyStyle(QStyleFactory::create("Fusion"))
    {
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight)
            return 18;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override
    {
        if (element != PE_IndicatorCheckBox && element != PE_IndicatorItemViewItemCheck)
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool marked = option->state & (State_On | State_NoChange);
        const QColor edge = !enabled                                             ? QColor("#779084")
                            : option->state & (State_MouseOver | State_HasFocus) ? QColor("#e1ffea")
                                                                                 : QColor("#b1d3be");
        const QColor fill = marked ? (enabled ? QColor("#9ee5af") : QColor("#91a499")) : QColor("#14251d");
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin(option->rect.width(), option->rect.height());
        painter->translate(QRectF(option->rect).center() - QPointF(side / 2, side / 2));
        painter->scale(side / 18, side / 18);
        painter->setPen(QPen(edge, 1.6));
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(1, 1, 16, 16), 3, 3);
        if (marked)
        {
            painter->setPen(QPen(QColor("#102219"), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (option->state.testFlag(State_NoChange))
                painter->drawLine(QPointF(5, 9), QPointF(13, 9));
            else
                painter->drawPolyline(QPolygonF{QPointF(4.5, 9), QPointF(7.6, 12.1), QPointF(13.6, 5.8)});
        }
        painter->restore();
    }
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *result) const override
    {
        if (hint == SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, result);
    }
};

inline void applyTheme(QApplication &app)
{
    app.setStyle(new MoxuStyle);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#0b1511"));
    palette.setColor(QPalette::WindowText, QColor("#d9eae0"));
    palette.setColor(QPalette::Text, QColor("#d9eae0"));
    palette.setColor(QPalette::PlaceholderText, QColor("#9baea1"));
    palette.setColor(QPalette::Base, QColor("#0d1c16"));
    palette.setColor(QPalette::AlternateBase, QColor("#13271f"));
    palette.setColor(QPalette::Button, QColor("#172c23"));
    palette.setColor(QPalette::ButtonText, QColor("#d9eae0"));
    palette.setColor(QPalette::Highlight, QColor("#a4f2aa"));
    palette.setColor(QPalette::HighlightedText, QColor("#102817"));
    app.setPalette(palette);
    app.setFont(QFont("Microsoft YaHei UI", 11));
    app.setStyleSheet(R"(
    QWidget { selection-background-color:#a4f2aa; selection-color:#092014; }
    QLabel#title {font-size:26px;font-weight:700;color:#dbf9df;}
    QLabel#sectionTitle {font-size:17px;font-weight:600;color:#abefbc;}
    QLabel#hint {color:#a0bcac;}
    QFrame#rail {background:#102019;border:1px solid #2b4737;border-radius:8px;}
    QFrame#contentPanel,QWidget#actionPage,QWidget#sheetContent {background:#12231b;border:1px solid #324b3c;border-radius:8px;}
    QListWidget#chapterList,QListWidget#siteList {background:#102019;border:none;padding:0;}
    QLabel#actionError,QLabel#sheetMessage {color:#efce9e;}
    QPlainTextEdit#manuscript {background:#0c1913;border:1px solid #34513e;border-radius:6px;}
    QPushButton#saveChapter {background:#a3eeb1;color:#102417;font-weight:600;}
    QPushButton,QToolButton {background:#162a21;border:1px solid #365443;border-radius:4px;padding:7px 12px;color:#dfefe4;}
    QPushButton:hover,QToolButton:hover {background:#234634;border-color:#68bd83;}
    QPushButton#primary {background:#a3eeb1;color:#102417;border-color:#a3eeb1;font-weight:600;}
    QPushButton:disabled,QPushButton#primary:disabled {background:#122019;color:#778d7e;border-color:#2a3d31;}
    QListWidget,QPlainTextEdit,QTextEdit,QLineEdit,QComboBox,QSpinBox,QTableWidget {background:#101f19;border:1px solid #345140;border-radius:4px;padding:6px;}
    QListWidget::item {background:transparent;border:1px solid transparent;margin:0;padding:7px;}
    QListWidget::item:selected {background:#254834;color:#bcffca;}
    QListWidget#navigation {border:none;background:transparent;padding:0;}
    QToolBar {spacing:3px;background:#102019;padding:7px 12px;border-bottom:1px solid #294536;}
    QToolBar QToolButton {border:none;padding:6px 9px;background:transparent;}
    QToolBar QToolButton:hover {background:#234634;}
    QToolBar QToolButton:checked {background:#284b37;color:#c5f9ce;}
    QToolBar::separator {width:1px;background:#345140;margin:4px 5px;}
    QScrollArea#writerSheet,QScrollArea#formScroll {border:none;background:transparent;}
    QPlainTextEdit:focus,QLineEdit:focus {border-color:#80be92;}
    QMenu {padding:4px;background:#112019;border:1px solid #48634f;}
    QMenu::item {padding:7px 18px;} QMenu::item:selected {background:#284f36;}
    QHeaderView::section {background:#193324;color:#c7e9ce;padding:8px;border:none;}
    QTreeWidget {background:#101f19;border:1px solid #345140;padding:6px;}
    QTreeWidget::item {background:transparent;border:1px solid transparent;margin:0;padding:4px;}
    QTreeWidget::item:selected {background:#254834;color:#bcffca;}
    QTabWidget::pane {border:1px solid #345140;}
    QTabBar::tab {background:#13271f;color:#b3c9bc;padding:8px 15px;}
    QTabBar::tab:selected {background:#244634;color:#c5f9ce;}
    QStatusBar {color:#a7cfb3;}
    )");
}

inline QVBoxLayout *panelColumn(QFrame *panel, bool rail)
{
    panel->setObjectName(rail ? "rail" : "contentPanel");
    auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);
    return column;
}

inline QVBoxLayout *panelColumn(QBoxLayout *parent, int stretch, bool rail = false)
{
    auto *panel = new QFrame;
    parent->addWidget(panel, stretch);
    return panelColumn(panel, rail);
}

inline QLabel *sectionLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionTitle");
    return label;
}

class MatrixBanner : public QWidget
{
    QString title;

  public:
    explicit MatrixBanner(QString text) : title(std::move(text))
    {
        setFixedHeight(62);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#102019"));
        p.setFont(QFont("Microsoft YaHei UI", 10));
        const QString glyphs = "墨序书卷山河风月星辰春秋诗文云海";
        const int step = p.fontMetrics().height() + 2;
        for (int x = width() / 2; x < width(); x += step)
            for (int y = step; y < height() + step; y += step)
            {
                int seed = (x * 3 + y * 13 + x * y / 11) % 91;
                if (seed < 27)
                    continue;
                p.setPen(QColor(68, 222, 111, seed + 15));
                p.drawText(x, y, glyphs.mid(seed % glyphs.size(), 1));
            }
        p.fillRect(0, 0, 3, height(), QColor("#9aefaa"));
        p.setPen(QColor("#baf6c5"));
        p.setFont(QFont("Microsoft YaHei UI", 18, QFont::Bold));
        QRect textRect = rect().adjusted(14, 4, -8, -4);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(title, Qt::ElideRight, textRect.width()));
    }
};

inline QString defaultBooksPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("书籍");
}
