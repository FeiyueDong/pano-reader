#include "note/NoteModel.h"

#include <QSet>

namespace pano {

int NoteModel::indexOf(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).id == id)
            return i;
    }
    return -1;
}

const Note *NoteModel::find(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? &m_notes.at(index) : nullptr;
}

Note *NoteModel::find(const QString &id)
{
    const int index = indexOf(id);
    return index >= 0 ? &m_notes[index] : nullptr;
}

const Note *NoteModel::forAnnotation(const QString &annotationId) const
{
    if (annotationId.isEmpty())
        return nullptr;
    for (const Note &note : m_notes) {
        if (note.annotationId == annotationId)
            return &note;
    }
    return nullptr;
}

Note *NoteModel::forAnnotation(const QString &annotationId)
{
    if (annotationId.isEmpty())
        return nullptr;
    for (Note &note : m_notes) {
        if (note.annotationId == annotationId)
            return &note;
    }
    return nullptr;
}

bool NoteModel::insert(const Note &note, QString *error)
{
    if (note.id.isEmpty()) {
        if (error)
            *error = QStringLiteral("笔记缺少 id。");
        return false;
    }
    if (indexOf(note.id) >= 0) {
        if (error)
            *error = QStringLiteral("笔记 id 重复：%1").arg(note.id);
        return false;
    }
    if (!note.annotationId.isEmpty()) {
        for (const Note &other : m_notes) {
            if (other.annotationId == note.annotationId) {
                if (error)
                    *error = QStringLiteral("一个标注只能关联一篇笔记。");
                return false;
            }
        }
    }
    m_notes.append(note);
    return true;
}

bool NoteModel::replace(const QString &id, const Note &note, QString *error)
{
    const int index = indexOf(id);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("笔记不存在：%1").arg(id);
        return false;
    }
    if (note.id != id) {
        if (error)
            *error = QStringLiteral("不能修改笔记 id。");
        return false;
    }
    if (!note.annotationId.isEmpty()) {
        for (int i = 0; i < m_notes.size(); ++i) {
            if (i != index && m_notes.at(i).annotationId == note.annotationId) {
                if (error)
                    *error = QStringLiteral("一个标注只能关联一篇笔记。");
                return false;
            }
        }
    }
    m_notes[index] = note;
    return true;
}

bool NoteModel::remove(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    m_notes.removeAt(index);
    return true;
}

bool NoteModel::setNotes(const QVector<Note> &notes, QString *error)
{
    QSet<QString> ids;
    QSet<QString> annotations;
    for (const Note &note : notes) {
        if (note.id.isEmpty() || ids.contains(note.id)) {
            if (error)
                *error = QStringLiteral("笔记 id 为空或重复：%1").arg(note.id);
            return false;
        }
        if (!note.annotationId.isEmpty() && annotations.contains(note.annotationId)) {
            if (error)
                *error = QStringLiteral("标注 %1 关联了多篇笔记。").arg(note.annotationId);
            return false;
        }
        ids.insert(note.id);
        if (!note.annotationId.isEmpty())
            annotations.insert(note.annotationId);
    }
    m_notes = notes;
    return true;
}

void NoteModel::reindex()
{
    // Notes are stored in creation order.  No sort index is persisted.
}

} // namespace pano
