#include "window.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Moxu");
    app.setOrganizationName("Moxu");
    app.setFont(QFont("Microsoft YaHei", 10));
    Window window;
    window.show();
    if (app.arguments().contains("--startup-check"))
    {
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
