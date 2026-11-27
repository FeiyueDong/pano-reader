#include "storage/AnnotationStore.h"
#include "pdf/PdfDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace pano {

namespace {

constexpr int kSchemaVersion = 1;
constexpr char kPdfSha[] = "sha256";
constexpr char kManifestPath[] = "document.json";
constexpr char kAnnotationsPath[] = "annotations.json";
constexpr char kAssetsPath[] = "assets";

QString newDocumentId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool readJsonObject(const QString &path, QJsonObject *object, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("%1 不是有效 JSON：%2").arg(path, parseError.errorString());
        return false;
    }
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("%1 的顶层必须是 JSON 对象。").arg(path);
        return false;
    }
    *object = document.object();
    return true;
}

bool writeJsonObject(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("写入 %1 失败：%2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace

QString AnnotationStore::directoryForPdf(const QString &pdfPath)
{
    const QFileInfo info(pdfPath);
    if (info.absoluteFilePath().isEmpty())
        return QString();
    return info.absoluteFilePath() + QStringLiteral(".pano");
}

QString AnnotationStore::documentFilePath() const
{
    return m_directory.isEmpty() ? QString() : QDir(m_directory).filePath(QString::fromLatin1(kManifestPath));
}

QString AnnotationStore::annotationsFilePath() const
{
    return m_directory.isEmpty() ? QString() : QDir(m_directory).filePath(QString::fromLatin1(kAnnotationsPath));
}

QString AnnotationStore::assetsDirectory() const
{
    return m_directory.isEmpty() ? QString() : QDir(m_directory).filePath(QString::fromLatin1(kAssetsPath));
}

bool AnnotationStore::attachToDocument(const PdfDocument &document, QString *error)
{
    m_attached = false;
    m_newStore = false;
    m_manifest = Manifest();
    m_directory.clear();

    if (!document.isLoaded()) {
        if (error)
            *error = QStringLiteral("当前没有已打开的 PDF。");
        return false;
    }

    const QFileInfo pdfInfo(document.filePath());
    const QString dir = directoryForPdf(pdfInfo.absoluteFilePath());
    if (dir.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法确定 PDF 的数据目录。");
        return false;
    }
    m_directory = dir;

    const QFileInfo dirInfo(dir);
    if (!dirInfo.exists()) {
        m_manifest.schemaVersion = kSchemaVersion;
        m_manifest.documentId = newDocumentId();
        m_manifest.pdfFileName = pdfInfo.fileName();
        m_manifest.pdfSha256 = document.sha256();
        m_manifest.pdfSizeBytes = document.fileSize();
        m_manifest.pdfPath = pdfInfo.absoluteFilePath();
        m_manifest.createdAt = nowIsoUtc();
        m_manifest.updatedAt = m_manifest.createdAt;
        m_newStore = true;
        m_attached = true;
        return true;
    }

    if (!dirInfo.isDir()) {
        if (error)
            *error = QStringLiteral("数据目录路径不是文件夹：%1").arg(dir);
        return false;
    }

    const QString manifestPath = QDir(dir).filePath(QString::fromLatin1(kManifestPath));
    if (!QFileInfo::exists(manifestPath)) {
        if (error) {
            *error = QStringLiteral("数据目录已存在但缺少 document.json：%1\n为避免覆盖原有数据，已停止加载。").arg(dir);
        }
        return false;
    }

    if (!readManifest(manifestPath, error))
        return false;

    if (m_manifest.schemaVersion != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("数据目录的 schema_version=%1，当前版本无法识别（支持 %2）。")
                         .arg(m_manifest.schemaVersion)
                         .arg(kSchemaVersion);
        }
        return false;
    }
    if (m_manifest.pdfSha256.compare(document.sha256(), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = QStringLiteral("当前 PDF 与数据目录绑定的版本不一致，已停止加载标注。\n"
                                    "数据目录：%1\n当前 SHA-256：%2\n目录记录：%3\n"
                                    "原数据未做任何修改。")
                         .arg(dir, document.sha256(), m_manifest.pdfSha256);
        }
        return false;
    }

    // SHA matches, so a relocated PDF is still the same document. Keep the
    // manifest's identity and refresh only the path metadata in memory.
    m_manifest.pdfFileName = pdfInfo.fileName();
    m_manifest.pdfPath = pdfInfo.absoluteFilePath();
    m_manifest.pdfSizeBytes = document.fileSize();
    m_newStore = false;
    m_attached = true;
    return true;
}

bool AnnotationStore::readManifest(const QString &path, QString *error)
{
    QJsonObject root;
    if (!readJsonObject(path, &root, error))
        return false;

    const QJsonValue versionValue = root.value(QStringLiteral("schema_version"));
    if (!versionValue.isDouble() || versionValue.toInt() <= 0) {
        if (error)
            *error = QStringLiteral("document.json 缺少有效的 schema_version。");
        return false;
    }
    m_manifest.schemaVersion = versionValue.toInt();

    m_manifest.documentId = root.value(QStringLiteral("document_id")).toString().trimmed();
    if (m_manifest.documentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("document.json 缺少 document_id。");
        return false;
    }

    const QJsonObject pdf = root.value(QStringLiteral("pdf")).toObject();
    m_manifest.pdfFileName = pdf.value(QStringLiteral("file_name")).toString();
    m_manifest.pdfSha256 = pdf.value(kPdfSha).toString().trimmed();
    m_manifest.pdfSizeBytes = static_cast<qint64>(pdf.value(QStringLiteral("size_bytes")).toDouble());
    m_manifest.pdfPath = pdf.value(QStringLiteral("path")).toString();
    m_manifest.createdAt = root.value(QStringLiteral("created_at")).toString();
    m_manifest.updatedAt = root.value(QStringLiteral("updated_at")).toString();

    if (m_manifest.pdfSha256.isEmpty()) {
        if (error)
            *error = QStringLiteral("document.json 缺少 PDF 的 SHA-256 校验值，无法安全绑定。");
        return false;
    }
    return true;
}

bool AnnotationStore::loadAnnotations(int pageCount, QVector<Annotation> *annotations, QString *error) const
{
    annotations->clear();
    if (!m_attached) {
        if (error)
            *error = QStringLiteral("数据目录尚未绑定到当前 PDF。");
        return false;
    }
    const QString path = annotationsFilePath();
    if (!QFileInfo::exists(path)) {
        // A fresh store, or a store whose annotations were never saved.
        return true;
    }

    QJsonObject root;
    if (!readJsonObject(path, &root, error))
        return false;

    const QJsonValue versionValue = root.value(QStringLiteral("schema_version"));
    if (!versionValue.isDouble() || versionValue.toInt() != kSchemaVersion) {
        if (error)
            *error = QStringLiteral("annotations.json 的 schema_version 不受支持。");
        return false;
    }
    const QString documentId = root.value(QStringLiteral("document_id")).toString();
    if (documentId != m_manifest.documentId) {
        if (error) {
            *error = QStringLiteral("annotations.json 的 document_id 与 document.json 不一致，已停止加载。\n"
                                    "标注文件：%1\n文档文件：%2")
                         .arg(documentId, m_manifest.documentId);
        }
        return false;
    }
    const QJsonValue arrayValue = root.value(QStringLiteral("annotations"));
    if (!arrayValue.isArray()) {
        if (error)
            *error = QStringLiteral("annotations.json 缺少 annotations 数组。");
        return false;
    }

    QSet<QString> ids;
    const QJsonArray array = arrayValue.toArray();
    for (int i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            if (error)
                *error = QStringLiteral("annotations[%1] 不是对象。").arg(i);
            return false;
        }
        Annotation annotation;
        QString annotationError;
        if (!Annotation::fromJson(array.at(i).toObject(), &annotation, &annotationError)) {
            if (error)
                *error = QStringLiteral("第 %1 个标注无效：%2").arg(i + 1).arg(annotationError);
            return false;
        }
        if (annotation.pageIndex < 0 || annotation.pageIndex >= pageCount) {
            if (error) {
                *error = QStringLiteral("标注 %1 的页索引 %2 超出范围（共 %3 页）。")
                             .arg(annotation.id)
                             .arg(annotation.pageIndex)
                             .arg(pageCount);
            }
            return false;
        }
        if (ids.contains(annotation.id)) {
            if (error)
                *error = QStringLiteral("标注 id 重复：%1").arg(annotation.id);
            return false;
        }
        ids.insert(annotation.id);
        annotations->append(annotation);
    }
    return true;
}

bool AnnotationStore::ensureDirectories(QString *error)
{
    if (m_directory.isEmpty()) {
        if (error)
            *error = QStringLiteral("数据目录尚未初始化。");
        return false;
    }
    QDir dir;
    if (!dir.mkpath(m_directory)) {
        if (error)
            *error = QStringLiteral("无法创建数据目录：%1").arg(m_directory);
        return false;
    }
    if (!dir.mkpath(assetsDirectory())) {
        if (error)
            *error = QStringLiteral("无法创建资源目录：%1").arg(assetsDirectory());
        return false;
    }
    return true;
}

bool AnnotationStore::save(const QVector<Annotation> &annotations, QString *error)
{
    if (!m_attached) {
        if (error)
            *error = QStringLiteral("数据目录尚未绑定到当前 PDF。");
        return false;
    }
    if (!ensureDirectories(error))
        return false;

    m_manifest.updatedAt = nowIsoUtc();
    if (!writeManifest(error))
        return false;

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), kSchemaVersion);
    root.insert(QStringLiteral("document_id"), m_manifest.documentId);
    QJsonArray array;
    for (const Annotation &annotation : annotations)
        array.append(annotation.toJson());
    root.insert(QStringLiteral("annotations"), array);

    if (!writeJsonObject(annotationsFilePath(), root, error))
        return false;
    m_newStore = false;
    return true;
}

bool AnnotationStore::writeManifest(QString *error)
{
    QJsonObject pdf;
    pdf.insert(QStringLiteral("file_name"), m_manifest.pdfFileName);
    pdf.insert(QString::fromLatin1(kPdfSha), m_manifest.pdfSha256);
    pdf.insert(QStringLiteral("size_bytes"), static_cast<double>(m_manifest.pdfSizeBytes));
    pdf.insert(QStringLiteral("path"), m_manifest.pdfPath);

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), kSchemaVersion);
    root.insert(QStringLiteral("document_id"), m_manifest.documentId);
    root.insert(QStringLiteral("pdf"), pdf);
    root.insert(QStringLiteral("created_at"), m_manifest.createdAt);
    root.insert(QStringLiteral("updated_at"), m_manifest.updatedAt);
    return writeJsonObject(documentFilePath(), root, error);
}

bool AnnotationStore::writeAsset(const QString &prefix, const QString &extension, const QByteArray &bytes,
                                 QString *relativePath, QString *error)
{
    if (!m_attached) {
        if (error)
            *error = QStringLiteral("数据目录尚未绑定到当前 PDF。");
        return false;
    }
    if (!ensureDirectories(error))
        return false;

    QString safeExtension = extension;
    if (safeExtension.startsWith(QLatin1Char('.')))
        safeExtension.remove(0, 1);
    safeExtension = safeExtension.toLower();
    for (const QChar ch : safeExtension) {
        if (!ch.isLetterOrNumber()) {
            if (error)
                *error = QStringLiteral("资源扩展名包含非法字符：%1").arg(extension);
            return false;
        }
    }
    if (safeExtension.isEmpty())
        safeExtension = QStringLiteral("bin");

    const QString name = QStringLiteral("%1-%2.%3")
                             .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8),
                                  safeExtension);
    const QString absolute = QDir(assetsDirectory()).filePath(name);
    QSaveFile file(absolute);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法创建资源文件：%1").arg(file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = QStringLiteral("写入资源文件失败：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交资源文件失败：%1").arg(file.errorString());
        return false;
    }
    *relativePath = QStringLiteral("assets/%1").arg(name);
    return true;
}

QString AnnotationStore::absoluteAssetPath(const QString &relativePath) const
{
    if (m_directory.isEmpty())
        return QString();
    return QDir(m_directory).filePath(relativePath);
}

} // namespace pano
