#include "app/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO"));
    QApplication::setApplicationDisplayName(QStringLiteral("PANO"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("PANO"));
    QApplication::setOrganizationDomain(QStringLiteral("pano.local"));

    QStringList arguments = QApplication::arguments();
    arguments.removeFirst();

    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        std::printf("PANO Phase 0 Demo\n"
                    "用法: pano-reader [document.pdf]\n\n"
                    "打开 PDF 后，标注与 Markdown 笔记保存在与 PDF 同级的 <文件名>.pano 目录。\n"
                    "原文 PDF 始终只读。\n");
        return 0;
    }

    pano::MainWindow window;
    window.show();

    QString documentPath;
    for (const QString &argument : arguments) {
        if (!argument.startsWith(QLatin1Char('-'))) {
            documentPath = QDir::cleanPath(argument);
            break;
        }
    }

    if (!documentPath.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, documentPath] {
            window.openDocument(documentPath);
        });
    }

    return application.exec();
}
