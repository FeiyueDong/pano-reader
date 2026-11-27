#include "note/MathRenderer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <QtMath>

#include <memory>
#include <vector>

namespace pano {

namespace {

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node
{
    enum class Kind { Row, Glyph, Fraction, Script, Sqrt, Accent, Space };

    Kind kind = Kind::Row;
    QString text;
    bool italic = false;
    bool bold = false;
    qreal space = 0.0;
    std::vector<NodePtr> children;
    NodePtr base;
    NodePtr numerator;
    NodePtr denominator;
    NodePtr radicand;
    NodePtr superscript;
    NodePtr subscript;
};

NodePtr makeNode(Node::Kind kind)
{
    auto node = std::make_unique<Node>();
    node->kind = kind;
    return node;
}

NodePtr makeGlyph(const QString &text, bool italic = false)
{
    auto node = makeNode(Node::Kind::Glyph);
    node->text = text;
    node->italic = italic;
    return node;
}

NodePtr makeSpace(qreal em)
{
    auto node = makeNode(Node::Kind::Space);
    node->space = em;
    return node;
}

NodePtr makeEmptyRow()
{
    return makeNode(Node::Kind::Row);
}

QString greekName(const QString &name)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("alpha"), QStringLiteral("α")},
        {QStringLiteral("beta"), QStringLiteral("β")},
        {QStringLiteral("gamma"), QStringLiteral("γ")},
        {QStringLiteral("delta"), QStringLiteral("δ")},
        {QStringLiteral("epsilon"), QStringLiteral("ε")},
        {QStringLiteral("varepsilon"), QStringLiteral("ε")},
        {QStringLiteral("zeta"), QStringLiteral("ζ")},
        {QStringLiteral("eta"), QStringLiteral("η")},
        {QStringLiteral("theta"), QStringLiteral("θ")},
        {QStringLiteral("vartheta"), QStringLiteral("ϑ")},
        {QStringLiteral("iota"), QStringLiteral("ι")},
        {QStringLiteral("kappa"), QStringLiteral("κ")},
        {QStringLiteral("lambda"), QStringLiteral("λ")},
        {QStringLiteral("mu"), QStringLiteral("μ")},
        {QStringLiteral("nu"), QStringLiteral("ν")},
        {QStringLiteral("xi"), QStringLiteral("ξ")},
        {QStringLiteral("pi"), QStringLiteral("π")},
        {QStringLiteral("rho"), QStringLiteral("ρ")},
        {QStringLiteral("sigma"), QStringLiteral("σ")},
        {QStringLiteral("tau"), QStringLiteral("τ")},
        {QStringLiteral("upsilon"), QStringLiteral("υ")},
        {QStringLiteral("phi"), QStringLiteral("φ")},
        {QStringLiteral("varphi"), QStringLiteral("φ")},
        {QStringLiteral("chi"), QStringLiteral("χ")},
        {QStringLiteral("psi"), QStringLiteral("ψ")},
        {QStringLiteral("omega"), QStringLiteral("ω")},
        {QStringLiteral("Gamma"), QStringLiteral("Γ")},
        {QStringLiteral("Delta"), QStringLiteral("Δ")},
        {QStringLiteral("Theta"), QStringLiteral("Θ")},
        {QStringLiteral("Lambda"), QStringLiteral("Λ")},
        {QStringLiteral("Xi"), QStringLiteral("Ξ")},
        {QStringLiteral("Pi"), QStringLiteral("Π")},
        {QStringLiteral("Sigma"), QStringLiteral("Σ")},
        {QStringLiteral("Upsilon"), QStringLiteral("Υ")},
        {QStringLiteral("Phi"), QStringLiteral("Φ")},
        {QStringLiteral("Psi"), QStringLiteral("Ψ")},
        {QStringLiteral("Omega"), QStringLiteral("Ω")},
    };
    return map.value(name);
}

QString symbolName(const QString &name)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("infty"), QStringLiteral("∞")},
        {QStringLiteral("partial"), QStringLiteral("∂")},
        {QStringLiteral("nabla"), QStringLiteral("∇")},
        {QStringLiteral("pm"), QStringLiteral("±")},
        {QStringLiteral("mp"), QStringLiteral("∓")},
        {QStringLiteral("times"), QStringLiteral("×")},
        {QStringLiteral("cdot"), QStringLiteral("·")},
        {QStringLiteral("div"), QStringLiteral("÷")},
        {QStringLiteral("ast"), QStringLiteral("∗")},
        {QStringLiteral("star"), QStringLiteral("⋆")},
        {QStringLiteral("circ"), QStringLiteral("∘")},
        {QStringLiteral("bullet"), QStringLiteral("•")},
        {QStringLiteral("le"), QStringLiteral("≤")},
        {QStringLiteral("leq"), QStringLiteral("≤")},
        {QStringLiteral("ge"), QStringLiteral("≥")},
        {QStringLiteral("geq"), QStringLiteral("≥")},
        {QStringLiteral("ne"), QStringLiteral("≠")},
        {QStringLiteral("neq"), QStringLiteral("≠")},
        {QStringLiteral("approx"), QStringLiteral("≈")},
        {QStringLiteral("equiv"), QStringLiteral("≡")},
        {QStringLiteral("sim"), QStringLiteral("∼")},
        {QStringLiteral("propto"), QStringLiteral("∝")},
        {QStringLiteral("to"), QStringLiteral("→")},
        {QStringLiteral("rightarrow"), QStringLiteral("→")},
        {QStringLiteral("leftarrow"), QStringLiteral("←")},
        {QStringLiteral("Rightarrow"), QStringLiteral("⇒")},
        {QStringLiteral("Leftarrow"), QStringLiteral("⇐")},
        {QStringLiteral("Leftrightarrow"), QStringLiteral("⇔")},
        {QStringLiteral("mapsto"), QStringLiteral("↦")},
        {QStringLiteral("sum"), QStringLiteral("∑")},
        {QStringLiteral("prod"), QStringLiteral("∏")},
        {QStringLiteral("int"), QStringLiteral("∫")},
        {QStringLiteral("oint"), QStringLiteral("∮")},
        {QStringLiteral("sqrt"), QStringLiteral("√")},
        {QStringLiteral("ldots"), QStringLiteral("…")},
        {QStringLiteral("cdots"), QStringLiteral("⋯")},
        {QStringLiteral("dots"), QStringLiteral("…")},
        {QStringLiteral("angle"), QStringLiteral("∠")},
        {QStringLiteral("perp"), QStringLiteral("⊥")},
        {QStringLiteral("parallel"), QStringLiteral("∥")},
        {QStringLiteral("in"), QStringLiteral("∈")},
        {QStringLiteral("notin"), QStringLiteral("∉")},
        {QStringLiteral("subset"), QStringLiteral("⊂")},
        {QStringLiteral("supset"), QStringLiteral("⊃")},
        {QStringLiteral("cup"), QStringLiteral("∪")},
        {QStringLiteral("cap"), QStringLiteral("∩")},
        {QStringLiteral("forall"), QStringLiteral("∀")},
        {QStringLiteral("exists"), QStringLiteral("∃")},
        {QStringLiteral("emptyset"), QStringLiteral("∅")},
        {QStringLiteral("degree"), QStringLiteral("°")},
        {QStringLiteral("prime"), QStringLiteral("′")},
        {QStringLiteral("hbar"), QStringLiteral("ℏ")},
        {QStringLiteral("ell"), QStringLiteral("ℓ")},
        {QStringLiteral("Re"), QStringLiteral("ℜ")},
        {QStringLiteral("Im"), QStringLiteral("ℑ")},
        {QStringLiteral("aleph"), QStringLiteral("ℵ")},
        {QStringLiteral("therefore"), QStringLiteral("∴")},
        {QStringLiteral("because"), QStringLiteral("∵")},
    };
    return map.value(name);
}

QString mathOperatorName(const QString &name)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("sin"), QStringLiteral("sin")},
        {QStringLiteral("cos"), QStringLiteral("cos")},
        {QStringLiteral("tan"), QStringLiteral("tan")},
        {QStringLiteral("cot"), QStringLiteral("cot")},
        {QStringLiteral("sec"), QStringLiteral("sec")},
        {QStringLiteral("csc"), QStringLiteral("csc")},
        {QStringLiteral("arcsin"), QStringLiteral("arcsin")},
        {QStringLiteral("arccos"), QStringLiteral("arccos")},
        {QStringLiteral("arctan"), QStringLiteral("arctan")},
        {QStringLiteral("sinh"), QStringLiteral("sinh")},
        {QStringLiteral("cosh"), QStringLiteral("cosh")},
        {QStringLiteral("tanh"), QStringLiteral("tanh")},
        {QStringLiteral("log"), QStringLiteral("log")},
        {QStringLiteral("ln"), QStringLiteral("ln")},
        {QStringLiteral("exp"), QStringLiteral("exp")},
        {QStringLiteral("lim"), QStringLiteral("lim")},
        {QStringLiteral("max"), QStringLiteral("max")},
        {QStringLiteral("min"), QStringLiteral("min")},
        {QStringLiteral("sup"), QStringLiteral("sup")},
        {QStringLiteral("inf"), QStringLiteral("inf")},
        {QStringLiteral("det"), QStringLiteral("det")},
        {QStringLiteral("dim"), QStringLiteral("dim")},
        {QStringLiteral("ker"), QStringLiteral("ker")},
        {QStringLiteral("arg"), QStringLiteral("arg")},
        {QStringLiteral("mod"), QStringLiteral("mod")},
    };
    return map.value(name);
}

void setStyle(Node *node, bool italic, bool bold)
{
    if (!node)
        return;
    if (node->kind == Node::Kind::Glyph) {
        node->italic = italic;
        node->bold = bold;
    }
    for (auto &child : node->children)
        setStyle(child.get(), italic, bold);
    setStyle(node->base.get(), italic, bold);
    setStyle(node->numerator.get(), italic, bold);
    setStyle(node->denominator.get(), italic, bold);
    setStyle(node->radicand.get(), italic, bold);
    setStyle(node->superscript.get(), italic, bold);
    setStyle(node->subscript.get(), italic, bold);
}

void applyBlackboard(Node *node)
{
    if (!node)
        return;
    if (node->kind == Node::Kind::Glyph && node->text.size() == 1) {
        static const QHash<QChar, QChar> map = {
            {u'R', QChar(0x211D)}, {u'N', QChar(0x2115)}, {u'Z', QChar(0x2124)},
            {u'Q', QChar(0x211A)}, {u'C', QChar(0x2102)}, {u'P', QChar(0x2119)},
        };
        node->text = QString(map.value(node->text.at(0), node->text.at(0)));
    }
    for (auto &child : node->children)
        applyBlackboard(child.get());
    applyBlackboard(node->base.get());
    applyBlackboard(node->numerator.get());
    applyBlackboard(node->denominator.get());
    applyBlackboard(node->radicand.get());
    applyBlackboard(node->superscript.get());
    applyBlackboard(node->subscript.get());
}

class Parser
{
public:
    explicit Parser(const QString &source)
        : m_source(source)
    {
        m_source.remove(QLatin1Char('\r'));
    }

    NodePtr parse()
    {
        NodePtr row = parseRow(QChar());
        return row;
    }

private:
    QChar peek() const
    {
        return m_pos < m_source.size() ? m_source.at(m_pos) : QChar();
    }

    void skipSpaces()
    {
        while (m_pos < m_source.size() && m_source.at(m_pos).isSpace())
            ++m_pos;
    }

    NodePtr parseRow(QChar end)
    {
        NodePtr row = makeNode(Node::Kind::Row);
        while (m_pos < m_source.size()) {
            const QChar c = m_source.at(m_pos);
            if (!end.isNull() && c == end)
                break;
            if (c == QLatin1Char('}'))
                break;
            if (c.isSpace()) {
                ++m_pos;
                continue;
            }
            NodePtr atom = parseScripted();
            if (!atom) {
                ++m_pos;
                continue;
            }
            row->children.push_back(std::move(atom));
        }
        return row;
    }

    NodePtr parseScripted()
    {
        NodePtr base = parseAtom();
        if (!base)
            return nullptr;

        while (true) {
            const int saved = m_pos;
            skipSpaces();
            const QChar c = peek();
            if (c != QLatin1Char('^') && c != QLatin1Char('_')) {
                m_pos = saved;
                break;
            }
            const bool superscript = c == QLatin1Char('^');
            ++m_pos;
            NodePtr script = parseArg();
            if (!script)
                script = makeEmptyRow();

            NodePtr *existingSlot = nullptr;
            if (base->kind == Node::Kind::Script) {
                existingSlot = superscript ? &base->superscript : &base->subscript;
            }
            if (existingSlot && !*existingSlot) {
                *existingSlot = std::move(script);
                continue;
            }

            NodePtr scripted = makeNode(Node::Kind::Script);
            scripted->base = std::move(base);
            if (superscript)
                scripted->superscript = std::move(script);
            else
                scripted->subscript = std::move(script);
            base = std::move(scripted);
        }
        return base;
    }

    NodePtr parseAtom()
    {
        skipSpaces();
        if (m_pos >= m_source.size())
            return nullptr;
        const QChar c = m_source.at(m_pos);
        if (c == QLatin1Char('}') || c == QLatin1Char('^') || c == QLatin1Char('_'))
            return nullptr;
        if (c == QLatin1Char('{')) {
            ++m_pos;
            NodePtr row = parseRow(QLatin1Char('}'));
            if (peek() == QLatin1Char('}'))
                ++m_pos;
            return row;
        }
        if (c == QLatin1Char('\\')) {
            ++m_pos;
            return parseCommand();
        }
        if (c == QLatin1Char('&')) {
            ++m_pos;
            return makeSpace(0.2);
        }
        ++m_pos;

        QString text(c);
        bool italic = false;
        if (c == QLatin1Char('-'))
            text = QStringLiteral("−");
        else if (c == QLatin1Char('*'))
            text = QStringLiteral("∗");
        else if (c == QLatin1Char('\''))
            text = QStringLiteral("′");
        else if (c.isLetter()) {
            italic = c.isLower();
        }
        if (text == QStringLiteral("~"))
            return makeSpace(0.3);
        return makeGlyph(text, italic);
    }

    NodePtr parseArg()
    {
        skipSpaces();
        if (m_pos >= m_source.size())
            return makeEmptyRow();
        if (peek() == QLatin1Char('{')) {
            ++m_pos;
            NodePtr row = parseRow(QLatin1Char('}'));
            if (peek() == QLatin1Char('}'))
                ++m_pos;
            return row;
        }
        return parseAtom();
    }

    QString readGroupRaw()
    {
        skipSpaces();
        if (peek() != QLatin1Char('{'))
            return QString();
        ++m_pos;
        int depth = 1;
        QString result;
        while (m_pos < m_source.size()) {
            const QChar c = m_source.at(m_pos);
            if (c == QLatin1Char('\\') && m_pos + 1 < m_source.size()) {
                result.append(c);
                result.append(m_source.at(m_pos + 1));
                m_pos += 2;
                continue;
            }
            if (c == QLatin1Char('{')) {
                ++depth;
                ++m_pos;
                result.append(c);
                continue;
            }
            if (c == QLatin1Char('}')) {
                --depth;
                ++m_pos;
                if (depth == 0)
                    break;
                result.append(c);
                continue;
            }
            result.append(c);
            ++m_pos;
        }
        return result;
    }

    NodePtr parseDelimiter()
    {
        skipSpaces();
        if (m_pos >= m_source.size())
            return nullptr;
        if (peek() == QLatin1Char('.')) {
            ++m_pos;
            return nullptr;
        }
        if (peek() == QLatin1Char('\\')) {
            ++m_pos;
            if (m_pos >= m_source.size())
                return nullptr;
            if (peek() == QLatin1Char('{') || peek() == QLatin1Char('}')
                || peek() == QLatin1Char('|')) {
                const QChar c = m_source.at(m_pos++);
                return makeGlyph(QString(c), false);
            }
            int start = m_pos;
            while (m_pos < m_source.size() && m_source.at(m_pos).isLetter())
                ++m_pos;
            const QString name = m_source.mid(start, m_pos - start);
            if (name == QStringLiteral("langle"))
                return makeGlyph(QStringLiteral("⟨"), false);
            if (name == QStringLiteral("rangle"))
                return makeGlyph(QStringLiteral("⟩"), false);
            if (name == QStringLiteral("vert") || name == QStringLiteral("mid"))
                return makeGlyph(QStringLiteral("|"), false);
            if (name == QStringLiteral("lfloor"))
                return makeGlyph(QStringLiteral("⌊"), false);
            if (name == QStringLiteral("rfloor"))
                return makeGlyph(QStringLiteral("⌋"), false);
            if (name == QStringLiteral("lceil"))
                return makeGlyph(QStringLiteral("⌈"), false);
            if (name == QStringLiteral("rceil"))
                return makeGlyph(QStringLiteral("⌉"), false);
            return makeGlyph(QStringLiteral("\\") + name, false);
        }
        const QChar c = m_source.at(m_pos++);
        return makeGlyph(QString(c), false);
    }

    NodePtr parseCommand()
    {
        if (m_pos >= m_source.size())
            return nullptr;

        const QChar first = peek();
        if (!first.isLetter()) {
            if (first.isSpace()) {
                ++m_pos;
                return makeSpace(0.3);
            }
            ++m_pos;
            if (first == QLatin1Char('{') || first == QLatin1Char('}')
                || first == QLatin1Char('%') || first == QLatin1Char('&')
                || first == QLatin1Char('_') || first == QLatin1Char('#'))
                return makeGlyph(QString(first), false);
            if (first == QLatin1Char(','))
                return makeSpace(0.17);
            if (first == QLatin1Char(';'))
                return makeSpace(0.28);
            if (first == QLatin1Char(':'))
                return makeSpace(0.22);
            if (first == QLatin1Char('!'))
                return makeSpace(0.0);
            if (first == QLatin1Char('\\'))
                return makeSpace(0.0);
            return makeGlyph(QString(first), false);
        }

        int start = m_pos;
        while (m_pos < m_source.size() && m_source.at(m_pos).isLetter())
            ++m_pos;
        const QString name = m_source.mid(start, m_pos - start);

        if (name == QStringLiteral("frac") || name == QStringLiteral("dfrac")
            || name == QStringLiteral("tfrac")) {
            NodePtr numerator = parseArg();
            NodePtr denominator = parseArg();
            NodePtr fraction = makeNode(Node::Kind::Fraction);
            fraction->numerator = std::move(numerator);
            fraction->denominator = std::move(denominator);
            return fraction;
        }

        if (name == QStringLiteral("sqrt")) {
            skipSpaces();
            if (peek() == QLatin1Char('[')) {
                while (m_pos < m_source.size() && m_source.at(m_pos) != QLatin1Char(']'))
                    ++m_pos;
                if (peek() == QLatin1Char(']'))
                    ++m_pos;
            }
            NodePtr radicand = parseArg();
            NodePtr root = makeNode(Node::Kind::Sqrt);
            root->radicand = std::move(radicand);
            return root;
        }

        if (name == QStringLiteral("text") || name == QStringLiteral("textrm")
            || name == QStringLiteral("mbox")) {
            const QString text = readGroupRaw();
            if (text.isEmpty())
                return makeEmptyRow();
            NodePtr glyph = makeGlyph(text, false);
            return glyph;
        }

        if (name == QStringLiteral("mathrm") || name == QStringLiteral("operatorname")
            || name == QStringLiteral("boldsymbol")) {
            NodePtr content = parseArg();
            setStyle(content.get(), false, name == QStringLiteral("boldsymbol"));
            return content;
        }

        if (name == QStringLiteral("mathbf")) {
            NodePtr content = parseArg();
            setStyle(content.get(), false, true);
            return content;
        }

        if (name == QStringLiteral("mathit")) {
            NodePtr content = parseArg();
            for (auto &child : content->children)
                setStyle(child.get(), true, false);
            return content;
        }

        if (name == QStringLiteral("mathbb")) {
            NodePtr content = parseArg();
            setStyle(content.get(), false, false);
            applyBlackboard(content.get());
            return content;
        }

        if (name == QStringLiteral("left") || name == QStringLiteral("right")) {
            NodePtr delimiter = parseDelimiter();
            return delimiter ? std::move(delimiter) : makeEmptyRow();
        }

        if (name == QStringLiteral("begin") || name == QStringLiteral("end")) {
            readGroupRaw();
            return makeEmptyRow();
        }

        if (name == QStringLiteral("dot") || name == QStringLiteral("ddot")
            || name == QStringLiteral("bar") || name == QStringLiteral("hat")
            || name == QStringLiteral("tilde") || name == QStringLiteral("vec")
            || name == QStringLiteral("overline")) {
            NodePtr base = parseArg();
            NodePtr accent = makeNode(Node::Kind::Accent);
            accent->text = name == QStringLiteral("overline") ? QStringLiteral("bar") : name;
            accent->base = std::move(base);
            return accent;
        }

        if (name == QStringLiteral("quad"))
            return makeSpace(1.0);
        if (name == QStringLiteral("qquad"))
            return makeSpace(2.0);
        if (name == QStringLiteral("displaystyle") || name == QStringLiteral("textstyle")
            || name == QStringLiteral("scriptstyle") || name == QStringLiteral("limits")
            || name == QStringLiteral("nolimits") || name == QStringLiteral("mathstrut")) {
            return makeEmptyRow();
        }

        if (name == QStringLiteral("label") || name == QStringLiteral("tag")
            || name == QStringLiteral("color")) {
            readGroupRaw();
            return makeEmptyRow();
        }

        if (const QString greek = greekName(name); !greek.isEmpty())
            return makeGlyph(greek, name.front().isLower());

        if (const QString symbol = symbolName(name); !symbol.isEmpty())
            return makeGlyph(symbol, false);

        if (const QString op = mathOperatorName(name); !op.isEmpty()) {
            NodePtr glyph = makeGlyph(op, false);
            return glyph;
        }

        return makeGlyph(QStringLiteral("\\") + name, false);
    }

    QString m_source;
    int m_pos = 0;
};

struct DrawOp
{
    enum class Type { Text, Line, Ellipse };
    Type type = Type::Text;
    QPointF p1;
    QPointF p2;
    QString text;
    QFont font;
    qreal strokeWidth = 0.0;
    qreal radius = 0.0;
};

struct Measured
{
    qreal width = 0.0;
    qreal ascent = 0.0;
    qreal descent = 0.0;
    QVector<DrawOp> ops;
};

QFont mathFont(bool italic, bool bold, qreal pixelSize)
{
    QFont font(QStringLiteral("DejaVu Serif"));
    font.setStyleHint(QFont::Serif);
    font.setPixelSize(qMax(1, qRound(pixelSize)));
    font.setItalic(italic);
    font.setBold(bold);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

void appendOps(Measured &target, const Measured &source, const QPointF &offset)
{
    for (DrawOp op : source.ops) {
        op.p1 += offset;
        op.p2 += offset;
        target.ops.append(op);
    }
}

Measured measureNode(const Node &node, qreal pixelSize)
{
    Measured result;
    switch (node.kind) {
    case Node::Kind::Row: {
        qreal x = 0.0;
        for (const auto &child : node.children) {
            const Measured part = measureNode(*child, pixelSize);
            appendOps(result, part, QPointF(x, 0.0));
            x += part.width;
            result.ascent = qMax(result.ascent, part.ascent);
            result.descent = qMax(result.descent, part.descent);
        }
        result.width = x;
        break;
    }
    case Node::Kind::Glyph: {
        const QFont font = mathFont(node.italic, node.bold, pixelSize);
        const QFontMetricsF metrics(font);
        result.width = metrics.horizontalAdvance(node.text);
        result.ascent = metrics.ascent();
        result.descent = metrics.descent();
        DrawOp op;
        op.type = DrawOp::Type::Text;
        op.p1 = QPointF(0.0, 0.0);
        op.text = node.text;
        op.font = font;
        result.ops.append(op);
        break;
    }
    case Node::Kind::Fraction: {
        const qreal childSize = pixelSize * 0.76;
        const Measured numerator = measureNode(*node.numerator, childSize);
        const Measured denominator = measureNode(*node.denominator, childSize);
        const qreal gap = pixelSize * 0.08;
        const qreal barY = -pixelSize * 0.28;
        const qreal barThickness = qMax(1.0, pixelSize * 0.035);
        const qreal numeratorBaseline = barY - gap - numerator.descent;
        const qreal denominatorBaseline = barY + gap + denominator.ascent;
        result.width = qMax(numerator.width, denominator.width) + pixelSize * 0.12;
        result.ascent = -(numeratorBaseline - numerator.ascent);
        result.descent = denominatorBaseline + denominator.descent;
        appendOps(result, numerator, QPointF((result.width - numerator.width) / 2.0,
                                             numeratorBaseline));
        appendOps(result, denominator, QPointF((result.width - denominator.width) / 2.0,
                                               denominatorBaseline));
        DrawOp bar;
        bar.type = DrawOp::Type::Line;
        bar.p1 = QPointF(pixelSize * 0.04, barY);
        bar.p2 = QPointF(result.width - pixelSize * 0.04, barY);
        bar.strokeWidth = barThickness;
        result.ops.append(bar);
        break;
    }
    case Node::Kind::Script: {
        const Measured base = measureNode(*node.base, pixelSize);
        const qreal scriptSize = pixelSize * 0.6;
        const bool limits = node.base->kind == Node::Kind::Glyph
            && (node.base->text == QStringLiteral("∑") || node.base->text == QStringLiteral("∏")
                || node.base->text == QStringLiteral("⋃") || node.base->text == QStringLiteral("⋂")
                || node.base->text == QStringLiteral("∐"));

        if (limits) {
            const Measured sup = node.superscript ? measureNode(*node.superscript, scriptSize) : Measured();
            const Measured sub = node.subscript ? measureNode(*node.subscript, scriptSize) : Measured();
            const qreal gap = pixelSize * 0.05;
            result.width = qMax(base.width, qMax(sup.width, sub.width));
            result.ascent = base.ascent;
            result.descent = base.descent;

            appendOps(result, base, QPointF((result.width - base.width) / 2.0, 0.0));
            if (node.superscript) {
                const qreal baseline = -(base.ascent + gap + sup.descent);
                appendOps(result, sup, QPointF((result.width - sup.width) / 2.0, baseline));
                result.ascent = qMax(result.ascent, -baseline + sup.ascent);
            }
            if (node.subscript) {
                const qreal baseline = base.descent + gap + sub.ascent;
                appendOps(result, sub, QPointF((result.width - sub.width) / 2.0, baseline));
                result.descent = qMax(result.descent, baseline + sub.descent);
            }
            break;
        }

        result.width = base.width;
        result.ascent = base.ascent;
        result.descent = base.descent;
        appendOps(result, base, QPointF(0.0, 0.0));

        const qreal scriptX = base.width + pixelSize * 0.04;
        if (node.superscript) {
            const Measured sup = measureNode(*node.superscript, scriptSize);
            const qreal baseline = -pixelSize * 0.42;
            appendOps(result, sup, QPointF(scriptX, baseline));
            result.width = qMax(result.width, scriptX + sup.width);
            result.ascent = qMax(result.ascent, -baseline + sup.ascent);
        }
        if (node.subscript) {
            const Measured sub = measureNode(*node.subscript, scriptSize);
            const qreal baseline = pixelSize * 0.2;
            appendOps(result, sub, QPointF(scriptX, baseline));
            result.width = qMax(result.width, scriptX + sub.width);
            result.descent = qMax(result.descent, baseline + sub.descent);
        }
        break;
    }
    case Node::Kind::Sqrt: {
        const Measured content = measureNode(*node.radicand, pixelSize);
        const QFont radicalFont = mathFont(false, false, pixelSize * 1.18);
        const QFontMetricsF radicalMetrics(radicalFont);
        const qreal radicalWidth = radicalMetrics.horizontalAdvance(QStringLiteral("√"));
        const qreal pad = pixelSize * 0.06;
        result.width = radicalWidth + content.width + pad;
        result.ascent = qMax(content.ascent + pixelSize * 0.12, radicalMetrics.ascent());
        result.descent = content.descent;
        appendOps(result, content, QPointF(radicalWidth * 0.88, 0.0));
        DrawOp radical;
        radical.type = DrawOp::Type::Text;
        radical.p1 = QPointF(0.0, 0.0);
        radical.text = QStringLiteral("√");
        radical.font = radicalFont;
        result.ops.append(radical);
        DrawOp overline;
        overline.type = DrawOp::Type::Line;
        overline.p1 = QPointF(radicalWidth * 0.8, -content.ascent - pixelSize * 0.06);
        overline.p2 = QPointF(result.width, -content.ascent - pixelSize * 0.06);
        overline.strokeWidth = qMax(1.0, pixelSize * 0.035);
        result.ops.append(overline);
        break;
    }
    case Node::Kind::Accent: {
        const Measured base = measureNode(*node.base, pixelSize);
        const qreal gap = pixelSize * 0.06;
        const qreal center = base.width / 2.0;
        result.width = base.width;
        result.ascent = base.ascent;
        result.descent = base.descent;
        appendOps(result, base, QPointF(0.0, 0.0));

        auto addLine = [&](const QPointF &a, const QPointF &b, qreal width) {
            DrawOp op;
            op.type = DrawOp::Type::Line;
            op.p1 = a;
            op.p2 = b;
            op.strokeWidth = width;
            result.ops.append(op);
        };
        auto addDot = [&](qreal x, qreal y, qreal radius) {
            DrawOp op;
            op.type = DrawOp::Type::Ellipse;
            op.p1 = QPointF(x, y);
            op.radius = radius;
            result.ops.append(op);
        };

        if (node.text == QStringLiteral("dot") || node.text == QStringLiteral("ddot")) {
            const qreal radius = pixelSize * 0.055;
            const qreal y = -(base.ascent + gap + radius * 1.4);
            const qreal offset = pixelSize * 0.09;
            if (node.text == QStringLiteral("ddot")) {
                addDot(center - offset, y, radius);
                addDot(center + offset, y, radius);
            } else {
                addDot(center, y, radius);
            }
            result.ascent = base.ascent + gap + radius * 2.8;
        } else if (node.text == QStringLiteral("bar")) {
            const qreal thickness = qMax(1.0, pixelSize * 0.045);
            const qreal y = -(base.ascent + gap);
            addLine(QPointF(center - base.width * 0.42, y),
                    QPointF(center + base.width * 0.42, y), thickness);
            result.ascent = base.ascent + gap + thickness;
        } else if (node.text == QStringLiteral("hat")) {
            const qreal thickness = qMax(1.0, pixelSize * 0.04);
            const qreal y = -(base.ascent + gap);
            const qreal half = pixelSize * 0.13;
            const qreal peak = y - pixelSize * 0.14;
            addLine(QPointF(center - half, y), QPointF(center, peak), thickness);
            addLine(QPointF(center, peak), QPointF(center + half, y), thickness);
            result.ascent = base.ascent + gap + pixelSize * 0.14;
        } else if (node.text == QStringLiteral("vec")) {
            const qreal thickness = qMax(1.0, pixelSize * 0.04);
            const qreal y = -(base.ascent + gap);
            const qreal left = center - base.width * 0.4;
            const qreal right = center + base.width * 0.4;
            addLine(QPointF(left, y), QPointF(right, y), thickness);
            addLine(QPointF(right, y), QPointF(right - pixelSize * 0.1, y - pixelSize * 0.06), thickness);
            addLine(QPointF(right, y), QPointF(right - pixelSize * 0.1, y + pixelSize * 0.06), thickness);
            result.ascent = base.ascent + gap + pixelSize * 0.07;
        } else {
            const QFont accentFont = mathFont(false, false, pixelSize * 0.9);
            const QFontMetricsF metrics(accentFont);
            const QString accentText = QStringLiteral("˜");
            const qreal accentWidth = metrics.horizontalAdvance(accentText);
            DrawOp accent;
            accent.type = DrawOp::Type::Text;
            accent.p1 = QPointF((result.width - accentWidth) / 2.0,
                                -(base.ascent + gap + metrics.ascent() * 0.2));
            accent.text = accentText;
            accent.font = accentFont;
            result.ops.append(accent);
            result.ascent = base.ascent + gap + metrics.ascent() * 0.35;
        }
        break;
    }
    case Node::Kind::Space:
        result.width = node.space * pixelSize;
        break;
    }
    return result;
}

} // namespace

QImage MathRenderer::render(const QString &latex, bool displayMode, qreal supersample)
{
    QString expression = latex.trimmed();
    if (expression.startsWith(QStringLiteral("$$")) && expression.endsWith(QStringLiteral("$$"))
        && expression.size() >= 4) {
        expression = expression.mid(2, expression.size() - 4).trimmed();
    } else if (expression.startsWith(QLatin1Char('$')) && expression.endsWith(QLatin1Char('$'))
               && expression.size() >= 2) {
        expression = expression.mid(1, expression.size() - 2).trimmed();
    }
    if (expression.isEmpty())
        return QImage();

    Parser parser(expression);
    NodePtr root = parser.parse();
    if (!root)
        return QImage();

    const qreal basePixels = (displayMode ? 22.0 : 15.0) * supersample;
    const Measured measured = measureNode(*root, basePixels);
    if (measured.width <= 0.0 || !std::isfinite(measured.width) || !std::isfinite(measured.ascent)
        || !std::isfinite(measured.descent)) {
        return QImage();
    }

    const int padding = qMax(2, qRound(2.0 * supersample));
    const int width = qCeil(measured.width) + padding * 2;
    const int height = qCeil(measured.ascent + measured.descent) + padding * 2;
    if (width <= 0 || height <= 0 || width > 8192 || height > 4096)
        return QImage();

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(padding, padding + measured.ascent);
    painter.setPen(QColor(QStringLiteral("#20242b")));
    painter.setBrush(Qt::NoBrush);

    const QColor ink(QStringLiteral("#20242b"));
    for (const DrawOp &op : measured.ops) {
        if (op.type == DrawOp::Type::Text) {
            painter.setFont(op.font);
            painter.setPen(ink);
            painter.drawText(op.p1, op.text);
        } else if (op.type == DrawOp::Type::Line) {
            QPen pen(ink);
            pen.setWidthF(op.strokeWidth > 0.0 ? op.strokeWidth
                                               : qMax(1.0, op.font.pixelSize() * 0.7));
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.drawLine(op.p1, op.p2);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawEllipse(op.p1, op.radius, op.radius);
            painter.setBrush(Qt::NoBrush);
        }
    }
    painter.end();
    return image;
}

} // namespace pano
