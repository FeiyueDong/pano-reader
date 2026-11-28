#include "app/LibraryView.h"

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
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
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "app/IconManager.h"
#include "library/LibraryManager.h"
#include "settings/SettingsManager.h"

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

QIcon coverIcon(const BookInfo &book, const QSize &target)
{
    QPixmap source;
    if (!book.coverPath.isEmpty())
        source.load(book.coverPath);
    QPixmap canvas(target);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!source.isNull()) {
        const QPixmap scaled = source.scaled(target, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        const QPoint offset((target.width() - scaled.width()) / 2,
                            (target.height() - scaled.height()) / 2);
        painter.drawPixmap(offset, scaled);
    } else {
        painter.fillRect(canvas.rect(), QColor(0xDCE4EF));
        painter.setPen(QColor(0x4B5B70));
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(canvas.rect().adjusted(4, 4, -4, -4),
                         Qt::AlignCenter | Qt::TextWordWrap, book.title.left(12));
    }
    painter.end();
    return QIcon(canvas);
}

class BookListWidget : public QListWidget
{
public:
    explicit BookListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem *item = currentItem();
        if (!item || item->data(Qt::UserRole).toString() != QStringLiteral("book"))
            return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-pano-book"),
                      item->data(Qt::UserRole + 1).toString().toUtf8());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        const QPixmap pixmap = item->icon().pixmap(48, 64);
        if (!pixmap.isNull())
            drag->setPixmap(pixmap);
        drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
    }
};

} // namespace

LibraryView::LibraryView(LibraryManager *manager, SettingsManager *settings, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_settings(settings)
{
    setAcceptDrops(true);
    if (m_settings)
        m_coverMode = m_settings->settings().libraryCoverView;
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
    m_importButton = new QToolButton(this);
    m_importButton->setIcon(IconManager::instance()->icon(QStringLiteral("file-plus")));
    m_importButton->setToolTip(tr("导入 PDF"));
    connect(m_importButton, &QToolButton::clicked, this, &LibraryView::importBooks);
    top->addWidget(m_importButton);

    m_newShelfButton = new QToolButton(this);
    m_newShelfButton->setIcon(IconManager::instance()->icon(QStringLiteral("folder-plus")));
    m_newShelfButton->setToolTip(tr("新建书架"));
    connect(m_newShelfButton, &QToolButton::clicked, this, &LibraryView::createShelf);
    top->addWidget(m_newShelfButton);

    m_renameShelfButton = new QToolButton(this);
    m_renameShelfButton->setIcon(IconManager::instance()->icon(QStringLiteral("pen-line")));
    m_renameShelfButton->setToolTip(tr("重命名书架"));
    connect(m_renameShelfButton, &QToolButton::clicked, this, &LibraryView::renameShelf);
    top->addWidget(m_renameShelfButton);

    m_deleteShelfButton = new QToolButton(this);
    m_deleteShelfButton->setIcon(IconManager::instance()->icon(QStringLiteral("trash-2")));
    m_deleteShelfButton->setToolTip(tr("删除书架"));
    connect(m_deleteShelfButton, &QToolButton::clicked, this, &LibraryView::deleteShelf);
    top->addWidget(m_deleteShelfButton);

    for (QToolButton *button : {m_importButton, m_newShelfButton, m_renameShelfButton,
                                m_deleteShelfButton}) {
        button->setIconSize(QSize(20, 20));
        button->setStyleSheet(QStringLiteral(
            "QToolButton { border: 1px solid palette(mid); border-radius: 4px; padding: 3px; }"
            "QToolButton:hover { background: palette(midlight); }"));
    }

    top->addSpacing(8);
    m_breadcrumb = new QLabel(this);
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(m_breadcrumb, &QLabel::linkActivated, this, &LibraryView::onBreadcrumbActivated);
    top->addWidget(m_breadcrumb, 1);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索书名、作者…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(180);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryView::refreshBooks);
    top->addWidget(m_search);

    m_sort = new QComboBox(this);
    m_sort->addItem(tr("按书名"), QStringLiteral("title"));
    m_sort->addItem(tr("按作者"), QStringLiteral("author"));
    m_sort->addItem(tr("按导入时间"), QStringLiteral("imported"));
    m_sort->addItem(tr("最近阅读"), QStringLiteral("recent"));
    m_sort->addItem(tr("收藏优先"), QStringLiteral("favorite"));
    connect(m_sort, &QComboBox::currentIndexChanged, this, &LibraryView::refreshBooks);
    top->addWidget(m_sort);

    m_viewToggle = new QToolButton(this);
    m_viewToggle->setCheckable(true);
    m_viewToggle->setChecked(m_coverMode);
    m_viewToggle->setToolTip(tr("切换封面 / 列表视图"));
    m_viewToggle->setIcon(IconManager::instance()->icon(
        m_coverMode ? QStringLiteral("layout-grid") : QStringLiteral("list")));
    connect(m_viewToggle, &QToolButton::toggled, this, &LibraryView::onViewModeToggled);
    top->addWidget(m_viewToggle);
    layout->addLayout(top);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_navigation = new QTreeWidget(m_splitter);
    m_navigation->setHeaderHidden(true);
    m_navigation->setMinimumWidth(180);
    m_navigation->setMaximumWidth(280);
    connect(m_navigation, &QTreeWidget::currentItemChanged, this,
            &LibraryView::onNavigationChanged);

    m_books = new BookListWidget(m_splitter);
    m_books->setDragEnabled(true);
    m_books->setDragDropMode(QAbstractItemView::DragOnly);
    m_books->setDefaultDropAction(Qt::MoveAction);
    m_books->setSpacing(4);
    m_books->setAlternatingRowColors(true);
    m_books->setUniformItemSizes(false);
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
    m_splitter->addWidget(m_navigation);
    m_splitter->addWidget(m_books);
    m_splitter->addWidget(m_infoPane);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setChildrenCollapsible(false);
    if (m_settings) {
        m_splitter->setSizes({m_settings->settings().libraryNavWidth, 520,
                              m_settings->settings().libraryInfoWidth});
    } else {
        m_splitter->setSizes({230, 520, 340});
    }
    connect(m_splitter, &QSplitter::splitterMoved, this, &LibraryView::onSplitterMoved);
    layout->addWidget(m_splitter, 1);

    applyViewMode(m_coverMode);
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
    all->setIcon(0, IconManager::instance()->icon(QStringLiteral("book-open")));
    auto *favorites = new QTreeWidgetItem(m_navigation, {tr("收藏")});
    favorites->setData(0, Qt::UserRole, QStringLiteral("favorites"));
    favorites->setIcon(0, IconManager::instance()->icon(QStringLiteral("star")));
    auto *shelvesRoot = new QTreeWidgetItem(m_navigation, {tr("书架")});
    shelvesRoot->setData(0, Qt::UserRole, QStringLiteral("shelves-root"));
    shelvesRoot->setIcon(0, IconManager::instance()->icon(QStringLiteral("folder")));
    auto *trash = new QTreeWidgetItem(m_navigation, {tr("回收站")});
    trash->setData(0, Qt::UserRole, QStringLiteral("trash"));
    trash->setIcon(0, IconManager::instance()->icon(QStringLiteral("trash-2")));

    if (m_manager) {
        QHash<QString, QTreeWidgetItem *> shelfItems;
        for (const ShelfRecord &shelf : m_manager->data().shelves) {
            auto *item = new QTreeWidgetItem({shelf.name});
            item->setData(0, Qt::UserRole, QStringLiteral("shelf"));
            item->setData(0, Qt::UserRole + 1, shelf.id);
            item->setIcon(0, IconManager::instance()->icon(QStringLiteral("folder")));
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
    updateBreadcrumb();
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

QString LibraryView::currentShelfId() const
{
    const Navigation nav = currentNavigation();
    return nav.kind == Navigation::Shelf ? nav.shelfId : QString();
}

QString LibraryView::shelfPath(const QString &shelfId) const
{
    QStringList parts;
    QString id = shelfId;
    QSet<QString> seen;
    while (!id.isEmpty() && !seen.contains(id)) {
        seen.insert(id);
        const ShelfRecord *shelf = m_manager ? m_manager->data().shelf(id) : nullptr;
        if (!shelf)
            break;
        parts.prepend(shelf->name);
        id = shelf->parentId;
    }
    return parts.join(QStringLiteral(" / "));
}

void LibraryView::updateBreadcrumb()
{
    if (!m_breadcrumb)
        return;
    const Navigation nav = currentNavigation();
    const QString home = QStringLiteral("<a href=\"all\">全部书籍</a>");
    QString text;
    if (nav.kind == Navigation::Favorites) {
        text = home + QStringLiteral(" / <a href=\"favorites\">收藏</a>");
    } else if (nav.kind == Navigation::Trash) {
        text = home + QStringLiteral(" / <a href=\"trash\">回收站</a>");
    } else if (nav.kind == Navigation::Shelf) {
        text = home + QStringLiteral(" / <a href=\"shelves\">书架</a>");
        QStringList ids;
        QString id = nav.shelfId;
        QSet<QString> seen;
        while (!id.isEmpty() && !seen.contains(id)) {
            seen.insert(id);
            ids.prepend(id);
            const ShelfRecord *shelf = m_manager ? m_manager->data().shelf(id) : nullptr;
            if (!shelf)
                break;
            id = shelf->parentId;
        }
        for (const QString &shelfId : ids) {
            const ShelfRecord *shelf = m_manager->data().shelf(shelfId);
            if (!shelf)
                continue;
            text += QStringLiteral(" / <a href=\"shelf:%1\">%2</a>")
                        .arg(shelfId.toHtmlEscaped(), shelf->name.toHtmlEscaped());
        }
    } else {
        text = QStringLiteral("<b>全部书籍</b>");
    }
    m_breadcrumb->setText(text);
}

void LibraryView::onBreadcrumbActivated(const QString &target)
{
    if (target == QStringLiteral("all")) {
        for (int i = 0; i < m_navigation->topLevelItemCount(); ++i) {
            if (m_navigation->topLevelItem(i)->data(0, Qt::UserRole).toString()
                == QStringLiteral("all")) {
                m_navigation->setCurrentItem(m_navigation->topLevelItem(i));
                return;
            }
        }
    } else if (target == QStringLiteral("favorites")) {
        for (int i = 0; i < m_navigation->topLevelItemCount(); ++i) {
            if (m_navigation->topLevelItem(i)->data(0, Qt::UserRole).toString()
                == QStringLiteral("favorites")) {
                m_navigation->setCurrentItem(m_navigation->topLevelItem(i));
                return;
            }
        }
    } else if (target == QStringLiteral("trash")) {
        for (int i = 0; i < m_navigation->topLevelItemCount(); ++i) {
            if (m_navigation->topLevelItem(i)->data(0, Qt::UserRole).toString()
                == QStringLiteral("trash")) {
                m_navigation->setCurrentItem(m_navigation->topLevelItem(i));
                return;
            }
        }
    } else if (target == QStringLiteral("shelves")) {
        for (int i = 0; i < m_navigation->topLevelItemCount(); ++i) {
            if (m_navigation->topLevelItem(i)->data(0, Qt::UserRole).toString()
                == QStringLiteral("shelves-root")) {
                m_navigation->setCurrentItem(m_navigation->topLevelItem(i));
                return;
            }
        }
    } else if (target.startsWith(QStringLiteral("shelf:"))) {
        selectShelf(target.mid(6));
    }
}

void LibraryView::selectShelf(const QString &shelfId)
{
    const QList<QTreeWidgetItem *> items =
        m_navigation->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive);
    for (QTreeWidgetItem *item : items) {
        if (item->data(0, Qt::UserRole + 1).toString() == shelfId) {
            m_navigation->setCurrentItem(item);
            return;
        }
    }
}

void LibraryView::onViewModeToggled(bool cover)
{
    applyViewMode(cover);
    if (m_settings)
        m_settings->setLibraryCoverView(cover);
    refreshBooks();
}

void LibraryView::onSplitterMoved()
{
    if (!m_settings || !m_splitter)
        return;
    const QList<int> sizes = m_splitter->sizes();
    if (sizes.size() == 3)
        m_settings->setLibraryPanelState(true, true, sizes.at(0), sizes.at(2));
}

void LibraryView::applyViewMode(bool cover)
{
    m_coverMode = cover;
    if (!m_books)
        return;
    if (m_viewToggle) {
        QSignalBlocker blocker(m_viewToggle);
        m_viewToggle->setChecked(cover);
        m_viewToggle->setIcon(IconManager::instance()->icon(
            cover ? QStringLiteral("layout-grid") : QStringLiteral("list")));
    }
    if (cover) {
        m_books->setViewMode(QListView::IconMode);
        m_books->setIconSize(QSize(112, 150));
        m_books->setGridSize(QSize(138, 188));
        m_books->setWordWrap(true);
        m_books->setResizeMode(QListView::Adjust);
        m_books->setMovement(QListView::Static);
    } else {
        m_books->setViewMode(QListView::ListMode);
        m_books->setIconSize(QSize(44, 58));
        m_books->setGridSize(QSize());
        m_books->setWordWrap(false);
        m_books->setResizeMode(QListView::Adjust);
        m_books->setMovement(QListView::Static);
    }
}

void LibraryView::onNavigationChanged(QTreeWidgetItem *current, QTreeWidgetItem *)
{
    if (!current)
        return;
    const QString kind = current->data(0, Qt::UserRole).toString();
    const bool shelfSelected = kind == QStringLiteral("shelf");
    m_renameShelfButton->setEnabled(shelfSelected);
    m_deleteShelfButton->setEnabled(shelfSelected);
    updateBreadcrumb();
    refreshBooks();
}

void LibraryView::refreshBooks()
{
    if (!m_manager || !m_manager->isOpen())
        return;
    const QString keepId = selectedBookId();
    const Navigation nav = currentNavigation();
    const QString query = m_search ? m_search->text().trimmed() : QString();

    QVector<ShelfRecord> folders;
    QVector<BookInfo> books = m_manager->books(true);
    QVector<BookInfo> filtered;
    filtered.reserve(books.size());

    const ShelfRecord *selectedShelf = nav.kind == Navigation::Shelf
        ? m_manager->data().shelf(nav.shelfId) : nullptr;
    QSet<QString> directBookIds;
    if (selectedShelf)
        directBookIds = QSet<QString>(selectedShelf->bookIds.begin(), selectedShelf->bookIds.end());

    for (const BookInfo &book : books) {
        if (nav.kind == Navigation::Trash) {
            if (!book.deleted)
                continue;
        } else if (book.deleted) {
            continue;
        }
        if (nav.kind == Navigation::Favorites && !book.favorite)
            continue;
        if (nav.kind == Navigation::Shelf && !directBookIds.contains(book.id))
            continue;
        if (!query.isEmpty()
            && !book.title.contains(query, Qt::CaseInsensitive)
            && !book.author.contains(query, Qt::CaseInsensitive))
            continue;
        filtered.append(book);
    }

    if (nav.kind == Navigation::Shelf) {
        for (const ShelfRecord &shelf : m_manager->data().shelves) {
            if (shelf.parentId != nav.shelfId)
                continue;
            if (!query.isEmpty() && !shelf.name.contains(query, Qt::CaseInsensitive))
                continue;
            folders.append(shelf);
        }
    }

    const QString sortKey = m_sort ? m_sort->currentData().toString() : QStringLiteral("title");
    std::sort(filtered.begin(), filtered.end(), [this, sortKey](const BookInfo &a, const BookInfo &b) {
        if (sortKey == QStringLiteral("favorite") && a.favorite != b.favorite)
            return a.favorite;
        if (sortKey == QStringLiteral("imported"))
            return a.importedAt > b.importedAt;
        if (sortKey == QStringLiteral("author"))
            return a.author.localeAwareCompare(b.author) < 0;
        if (sortKey == QStringLiteral("recent"))
            return m_manager->data().history.value(a.id) > m_manager->data().history.value(b.id);
        return a.title.localeAwareCompare(b.title) < 0;
    });
    std::sort(folders.begin(), folders.end(), [](const ShelfRecord &a, const ShelfRecord &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    QSignalBlocker blocker(m_books);
    m_books->clear();
    for (const ShelfRecord &shelf : folders) {
        auto *item = new QListWidgetItem(
            IconManager::instance()->icon(QStringLiteral("folder")), shelf.name);
        item->setData(Qt::UserRole, QStringLiteral("shelf"));
        item->setData(Qt::UserRole + 1, shelf.id);
        item->setSizeHint(m_coverMode ? QSize(138, 76) : QSize(0, 52));
        item->setToolTip(tr("打开书架：%1").arg(shelf.name));
        m_books->addItem(item);
    }
    for (const BookInfo &book : filtered) {
        const QString text = m_coverMode
            ? QStringLiteral("%1\n%2").arg(book.title,
                                             book.author.isEmpty() ? tr("未知作者") : book.author)
            : QStringLiteral("%1 — %2").arg(book.title,
                                              book.author.isEmpty() ? tr("未知作者") : book.author);
        auto *item = new QListWidgetItem(coverIcon(book, m_coverMode ? QSize(112, 150) : QSize(44, 58)), text);
        item->setData(Qt::UserRole, QStringLiteral("book"));
        item->setData(Qt::UserRole + 1, book.id);
        item->setData(Qt::UserRole + 2, book.deleted);
        item->setSizeHint(m_coverMode ? QSize(138, 188) : QSize(0, 66));
        item->setToolTip(book.pdfPath);
        if (book.deleted)
            item->setText(text + QStringLiteral("\n") + tr("（回收站）"));
        m_books->addItem(item);
    }
    if (!keepId.isEmpty()) {
        for (int row = 0; row < m_books->count(); ++row) {
            if (m_books->item(row)->data(Qt::UserRole).toString() == QStringLiteral("book")
                && m_books->item(row)->data(Qt::UserRole + 1).toString() == keepId) {
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
    if (!item || item->data(Qt::UserRole).toString() != QStringLiteral("book"))
        return QString();
    return item->data(Qt::UserRole + 1).toString();
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
    const QListWidgetItem *item = m_books ? m_books->currentItem() : nullptr;
    if (item && item->data(Qt::UserRole).toString() == QStringLiteral("shelf")) {
        const QString shelfId = item->data(Qt::UserRole + 1).toString();
        const ShelfRecord *shelf = m_manager ? m_manager->data().shelf(shelfId) : nullptr;
        m_cover->setPixmap(IconManager::instance()->icon(QStringLiteral("folder"), 96)
                               .pixmap(96, 96));
        m_title->setText(shelf ? shelf->name : tr("书架"));
        m_author->clear();
        m_meta->setText(tr("书架内容：%1 本；上级：%2")
                            .arg(shelf ? shelf->bookIds.size() : 0)
                            .arg(shelf && !shelf->parentId.isEmpty()
                                     ? shelfPath(shelf->parentId) : tr("根书架")));
        m_source->setText(tr("双击文件夹可进入子书架。"));
        m_counts->clear();
        for (QPushButton *button : {m_openButton, m_favoriteButton, m_shelfButton, m_trashButton,
                                    m_restoreButton, m_deleteButton, m_sourceButton}) {
            button->setEnabled(false);
        }
        return;
    }

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
    const ReadingState reading = m_manager ? m_manager->readingState(book.id) : ReadingState();
    QString readingText;
    if (reading.valid)
        readingText = tr("\n上次阅读：第 %1 页").arg(reading.pageIndex + 1);
    m_meta->setText(tr("导入：%1\n大小：%2\n模式：%3%4")
                        .arg(book.importedAt.left(10), humanSize(book.sizeBytes),
                             importModeDisplayName(book.mode), readingText));
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
    const QListWidgetItem *item = m_books ? m_books->currentItem() : nullptr;
    if (item && item->data(Qt::UserRole).toString() == QStringLiteral("shelf")) {
        selectShelf(item->data(Qt::UserRole + 1).toString());
        return;
    }
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
    const int initial = m_settings
            && m_settings->settings().defaultImportMode == ImportMode::Reference
        ? 1
        : 0;
    const QString choice = QInputDialog::getItem(this, tr("导入方式"), tr("选择导入方式："), options,
                                                 initial, false, &accepted);
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
    if (m_beforeTrash && !m_beforeTrash(book.id))
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
        this, tr("删除书架"), tr("删除书架《%1》？书籍本身不会被删除；直接子书架会移到上一级。").arg(shelf->name),
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

void LibraryView::moveShelfDialog()
{
    const Navigation nav = currentNavigation();
    if (nav.kind != Navigation::Shelf || !m_manager)
        return;
    const ShelfRecord *selected = m_manager->data().shelf(nav.shelfId);
    if (!selected)
        return;
    QStringList names{tr("根目录")};
    QStringList ids{QString()};
    for (const ShelfRecord &shelf : m_manager->data().shelves) {
        if (shelf.id == selected->id)
            continue;
        bool descendant = false;
        QString cursor = shelf.id;
        while (!cursor.isEmpty()) {
            if (cursor == selected->id) {
                descendant = true;
                break;
            }
            const ShelfRecord *parent = m_manager->data().shelf(cursor);
            cursor = parent ? parent->parentId : QString();
        }
        if (descendant)
            continue;
        names.append(shelfPath(shelf.id));
        ids.append(shelf.id);
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("移动书架"), tr("目标位置："),
                                                  names, 0, false, &accepted);
    if (!accepted)
        return;
    const int index = names.indexOf(chosen);
    if (index < 0)
        return;
    QString error;
    if (!m_manager->moveShelf(selected->id, ids.value(index), &error)) {
        QMessageBox::warning(this, tr("移动书架失败"), error);
        return;
    }
    refresh();
    selectShelf(selected->id);
}

void LibraryView::showNavContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = m_navigation->itemAt(position);
    if (!item)
        return;
    m_navigation->setCurrentItem(item);
    const QString kind = item->data(0, Qt::UserRole).toString();
    const QString shelfId = item->data(0, Qt::UserRole + 1).toString();
    QMenu menu(this);
    QAction *create = menu.addAction(tr("新建子书架…"));
    create->setEnabled(kind == QStringLiteral("all") || kind == QStringLiteral("shelf"));
    QAction *rename = nullptr;
    QAction *remove = nullptr;
    QAction *move = nullptr;
    if (kind == QStringLiteral("shelf")) {
        rename = menu.addAction(tr("重命名书架…"));
        move = menu.addAction(tr("移动到书架…"));
        remove = menu.addAction(tr("删除书架"));
    }
    QAction *chosen = menu.exec(m_navigation->viewport()->mapToGlobal(position));
    if (!chosen)
        return;
    if (chosen == create) {
        onNavigationChanged(m_navigation->currentItem(), nullptr);
        createShelf();
    } else if (rename && chosen == rename) {
        selectShelf(shelfId);
        renameShelf();
    } else if (move && chosen == move) {
        selectShelf(shelfId);
        moveShelfDialog();
    } else if (remove && chosen == remove) {
        selectShelf(shelfId);
        deleteShelf();
    }
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
        if (m_books->item(row)->data(Qt::UserRole).toString() == QStringLiteral("book")
            && m_books->item(row)->data(Qt::UserRole + 1).toString() == bookId) {
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
    if (!m_manager || !m_manager->isOpen())
        return;
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-pano-book"))) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    if (!event->mimeData()->hasUrls())
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

void LibraryView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_manager || !m_manager->isOpen())
        return;
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-pano-book"))) {
        const QPoint pos = m_navigation->viewport()->mapFrom(this, event->position().toPoint());
        if (m_navigation->itemAt(pos)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        }
        return;
    }
    event->acceptProposedAction();
}

void LibraryView::dropEvent(QDropEvent *event)
{
    if (!m_manager || !m_manager->isOpen())
        return;

    if (event->mimeData()->hasFormat(QStringLiteral("application/x-pano-book"))) {
        const QString bookId =
            QString::fromUtf8(event->mimeData()->data(QStringLiteral("application/x-pano-book")));
        const QPoint pos = m_navigation->viewport()->mapFrom(this, event->position().toPoint());
        QTreeWidgetItem *target = m_navigation->itemAt(pos);
        if (!target || bookId.isEmpty())
            return;
        const QString kind = target->data(0, Qt::UserRole).toString();
        const QString targetShelf = target->data(0, Qt::UserRole + 1).toString();
        const Navigation nav = currentNavigation();
        const QString sourceShelf = nav.kind == Navigation::Shelf ? nav.shelfId : QString();
        QString error;
        bool ok = true;
        if (kind == QStringLiteral("favorites")) {
            ok = m_manager->setFavorite(bookId, true, &error);
        } else if (kind == QStringLiteral("trash")) {
            if (QMessageBox::question(this, tr("移入回收站"),
                                      tr("把这本书记录移入回收站？源 PDF 不会被删除。"))
                == QMessageBox::Yes) {
                ok = (!m_beforeTrash || m_beforeTrash(bookId))
                    && m_manager->moveToTrash(bookId, &error);
            } else {
                return;
            }
        } else if (kind == QStringLiteral("shelf")) {
            const bool additive = event->modifiers().testFlag(Qt::ControlModifier);
            if (!additive && !sourceShelf.isEmpty() && sourceShelf != targetShelf)
                ok = m_manager->removeBookFromShelf(sourceShelf, bookId, &error);
            if (ok)
                ok = m_manager->addBookToShelf(targetShelf, bookId, &error);
        } else {
            return;
        }
        if (!ok) {
            setStatus(error);
            QMessageBox::warning(this, tr("操作失败"), error);
            return;
        }
        event->acceptProposedAction();
        refresh();
        if (kind == QStringLiteral("shelf"))
            selectShelf(targetShelf);
        return;
    }

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
