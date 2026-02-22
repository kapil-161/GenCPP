#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "MainWindow.h"
#include "Config.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(Config::APP_NAME);
    QCoreApplication::setApplicationVersion(Config::APP_VERSION);
    QCoreApplication::setOrganizationName(Config::ORG_NAME);

    // Fusion style for a consistent cross-platform look
    app.setStyle(QStyleFactory::create("Fusion"));

    // Application icon
    QString iconPath = QDir::currentPath() + "/resources/final.ico";
    if (QFile::exists(iconPath))
        app.setWindowIcon(QIcon(iconPath));

    try {
        MainWindow window;
        window.show();
        return app.exec();
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, "Fatal Error",
            QString("A fatal error occurred:\n\n%1").arg(e.what()));
        return 1;
    } catch (...) {
        QMessageBox::critical(nullptr, "Fatal Error",
            "An unknown fatal error occurred.");
        return 1;
    }
}
