#include "app/LibraryView.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "library/LibraryManager.h"

namespace pano {

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QIcon coverIcon(const BookInfo &book)
{
    QPixmap pixmap;
    if (!book.coverPath.isEmpty())
        pixmap.load(book.coverPath);
    if (pixmap.isNull()) {
        pixmap = QPixmap(56, 76);
        pixmap.fill(QColor(0xDCE4EF));
        QPainter painter(&pixmap);
        painter.setPen(QColor(0x4B5B70));
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(pixmap.rect().adjusted(4, 4, -4, -4),
                         Qt::AlignCenter | Qt::TextWordWrap, book.title.left(12));
    }
    return QIcon(pixmap.scaled(56, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace

LibraryView::LibraryView(LibraryManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    setAcceptDrops(true);
    buildUi();
    if (m_manager) {
        connect(m_manager, &LibraryManager::libraryChanged, this, &LibraryView::refresh);
        connect(m_manager, &LibraryManager::libraryChanged, this, &LibraryView::refreshBooks);
    }
    refresh();
}

void LibraryView::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *top = new QHBoxLayout;
    auto *importButton = new QPushButton(tr("导入 PDF…"), this);
    connect(importButton, &QPushButton::clicked, this, &LibraryView::importBooks);
    top->addWidget(importButton);

    m_newShelfButton = new QPushButton(tr("新建书架"), this);
    connect(m_newShelfButton, &QPushButton::clicked, this, &LibraryView::createShelf);
    top->addWidget(m_newShelfButton);

    m_renameShelfButton = new QPushButton(tr("重命名书架"), this);
    connect(m_renameShelfButton, &QPushButton::clicked, this, &LibraryView::renameShelf);
    top->addWidget(m_renameShelfButton);

    m_deleteShelfButton = new QPushButton(tr("删除书架"), this);
    connect(m_deleteShelfButton, &QPushButton::clicked, this, &LibraryView::deleteShelf);
    top->addWidget(m_deleteShelfButton);

    top->addStretch(1);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索书名、作者…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryView::refreshBooks);
    top->addWidget(m_search);

    m_sort = new QComboBox(this);
    m_sort->addItem(tr("按书名"), QStringLiteral("title"));
    m_sort->addItem(tr("按导入时间"), QStringLiteral("imported"));
    m_sort->addItem(tr("收藏优先"), QStringLiteral("favorite"));
    connect(m_sort, &QComboBox::currentIndexChanged, this, &LibraryView::refreshBooks);
    top->addWidget(m_sort);
    layout->addLayout(top);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_navigation = new QTreeWidget(splitter);
    m_navigation->setHeaderHidden(true);
    m_navigation->setMinimumWidth(180);
    m_navigation->setMaximumWidth(280);
    connect(m_navigation, &QTreeWidget::currentItemChanged, this,
            &LibraryView::onNavigationChanged);

    m_books = new QListWidget(splitter);
    m_books->setIconSize(QSize(56, 76));
    m_books->setSpacing(4);
    m_books->setAlternatingRowColors(true);
    connect(m_books, &QListWidget::currentRowChanged, this, &LibraryView::onBookSelectionChanged);
    connect(m_books, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onBookActivated();
    });
    connect(m_books, &QListWidget::itemActivated, this, [this](QListWidgetItem *) {
        onBookActivated();
    });
    m_books->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_books, &QListWidget::customContextMenuRequested, this,
            &LibraryView::showBookContextMenu);

    m_infoPane = buildInfoPane();
    splitter->addWidget(m_navigation);
    splitter->addWidget(m_books);
    splitter->addWidget(m_infoPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({220, 420, 360});
    layout->addWidget(splitter, 1);

    rebuildNavigation();
}

QWidget *LibraryView::buildInfoPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(16, 4, 16, 4);

    m_cover = new QLabel(pane);
    m_cover->setAlignment(Qt::AlignCenter);
    m_cover->setMinimumHeight(180);
    layout->addWidget(m_cover);

    m_title = new QLabel(pane);
    m_title->setWordWrap(true);
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 5);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_author = new QLabel(pane);
    m_author->setWordWrap(true);
    layout->addWidget(m_author);

    m_meta = new QLabel(pane);
    m_meta->setWordWrap(true);
    layout->addWidget(m_meta);

    m_source = new QLabel(pane);
    m_source->setWordWrap(true);
    layout->addWidget(m_source);

    m_counts = new QLabel(pane);
    m_counts->setWordWrap(true);
    layout->addWidget(m_counts);

    auto *actions = new QHBoxLayout;
    m_openButton = new QPushButton(tr("阅读"), pane);
    connect(m_openButton, &QPushButton::clicked, this, &LibraryView::onBookActivated);
    actions->addWidget(m_openButton);
    m_favoriteButton = new QPushButton(tr("收藏"), pane);
    connect(m_favoriteButton, &QPushButton::clicked, this, &LibraryView::toggleFavorite);
    actions->addWidget(m_favoriteButton);
    m_shelfButton = new QPushButton(tr("加入书架…"), pane);
    connect(m_shelfButton, &QPushButton::clicked, this, &LibraryView::addBookToShelf);
    actions->addWidget(m_shelfButton);
    layout->addLayout(actions);

    auto *actions2 = new QHBoxLayout;
    m_trashButton = new QPushButton(tr("移入回收站"), pane);
    connect(m_trashButton, &QPushButton::clicked, this, &LibraryView::moveToTrash);
    actions2->addWidget(m_trashButton);
    m_restoreButton = new QPushButton(tr("恢复"), pane);
    connect(m_restoreButton, &QPushButton::clicked, this, &LibraryView::restoreBook);
    actions2->addWidget(m_restoreButton);
    m_deleteButton = new QPushButton(tr("永久删除"), pane);
    connect(m_deleteButton, &QPushButton::clicked, this, &LibraryView::deletePermanently);
    actions2->addWidget(m_deleteButton);
    m_sourceButton = new QPushButton(tr("更换源文件…"), pane);
    connect(m_sourceButton, &QPushButton::clicked, this, &LibraryView::changeSource);
    actions2->addWidget(m_sourceButton);
    layout->addLayout(actions2);

    layout->addStretch(1);
    return pane;
}

void LibraryView::refresh()
{
    if (!m_manager || !m_manager->isOpen())
        return;
    rebuildNavigation();
    refreshBooks();
}

void LibraryView::rebuildNavigation()
{
    const Navigation previous = currentNavigation();
    const bool wasExpanded = m_navigation->topLevelItemCount() > 2
        && m_navigation->topLevelItem(2)->isExpanded();
    QSignalBlocker blocker(m_navigation);
    m_navigation->clear();

    auto *all = new QTreeWidgetItem(m_navigation, {tr("全部书籍")});
    all->setData(0, Qt::UserRole, QStringLiteral("all"));
    auto *favorites = new QTreeWidgetItem(m_navigation, {tr("收藏")});
    favorites->setData(0, Qt::UserRole, QStringLiteral("favorites"));
    auto *shelvesRoot = new QTreeWidgetItem(m_navigation, {tr("书架")});
    shelvesRoot->setData(0, Qt::UserRole, QStringLiteral("shelves-root"));
    auto *trash = new QTreeWidgetItem(m_navigation, {tr("回收站")});
    trash->setData(0, Qt::UserRole, QStringLiteral("trash"));

    if (m_manager) {
        QHash<QString, QTreeWidgetItem *> shelfItems;
        for (const ShelfRecord &shelf : m_manager->data().shelves) {
            auto *item = new QTreeWidgetItem({shelf.name});
            item->setData(0, Qt::UserRole, QStringLiteral("shelf"));
            item->setData(0, Qt::UserRole + 1, shelf.id);
            shelfItems.insert(shelf.id, item);
        }
        // Attach after all items exist, so parents can themselves be nested.
        for (const ShelfRecord &shelf : m_manager->data().shelves) {
            QTreeWidgetItem *item = shelfItems.value(shelf.id);
            QTreeWidgetItem *parentItem = shelfItems.value(shelf.parentId, shelvesRoot);
            if (!item || parentItem == item)
                parentItem = shelvesRoot;
            parentItem->addChild(item);
        }
    }
    shelvesRoot->setExpanded(wasExpanded);
    shelvesRoot->setExpanded(true);

    // Restore the closest navigation selection.
    QTreeWidgetItem *target = all;
    if (previous.kind == Navigation::Trash)
        target = trash;
    else if (previous.kind == Navigation::Favorites)
        target = favorites;
    else if (previous.kind == Navigation::Shelf) {
        QList<QTreeWidgetItem *> matches =
            m_navigation->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive);
        for (QTreeWidgetItem *item : matches) {
            if (item->data(0, Qt::UserRole + 1).toString() == previous.shelfId) {
                target = item;
                break;
            }
        }
    }
    m_navigation->setCurrentItem(target);
    m_navigation->expandAll();
    const bool shelfSelected = target->data(0, Qt::UserRole).toString() == QStringLiteral("shelf");
    m_renameShelfButton->setEnabled(shelfSelected);
    m_deleteShelfButton->setEnabled(shelfSelected);
}

LibraryView::Navigation LibraryView::currentNavigation() const
{
    Navigation nav;
    const QTreeWidgetItem *item = m_navigation ? m_navigation->currentItem() : nullptr;
    if (!item)
        return nav;
    const QString kind = item->data(0, Qt::UserRole).toString();
    if (kind == QStringLiteral("favorites"))
        nav.kind = Navigation::Favorites;
    else if (kind == QStringLiteral("trash"))
        nav.kind = Navigation::Trash;
    else if (kind == QStringLiteral("shelf")) {
        nav.kind = Navigation::Shelf;
        nav.shelfId = item->data(0, Qt::UserRole + 1).toString();
    }
    return nav;
}

void LibraryView::onNavigationChanged(QTreeWidgetItem *current, QTreeWidgetItem *)
{
    if (!current)
        return;
    const QString kind = current->data(0, Qt::UserRole).toString();
    const bool shelfSelected = kind == QStringLiteral("shelf");
    m_renameShelfButton->setEnabled(shelfSelected);
    m_deleteShelfButton->setEnabled(shelfSelected);
    refreshBooks();
}

void LibraryView::refreshBooks()
{
    if (!m_manager || !m_manager->isOpen())
        return;
    const QString keepId = selectedBookId();
    const Navigation nav = currentNavigation();
    const QString query = m_search ? m_search->text().trimmed() : QString();
    QVector<BookInfo> books = m_manager->books(true);

    QVector<BookInfo> filtered;
    filtered.reserve(books.size());
    for (const BookInfo &book : books) {
        if (nav.kind == Navigation::Trash) {
            if (!book.deleted)
                continue;
        } else if (book.deleted) {
            continue;
        }
        if (nav.kind == Navigation::Favorites && !book.favorite)
            continue;
        if (nav.kind == Navigation::Shelf) {
            QSet<QString> shelfIds;
            QSet<QString> pending{nav.shelfId};
            while (!pending.isEmpty()) {
                const QString id = *pending.begin();
                pending.remove(id);
                if (shelfIds.contains(id))
                    continue;
                shelfIds.insert(id);
                for (const ShelfRecord &child : m_manager->data().shelves) {
                    if (child.parentId == id)
                        pending.insert(child.id);
                }
            }
            bool belongs = false;
            for (const QString &shelfId : shelfIds) {
                const ShelfRecord *shelf = m_manager->data().shelf(shelfId);
                if (shelf && shelf->bookIds.contains(book.id)) {
                    belongs = true;
                    break;
                }
            }
            if (!belongs)
                continue;
        }
        if (!query.isEmpty()
            && !book.title.contains(query, Qt::CaseInsensitive)
            && !book.author.contains(query, Qt::CaseInsensitive))
            continue;
        filtered.append(book);
    }

    const QString sortKey = m_sort ? m_sort->currentData().toString() : QStringLiteral("title");
    std::sort(filtered.begin(), filtered.end(), [sortKey](const BookInfo &a, const BookInfo &b) {
        if (sortKey == QStringLiteral("favorite") && a.favorite != b.favorite)
            return a.favorite;
        if (sortKey == QStringLiteral("imported"))
            return a.importedAt > b.importedAt;
        return a.title.localeAwareCompare(b.title) < 0;
    });

    QSignalBlocker blocker(m_books);
    m_books->clear();
    for (const BookInfo &book : filtered) {
        auto *item = new QListWidgetItem(coverIcon(book),
                                         QStringLiteral("%1\n%2").arg(book.title, book.author.isEmpty()
                                                                                     ? tr("未知作者")
                                                                                     : book.author));
        item->setData(Qt::UserRole, book.id);
        item->setData(Qt::UserRole + 1, book.deleted);
        item->setSizeHint(QSize(0, 84));
        item->setToolTip(book.pdfPath);
        m_books->addItem(item);
    }
    if (!keepId.isEmpty()) {
        for (int row = 0; row < m_books->count(); ++row) {
            if (m_books->item(row)->data(Qt::UserRole).toString() == keepId) {
                m_books->setCurrentRow(row);
                break;
            }
        }
    }
    if (m_books->count() == 0) {
        auto *empty = new QListWidgetItem(
            m_search->text().trimmed().isEmpty()
                ? tr("书库为空\n\n点击“导入 PDF”，或把 PDF 拖到这里。")
                : tr("没有匹配的书籍。"),
            m_books);
        empty->setFlags(Qt::NoItemFlags);
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setSizeHint(QSize(0, 120));
    } else if (m_books->currentRow() < 0) {
        m_books->setCurrentRow(0);
    }
    refreshInfo();
}

QString LibraryView::selectedBookId() const
{
    const QListWidgetItem *item = m_books ? m_books->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

BookInfo LibraryView::selectedBook() const
{
    return m_manager ? m_manager->bookInfo(selectedBookId()) : BookInfo();
}

void LibraryView::onBookSelectionChanged()
{
    refreshInfo();
}

void LibraryView::refreshInfo()
{
    const BookInfo book = selectedBook();
    const bool hasBook = !book.id.isEmpty();
    if (!hasBook) {
        m_cover->setPixmap(QPixmap());
        m_title->setText(tr("未选择书籍"));
        m_author->clear();
        m_meta->setText(tr("从左侧选择一本书，或导入 PDF 开始。"));
        m_source->clear();
        m_counts->clear();
        for (QPushButton *button : {m_openButton, m_favoriteButton, m_shelfButton, m_trashButton,
                                    m_restoreButton, m_deleteButton, m_sourceButton}) {
            button->setEnabled(false);
        }
        return;
    }

    const QPixmap cover(book.coverPath);
    m_cover->setPixmap(cover.isNull() ? QPixmap()
                                      : cover.scaledToHeight(180, Qt::SmoothTransformation));
    m_title->setText(book.title);
    m_author->setText(book.author.isEmpty() ? tr("未知作者") : book.author);
    m_meta->setText(tr("导入：%1\n大小：%2\n模式：%3")
                        .arg(book.importedAt.left(10), humanSize(book.sizeBytes),
                             importModeDisplayName(book.mode)));
    if (book.sourceAvailable) {
        m_source->setText(tr("源文件：可用\n%1").arg(book.pdfPath));
    } else {
        m_source->setText(tr("源文件：不可用\n%1")
                              .arg(book.sourceError.isEmpty() ? book.pdfPath : book.sourceError));
    }
    m_counts->setText(tr("标注：%1 个\n笔记：%2 个\nSHA-256：%3")
                          .arg(book.annotationCount)
                          .arg(book.noteCount)
                          .arg(book.sha256.left(16) + QStringLiteral("…")));

    const bool deleted = book.deleted;
    m_openButton->setEnabled(!deleted && book.sourceAvailable);
    m_favoriteButton->setEnabled(!deleted);
    m_favoriteButton->setText(book.favorite ? tr("取消收藏") : tr("收藏"));
    m_shelfButton->setEnabled(!deleted);
    m_trashButton->setVisible(!deleted);
    m_trashButton->setEnabled(!deleted);
    m_restoreButton->setVisible(deleted);
    m_restoreButton->setEnabled(deleted);
    m_deleteButton->setVisible(deleted);
    m_deleteButton->setEnabled(deleted);
    m_sourceButton->setEnabled(!deleted);
}

void LibraryView::onBookActivated()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty() || book.deleted)
        return;
    if (!book.sourceAvailable) {
        QMessageBox::warning(this, tr("无法打开"),
                             book.sourceError.isEmpty() ? tr("源 PDF 不可用。") : book.sourceError);
        return;
    }
    emit bookOpenRequested(book.id);
}

void LibraryView::setStatus(const QString &message)
{
    emit statusMessage(message);
}

bool LibraryView::askImportMode(ImportMode *mode)
{
    const QStringList options{tr("复制到书库"), tr("引用原文件（不复制）")};
    bool accepted = false;
    const QString choice = QInputDialog::getItem(this, tr("导入方式"), tr("选择导入方式："), options,
                                                 0, false, &accepted);
    if (!accepted)
        return false;
    if (mode)
        *mode = choice == options.at(1) ? ImportMode::Reference : ImportMode::Copy;
    return true;
}

void LibraryView::importBooks()
{
    if (!m_manager || !m_manager->isOpen())
        return;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("导入 PDF"), QString(), tr("PDF 文件 (*.pdf);;所有文件 (*)"));
    if (!paths.isEmpty())
        importFiles(paths);
}

void LibraryView::importFiles(const QStringList &candidatePaths)
{
    if (!m_manager || !m_manager->isOpen())
        return;
    QStringList paths;
    for (const QString &path : candidatePaths) {
        if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
            && !paths.contains(path))
            paths.append(path);
    }
    if (paths.isEmpty())
        return;

    ImportMode mode = ImportMode::Copy;
    if (!askImportMode(&mode))
        return;

    QStringList imported;
    QStringList duplicates;
    QString error;
    if (!m_manager->importBooks(paths, mode, false, &imported, &duplicates, &error)) {
        QMessageBox::warning(this, tr("导入失败"), error);
        return;
    }
    if (!duplicates.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("发现重复书籍"),
            tr("有 %1 本 PDF 已存在于书库中。是否仍然作为独立副本导入？").arg(duplicates.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            QString secondError;
            QStringList secondImported;
            QStringList secondDuplicates;
            if (!m_manager->importBooks(duplicates, mode, true, &secondImported,
                                        &secondDuplicates, &secondError)) {
                QMessageBox::warning(this, tr("导入失败"), secondError);
            } else {
                imported += secondImported;
            }
        }
    }
    refresh();
    if (imported.isEmpty()) {
        setStatus(tr("没有导入新书籍。"));
        return;
    }
    selectBook(imported.first());
    setStatus(tr("已导入 %1 本书。").arg(imported.size()));
}

void LibraryView::toggleFavorite()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    QString error;
    if (!m_manager->setFavorite(book.id, !book.favorite, &error)) {
        QMessageBox::warning(this, tr("操作失败"), error);
        return;
    }
    refresh();
}

void LibraryView::moveToTrash()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, tr("移入回收站"),
        tr("将《%1》移入回收站？原 PDF 不会被删除，可随时恢复。").arg(book.title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!m_manager->moveToTrash(book.id, &error))
        QMessageBox::warning(this, tr("操作失败"), error);
    refresh();
}

void LibraryView::restoreBook()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    QString error;
    if (!m_manager->restoreBook(book.id, &error))
        QMessageBox::warning(this, tr("恢复失败"), error);
    refresh();
}

void LibraryView::deletePermanently()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    const auto answer = QMessageBox::warning(
        this, tr("永久删除"),
        tr("永久删除《%1》及其标注、笔记和复制到书库中的源文件？\n\n此操作不可撤销。")
            .arg(book.title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!m_manager->deletePermanently(book.id, &error))
        QMessageBox::warning(this, tr("删除失败"), error);
    refresh();
}

void LibraryView::changeSource()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("选择新的源 PDF"), QString(),
                                                       tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!m_manager->setSourcePath(book.id, path, &error)) {
        QMessageBox::warning(this, tr("更换源文件失败"), error);
        return;
    }
    refresh();
    setStatus(tr("源文件已更新。"));
}

void LibraryView::addBookToShelf()
{
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    QVector<ShelfRecord> shelves = m_manager->data().shelves;
    if (shelves.isEmpty()) {
        QMessageBox::information(this, tr("暂无书架"), tr("请先新建一个书架。"));
        return;
    }
    QStringList names;
    for (const ShelfRecord &shelf : shelves)
        names.append(shelf.name);
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("加入书架"), tr("选择书架："), names, 0,
                                                 false, &accepted);
    if (!accepted)
        return;
    const int index = names.indexOf(chosen);
    if (index < 0)
        return;
    QString error;
    if (!m_manager->addBookToShelf(shelves.at(index).id, book.id, &error)) {
        QMessageBox::warning(this, tr("操作失败"), error);
        return;
    }
    refresh();
    setStatus(tr("已加入书架《%1》。").arg(chosen));
}

void LibraryView::createShelf()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("新建书架"), tr("书架名称："), QLineEdit::Normal,
                                                QString(), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;
    const Navigation nav = currentNavigation();
    const QString parentId = nav.kind == Navigation::Shelf ? nav.shelfId : QString();
    QString error;
    if (!m_manager->createShelf(name.trimmed(), parentId, &error)) {
        QMessageBox::warning(this, tr("新建书架失败"), error);
        return;
    }
    refresh();
}

void LibraryView::renameShelf()
{
    const Navigation nav = currentNavigation();
    if (nav.kind != Navigation::Shelf)
        return;
    const ShelfRecord *shelf = m_manager->data().shelf(nav.shelfId);
    if (!shelf)
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("重命名书架"), tr("书架名称："),
                                                QLineEdit::Normal, shelf->name, &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;
    QString error;
    if (!m_manager->renameShelf(shelf->id, name.trimmed(), &error)) {
        QMessageBox::warning(this, tr("重命名失败"), error);
        return;
    }
    refresh();
}

void LibraryView::deleteShelf()
{
    const Navigation nav = currentNavigation();
    if (nav.kind != Navigation::Shelf)
        return;
    const ShelfRecord *shelf = m_manager->data().shelf(nav.shelfId);
    if (!shelf)
        return;
    const auto answer = QMessageBox::question(
        this, tr("删除书架"), tr("删除书架《%1》？书籍本身不会被删除。").arg(shelf->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!m_manager->deleteShelf(shelf->id, &error)) {
        QMessageBox::warning(this, tr("删除书架失败"), error);
        return;
    }
    refresh();
}

void LibraryView::showBookContextMenu(const QPoint &position)
{
    QListWidgetItem *item = m_books->itemAt(position);
    if (!item)
        return;
    m_books->setCurrentItem(item);
    const BookInfo book = selectedBook();
    if (book.id.isEmpty())
        return;
    QMenu menu(this);
    QAction *open = menu.addAction(tr("阅读"));
    open->setEnabled(!book.deleted && book.sourceAvailable);
    QAction *favorite = menu.addAction(book.favorite ? tr("取消收藏") : tr("收藏"));
    favorite->setEnabled(!book.deleted);
    QAction *shelf = menu.addAction(tr("加入书架…"));
    shelf->setEnabled(!book.deleted);
    menu.addSeparator();
    QAction *trash = menu.addAction(book.deleted ? tr("恢复") : tr("移入回收站"));
    QAction *remove = nullptr;
    if (book.deleted)
        remove = menu.addAction(tr("永久删除"));
    QAction *chosen = menu.exec(m_books->viewport()->mapToGlobal(position));
    if (chosen == open)
        onBookActivated();
    else if (chosen == favorite)
        toggleFavorite();
    else if (chosen == shelf)
        addBookToShelf();
    else if (chosen == trash) {
        if (book.deleted)
            restoreBook();
        else
            moveToTrash();
    } else if (remove && chosen == remove)
        deletePermanently();
}

void LibraryView::selectBook(const QString &bookId)
{
    for (int row = 0; row < m_books->count(); ++row) {
        if (m_books->item(row)->data(Qt::UserRole).toString() == bookId) {
            m_books->setCurrentRow(row);
            return;
        }
    }
}

void LibraryView::saveExpandedState()
{
}

void LibraryView::restoreExpandedState()
{
}


void LibraryView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_manager || !m_manager->isOpen() || !event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()
            && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
}

void LibraryView::dropEvent(QDropEvent *event)
{
    if (!m_manager || !m_manager->isOpen())
        return;
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
            && !paths.contains(path))
            paths.append(path);
    }
    if (paths.isEmpty())
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    importFiles(paths);
}
} // namespace pano
