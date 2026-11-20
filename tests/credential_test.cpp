#include "ai_sites.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("Moxu");
    app.setApplicationName("Moxu");
    const auto args = app.arguments();
    const bool reopened = args.size() == 2;
    const auto uuid = reopened ? QUuid(args.at(1)) : QUuid::createUuid();
    if (uuid.isNull()) return 1;
    const AiSite site{"credential-test-" + uuid.toString(QUuid::WithoutBraces), "测试站点",
                      "https://example.invalid/api/anthropic", {"test"}, "messages-bearer"};
    const QString fixture = QStringLiteral("synthetic-credential-test");
    const auto releasedTarget = site.credentialId();
    const auto legacyTarget = "Moxu/AI/" + site.id + "/" +
        QString::fromLatin1(QCryptographicHash::hash(site.endpoint().toEncoded(), QCryptographicHash::Sha256).toHex());
    if (releasedTarget != legacyTarget) return 2;
    if (reopened)
    {
        app.setApplicationVersion("2");
        return storedAiKey(site.credentialId()) == fixture ? 0 : 3;
    }
    app.setApplicationVersion("1");
    QTemporaryDir dir;
    if (!dir.isValid()) return 4;
    // Only the randomly named synthetic site is accessed, including in the production namespace.
    int result = 0;
    try
    {
        storeAiKey(releasedTarget, fixture);
        app.setOrganizationName("MoxuTests");
        app.setApplicationName("Writing");
        const auto testTarget = site.credentialId();
        if (testTarget == releasedTarget || !storedAiKey(testTarget).isEmpty()) result = 5;
        else
        {
            storeAiKey(testTarget, "mock-site-key");
            storeAiKey(testTarget, {}); // Mirrors removing the old target after changing the test site address.
            QProcess fresh;
            fresh.setWorkingDirectory(dir.path());
            fresh.start(app.applicationFilePath(), {uuid.toString()});
            if (!fresh.waitForFinished(15000) || fresh.exitStatus() != QProcess::NormalExit || fresh.exitCode())
                result = 6;
        }
        storeAiKey(testTarget, {});
        storeAiKey(releasedTarget, {});
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 7;
    }
    return result;
}
