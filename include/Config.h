#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QStringList>
#include <QColor>

// Debug output control
#ifdef ENABLE_DEBUG_OUTPUT
    #include <QDebug>
    #define DEBUG_OUT(x) qDebug() << x
    #define WARN_OUT(x)  qWarning() << x
#else
    #define DEBUG_OUT(x) ((void)0)
    #define WARN_OUT(x)  ((void)0)
#endif

namespace Config {

    const QString APP_NAME    = "GeneticsEditor";
    const QString APP_VERSION = "1.0.0";
    const QString ORG_NAME    = "DSSAT";

#ifdef Q_OS_WIN
    const QString DSSAT_BASE      = "C:\\DSSAT48";
    const QString DSSATPRO_FILE   = "C:\\DSSAT48\\DSSATPRO.v48";
    const QString DETAIL_CDE_FILE = "C:\\DSSAT48\\DETAIL.CDE";
#else
    const QString DSSAT_BASE      = "/Applications/DSSAT48";
    const QString DSSATPRO_FILE   = "/Applications/DSSAT48/DSSATPRO.v48";
    const QString DETAIL_CDE_FILE = "/Applications/DSSAT48/DETAIL.CDE";
#endif

    // UI colours
    const QColor SUCCESS_COLOR   = QColor("#4CAF50");
    const QColor ERROR_COLOR     = QColor("#F44336");
    const QColor WARNING_COLOR   = QColor("#FF9800");
    const QColor INFO_COLOR      = QColor("#2196F3");
    const QColor MINMAX_COLOR    = QColor("#FFF9C4");   // yellow – MINIMA/MAXIMA rows
    const QColor OOR_COLOR       = QColor("#FFCDD2");   // red    – out-of-range values
    const QColor ECO_REF_COLOR   = QColor("#E8F5E9");   // green  – eco reference columns

    // Window
    const int WIN_W     = 1200;
    const int WIN_H     = 700;
    const int WIN_MIN_W = 900;
    const int WIN_MIN_H = 600;

} // namespace Config

#endif // CONFIG_H
