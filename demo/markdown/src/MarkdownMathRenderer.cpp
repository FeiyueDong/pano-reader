#include "MarkdownMathRenderer.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QUrl>

#include <md4c-html.h>

#include <algorithm>
#include <cmath>

#include <jkqtmathtext/jkqtmathtext.h>

namespace pano::markdown {
namespace {

QString decodeHtmlEntities(const QString& value)
{
    QString out;
    out.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        if (value.at(i) != QLatin1Char('&')) {
            out.append(value.at(i));
            continue;
        }
        const int semi = value.indexOf(QLatin1Char(';'), i + 1);
        if (semi < 0 || semi - i > 12) {
            out.append(value.at(i));
            continue;
        }
        const QString entity = value.mid(i + 1, semi - i - 1);
        if (entity == QStringLiteral("amp")) out.append(QLatin1Char('&'));
        else if (entity == QStringLiteral("lt")) out.append(QLatin1Char('<'));
        else if (entity == QStringLiteral("gt")) out.append(QLatin1Char('>'));
        else if (entity == QStringLiteral("quot")) out.append(QLatin1Char('"'));
        else if (entity == QStringLiteral("apos")) out.append(QLatin1Char('\''));
        else if (entity.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const int code = entity.mid(1).toInt(&ok, entity.startsWith(QStringLiteral("#x"))
                                                           ? 16 : 10);
            if (ok && code > 0 && code <= 0x10ffff)
                out.append(QString::fromUcs4(reinterpret_cast<const char32_t*>(&code), 1));
            else
                out.append(value.mid(i, semi - i + 1));
        } else {
            out.append(value.mid(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

void appendHtml(const MD_CHAR* text, MD_SIZE size, void* data)
{
    auto* target = static_cast<QByteArray*>(data);
    target->append(text, static_cast<int>(size));
}

} // namespace

MathRenderResult MarkdownMathRenderer::renderMath(const QString& latex, bool display,
                                                  qreal fontPt, const QColor& color,
                                                  qreal dpr)
{
    MathRenderResult result;
    JKQTMathText math;
    math.setFontSize(fontPt);
    math.setFontColor(color);

    const QString source = QStringLiteral("$") + latex + QStringLiteral("$");
    math.parse(source, JKQTMathText::LatexParser, JKQTMathText::DefaultParseOptions);
    const QStringList errors = math.getErrorList();
    if (!errors.isEmpty()) {
        result.error = errors.first();
        return result;
    }

    QImage measuring(1, 1, QImage::Format_ARGB32_Premultiplied);
    measuring.fill(Qt::transparent);
    QPainter measurePainter(&measuring);
    measurePainter.setRenderHint(QPainter::Antialiasing, true);
    double width = 0.0;
    double ascent = 0.0;
    double descent = 0.0;
    double strikeout = 0.0;
    math.getSizeDetail(measurePainter, width, ascent, descent, strikeout);
    measurePainter.end();
    if (!(width > 0.0) || !(ascent + descent > 0.0)) {
        result.error = QStringLiteral("公式尺寸无效");
        return result;
    }

    const qreal padding = 2.0;
    const QSizeF logical(width + 2 * padding, ascent + descent + 2 * padding);
    const qreal scale = std::max<qreal>(1.0, dpr);
    const int pixelWidth = std::max(1, static_cast<int>(std::ceil(logical.width() * scale)));
    const int pixelHeight = std::max(1, static_cast<int>(std::ceil(logical.height() * scale)));
    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.scale(scale, scale);
    painter.translate(padding, padding);
    math.draw(painter, 0.0, ascent);
    painter.end();

    result.image = image;
    result.logicalSize = logical;
    result.error.clear();
    Q_UNUSED(display);
    return result;
}

MarkdownRenderResult MarkdownMathRenderer::render(const QString& markdown,
                                                  const QString& baseDirectory,
                                                  qreal fontPt, const QColor& textColor,
                                                  qreal dpr, qreal availableWidth)
{
    MarkdownRenderResult result;
    QElapsedTimer parseTimer;
    parseTimer.start();

    int displayFenceCount = 0;
    for (const QString& line : markdown.split(QLatin1Char('\n'))) {
        if (line.trimmed() == QStringLiteral("$$"))
            ++displayFenceCount;
    }
    if (displayFenceCount % 2 != 0) {
        ++result.formulaFailures;
        result.warnings.append(QStringLiteral("存在未闭合的 $$ 显示公式，原始文本已保留"));
    }

    QByteArray htmlBytes;
    const QByteArray utf8 = markdown.toUtf8();
    const unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTML;
    if (md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), appendHtml, &htmlBytes,
                parserFlags, 0) != 0) {
        result.parseMs = parseTimer.elapsed();
        result.warnings.append(QStringLiteral("MD4C 解析失败"));
        result.html = QStringLiteral("<p>Markdown 解析失败。</p>");
        return result;
    }
    QString html = QString::fromUtf8(htmlBytes);
    result.parseMs = parseTimer.elapsed();

    QElapsedTimer mathTimer;
    mathTimer.start();
    QHash<QString, QUrl> mathUrls;
    auto mathImageTag = [&](const QString& latex, bool display) {
        ++result.formulaCount;
        const QString trimmed = latex.trimmed();
        const QString key = QString::number(result.formulaCount);
        const QUrl url(QStringLiteral("pano-math:///") + key);
        const MathRenderResult rendered =
            renderMath(decodeHtmlEntities(trimmed), display, fontPt, textColor, dpr);
        if (!rendered.ok()) {
            ++result.formulaFailures;
            result.warnings.append(QStringLiteral("公式 %1 失败：%2").arg(result.formulaCount).arg(rendered.error));
            const QString original = decodeHtmlEntities(trimmed).toHtmlEscaped();
            return QStringLiteral("<span class=\"math-error\">[公式渲染失败：%1] <code>%2</code></span>")
                .arg(rendered.error.toHtmlEscaped(), original);
        }
        result.resources.insert(url, rendered.image);
        const qreal maxWidth = display
            ? std::clamp(availableWidth, 220.0, 900.0)
            : std::clamp(availableWidth, 120.0, 760.0);
        qreal logicalWidth = rendered.logicalSize.width();
        qreal logicalHeight = rendered.logicalSize.height();
        if (logicalWidth > maxWidth) {
            const qreal factor = maxWidth / logicalWidth;
            logicalWidth = maxWidth;
            logicalHeight *= factor;
            result.warnings.append(QStringLiteral("公式 %1 过宽，已缩放到 %2 px 显示")
                                       .arg(result.formulaCount)
                                       .arg(static_cast<int>(maxWidth)));
        }
        const QString tag = QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"math\">")
                                .arg(url.toString())
                                .arg(static_cast<int>(std::ceil(logicalWidth)))
                                .arg(static_cast<int>(std::ceil(logicalHeight)));
        if (display)
            return QStringLiteral("<div align=\"center\">%1</div>").arg(tag);
        return tag;
    };

    // MD4C emits display formulas inside a paragraph. Replace that whole paragraph
    // so Qt renders a centered block instead of an inline paragraph image.
    {
        static const QRegularExpression displayRe(
            QStringLiteral("<p>\\s*<x-equation type=\"display\">(.*?)</x-equation>\\s*</p>"),
            QRegularExpression::DotMatchesEverythingOption);
        QString replaced;
        int last = 0;
        QRegularExpressionMatchIterator it = displayRe.globalMatch(html);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            replaced += html.mid(last, match.capturedStart() - last);
            replaced += mathImageTag(match.captured(1), true);
            last = match.capturedEnd();
        }
        replaced += html.mid(last);
        html = replaced;
    }
    {
        static const QRegularExpression inlineRe(
            QStringLiteral("<x-equation type=\"display\">(.*?)</x-equation>"),
            QRegularExpression::DotMatchesEverythingOption);
        QString replaced;
        int last = 0;
        QRegularExpressionMatchIterator it = inlineRe.globalMatch(html);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            replaced += html.mid(last, match.capturedStart() - last);
            replaced += mathImageTag(match.captured(1), true);
            last = match.capturedEnd();
        }
        replaced += html.mid(last);
        html = replaced;
    }
    {
        static const QRegularExpression inlineRe(
            QStringLiteral("<x-equation>(.*?)</x-equation>"),
            QRegularExpression::DotMatchesEverythingOption);
        QString replaced;
        int last = 0;
        QRegularExpressionMatchIterator it = inlineRe.globalMatch(html);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            replaced += html.mid(last, match.capturedStart() - last);
            replaced += mathImageTag(match.captured(1), false);
            last = match.capturedEnd();
        }
        replaced += html.mid(last);
        html = replaced;
    }
    // QTextDocument's HTML subset does not consistently apply <del>; use inline CSS.
    html.replace(QStringLiteral("<del>"), QStringLiteral("<span style=\"text-decoration: line-through;\">"));
    html.replace(QStringLiteral("</del>"), QStringLiteral("</span>"));
    result.mathMs = mathTimer.elapsed();

    // Resolve local images only. External images are removed rather than loaded.
    {
        static const QRegularExpression imageRe(QStringLiteral("<img\\s+([^>]*?)src=\"([^\"]+)\"([^>]*)>"));
        QString replaced;
        int last = 0;
        int imageIndex = 0;
        QRegularExpressionMatchIterator it = imageRe.globalMatch(html);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            replaced += html.mid(last, match.capturedStart() - last);
            const QString src = match.captured(2);
            if (src.startsWith(QStringLiteral("pano-math:///"))) {
                replaced += match.captured(0);
            } else if (src.contains(QStringLiteral("://")) || src.startsWith(QLatin1Char('/'))) {
                ++result.missingImages;
                result.warnings.append(QStringLiteral("已阻止外部/绝对图片：%1").arg(src));
                replaced += QStringLiteral("<span class=\"asset-missing\">[图片已阻止：%1]</span>")
                                .arg(src.toHtmlEscaped());
            } else {
                const QString path = QDir(baseDirectory).absoluteFilePath(src);
                QImageReader reader(path);
                reader.setAutoTransform(true);
                const QImage image = reader.read();
                if (image.isNull()) {
                    ++result.missingImages;
                    result.warnings.append(QStringLiteral("图片缺失：%1").arg(path));
                    replaced += QStringLiteral("<span class=\"asset-missing\">[图片缺失：%1]</span>")
                                    .arg(src.toHtmlEscaped());
                } else {
                    const QUrl url(QStringLiteral("pano-image:///") + QString::number(imageIndex++));
                    result.resources.insert(url, image);
                    QString tag = match.captured(0);
                    tag.replace(QStringLiteral("src=\"%1\"").arg(src),
                                QStringLiteral("src=\"%1\"").arg(url.toString()));
                    if (image.width() > 720) {
                        tag.insert(tag.size() - 1,
                                   QStringLiteral(" width=\"720\" height=\"%1\"")
                                       .arg(image.height() * 720 / image.width()));
                    }
                    replaced += tag;
                }
            }
            last = match.capturedEnd();
        }
        replaced += html.mid(last);
        html = replaced;
    }

    result.html = html;
    return result;
}

QString MarkdownMathRenderer::defaultStyleSheet(bool dark, qreal fontPt)
{
    const QString fg = dark ? QStringLiteral("#e6e8ee") : QStringLiteral("#20242b");
    const QString muted = dark ? QStringLiteral("#aab0bd") : QStringLiteral("#667085");
    const QString border = dark ? QStringLiteral("#3a4150") : QStringLiteral("#d0d5dd");
    const QString codeBg = dark ? QStringLiteral("#262b34") : QStringLiteral("#f2f4f7");
    const QString link = dark ? QStringLiteral("#8ab4ff") : QStringLiteral("#1d5fbf");
    return QStringLiteral(
               "body { color:%1; font-family:'Noto Sans CJK SC','DejaVu Sans',sans-serif; "
               "font-size:%2pt; line-height:1.55; }\n"
               "h1 { font-size:%3pt; margin:8px 0 10px 0; }\n"
               "h2 { font-size:%4pt; margin:14px 0 8px 0; }\n"
               "h3 { font-size:%5pt; margin:12px 0 6px 0; }\n"
               "p { margin:6px 0; }\n"
               "blockquote { color:%6; border-left:3px solid %7; margin:8px 0; padding:2px 12px; }\n"
               "code { background-color:%8; color:%1; font-family:'DejaVu Sans Mono',monospace; }\n"
               "pre { background-color:%8; color:%1; padding:8px; margin:8px 0; "
               "font-family:'DejaVu Sans Mono',monospace; font-size:%9pt; }\n"
               "table { border-collapse:collapse; margin:8px 0; }\n"
               "th, td { border:1px solid %7; padding:4px 8px; }\n"
               "th { background-color:%8; }\n"
               "a { color:%10; }\n"
               "hr { border:1px solid %7; }\n"
               "img { margin:2px 0; }\n"
               ".math-error { color:#c0392b; }\n"
               ".asset-missing { color:#c0392b; }\n")
        .arg(fg)
        .arg(fontPt)
        .arg(fontPt * 1.75)
        .arg(fontPt * 1.45)
        .arg(fontPt * 1.2)
        .arg(muted, border, codeBg)
        .arg(std::max<qreal>(8.0, fontPt - 1.0))
        .arg(link);
}

} // namespace pano::markdown
