#include "note/Note.h"

#include <QJsonValue>
#include <QUuid>

#include "annotation/Annotation.h"

namespace pano {

QString Note::displayTitle() const
{
    const QString value = title.trimmed();
    return value.isEmpty() ? QStringLiteral("未命名笔记") : value;
}

QJsonObject Note::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("title"), title);
    object.insert(QStringLiteral("markdown"), markdown);
    if (annotationId.isEmpty())
        object.insert(QStringLiteral("annotation_id"), QJsonValue::Null);
    else
        object.insert(QStringLiteral("annotation_id"), annotationId);
    object.insert(QStringLiteral("created_at"), createdAt);
    object.insert(QStringLiteral("modified_at"), modifiedAt);
    return object;
}

bool Note::fromJson(const QJsonObject &object, Note *out, QString *error)
{
    if (!out)
        return false;
    Note note;
    note.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (note.id.isEmpty()) {
        if (error)
            *error = QStringLiteral("笔记缺少 id。");
        return false;
    }
    note.title = object.value(QStringLiteral("title")).toString();
    if (note.title.trimmed().isEmpty())
        note.title = QStringLiteral("未命名笔记");
    note.markdown = object.value(QStringLiteral("markdown")).toString();
    const QJsonValue annotation = object.value(QStringLiteral("annotation_id"));
    if (annotation.isString())
        note.annotationId = annotation.toString().trimmed();
    else if (!annotation.isNull() && !annotation.isUndefined()) {
        if (error)
            *error = QStringLiteral("笔记 annotation_id 字段无效。");
        return false;
    }
    note.createdAt = object.value(QStringLiteral("created_at")).toString();
    note.modifiedAt = object.value(QStringLiteral("modified_at")).toString();
    if (note.createdAt.isEmpty())
        note.createdAt = nowIsoUtc();
    if (note.modifiedAt.isEmpty())
        note.modifiedAt = note.createdAt;
    *out = note;
    return true;
}

QString newNoteId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace pano
