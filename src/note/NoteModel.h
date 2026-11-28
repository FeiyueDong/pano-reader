#pragma once

#include <QString>
#include <QVector>

#include "note/Note.h"

namespace pano {

class NoteModel
{
public:
    void clear() { m_notes.clear(); }
    const QVector<Note> &notes() const { return m_notes; }
    QVector<Note> &notes() { return m_notes; }
    int size() const { return m_notes.size(); }
    bool isEmpty() const { return m_notes.isEmpty(); }

    int indexOf(const QString &id) const;
    const Note *find(const QString &id) const;
    Note *find(const QString &id);
    const Note *forAnnotation(const QString &annotationId) const;
    Note *forAnnotation(const QString &annotationId);
    bool contains(const QString &id) const { return indexOf(id) >= 0; }

    bool insert(const Note &note, QString *error = nullptr);
    bool replace(const QString &id, const Note &note, QString *error = nullptr);
    bool remove(const QString &id);
    bool setNotes(const QVector<Note> &notes, QString *error = nullptr);
    void reindex();

private:
    QVector<Note> m_notes;
};

} // namespace pano
