#ifndef SPESYNTAXHIGHLIGHTER_H
#define SPESYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

// Syntax highlighter for DSSAT .SPE (species) files.
// Colours:
//   Section headers  (* or !*  lines)  — bold dark-blue
//   Comment lines    (! lines)          — italic grey
//   Parameter names  (first UPPER word) — bold dark-green
//   Inline comments  (trailing !)       — italic light-grey
class SpeSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SpeSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat m_sectionFmt;
    QTextCharFormat m_commentFmt;
    QTextCharFormat m_paramFmt;
    QTextCharFormat m_inlineCommentFmt;
    QRegularExpression m_paramRe;
};

#endif // SPESYNTAXHIGHLIGHTER_H
