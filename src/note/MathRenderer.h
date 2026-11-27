#pragma once

#include <QImage>
#include <QString>

namespace pano {

// A deliberately small, offline TeX subset renderer used by the Markdown
// preview. It covers the representative formulas needed by the demo:
// fractions, roots, scripts, accents, Greek letters and common operators.
// It is not a general TeX engine and never executes scripts or fetches fonts.
class MathRenderer
{
public:
    static QImage render(const QString &latex, bool displayMode, qreal supersample = 2.0);
    static qreal defaultSupersample() { return 2.0; }
};

} // namespace pano
