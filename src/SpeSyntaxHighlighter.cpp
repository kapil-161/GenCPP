#include "SpeSyntaxHighlighter.h"
#include <QFont>

SpeSyntaxHighlighter::SpeSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
    , m_paramRe("^\\s*([A-Z][A-Z0-9_]{1,})")
{
    // Section headers: * SECTION NAME  or  !* SECTION NAME
    m_sectionFmt.setForeground(QColor("#1565C0"));   // dark blue
    m_sectionFmt.setFontWeight(QFont::Bold);
    m_sectionFmt.setFontItalic(false);

    // Full-line comments: lines starting with !
    m_commentFmt.setForeground(QColor("#757575"));   // grey
    m_commentFmt.setFontItalic(true);

    // Parameter names: first uppercase token on a data line
    m_paramFmt.setForeground(QColor("#2E7D32"));     // dark green
    m_paramFmt.setFontWeight(QFont::Bold);

    // Inline comment: text from ! to end of line on a data line
    m_inlineCommentFmt.setForeground(QColor("#9E9E9E")); // light grey
    m_inlineCommentFmt.setFontItalic(true);
}

void SpeSyntaxHighlighter::highlightBlock(const QString &text)
{
    const QString trimmed = text.trimmed();

    if (trimmed.isEmpty())
        return;

    // ── Section header ───────────────────────────────────────────────────────
    // Lines like: *TEMPERATURE EFFECTS  or  !*EVAPOTRANSPIRATION
    // Exclude file-header lines starting with ** or *MAIZE...MODEL (first line)
    if (trimmed.startsWith("!*") ||
        (trimmed.startsWith('*') && !trimmed.startsWith("**"))) {
        setFormat(0, text.length(), m_sectionFmt);
        return;
    }

    // ── Full-line comment ────────────────────────────────────────────────────
    if (trimmed.startsWith('!')) {
        setFormat(0, text.length(), m_commentFmt);
        return;
    }

    // ── Data line ────────────────────────────────────────────────────────────
    // 1. Parameter name (leading uppercase token)
    QRegularExpressionMatch m = m_paramRe.match(text);
    if (m.hasMatch()) {
        setFormat(m.capturedStart(1), m.capturedLength(1), m_paramFmt);
    }

    // 2. Inline comment — everything from the first ! onwards
    int excl = text.indexOf('!');
    if (excl >= 0) {
        setFormat(excl, text.length() - excl, m_inlineCommentFmt);
    }
}
