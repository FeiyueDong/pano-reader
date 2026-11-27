#pragma once

#include <QHash>
#include <QString>

#include "annotation/Annotation.h"

namespace pano {

// Stable, tool-id based presets.  These are deliberately independent from the
// annotation type: an existing annotation never changes when a preset changes.
class ToolPresets
{
public:
    ToolPresets();
    ~ToolPresets();

    static QStringList toolIds();
    static QString displayName(const QString &toolId);
    static AnnotationStyle defaultStyle(const QString &toolId);
    static bool isValidToolId(const QString &toolId);

    AnnotationStyle style(const QString &toolId) const;
    void setStyle(const QString &toolId, const AnnotationStyle &style);
    void reset(const QString &toolId);
    void resetAll();

    bool load(QString *warningMessage = nullptr);
    bool save(QString *errorMessage = nullptr) const;
    static QString filePath();

private:
    QHash<QString, AnnotationStyle> m_styles;
};

} // namespace pano
