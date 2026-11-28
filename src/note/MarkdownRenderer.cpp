#include "note/MarkdownRenderer.h"

#include <QDir>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QUrl>

#include <md4c-html.h>

#include <algorithm>
#include <cmath>

#include <jkqtmathtext/jkqtmathtext.h>

namespace pano {
namespace {

void appendHtml(const MD_CHAR *text, MD_SIZE size, void *data)
{
    auto *target = static_cast<QByteArray *>(data);
    target->append(text, static_cast<int>(size));
}

QString decodeHtmlEntities(const QString &value)
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
            const int code = entity.mid(1).toInt(
                &ok, entity.startsWith(QStringLiteral("#x")) ? 16 : 10);
            if (ok && code > 0 && code <= 0x10ffff) {
                const char32_t ch = static_cast<char32_t>(code);
                out.append(QString::fromUcs4(&ch, 1));
            } else {
                out.append(value.mid(i, semi - i + 1));
            }
        } else {
            out.append(value.mid(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

struct MathImage
{
    QImage image;
    QSizeF logicalSize;
    QString error;
    bool ok() const { return !image.isNull() && error.isEmpty(); }
};

MathImage renderMath(const QString &latex, qreal fontPt, const QColor &color)
{
    MathImage result;
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
    const qreal scale = 2.0;
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
    image.setDevicePixelRatio(1.0);

    result.image = image;
    result.logicalSize = logical;
    return result;
}

} // namespace

MarkdownRenderResult MarkdownRenderer::render(const QString &markdown,
                                              const QString &baseDirectory,
                                              qreal fontPt,
                                              const QColor &textColor)
{
    MarkdownRenderResult result;
    int displayFenceCount = 0;
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        if (line.trimmed() == QStringLiteral("$$"))
            ++displayFenceCount;
    }
    if (displayFenceCount % 2 != 0) {
        ++result.mathFailures;
        result.warnings.append(QStringLiteral("存在未闭合的 $$ 显示公式，原始文本已保留"));
    }

    QByteArray htmlBytes;
    const QByteArray utf8 = markdown.toUtf8();
    const unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTML;
    if (md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), appendHtml, &htmlBytes,
                parserFlags, 0) != 0) {
        result.warnings.append(QStringLiteral("MD4C 解析失败"));
        result.html = QStringLiteral("<p>Markdown 解析失败。</p>");
        return result;
    }
    QString html = QString::fromUtf8(htmlBytes);

    auto mathImageTag = [&](const QString &latex, bool display) {
        ++result.formulaCount;
        const QString trimmed = latex.trimmed();
        const QString key = QString::number(result.formulaCount);
        const QUrl url(QStringLiteral("pano-math:///") + key);
        const MathImage rendered = renderMath(decodeHtmlEntities(trimmed), fontPt, textColor);
        if (!rendered.ok()) {
            ++result.mathFailures;
            result.warnings.append(QStringLiteral("公式 %1 失败：%2")
                                       .arg(result.formulaCount)
                                       .arg(rendered.error));
            const QString original = decodeHtmlEntities(trimmed).toHtmlEscaped();
            return QStringLiteral("<span class=\"math-error\">[公式渲染失败：%1] <code>%2</code></span>")
                .arg(rendered.error.toHtmlEscaped(), original);
        }
        result.resources.insert(url, rendered.image);
        const qreal maxWidth = display ? 760.0 : 520.0;
        qreal logicalWidth = rendered.logicalSize.width();
        qreal logicalHeight = rendered.logicalSize.height();
        if (logicalWidth > maxWidth) {
            const qreal factor = maxWidth / logicalWidth;
            logicalWidth = maxWidth;
            logicalHeight *= factor;
            result.warnings.append(QStringLiteral("公式 %1 过宽，已缩放显示")
                                       .arg(result.formulaCount));
        }
        const QString tag = QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"math\">")
                                .arg(url.toString())
                                .arg(static_cast<int>(std::ceil(logicalWidth)))
                                .arg(static_cast<int>(std::ceil(logicalHeight)));
        if (display)
            return QStringLiteral("<div align=\"center\">%1</div>").arg(tag);
        return tag;
    };

    {
        static const QRegularExpression displayRe(
            QStringLiteral("<p>\\s*<x-equation type=\"display\">(.*?)</x-equation>\\s*</p>"),
            QRegularExpression::DotMatchesEverythingOption);
        QString replaced;
        int last = 0;
        auto it = displayRe.globalMatch(html);
        while (it.hasNext()) {
            const auto match = it.next();
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
        auto it = inlineRe.globalMatch(html);
        while (it.hasNext()) {
            const auto match = it.next();
            replaced += html.mid(last, match.capturedStart() - last);
            replaced += mathImageTag(match.captured(1), false);
            last = match.capturedEnd();
        }
        replaced += html.mid(last);
        html = replaced;
    }
    html.replace(QStringLiteral("<del>"),
                 QStringLiteral("<span style=\"text-decoration: line-through;\">"));
    html.replace(QStringLiteral("</del>"), QStringLiteral("</span>"));

    {
        static const QRegularExpression imageRe(
            QStringLiteral("<img\\s+([^>]*?)src=\"([^\"]+)\"([^>]*)>"));
        QString replaced;
        int last = 0;
        int imageIndex = 0;
        auto it = imageRe.globalMatch(html);
        while (it.hasNext()) {
            const auto match = it.next();
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

} // namespace pano
