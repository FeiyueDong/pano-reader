#include "note/NoteTree.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <algorithm>
#include <functional>
#include <limits>

#include "note/Note.h"

namespace pano {

namespace {
constexpr int kMaxDepth = 32;
constexpr int kMaxNodes = 10000;
}

int NoteTree::indexOf(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).id == id)
            return i;
    }
    return -1;
}

const NoteTreeNode *NoteTree::findNode(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? &m_nodes.at(index) : nullptr;
}

NoteTreeNode *NoteTree::findNode(const QString &id)
{
    const int index = indexOf(id);
    return index >= 0 ? &m_nodes[index] : nullptr;
}

bool NoteTree::isGroup(const QString &id) const
{
    const NoteTreeNode *node = findNode(id);
    return node && node->type == NoteNodeType::Group;
}

bool NoteTree::containsNote(const QString &noteId) const
{
    const NoteTreeNode *node = findNode(noteId);
    return node && node->type == NoteNodeType::Note && node->noteId == noteId;
}

QString NoteTree::parentOf(const QString &id) const
{
    const NoteTreeNode *node = findNode(id);
    return node ? node->parentId : QString();
}

QVector<QString> NoteTree::childIds(const QString &parentId) const
{
    QVector<const NoteTreeNode *> children;
    for (const NoteTreeNode &node : m_nodes) {
        if (node.parentId == parentId)
            children.append(&node);
    }
    std::stable_sort(children.begin(), children.end(),
                     [](const NoteTreeNode *a, const NoteTreeNode *b) {
                         if (a->order != b->order)
                             return a->order < b->order;
                         return a->id < b->id;
                     });
    QVector<QString> result;
    result.reserve(children.size());
    for (const NoteTreeNode *node : children)
        result.append(node->id);
    return result;
}

int NoteTree::depthOf(const QString &id) const
{
    int depth = 0;
    QString current = id;
    QSet<QString> seen;
    while (!current.isEmpty() && depth <= kMaxDepth) {
        if (seen.contains(current))
            return kMaxDepth + 1;
        seen.insert(current);
        const NoteTreeNode *node = findNode(current);
        if (!node)
            break;
        ++depth;
        current = node->parentId;
    }
    return depth;
}

int NoteTree::nextOrder(const QString &parentId) const
{
    int value = 0;
    for (const NoteTreeNode &node : m_nodes) {
        if (node.parentId == parentId)
            value = std::max(value, node.order + 1);
    }
    return value;
}

bool NoteTree::isDescendant(const QString &candidate, const QString &ancestor) const
{
    QString current = candidate;
    QSet<QString> seen;
    while (!current.isEmpty()) {
        if (current == ancestor)
            return true;
        if (seen.contains(current))
            return false;
        seen.insert(current);
        const NoteTreeNode *node = findNode(current);
        if (!node)
            return false;
        current = node->parentId;
    }
    return false;
}

bool NoteTree::addGroup(const QString &parentId, const QString &title, QString *groupId, QString *error)
{
    if (!parentId.isEmpty() && !isGroup(parentId)) {
        if (error)
            *error = QStringLiteral("目标父节点不是分组。");
        return false;
    }
    if (parentId.isEmpty() ? 1 > kMaxDepth : depthOf(parentId) + 1 > kMaxDepth) {
        if (error)
            *error = QStringLiteral("笔记树层级过深。");
        return false;
    }
    if (m_nodes.size() >= kMaxNodes) {
        if (error)
            *error = QStringLiteral("笔记树节点数量超过限制。");
        return false;
    }
    NoteTreeNode node;
    node.type = NoteNodeType::Group;
    node.title = title.trimmed().isEmpty() ? QStringLiteral("新建分组") : title.trimmed();
    node.parentId = parentId;
    node.order = nextOrder(parentId);
    node.id = newNoteId();
    while (indexOf(node.id) >= 0)
        node.id = newNoteId();
    if (depthOf(parentId) + 1 > kMaxDepth) {
        if (error)
            *error = QStringLiteral("笔记树层级过深。");
        return false;
    }
    m_nodes.append(node);
    if (groupId)
        *groupId = node.id;
    return true;
}

bool NoteTree::addNote(const QString &parentId, const QString &noteId, QString *error)
{
    if (noteId.isEmpty() || indexOf(noteId) >= 0) {
        if (error)
            *error = QStringLiteral("笔记节点 id 为空或重复。");
        return false;
    }
    if (!parentId.isEmpty() && !isGroup(parentId)) {
        if (error)
            *error = QStringLiteral("笔记只能加入根节点或分组。");
        return false;
    }
    if (m_nodes.size() >= kMaxNodes) {
        if (error)
            *error = QStringLiteral("笔记树节点数量超过限制。");
        return false;
    }
    if (depthOf(parentId) + 1 > kMaxDepth) {
        if (error)
            *error = QStringLiteral("笔记树层级过深。");
        return false;
    }
    NoteTreeNode node;
    node.id = noteId;
    node.noteId = noteId;
    node.type = NoteNodeType::Note;
    node.parentId = parentId;
    node.order = nextOrder(parentId);
    m_nodes.append(node);
    return true;
}

void NoteTree::normalizeChildren(const QString &parentId)
{
    QVector<NoteTreeNode *> children;
    for (NoteTreeNode &node : m_nodes) {
        if (node.parentId == parentId)
            children.append(&node);
    }
    std::stable_sort(children.begin(), children.end(),
                     [](const NoteTreeNode *a, const NoteTreeNode *b) {
                         if (a->order != b->order)
                             return a->order < b->order;
                         return a->id < b->id;
                     });
    for (int i = 0; i < children.size(); ++i)
        children.at(i)->order = i;
}

bool NoteTree::removeNode(const QString &id, bool promoteChildren, QString *error)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    const NoteTreeNode node = m_nodes.at(index);
    if (node.type == NoteNodeType::Note) {
        m_nodes.removeAt(index);
        normalizeChildren(node.parentId);
        return true;
    }

    const QString parentId = node.parentId;
    QVector<QString> promoted;
    for (const NoteTreeNode &child : m_nodes) {
        if (child.parentId == id) {
            if (!promoteChildren) {
                if (error)
                    *error = QStringLiteral("分组非空，不能直接删除。");
                return false;
            }
            promoted.append(child.id);
        }
    }
    m_nodes.removeAt(index);
    int order = nextOrder(parentId);
    for (const QString &childId : promoted) {
        NoteTreeNode *child = findNode(childId);
        if (!child)
            continue;
        child->parentId = parentId;
        child->order = order++;
    }
    normalizeChildren(parentId);
    return true;
}

bool NoteTree::renameNode(const QString &id, const QString &title, QString *error)
{
    NoteTreeNode *node = findNode(id);
    if (!node) {
        if (error)
            *error = QStringLiteral("节点不存在。");
        return false;
    }
    if (node->type != NoteNodeType::Group) {
        if (error)
            *error = QStringLiteral("笔记标题应在笔记中修改。");
        return false;
    }
    node->title = title.trimmed().isEmpty() ? QStringLiteral("新建分组") : title.trimmed();
    return true;
}

bool NoteTree::moveNode(const QString &id, const QString &newParentId, QString *error)
{
    NoteTreeNode *node = findNode(id);
    if (!node) {
        if (error)
            *error = QStringLiteral("节点不存在。");
        return false;
    }
    if (!newParentId.isEmpty() && !isGroup(newParentId)) {
        if (error)
            *error = QStringLiteral("目标父节点不是分组。");
        return false;
    }
    if (newParentId == id || (!newParentId.isEmpty() && isDescendant(newParentId, id))) {
        if (error)
            *error = QStringLiteral("不能把节点移动到自身或子节点下。");
        return false;
    }
    if (node->parentId == newParentId) {
        normalizeChildren(newParentId);
        return true;
    }
    if (newParentId == id)
        return true;
    int subtreeDepth = 0;
    for (const NoteTreeNode &candidate : m_nodes) {
        if (candidate.id == id || isDescendant(candidate.id, id))
            subtreeDepth = qMax(subtreeDepth, depthOf(candidate.id) - depthOf(id));
    }
    const int parentDepth = newParentId.isEmpty() ? 0 : depthOf(newParentId);
    if (parentDepth + 1 + subtreeDepth > kMaxDepth) {
        if (error)
            *error = QStringLiteral("笔记树层级过深。");
        return false;
    }
    const QString oldParentId = node->parentId;
    node->parentId = newParentId;
    node->order = nextOrder(newParentId);
    normalizeChildren(oldParentId);
    normalizeChildren(newParentId);
    return true;
}

bool NoteTree::moveUp(const QString &id, QString *error)
{
    const NoteTreeNode *node = findNode(id);
    if (!node) {
        if (error)
            *error = QStringLiteral("节点不存在。");
        return false;
    }
    QVector<QString> siblings = childIds(node->parentId);
    const int index = siblings.indexOf(id);
    if (index <= 0)
        return false;
    NoteTreeNode *a = findNode(siblings.at(index - 1));
    NoteTreeNode *b = findNode(id);
    std::swap(a->order, b->order);
    normalizeChildren(node->parentId);
    return true;
}

bool NoteTree::moveDown(const QString &id, QString *error)
{
    const NoteTreeNode *node = findNode(id);
    if (!node) {
        if (error)
            *error = QStringLiteral("节点不存在。");
        return false;
    }
    QVector<QString> siblings = childIds(node->parentId);
    const int index = siblings.indexOf(id);
    if (index < 0 || index + 1 >= siblings.size())
        return false;
    NoteTreeNode *a = findNode(siblings.at(index + 1));
    NoteTreeNode *b = findNode(id);
    std::swap(a->order, b->order);
    normalizeChildren(node->parentId);
    return true;
}

bool NoteTree::validate(const QSet<QString> &noteIds, QString *error) const
{
    QSet<QString> nodeIds;
    QSet<QString> noteNodes;
    for (const NoteTreeNode &node : m_nodes) {
        if (node.id.isEmpty() || nodeIds.contains(node.id)) {
            if (error)
                *error = QStringLiteral("笔记树节点 id 为空或重复：%1").arg(node.id);
            return false;
        }
        nodeIds.insert(node.id);
        if (node.type == NoteNodeType::Note) {
            if (node.noteId.isEmpty() || node.noteId != node.id || !noteIds.contains(node.noteId)) {
                if (error)
                    *error = QStringLiteral("笔记树引用了不存在的笔记：%1").arg(node.noteId);
                return false;
            }
            if (noteNodes.contains(node.noteId)) {
                if (error)
                    *error = QStringLiteral("笔记在树中重复出现：%1").arg(node.noteId);
                return false;
            }
            noteNodes.insert(node.noteId);
        } else if (node.title.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("分组标题为空。");
            return false;
        }
        if (!node.parentId.isEmpty()) {
            const NoteTreeNode *parent = findNode(node.parentId);
            if (!parent || parent->type != NoteNodeType::Group) {
                if (error)
                    *error = QStringLiteral("笔记树父节点无效：%1").arg(node.parentId);
                return false;
            }
        }
        if (depthOf(node.id) > kMaxDepth) {
            if (error)
                *error = QStringLiteral("笔记树存在循环或层级过深。");
            return false;
        }
    }
    if (noteNodes != noteIds) {
        if (error)
            *error = QStringLiteral("笔记与笔记树节点不一致。");
        return false;
    }
    return true;
}

QJsonArray NoteTree::toJson() const
{
    std::function<QJsonArray(const QString &)> encode = [&](const QString &parentId) {
        QJsonArray array;
        for (const QString &id : childIds(parentId)) {
            const NoteTreeNode *node = findNode(id);
            if (!node)
                continue;
            QJsonObject object;
            if (node->type == NoteNodeType::Group) {
                object.insert(QStringLiteral("type"), QStringLiteral("group"));
                object.insert(QStringLiteral("id"), node->id);
                object.insert(QStringLiteral("title"), node->title);
                object.insert(QStringLiteral("children"), encode(node->id));
            } else {
                object.insert(QStringLiteral("type"), QStringLiteral("note"));
                object.insert(QStringLiteral("note_id"), node->noteId);
            }
            array.append(object);
        }
        return array;
    };
    return encode(QString());
}

bool NoteTree::fromJson(const QJsonArray &array, NoteTree *out, QString *error)
{
    if (!out)
        return false;
    NoteTree tree;
    std::function<bool(const QJsonArray &, const QString &, int)> decode =
        [&](const QJsonArray &items, const QString &parentId, int depth) -> bool {
        if (depth > kMaxDepth) {
            if (error)
                *error = QStringLiteral("笔记树层级过深。");
            return false;
        }
        if (tree.m_nodes.size() + items.size() > kMaxNodes) {
            if (error)
                *error = QStringLiteral("笔记树节点数量超过限制。");
            return false;
        }
        int order = 0;
        for (const QJsonValue &value : items) {
            if (!value.isObject()) {
                if (error)
                    *error = QStringLiteral("笔记树节点必须是对象。");
                return false;
            }
            const QJsonObject object = value.toObject();
            const QString type = object.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("group")) {
                const QString id = object.value(QStringLiteral("id")).toString();
                const QString title = object.value(QStringLiteral("title")).toString();
                if (id.isEmpty() || title.trimmed().isEmpty() || tree.indexOf(id) >= 0) {
                    if (error)
                        *error = QStringLiteral("笔记树分组 id 或标题无效。");
                    return false;
                }
                NoteTreeNode node;
                node.id = id;
                node.type = NoteNodeType::Group;
                node.title = title;
                node.parentId = parentId;
                node.order = order++;
                tree.m_nodes.append(node);
                const QJsonValue children = object.value(QStringLiteral("children"));
                if (!children.isArray() || !decode(children.toArray(), id, depth + 1))
                    return false;
            } else if (type == QLatin1String("note")) {
                const QString noteId = object.value(QStringLiteral("note_id")).toString();
                if (noteId.isEmpty() || tree.indexOf(noteId) >= 0) {
                    if (error)
                        *error = QStringLiteral("笔记树 note_id 为空或重复。");
                    return false;
                }
                NoteTreeNode node;
                node.id = noteId;
                node.noteId = noteId;
                node.type = NoteNodeType::Note;
                node.parentId = parentId;
                node.order = order++;
                tree.m_nodes.append(node);
            } else {
                if (error)
                    *error = QStringLiteral("未知笔记树节点类型：%1").arg(type);
                return false;
            }
        }
        return true;
    };
    if (!array.isEmpty() && !decode(array, QString(), 1))
        return false;
    *out = tree;
    return true;
}

} // namespace pano
