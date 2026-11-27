#include "AnnotationModel.h"

namespace pano {

AnnotationModel::AnnotationModel(QObject *parent)
    : QObject(parent)
{
}

int AnnotationModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations.at(i).id == id)
            return i;
    }
    return -1;
}

const Annotation *AnnotationModel::find(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? &m_annotations.at(index) : nullptr;
}

Annotation *AnnotationModel::find(const QString &id)
{
    const int index = indexOf(id);
    return index >= 0 ? &m_annotations[index] : nullptr;
}

void AnnotationModel::clear()
{
    if (m_annotations.isEmpty())
        return;
    m_annotations.clear();
    emit annotationsReset();
    emit structureChanged();
}

void AnnotationModel::setAnnotations(const QVector<Annotation> &annotations)
{
    m_annotations = annotations;
    emit annotationsReset();
    emit structureChanged();
}

bool AnnotationModel::insertAt(int index, const Annotation &annotation)
{
    if (annotation.id.isEmpty() || indexOf(annotation.id) >= 0)
        return false;
    index = qBound(0, index, m_annotations.size());
    m_annotations.insert(index, annotation);
    emit structureChanged();
    return true;
}

bool AnnotationModel::removeById(const QString &id, Annotation *removed, int *index)
{
    const int position = indexOf(id);
    if (position < 0)
        return false;
    if (removed)
        *removed = m_annotations.at(position);
    if (index)
        *index = position;
    m_annotations.removeAt(position);
    emit structureChanged();
    return true;
}

bool AnnotationModel::replace(const Annotation &annotation)
{
    const int position = indexOf(annotation.id);
    if (position < 0)
        return false;
    m_annotations[position] = annotation;
    emit annotationUpdated(annotation.id);
    return true;
}

} // namespace pano
