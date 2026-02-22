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
    return in.readAll();
}

bool SpeEditor::save(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
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
