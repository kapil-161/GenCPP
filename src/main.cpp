#include <QStyleFactory>
#include <QStyle>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "MainWindow.h"
#include "Config.h"
#include "SingleInstanceApp.h"
#include "CommandLineHandler.h"
#include "SpeGraphBuilder.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // --spe-render <file.SPE> <line> <out.png>: draw one SPE line's graph to an
    // image (checks the graphs without the GUI). Needs a QApplication for widgets.
    if (argc == 5 && QString(argv[1]) == "--spe-render") {
        QApplication app(argc, argv);
        QFile f(QString::fromLocal8Bit(argv[2]));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 1;
        const QStringList lines = QString::fromLatin1(f.readAll()).split('\n');
        SpeGraphData data;
        if (!SpeGraphBuilder::build(lines, QString(argv[3]).toInt() - 1, data)) return 2;
        SpeGraphWidget w;
        w.resize(640, 380);
        w.setData(data);
        return w.grab().save(QString::fromLocal8Bit(argv[4])) ? 0 : 3;
    }

    // Handle CLI modes (--test, --glue) before creating GUI
    {
        QCoreApplication cliApp(argc, argv);
        CommandLineHandler handler;
        int code = handler.run(cliApp.arguments());
        if (code >= 0) return code; // CLI mode handled — exit without GUI
    }

    // Hide console window when launched as GUI (no CLI flags)
#ifdef Q_OS_WIN
    FreeConsole();
#endif

    SingleInstanceApp app(argc, argv);

    if (!app.isFirstInstance()) {
        app.showAlreadyRunningMessage();
        return 0;
    }

    QCoreApplication::setApplicationName(Config::APP_NAME);
    QCoreApplication::setApplicationVersion(Config::APP_VERSION);
    QCoreApplication::setOrganizationName(Config::ORG_NAME);

    // Fusion style for a consistent cross-platform look
    app.setStyle(QStyleFactory::create("Fusion"));
    // Fusion follows Windows dark mode; the UI is styled light, so pin the
    // light palette or unstyled parts (tooltips, menus) render dark.
    app.setPalette(app.style()->standardPalette());

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
