#pragma once

#include <QJsonObject>
#include <QString>

namespace pano {

// A note is an independent document object.  It may optionally be attached to
// one annotation, but never stores page coordinates or annotation style.
struct Note {
    QString id;
    QString title;
    QString markdown;
    QString annotationId; // empty == unlinked
    QString createdAt;
    QString modifiedAt;

    bool isEmpty() const { return title.trimmed().isEmpty() && markdown.trimmed().isEmpty(); }
    QString displayTitle() const;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Note *out, QString *error);
};

QString newNoteId();

} // namespace pano
