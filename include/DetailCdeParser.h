#ifndef DETAILCDEPARSER_H
#define DETAILCDEPARSER_H

#include <QString>
#include <QMap>

class DetailCdeParser
{
public:
    // Returns sections["Headers"]["INGENO"] = "Cultivar identifier"
    static QMap<QString, QMap<QString, QString>> parse(const QString &filePath);

    // Convenience: get crop name from code, e.g. "LU" -> "Lettuce"
    static QString cropName(const QMap<QString, QMap<QString, QString>> &sections,
                            const QString &cropCode);

    // Convenience: get tooltip for a column header, e.g. "INGENO" -> "Cultivar identifier"
    static QString headerTooltip(const QMap<QString, QMap<QString, QString>> &sections,
                                 const QString &header);
};

#endif // DETAILCDEPARSER_H
