#include "DetailCdeParser.h"
#include <QFile>
#include <QTextStream>

QMap<QString, QMap<QString, QString>> DetailCdeParser::parse(const QString &filePath)
{
    QMap<QString, QMap<QString, QString>> sections;
    QString currentSection;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return sections;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QString trimmed = line.trimmed();

        if (trimmed.isEmpty() || trimmed.startsWith('!'))
            continue;

        if (trimmed.startsWith('*')) {
            currentSection = trimmed.mid(1).trimmed();
            sections[currentSection] = QMap<QString, QString>();
            continue;
        }

        if (trimmed.startsWith('@'))
            continue;   // column header line, skip

        if (currentSection.isEmpty()) continue;

        // Data row: code is first 8 chars, description follows
        if (line.length() < 2) continue;
        QString code = line.left(8).trimmed();
        QString desc = line.length() > 8 ? line.mid(8, 64).trimmed() : QString();

        if (!code.isEmpty())
            sections[currentSection][code] = desc;
    }

    return sections;
}

QString DetailCdeParser::cropName(const QMap<QString, QMap<QString, QString>> &sections,
                                   const QString &cropCode)
{
    auto it = sections.find("Crop and Weed Species");
    if (it == sections.end()) {
        // Try alternate section name
        it = sections.find("Crops");
    }
    if (it == sections.end()) return cropCode;

    auto jt = it->find(cropCode);
    if (jt == it->end()) return cropCode;
    return jt.value();
}

QString DetailCdeParser::headerTooltip(const QMap<QString, QMap<QString, QString>> &sections,
                                        const QString &header)
{
    auto it = sections.find("Headers");
    if (it == sections.end()) return QString();
    auto jt = it->find(header);
    if (jt == it->end()) return QString();
    return jt.value();
}
