#include "window.h"

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QTranslator chinese;
    if (chinese.load(":/lang/qtbase_zh_CN.qm"))
        app.installTranslator(&chinese);
    applyTheme(app);
    app.setApplicationName("Moxu");
    app.setOrganizationName("Moxu");
    Window window;
    window.show();
    if (app.arguments().contains("--startup-check"))
    {
        if (!QSslSocket::supportsSsl())
            return 7;
        QDialogButtonBox controls(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        if (!controls.button(QDialogButtonBox::Cancel)->text().contains("取消"))
            return 6;
        QTemporaryDir temporary;
        try
        {
            Book probe(temporary.filePath("probe.moxu"), "启动验证");
            probe.atomic([&] { probe.save(1, "正文保存验证"); });
            if (probe.value("SELECT body FROM chapters WHERE id=1").toString() != "正文保存验证")
                return 5;
        }
        catch (const std::exception &)
        {
            return 5;
        }
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
