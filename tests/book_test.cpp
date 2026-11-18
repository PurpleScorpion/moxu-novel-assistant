#include "book.h"
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    QString path = dir.filePath("test.moxu");
    try
    {
        {
            Book book(path, "测试");
            book.run("INSERT INTO people VALUES(1,'甲','')");
            book.run("INSERT INTO chapters(title) VALUES('第二章')");
            book.event(1, 1, "积分", 300, "奖励");
            QString text = "剩余180积分";
            book.event(2, 1, "积分", -120, "兑换", &text);
            book.event(2, 1, "积分", -80, "消费");
            try
            {
                QString wrong = "不能保存";
                book.event(1, 1, "积分", -150, "补录", &wrong);
                return 1;
            }
            catch (const std::runtime_error &)
            {
            }
            if (book.value("SELECT COUNT(*) FROM events").toInt() != 3 ||
                !book.value("SELECT body FROM chapters WHERE id=1").toString().isEmpty())
                return 2;
        }
        Book again(path);
        if (again.balance(1, "积分", 1) != 300 || again.balance(1, "积分", 2) != 100 ||
            again.value("SELECT body FROM chapters WHERE id=2").toString() != "剩余180积分")
            return 3;
        try
        {
            Book collision(path, "重名");
            return 4;
        }
        catch (const std::runtime_error &)
        {
        }
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 5;
    }
}
