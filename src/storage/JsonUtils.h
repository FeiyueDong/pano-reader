#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace pano {

// Small, dependency-free helpers shared by the library and document stores.
// They deliberately do not "repair" malformed JSON: callers must reject and
// keep the previous on-disk state intact.
bool readJsonObject(const QString &path, QJsonObject *object, QString *errorMessage);
bool writeJsonObjectAtomic(const QString &path, const QJsonObject &object, QString *errorMessage);

bool ensureDirectory(const QString &path, QString *errorMessage);
bool sha256File(const QString &path, QByteArray *digest, QString *errorMessage);
QByteArray sha256File(const QString &path);

QString appConfigDirectory();
QString newStableId();
QString utcNowIso();
QString utcIso(const QDateTime &dateTime);

// A library-relative path such as "books/My Book" may not escape the library
// root, be absolute, or use Windows device/UNC syntax.
bool isSafeLibraryRelativePath(const QString &path);
bool isPathInside(const QString &child, const QString &parent);

} // namespace pano
