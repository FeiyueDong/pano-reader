#include "AnnotationCommands.h"

#include "AnnotationModel.h"

#include <algorithm>

namespace pano {

AddAnnotationCommand::AddAnnotationCommand(AnnotationModel *model, const Annotation &annotation,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_annotation(annotation)
    , m_index(model ? model->count() : 0)
{
    setText(QStringLiteral("添加%1").arg(annotationTypeDisplayName(annotation.type)));
}

void AddAnnotationCommand::undo()
{
    m_model->removeById(m_annotation.id);
}

void AddAnnotationCommand::redo()
{
    m_model->insertAt(m_index, m_annotation);
}

RemoveAnnotationsCommand::RemoveAnnotationsCommand(AnnotationModel *model,
                                                   const QVector<Annotation> &annotations,
                                                   QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
{
    for (const Annotation &annotation : annotations) {
        const int index = model ? model->indexOf(annotation.id) : -1;
        if (index >= 0)
            m_entries.append(Entry{index, annotation});
    }
    std::sort(m_entries.begin(), m_entries.end(),
              [](const Entry &lhs, const Entry &rhs) { return lhs.index < rhs.index; });

    if (m_entries.size() == 1) {
        setText(QStringLiteral("删除%1").arg(annotationTypeDisplayName(m_entries.first().annotation.type)));
    } else {
        setText(QStringLiteral("删除 %1 个标注").arg(m_entries.size()));
    }
}

void RemoveAnnotationsCommand::undo()
{
    for (const Entry &entry : m_entries)
        m_model->insertAt(entry.index, entry.annotation);
}

void RemoveAnnotationsCommand::redo()
{
    for (const Entry &entry : m_entries)
        m_model->removeById(entry.annotation.id);
}

ModifyAnnotationCommand::ModifyAnnotationCommand(AnnotationModel *model, const Annotation &before,
                                                 const Annotation &after, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_before(before)
    , m_after(after)
{
    setText(QStringLiteral("修改%1").arg(annotationTypeDisplayName(after.type)));
}

void ModifyAnnotationCommand::undo()
{
    m_model->replace(m_before);
}

void ModifyAnnotationCommand::redo()
{
    m_model->replace(m_after);
}

} // namespace pano
