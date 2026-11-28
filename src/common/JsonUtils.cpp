#include "common/JsonUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace pano {

bool readJsonObject(const QString &path, QJsonObject *object, QString *errorMessage)
{
    if (!object)
        return false;
    QFile file(path);
    if (!file.exists()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法读取文件：%1").arg(path);
        return false;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("JSON 格式无效：%1（%2，偏移 %3）")
                                .arg(path, parseError.errorString())
                                .arg(parseError.offset);
        }
        return false;
    }
    *object = document.object();
    return true;
}

bool writeJsonObjectAtomic(const QString &path, const QJsonObject &object, QString *errorMessage)
{
    const QFileInfo info(path);
    if (!ensureDirectory(info.absolutePath(), errorMessage))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!bytes.endsWith('\n'))
        bytes.append('\n');
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("写入文件失败：%1").arg(path);
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("提交文件失败：%1").arg(path);
        return false;
    }
    return true;
}

bool ensureDirectory(const QString &path, QString *errorMessage)
{
    if (path.isEmpty())
        return true;
    QDir dir;
    if (dir.mkpath(path))
        return true;
    if (errorMessage)
        *errorMessage = QStringLiteral("无法创建目录：%1").arg(path);
    return false;
}

bool sha256File(const QString &path, QByteArray *digest, QString *errorMessage)
{
    if (!digest)
        return false;
    QFile file(path);
    if (!file.exists()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法读取文件：%1").arg(path);
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("读取文件失败：%1").arg(path);
        return false;
    }
    *digest = hash.result();
    return true;
}

QByteArray sha256File(const QString &path)
{
    QByteArray digest;
    if (!sha256File(path, &digest, nullptr))
        return {};
    return digest;
}

QString appConfigDirectory()
{
    const QString override = qEnvironmentVariable("PANO_CONFIG_DIR");
    if (!override.isEmpty())
        return override;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString newStableId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString utcNowIso()
{
    return utcIso(QDateTime::currentDateTimeUtc());
}

QString utcIso(const QDateTime &dateTime)
{
    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

bool isSafeLibraryRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 512)
        return false;
    if (QDir::isAbsolutePath(path))
        return false;
    if (path.contains('\\') || path.contains('\0'))
        return false;
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned == QLatin1String(".") || cleaned.startsWith(QLatin1String("../"))
        || cleaned == QLatin1String("..") || cleaned.contains(QLatin1String("/../")))
        return false;
    // Reject Windows drive/UNC style prefixes even on Linux, because a library
    // may be moved to Windows later.
    if (cleaned.size() >= 2 && cleaned.at(1) == ':')
        return false;
    return true;
}

bool isPathInside(const QString &child, const QString &parent)
{
    const QString canonicalParent = QFileInfo(parent).canonicalFilePath();
    const QString canonicalChild = QFileInfo(child).canonicalFilePath();
    if (canonicalParent.isEmpty() || canonicalChild.isEmpty())
        return false;
    const QString prefix = canonicalParent.endsWith('/') ? canonicalParent : canonicalParent + '/';
    return canonicalChild == canonicalParent || canonicalChild.startsWith(prefix);
}

} // namespace pano
