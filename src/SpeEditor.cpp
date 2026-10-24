#include "SpeEditor.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>

QString SpeEditor::load(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);
    in.setAutoDetectUnicode(false);   // byte-exact Latin1, same as CulParser/EcoParser
    return in.readAll();
}

bool SpeEditor::save(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    // Binary mode: we write \r\n ourselves (Text mode would make it \r\r\n on Windows)
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Latin1);

    // Normalise to Windows line endings
    QString normalised = text;
    normalised.replace("\r\n", "\n").replace('\r', '\n').replace('\n', "\r\n");
    out << normalised;
    return true;
}

QStringList SpeEditor::sectionNames(const QString &text)
{
    QStringList names;
    const QStringList lines = text.split('\n');
    for (const QString &line : lines) {
        QString t = line.trimmed();
        if (t.startsWith("!*")) {
            names << t.mid(2).trimmed();
        } else if (t.startsWith('*') && !t.startsWith("**")) {
            names << t.mid(1).trimmed();
        }
    }
    return names;
}

int SpeEditor::sectionOffset(const QString &text, const QString &sectionName)
{
    // Search for "!*SECTION NAME" or "*SECTION NAME"
    QStringList candidates = {
        "!*" + sectionName,
        "*"  + sectionName,
        "!* " + sectionName,
        "* "  + sectionName,
    };
    for (const QString &needle : candidates) {
        int pos = text.indexOf(needle, 0, Qt::CaseInsensitive);
        if (pos >= 0) return pos;
    }
    return -1;
}
