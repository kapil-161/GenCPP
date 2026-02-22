#ifndef SPEEDITOR_H
#define SPEEDITOR_H

#include <QString>
#include <QStringList>

class SpeEditor
{
public:
    // Load raw text from a .SPE file.
    static QString load(const QString &filePath);

    // Save text to a .SPE file (Windows line endings).
    static bool save(const QString &filePath, const QString &text);

    // Return list of section names found in the text (lines starting with * or !*).
    static QStringList sectionNames(const QString &text);

    // Return character offset of a given section name within text.
    static int sectionOffset(const QString &text, const QString &sectionName);
};

#endif // SPEEDITOR_H
