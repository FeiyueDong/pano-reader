#pragma once

#include <QJsonArray>
#include <QSet>
#include <QString>
#include <QVector>

namespace pano {

enum class NoteNodeType { Group, Note };

// A flat representation of the note tree.  Persistence is nested, but the
// in-memory model keeps explicit parent/order fields so moves and validation
// cannot accidentally duplicate a subtree.
struct NoteTreeNode {
    QString id;                 // group id, or the note id for note leaves
    NoteNodeType type = NoteNodeType::Group;
    QString title;              // group title
    QString noteId;             // note leaf only
    QString parentId;           // empty == book root
    int order = 0;              // sibling order
};

class NoteTree
{
public:
    void clear() { m_nodes.clear(); }
    const QVector<NoteTreeNode> &nodes() const { return m_nodes; }
    QVector<NoteTreeNode> &nodes() { return m_nodes; }
    bool isEmpty() const { return m_nodes.isEmpty(); }

    int indexOf(const QString &id) const;
    const NoteTreeNode *findNode(const QString &id) const;
    NoteTreeNode *findNode(const QString &id);
    bool isGroup(const QString &id) const;
    bool containsNote(const QString &noteId) const;
    QString parentOf(const QString &id) const;
    QVector<QString> childIds(const QString &parentId) const;
    int depthOf(const QString &id) const;

    bool addGroup(const QString &parentId, const QString &title, QString *groupId, QString *error);
    bool addNote(const QString &parentId, const QString &noteId, QString *error);
    bool removeNode(const QString &id, bool promoteChildren, QString *error);
    bool renameNode(const QString &id, const QString &title, QString *error);
    bool moveNode(const QString &id, const QString &newParentId, QString *error);
    bool moveUp(const QString &id, QString *error);
    bool moveDown(const QString &id, QString *error);

    bool isDescendant(const QString &candidate, const QString &ancestor) const;
    bool validate(const QSet<QString> &noteIds, QString *error) const;
    QJsonArray toJson() const;
    static bool fromJson(const QJsonArray &array, NoteTree *out, QString *error);

private:
    void normalizeChildren(const QString &parentId);
    int nextOrder(const QString &parentId) const;

    QVector<NoteTreeNode> m_nodes;
};

} // namespace pano
