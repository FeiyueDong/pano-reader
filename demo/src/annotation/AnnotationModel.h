#pragma once

#include "Annotation.h"

#include <QObject>
#include <QVector>

namespace pano {

// Flat, creation-ordered annotation collection. The model deliberately has no
// document/back-end dependency so that it can be serialized and undone
// independently of the PDF renderer.
class AnnotationModel : public QObject
{
    Q_OBJECT

public:
    explicit AnnotationModel(QObject *parent = nullptr);

    const QVector<Annotation> &annotations() const { return m_annotations; }
    int count() const { return m_annotations.size(); }
    int indexOf(const QString &id) const;
    const Annotation *find(const QString &id) const;
    Annotation *find(const QString &id);

    void clear();
    void setAnnotations(const QVector<Annotation> &annotations);

    bool insertAt(int index, const Annotation &annotation);
    bool removeById(const QString &id, Annotation *removed = nullptr, int *index = nullptr);
    bool replace(const Annotation &annotation);

signals:
    void structureChanged();
    void annotationUpdated(const QString &id);
    void annotationsReset();

private:
    QVector<Annotation> m_annotations;
};

} // namespace pano
