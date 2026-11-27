#include "note/MarkdownRenderer.h"

#include "note/MathRenderer.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>

#include <cmath>
#include <utility>

namespace pano {
namespace {

QString escapeHtml(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        case '\'': out += QStringLiteral("&#39;"); break;
        default: out += ch; break;
        }
    }
    return out;
}

QString attribute(const QString &text)
{
    return escapeHtml(text);
}

bool isEscaped(const QString &text, int index)
{
    int backslashes = 0;
    for (int i = index - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++backslashes;
    return (backslashes % 2) == 1;
}

bool isHeadingLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^ {0,3}#{1,6}\\s+\\S"));
    return re.match(line).hasMatch();
}

bool isFenceLine(const QString &line)
{
    return line.trimmed().startsWith(QStringLiteral("```"));
}

bool isHrLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.size() < 3)
        return false;
    const QChar c = t.at(0);
    if (c != QLatin1Char('-') && c != QLatin1Char('*') && c != QLatin1Char('_'))
        return false;
    for (const QChar ch : t) {
        if (ch != c && ch != QLatin1Char(' '))
            return false;
    }
    return true;
}

bool isBlockQuoteLine(const QString &line)
{
    return line.trimmed().startsWith(QLatin1Char('>'));
}

bool isBulletLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^\\s*[-*+]\\s+\\S"));
    return re.match(line).hasMatch();
}

bool isOrderedLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^\\s*\\d+[.)]\\s+\\S"));
    return re.match(line).hasMatch();
}

bool looksLikeTableStart(const QStringList &lines, int index)
{
    if (index + 1 >= lines.size())
        return false;
    const QString header = lines.at(index).trimmed();
    if (!header.contains(QLatin1Char('|')))
        return false;
    const QStringList cells = header.split(QLatin1Char('|'));
    const QString separator = lines.at(index + 1).trimmed();
    if (!separator.contains(QLatin1Char('|')))
        return false;
    const QStringList separators = separator.split(QLatin1Char('|'));
    int checked = 0;
    for (const QString &cell : separators) {
        const QString t = cell.trimmed();
        if (t.isEmpty())
            continue;
        static const QRegularExpression re(QStringLiteral("^:?-{3,}:?$"));
        if (!re.match(t).hasMatch())
            return false;
        ++checked;
    }
    return checked > 0 && checked >= qMin(cells.size() - 2, 1);
}

QStringList splitTableRow(const QString &line)
{
    QString t = line.trimmed();
    if (t.startsWith(QLatin1Char('|')))
        t.remove(0, 1);
    if (t.endsWith(QLatin1Char('|')))
        t.chop(1);
    QStringList cells = t.split(QLatin1Char('|'));
    for (QString &cell : cells) {
        cell = cell.trimmed();
        cell.replace(QStringLiteral("\\|"), QStringLiteral("|"));
        cell.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    return cells;
}

QString cellAlignment(const QString &separator)
{
    const QString t = separator.trimmed();
    if (t.startsWith(QLatin1Char(':')) && t.endsWith(QLatin1Char(':')))
        return QStringLiteral("center");
    if (t.endsWith(QLatin1Char(':')))
        return QStringLiteral("right");
    if (t.startsWith(QLatin1Char(':')))
        return QStringLiteral("left");
    return QString();
}

bool isSafeLinkTarget(const QString &target)
{
    const QString lower = target.trimmed().toLower();
    return !lower.startsWith(QStringLiteral("javascript:"))
        && !lower.startsWith(QStringLiteral("data:"))
        && !lower.startsWith(QStringLiteral("vbscript:"));
}

class Renderer
{
public:
    Renderer(QString markdown, QString baseDirectory)
        : m_markdown(std::move(markdown))
        , m_baseDirectory(std::move(baseDirectory))
    {
    }

    MarkdownRenderResult run()
    {
        MarkdownRenderResult result;
        QString normalized = m_markdown;
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

        const QStringList lines = normalized.split(QLatin1Char('\n'));
        QString html;
        QStringList paragraph;
        bool paragraphStartsHard = false;

        auto flushParagraph = [&]() {
            if (paragraph.isEmpty())
                return;
            QString joined;
            for (int i = 0; i < paragraph.size(); ++i) {
                if (i > 0)
                    joined += paragraphStartsHard ? QLatin1Char('\n') : QLatin1Char(' ');
                joined += paragraph.at(i);
                paragraphStartsHard = paragraph.at(i).endsWith(QStringLiteral("  "));
            }
            html += QStringLiteral("<p>") + inlineText(joined) + QStringLiteral("</p>\n");
            paragraph.clear();
            paragraphStartsHard = false;
        };

        int i = 0;
        while (i < lines.size()) {
            const QString line = lines.at(i);
            const QString trimmed = line.trimmed();

            if (trimmed.isEmpty()) {
                flushParagraph();
                ++i;
                continue;
            }

            if (isFenceLine(line)) {
                flushParagraph();
                QString language = trimmed.mid(3).trimmed();
                QStringList code;
                ++i;
                while (i < lines.size() && !isFenceLine(lines.at(i))) {
                    code.append(lines.at(i));
                    ++i;
                }
                if (i < lines.size())
                    ++i;
                const QString codeText = escapeHtml(code.join(QLatin1Char('\n')));
                if (!language.isEmpty())
                    html += QStringLiteral("<pre><code class=\"language-%1\">").arg(attribute(language));
                else
                    html += QStringLiteral("<pre><code>");
                html += codeText;
                html += QStringLiteral("</code></pre>\n");
                continue;
            }

            if (trimmed.startsWith(QStringLiteral("$$")) || trimmed.startsWith(QStringLiteral("\\["))) {
                flushParagraph();
                const bool bracket = trimmed.startsWith(QStringLiteral("\\["));
                const QString opening = bracket ? QStringLiteral("\\[") : QStringLiteral("$$");
                const QString closing = bracket ? QStringLiteral("\\]") : QStringLiteral("$$");
                QString expression = trimmed.mid(opening.size());
                ++i;
                if (expression.contains(closing)) {
                    expression = expression.left(expression.indexOf(closing));
                } else {
                    while (i < lines.size() && !lines.at(i).contains(closing)) {
                        expression += QLatin1Char('\n');
                        expression += lines.at(i);
                        ++i;
                    }
                    if (i < lines.size()) {
                        const QString closingLine = lines.at(i);
                        expression += QLatin1Char('\n');
                        expression += closingLine.left(closingLine.indexOf(closing));
                        ++i;
                    }
                }
                const QString image = mathImage(expression.trimmed(), true, 680);
                if (!image.isEmpty())
                    html += QStringLiteral("<p align=\"center\">") + image + QStringLiteral("</p>\n");
                else
                    html += QStringLiteral("<p align=\"center\">%1</p>\n")
                                .arg(escapeHtml(opening + expression + closing));
                continue;
            }

            if (isHeadingLine(line)) {
                flushParagraph();
                static const QRegularExpression re(QStringLiteral("^ {0,3}(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
                const QRegularExpressionMatch match = re.match(line);
                const int level = match.captured(1).size();
                html += QStringLiteral("<h%1>%2</h%1>\n")
                            .arg(level)
                            .arg(inlineText(match.captured(2)));
                ++i;
                continue;
            }

            if (isHrLine(line)) {
                flushParagraph();
                html += QStringLiteral("<hr/>\n");
                ++i;
                continue;
            }

            if (isBlockQuoteLine(line)) {
                flushParagraph();
                QStringList quoteLines;
                while (i < lines.size() && isBlockQuoteLine(lines.at(i))) {
                    QString q = lines.at(i).trimmed();
                    q.remove(0, 1);
                    if (q.startsWith(QLatin1Char(' ')))
                        q.remove(0, 1);
                    quoteLines.append(q);
                    ++i;
                }
                html += QStringLiteral("<blockquote><p>%1</p></blockquote>\n")
                            .arg(inlineText(quoteLines.join(QLatin1Char(' '))));
                continue;
            }

            if (isBulletLine(line) || isOrderedLine(line)) {
                flushParagraph();
                static const QRegularExpression orderedRe(QStringLiteral("^\\s*\\d+[.)]\\s+(.*)$"));
                static const QRegularExpression bulletRe(QStringLiteral("^\\s*[-*+]\\s+(.*)$"));
                const bool ordered = isOrderedLine(line);
                QStringList items;
                while (i < lines.size()) {
                    const QString itemLine = lines.at(i);
                    QRegularExpressionMatch match = ordered ? orderedRe.match(itemLine) : bulletRe.match(itemLine);
                    if (!match.hasMatch())
                        break;
                    QString item = match.captured(1).trimmed();
                    ++i;
                    while (i < lines.size() && !lines.at(i).trimmed().isEmpty()
                           && (lines.at(i).startsWith(QStringLiteral("  "))
                               || lines.at(i).startsWith(QLatin1Char('\t')))) {
                        if (!isBulletLine(lines.at(i)) && !isOrderedLine(lines.at(i)))
                            item += QLatin1Char(' ') + lines.at(i).trimmed();
                        ++i;
                    }
                    items.append(item);
                }
                html += ordered ? QStringLiteral("<ol>\n") : QStringLiteral("<ul>\n");
                for (const QString &item : items)
                    html += QStringLiteral("<li>%1</li>\n").arg(inlineText(item));
                html += ordered ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n");
                continue;
            }

            if (looksLikeTableStart(lines, i)) {
                flushParagraph();
                const QStringList headers = splitTableRow(lines.at(i));
                const QStringList separators = splitTableRow(lines.at(i + 1));
                QStringList aligns;
                for (int c = 0; c < headers.size(); ++c)
                    aligns.append(c < separators.size() ? cellAlignment(separators.at(c)) : QString());
                i += 2;
                QStringList rows;
                while (i < lines.size() && lines.at(i).trimmed().contains(QLatin1Char('|'))
                       && !lines.at(i).trimmed().isEmpty()) {
                    rows.append(lines.at(i));
                    ++i;
                }
                html += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n<tr>");
                for (int c = 0; c < headers.size(); ++c) {
                    const QString align = aligns.at(c).isEmpty()
                        ? QString()
                        : QStringLiteral(" align=\"%1\"").arg(aligns.at(c));
                    html += QStringLiteral("<th%1>%2</th>").arg(align, inlineText(headers.at(c)));
                }
                html += QStringLiteral("</tr>\n");
                for (const QString &row : rows) {
                    const QStringList cells = splitTableRow(row);
                    html += QStringLiteral("<tr>");
                    for (int c = 0; c < headers.size(); ++c) {
                        const QString align = aligns.at(c).isEmpty()
                            ? QString()
                            : QStringLiteral(" align=\"%1\"").arg(aligns.at(c));
                        html += QStringLiteral("<td%1>%2</td>")
                                    .arg(align, inlineText(c < cells.size() ? cells.at(c) : QString()));
                    }
                    html += QStringLiteral("</tr>\n");
                }
                html += QStringLiteral("</table>\n");
                continue;
            }

            if (!paragraph.isEmpty() && isHeadingLine(line))
                flushParagraph();
            paragraph.append(trimmed);
            ++i;
        }
        flushParagraph();

        result.html = html;
        result.resources = m_resources;
        result.warnings = m_warnings;
        return result;
    }

private:
    QString inlineText(const QString &text)
    {
        QString out;
        out.reserve(text.size() + 32);
        int i = 0;
        while (i < text.size()) {
            const QChar ch = text.at(i);

            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                out += escapeHtml(QString(text.at(i + 1)));
                i += 2;
                continue;
            }

            if (ch == QLatin1Char('\n')) {
                out += QStringLiteral("<br/>");
                ++i;
                continue;
            }

            if (ch == QLatin1Char('`')) {
                const int end = text.indexOf(QLatin1Char('`'), i + 1);
                if (end > i + 1) {
                    out += QStringLiteral("<code>") + escapeHtml(text.mid(i + 1, end - i - 1))
                        + QStringLiteral("</code>");
                    i = end + 1;
                    continue;
                }
            }

            if (ch == QLatin1Char('$') && !isEscaped(text, i)) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('$')) {
                    out += QStringLiteral("$$");
                    i += 2;
                    continue;
                }
                int end = -1;
                for (int p = i + 1; p < text.size(); ++p) {
                    if (text.at(p) == QLatin1Char('$') && !isEscaped(text, p)) {
                        end = p;
                        break;
                    }
                }
                if (end > i + 1) {
                    const QString expression = text.mid(i + 1, end - i - 1).trimmed();
                    const QString image = mathImage(expression, false, 0);
                    if (!image.isEmpty()) {
                        out += image;
                        i = end + 1;
                        continue;
                    }
                }
            }

            if (ch == QLatin1Char('!') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('[')) {
                const int labelEnd = text.indexOf(QStringLiteral("]("), i + 2);
                if (labelEnd > i) {
                    const QString alt = text.mid(i + 2, labelEnd - i - 2);
                    QString target;
                    int close = -1;
                    if (parseTarget(text, labelEnd + 1, &target, &close)) {
                        out += imageHtml(alt, target);
                        i = close + 1;
                        continue;
                    }
                }
            }

            if (ch == QLatin1Char('[') && !isEscaped(text, i)) {
                const int labelEnd = text.indexOf(QStringLiteral("]("), i + 1);
                if (labelEnd > i) {
                    QString target;
                    int close = -1;
                    if (parseTarget(text, labelEnd + 1, &target, &close) && isSafeLinkTarget(target)) {
                        out += QStringLiteral("<a href=\"%1\">%2</a>")
                                   .arg(attribute(target.trimmed()), inlineText(text.mid(i + 1, labelEnd - i - 1)));
                        i = close + 1;
                        continue;
                    }
                }
            }

            if (text.mid(i, 2) == QStringLiteral("**") || text.mid(i, 2) == QStringLiteral("__")) {
                const QString marker = text.mid(i, 2);
                const int end = text.indexOf(marker, i + 2);
                if (end > i + 2) {
                    out += QStringLiteral("<b>") + inlineText(text.mid(i + 2, end - i - 2))
                        + QStringLiteral("</b>");
                    i = end + 2;
                    continue;
                }
            }

            if (text.mid(i, 2) == QStringLiteral("~~")) {
                const int end = text.indexOf(QStringLiteral("~~"), i + 2);
                if (end > i + 2) {
                    out += QStringLiteral("<s>") + inlineText(text.mid(i + 2, end - i - 2))
                        + QStringLiteral("</s>");
                    i = end + 2;
                    continue;
                }
            }

            if ((ch == QLatin1Char('*') || ch == QLatin1Char('_')) && !isEscaped(text, i)) {
                const bool wordUnderscore = ch == QLatin1Char('_') && i > 0
                    && text.at(i - 1).isLetterOrNumber();
                if (!wordUnderscore && i + 1 < text.size() && !text.at(i + 1).isSpace()) {
                    int end = -1;
                    for (int p = i + 1; p < text.size(); ++p) {
                        if (text.at(p) == ch && !isEscaped(text, p)) {
                            if (ch == QLatin1Char('*') && p + 1 < text.size() && text.at(p + 1) == QLatin1Char('*')) {
                                ++p;
                                continue;
                            }
                            end = p;
                            break;
                        }
                    }
                    if (end > i + 1) {
                        out += QStringLiteral("<i>") + inlineText(text.mid(i + 1, end - i - 1))
                            + QStringLiteral("</i>");
                        i = end + 1;
                        continue;
                    }
                }
            }

            out += escapeHtml(QString(ch));
            ++i;
        }
        return out;
    }

    bool parseTarget(const QString &text, int openParen, QString *target, int *closeParen) const
    {
        if (openParen >= text.size() || text.at(openParen) != QLatin1Char('('))
            return false;
        int depth = 1;
        int p = openParen + 1;
        while (p < text.size()) {
            if (text.at(p) == QLatin1Char('('))
                ++depth;
            else if (text.at(p) == QLatin1Char(')')) {
                --depth;
                if (depth == 0)
                    break;
            }
            ++p;
        }
        if (p >= text.size())
            return false;
        QString inside = text.mid(openParen + 1, p - openParen - 1).trimmed();
        if (inside.startsWith(QLatin1Char('<')) && inside.contains(QLatin1Char('>'))) {
            const int gt = inside.indexOf(QLatin1Char('>'));
            inside = inside.mid(1, gt - 1);
        } else {
            const int ws = inside.indexOf(QRegularExpression(QStringLiteral("\\s")));
            if (ws > 0)
                inside = inside.left(ws);
        }
        if (inside.isEmpty())
            return false;
        *target = inside;
        *closeParen = p;
        return true;
    }

    QString mathImage(const QString &expression, bool display, qreal maxWidth)
    {
        if (expression.trimmed().isEmpty())
            return QString();
        QImage image = MathRenderer::render(expression, display, MathRenderer::defaultSupersample());
        if (image.isNull())
            return QString();

        const qreal scale = MathRenderer::defaultSupersample();
        qreal width = image.width() / scale;
        qreal height = image.height() / scale;
        if (display && maxWidth > 0 && width > maxWidth) {
            const qreal factor = maxWidth / width;
            width = maxWidth;
            height *= factor;
        }
        if (!display && height > 46) {
            const qreal factor = 46.0 / height;
            width *= factor;
            height = 46;
        }

        const QUrl url(QStringLiteral("pano-math:///%1").arg(m_resourceCounter++));
        m_resources.insert(url, image);
        return QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" align=\"middle\"/>")
            .arg(url.toString())
            .arg(qMax(1, qRound(width)))
            .arg(qMax(1, qRound(height)));
    }

    QString resolveImagePath(const QString &target) const
    {
        const QString t = target.trimmed();
        if (t.isEmpty() || !isSafeLinkTarget(t))
            return QString();
        if (t.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
            return QUrl(t).toLocalFile();
        if (t.contains(QStringLiteral("://")))
            return QString();
        const QFileInfo info(t);
        if (info.isAbsolute())
            return info.absoluteFilePath();
        if (m_baseDirectory.isEmpty())
            return QString();
        return QDir(m_baseDirectory).absoluteFilePath(t);
    }

    QString imageHtml(const QString &alt, const QString &target)
    {
        const QString path = resolveImagePath(target);
        QImage image;
        if (!path.isEmpty()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            image = reader.read();
        }
        const QString altText = alt.trimmed().isEmpty() ? QStringLiteral("图片") : alt.trimmed();
        if (image.isNull()) {
            m_warnings.append(QStringLiteral("无法加载图片：%1").arg(target));
            return QStringLiteral("<span>[%1]</span>").arg(escapeHtml(altText));
        }
        constexpr int maxWidth = 640;
        if (image.width() > maxWidth)
            image = image.scaledToWidth(maxWidth, Qt::SmoothTransformation);

        const QUrl url(QStringLiteral("pano-image:///%1").arg(m_resourceCounter++));
        m_resources.insert(url, image);
        return QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"%4\"/>")
            .arg(url.toString())
            .arg(image.width())
            .arg(image.height())
            .arg(attribute(altText));
    }

    QString m_markdown;
    QString m_baseDirectory;
    QStringList m_warnings;
    QHash<QUrl, QImage> m_resources;
    int m_resourceCounter = 0;
};

} // namespace

MarkdownRenderResult MarkdownRenderer::render(const QString &markdown, const QString &baseDirectory)
{
    Renderer renderer(markdown, baseDirectory);
    return renderer.run();
}

} // namespace pano
