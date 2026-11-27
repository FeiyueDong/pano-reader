#pragma once

#include "Annotation.h"

#include <QPair>
#include <QUndoCommand>
#include <QVector>

namespace pano {

class AnnotationModel;

class AddAnnotationCommand : public QUndoCommand
{
public:
    AddAnnotationCommand(AnnotationModel *model, const Annotation &annotation,
                         QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    AnnotationModel *m_model;
    Annotation m_annotation;
    int m_index = 0;
};

class RemoveAnnotationsCommand : public QUndoCommand
{
public:
    RemoveAnnotationsCommand(AnnotationModel *model, const QVector<Annotation> &annotations,
                             QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    struct Entry {
        int index = 0;
        Annotation annotation;
    };

    AnnotationModel *m_model;
    QVector<Entry> m_entries;
};

class ModifyAnnotationCommand : public QUndoCommand
{
public:
    ModifyAnnotationCommand(AnnotationModel *model, const Annotation &before,
                            const Annotation &after, QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    AnnotationModel *m_model;
    Annotation m_before;
    Annotation m_after;
};

} // namespace pano
