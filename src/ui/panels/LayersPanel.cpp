#include "ui/panels/LayersPanel.h"

#include "app/Theme.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "core/LayerStyle.h"
#include "core/LayerTree.h"
#include "ui/Widgets.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>

namespace {

constexpr int kRowHeight = 42;
constexpr int kEyeWidth = 28;
constexpr int kThumb = 32;
constexpr int kIndent = 18;
const char* kMime = "application/x-photoslop-layer-row";

QPixmap renderThumb(const Layer& l, const QSize& canvas)
{
    const qreal dpr = qApp->devicePixelRatio();
    const QSize box = QSize(kThumb, kThumb) * dpr;
    const double s = std::min(double(box.width()) / canvas.width(), double(box.height()) / canvas.height());
    const QSize ts(std::max(1, int(canvas.width() * s)), std::max(1, int(canvas.height() * s)));
    QImage out(ts, QImage::Format_ARGB32_Premultiplied);
    // Checkerboard for transparency.
    QPainter p(&out);
    const int c = std::max(2, int(4 * dpr));
    for (int y = 0; y < ts.height(); y += c)
        for (int x = 0; x < ts.width(); x += c)
            p.fillRect(x, y, c, c, ((x + y) / c) % 2 ? QColor(0xcc, 0xcc, 0xcc) : Qt::white);
    if (!l.image.isNull()) {
        QImage src = l.image;
        const QSize target(std::max(1, int(src.width() * s)), std::max(1, int(src.height() * s)));
        if (src.width() > target.width() * 4) src = src.scaled(target * 4, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(l.offset.x() * s, l.offset.y() * s, l.image.width() * s, l.image.height() * s), src);
    }
    p.end();
    QPixmap pm = QPixmap::fromImage(out);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QPixmap renderMaskThumb(const Layer& l, const QSize& canvas)
{
    const qreal dpr = qApp->devicePixelRatio();
    const QSize box = QSize(kThumb, kThumb) * dpr;
    const double s = std::min(double(box.width()) / canvas.width(), double(box.height()) / canvas.height());
    const QSize ts(std::max(1, int(canvas.width() * s)), std::max(1, int(canvas.height() * s)));
    QImage out(ts, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(l.maskDefault, l.maskDefault, l.maskDefault));
    if (l.mask && !l.mask->image.isNull()) {
        // Transparent mask pixels show the default, which the fill already provides.
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const Layer& m = *l.mask;
        p.drawImage(QRectF(m.offset.x() * s, m.offset.y() * s, m.image.width() * s, m.image.height() * s), m.image);
    }
    QPixmap pm = QPixmap::fromImage(out);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

} // namespace

// ---------------- LayerModel ----------------

LayerModel::LayerModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void LayerModel::rebuildRows()
{
    m_rows.clear();
    if (!m_doc) return;
    const QList<Layer>& layers = m_doc->layers();
    for (int i = int(layers.size()) - 1; i >= 0; --i)
        if (Tree::shownInPanel(layers, i)) m_rows.append(i);
}

void LayerModel::setDocument(Document* doc)
{
    beginResetModel();
    m_doc = doc;
    m_thumbs.clear();
    rebuildRows();
    endResetModel();
}

void LayerModel::reset()
{
    beginResetModel();
    rebuildRows();
    endResetModel();
}

int LayerModel::docIndex(int row) const { return row >= 0 && row < m_rows.size() ? m_rows[row] : -1; }

int LayerModel::rowFor(int docIndex) const { return int(m_rows.indexOf(docIndex)); }

QPixmap LayerModel::thumbnail(int docIndex)
{
    const Layer& l = m_doc->layerAt(docIndex);
    auto it = m_thumbs.find(l.id);
    if (it != m_thumbs.end() && it->key == l.image.cacheKey() && it->offset == l.offset && it->canvas == m_doc->size())
        return it->pixmap;
    Thumb t{l.image.cacheKey(), l.offset, m_doc->size(), 0, renderThumb(l, m_doc->size())};
    m_thumbs.insert(l.id, t);
    return t.pixmap;
}

QPixmap LayerModel::maskThumbnail(int docIndex)
{
    const Layer& l = m_doc->layerAt(docIndex);
    if (!l.mask) return QPixmap();
    const Layer& m = *l.mask;
    auto it = m_thumbs.find(m.id);
    if (it != m_thumbs.end() && it->key == m.image.cacheKey() && it->offset == m.offset && it->canvas == m_doc->size()
        && it->extra == l.maskDefault)
        return it->pixmap;
    Thumb t{m.image.cacheKey(), m.offset, m_doc->size(), l.maskDefault, renderMaskThumb(l, m_doc->size())};
    m_thumbs.insert(m.id, t);
    return t.pixmap;
}

int LayerModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !m_doc ? 0 : int(m_rows.size());
}

QVariant LayerModel::data(const QModelIndex& index, int role) const
{
    if (!m_doc || !index.isValid()) return {};
    const int i = docIndex(index.row());
    if (i < 0 || i >= m_doc->layerCount()) return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole) return m_doc->layerAt(i).name;
    return {};
}

bool LayerModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!m_doc || role != Qt::EditRole) return false;
    Ops::rename(m_doc, docIndex(index.row()), value.toString().trimmed());
    return true;
}

Qt::ItemFlags LayerModel::flags(const QModelIndex& index) const
{
    if (!m_doc || !index.isValid()) return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    const Layer& l = m_doc->layerAt(docIndex(index.row()));
    if (!l.isBackground) f |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    return f;
}

QStringList LayerModel::mimeTypes() const { return {QString::fromLatin1(kMime)}; }

QMimeData* LayerModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty()) return nullptr;
    auto* md = new QMimeData;
    md->setData(QString::fromLatin1(kMime), QByteArray::number(docIndex(indexes.first().row())));
    return md;
}

bool LayerModel::dropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent)
{
    if (!m_doc || !data->hasFormat(QString::fromLatin1(kMime))) return false;
    const int from = data->data(QString::fromLatin1(kMime)).toInt();
    if (from < 0 || from >= m_doc->layerCount()) return false;
    const QList<Layer>& layers = m_doc->layers();
    if (row < 0 && parent.isValid()) {
        // Dropped onto a row: into a group, or just above a layer.
        const int target = docIndex(parent.row());
        if (layers[target].isGroup()) Ops::moveLayerInto(m_doc, from, target);
        else Ops::moveLayerAbove(m_doc, from, target);
        return false;
    }
    if (row < 0) row = rowCount();
    // Between rows: below the row above (at the top of it, if it is an open group).
    const int above = row > 0 ? docIndex(row - 1) : -1;
    const int below = row < rowCount() ? docIndex(row) : -1;
    if (above >= 0 && layers[above].isGroup() && layers[above].expanded) Ops::moveLayerInto(m_doc, from, above);
    else if (above >= 0) Ops::moveLayerBelow(m_doc, from, above);
    else if (below >= 0) Ops::moveLayerAbove(m_doc, from, below);
    // Returning false stops the view from removing the source row itself.
    return false;
}

// ---------------- LayerDelegate ----------------

LayerDelegate::LayerDelegate(LayerModel* model, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_model(model)
{
}

QSize LayerDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(200, kRowHeight);
}

QRect LayerDelegate::partRect(int di, Part part, int width) const
{
    Document* doc = m_model->document();
    if (!doc || di < 0 || di >= doc->layerCount()) return QRect();
    const QList<Layer>& layers = doc->layers();
    const Layer& l = layers[di];
    const int x0 = kEyeWidth + 4 + Tree::depth(layers, di) * kIndent;
    const bool prefix = l.isGroup() || Tree::isClipped(layers, di);
    const QRect thumb(x0 + (prefix ? 16 : 2), (kRowHeight - kThumb) / 2, kThumb, kThumb);
    const QRect link(thumb.right() + 3, 15, 10, 12);
    const QRect mask(link.right() + 3, thumb.top(), kThumb, kThumb);
    const int lockW = (l.isBackground || l.hasAnyLock()) ? 20 : 0;
    const QRect fx(width - lockW - 24, 13, 20, 16);
    switch (part) {
    case Part::Eye: return QRect(0, 0, kEyeWidth, kRowHeight);
    case Part::Disclosure: return l.isGroup() ? QRect(x0, 13, 16, 16) : QRect();
    case Part::Thumbnail: return thumb;
    case Part::Link: return l.mask ? link : QRect();
    case Part::Mask: return l.mask ? mask : QRect();
    case Part::Effects: return l.style ? fx : QRect();
    case Part::Name: {
        const int left = (l.mask ? mask.right() : thumb.right()) + 8;
        const int right = (l.style ? fx.left() : width - lockW) - 4;
        return QRect(left, 0, std::max(10, right - left), kRowHeight);
    }
    case Part::None: break;
    }
    return QRect();
}

LayerDelegate::Part LayerDelegate::partAt(int di, const QPoint& pos, int width) const
{
    for (Part p : {Part::Eye, Part::Disclosure, Part::Link, Part::Mask, Part::Thumbnail, Part::Effects, Part::Name})
        if (partRect(di, p, width).contains(pos)) return p;
    return Part::None;
}

void LayerDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const
{
    Document* doc = m_model->document();
    if (!doc) return;
    const int di = m_model->docIndex(index.row());
    if (di < 0) return;
    const QList<Layer>& layers = doc->layers();
    const Layer& l = layers[di];
    const QRect r = opt.rect;
    const QPoint o = r.topLeft();
    const bool selected = di == doc->activeIndex();
    auto at = [&](Part part) { return partRect(di, part, r.width()).translated(o); };

    p->save();
    p->fillRect(r, selected ? Theme::kRowSelected : Theme::kPanel);
    p->setPen(QColor(0x26, 0x26, 0x26));
    p->drawLine(r.bottomLeft(), r.bottomRight());
    p->drawLine(QPoint(r.left() + kEyeWidth, r.top()), QPoint(r.left() + kEyeWidth, r.bottom()));

    if (l.visible) {
        // Dimmed when a containing group is hidden.
        p->setOpacity(Tree::effectivelyVisible(layers, di) ? 1.0 : 0.4);
        Theme::icon(QStringLiteral("eye")).paint(p, QRect(r.left() + 6, r.top() + 13, 16, 16));
        p->setOpacity(1.0);
    }
    if (l.isGroup())
        Theme::icon(l.expanded ? QStringLiteral("disclosure-open") : QStringLiteral("disclosure-closed")).paint(p, at(Part::Disclosure));
    else if (Tree::isClipped(layers, di))
        Theme::icon(QStringLiteral("clip")).paint(p, QRect(at(Part::Thumbnail).left() - 16, r.top() + 13, 14, 16));

    const QRect thumb = at(Part::Thumbnail);
    const bool maskTarget = selected && doc->editingMask();
    auto frame = [&](const QRect& rr, bool target) {
        p->setPen(target ? QPen(Qt::white, 2) : QPen(selected ? QColor(0xd6, 0xd6, 0xd6) : QColor(0x1e, 0x1e, 0x1e), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(target ? rr.adjusted(-2, -2, 1, 1) : rr.adjusted(-1, -1, 0, 0));
    };
    switch (l.kind) {
    case LayerKind::Group:
        Theme::icon(QStringLiteral("folder")).paint(p, thumb.adjusted(4, 4, -4, -4));
        break;
    case LayerKind::Adjustment:
        Theme::icon(QStringLiteral("adjustment")).paint(p, thumb.adjusted(5, 5, -5, -5));
        break;
    case LayerKind::Text:
        Theme::icon(QStringLiteral("layer-text")).paint(p, thumb.adjusted(2, 2, -2, -2));
        break;
    default: {
        QPixmap pm = m_model->thumbnail(di);
        QRect pr(QPoint(), pm.deviceIndependentSize().toSize());
        pr.moveCenter(thumb.center());
        p->drawPixmap(pr, pm);
        frame(pr, selected && !maskTarget && l.mask);
        break;
    }
    }
    if (l.mask) {
        if (l.maskLinked) Theme::icon(QStringLiteral("link")).paint(p, at(Part::Link));
        QPixmap mp = m_model->maskThumbnail(di);
        QRect mr(QPoint(), mp.deviceIndependentSize().toSize());
        mr.moveCenter(at(Part::Mask).center());
        p->drawPixmap(mr, mp);
        frame(mr, maskTarget);
        if (!l.maskEnabled) {
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(QPen(QColor(220, 40, 40), 2));
            p->drawLine(mr.topLeft(), mr.bottomRight());
            p->drawLine(mr.topRight(), mr.bottomLeft());
        }
    }

    QFont f = opt.font;
    f.setItalic(l.isBackground);
    p->setFont(f);
    p->setPen(selected ? Qt::white : Theme::kText);
    const QRect textRect = at(Part::Name);
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, p->fontMetrics().elidedText(l.name, Qt::ElideRight, textRect.width()));
    if (l.style) {
        p->setOpacity(l.style->visible ? 1.0 : 0.4);
        Theme::icon(QStringLiteral("fx")).paint(p, at(Part::Effects));
        p->setOpacity(1.0);
    }
    if (l.isBackground || l.hasAnyLock()) {
        const bool full = l.isBackground || l.lockAll;
        p->setOpacity(full ? 1.0 : 0.55);
        Theme::icon(QStringLiteral("lock")).paint(p, QRect(r.right() - 20, r.top() + 14, 14, 14));
    }
    p->restore();
}

bool LayerDelegate::editorEvent(QEvent* e, QAbstractItemModel*, const QStyleOptionViewItem& opt, const QModelIndex& index)
{
    Document* doc = m_model->document();
    if (!doc || (e->type() != QEvent::MouseButtonPress && e->type() != QEvent::MouseButtonDblClick)) return false;
    auto* me = static_cast<QMouseEvent*>(e);
    if (me->button() != Qt::LeftButton) return false;
    const QPoint pos = me->position().toPoint() - opt.rect.topLeft();
    const int di = m_model->docIndex(index.row());
    if (di < 0) return false;
    const Part part = partAt(di, pos, opt.rect.width());
    if (e->type() == QEvent::MouseButtonDblClick) {
        if (part == Part::Eye || part == Part::Disclosure) return true;
        emit doubleClicked(di, part);
        return true;
    }
    const Qt::KeyboardModifiers mods = me->modifiers();
    auto selOp = [&] {
        if (mods & Qt::ShiftModifier) return Sel::Op::Add;
        if (mods & Qt::AltModifier) return Sel::Op::Subtract;
        return Sel::Op::Replace;
    };
    switch (part) {
    case Part::Eye:
        if (mods & Qt::AltModifier) Ops::soloVisibility(doc, di);
        else Ops::setVisible(doc, di, !doc->layerAt(di).visible);
        return true;
    case Part::Disclosure:
        Ops::setExpanded(doc, di, !doc->layerAt(di).expanded);
        return true;
    case Part::Link:
        Ops::setMaskLinked(doc, di, !doc->layerAt(di).maskLinked);
        return true;
    case Part::Mask:
        if (mods & Qt::ControlModifier) {
            // Ctrl/Cmd-click loads the mask as a selection.
            Ops::loadSelectionFromMask(doc, di, selOp());
        } else if (mods & Qt::ShiftModifier) {
            // Shift-click disables or enables the mask.
            Ops::setMaskEnabled(doc, di, !doc->layerAt(di).maskEnabled);
        } else {
            doc->setActiveIndex(di);
            doc->setMaskTargeted(true);
        }
        return true;
    case Part::Thumbnail:
        if (mods & Qt::ControlModifier) {
            // Ctrl/Cmd-click the thumbnail loads the layer's transparency as a selection.
            Ops::loadSelectionFromLayer(doc, di, selOp());
            return true;
        }
        doc->setActiveIndex(di);
        doc->setMaskTargeted(false);
        return false; // let the view start a drag
    default:
        return false;
    }
}

void LayerDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& opt, const QModelIndex& index) const
{
    const QRect name = partRect(m_model->docIndex(index.row()), Part::Name, opt.rect.width()).translated(opt.rect.topLeft());
    editor->setGeometry(name.left() - 2, opt.rect.top() + 10, name.width(), opt.rect.height() - 20);
}

// ---------------- LayersPanel ----------------

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent)
    , m_model(new LayerModel(this))
    , m_thumbTimer(new QTimer(this))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 4, 0, 0);
    root->setSpacing(4);

    // Filter row (Photoshop's "Kind" filter).
    auto* filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(6, 0, 6, 0);
    auto* kind = new QComboBox(this);
    kind->addItem(QStringLiteral("Kind"));
    kind->setEnabled(false);
    filterRow->addWidget(kind);
    filterRow->addStretch();
    root->addLayout(filterRow);

    auto* modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(6, 0, 6, 0);
    m_mode = new BlendModeCombo(this);
    m_mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_mode->setMinimumWidth(110);
    m_opacity = new ValueField(QStringLiteral("Opacity:"), 0, 100, QStringLiteral("%"), true, this);
    modeRow->addWidget(m_mode, 1);
    modeRow->addWidget(m_opacity);
    root->addLayout(modeRow);

    auto* lockRow = new QHBoxLayout;
    lockRow->setContentsMargins(6, 0, 6, 0);
    lockRow->setSpacing(2);
    lockRow->addWidget(new QLabel(QStringLiteral("Lock:"), this));
    m_lockTransparency = makeIconButton(QStringLiteral("lock-transparency"), QStringLiteral("Lock transparent pixels"), this, true, 20);
    m_lockPixels = makeIconButton(QStringLiteral("lock-pixels"), QStringLiteral("Lock image pixels"), this, true, 20);
    m_lockPosition = makeIconButton(QStringLiteral("lock-position"), QStringLiteral("Lock position"), this, true, 20);
    m_lockAll = makeIconButton(QStringLiteral("lock-all"), QStringLiteral("Lock all"), this, true, 20);
    for (QToolButton* b : {m_lockTransparency, m_lockPixels, m_lockPosition, m_lockAll}) lockRow->addWidget(b);
    lockRow->addStretch();
    m_fill = new ValueField(QStringLiteral("Fill:"), 0, 100, QStringLiteral("%"), true, this);
    lockRow->addWidget(m_fill);
    root->addLayout(lockRow);

    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_delegate = new LayerDelegate(m_model, this);
    m_list->setItemDelegate(m_delegate);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    // Double-clicks are handled per part of the row (rename, style, settings).
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_list->setUniformItemSizes(true);
    m_list->setStyleSheet(QStringLiteral("QListView { border-top: 1px solid #262626; border-bottom: 1px solid #262626; }"));
    root->addWidget(m_list, 1);

    // Bottom button bar.
    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(6, 2, 6, 4);
    bottom->setSpacing(2);
    bottom->addStretch();
    auto* link = makeIconButton(QStringLiteral("link"), QStringLiteral("Link layers"), this, false, 22);
    link->setEnabled(false);
    bottom->addWidget(link);
    auto* fx = makeIconButton(QStringLiteral("fx"), QStringLiteral("Add a layer style"), this, false, 22);
    connect(fx, &QToolButton::clicked, this, [this, fx] {
        popupMenu(fx, {QStringLiteral("layer.styleBlending"), QStringLiteral("-"), QStringLiteral("layer.styleStroke"),
                       QStringLiteral("layer.styleColorOverlay"), QStringLiteral("layer.styleOuterGlow"),
                       QStringLiteral("layer.styleDropShadow")});
    });
    bottom->addWidget(fx);
    auto* mask = makeIconButton(QStringLiteral("mask"), QStringLiteral("Add layer mask"), this, false, 22);
    connect(mask, &QToolButton::clicked, this, [this] { trigger(QStringLiteral("layer.maskAdd")); });
    bottom->addWidget(mask);
    auto* adj = makeIconButton(QStringLiteral("adjustment"), QStringLiteral("Create new fill or adjustment layer"), this, false, 22);
    connect(adj, &QToolButton::clicked, this, [this, adj] {
        popupMenu(adj, {QStringLiteral("layer.newAdjustment.brightnessContrast"), QStringLiteral("layer.newAdjustment.levels"),
                        QStringLiteral("layer.newAdjustment.curves"), QStringLiteral("-"),
                        QStringLiteral("layer.newAdjustment.hueSaturation"), QStringLiteral("layer.newAdjustment.colorBalance"),
                        QStringLiteral("layer.newAdjustment.blackWhite"), QStringLiteral("-"),
                        QStringLiteral("layer.newAdjustment.invert"), QStringLiteral("layer.newAdjustment.posterize"),
                        QStringLiteral("layer.newAdjustment.threshold")});
    });
    bottom->addWidget(adj);
    const struct { const char* icon; const char* tip; const char* action; } buttons[] = {
        {"folder", "Create a new group", "layer.newGroupQuick"},
        {"new-layer", "Create a new layer", "layer.newQuick"},
        {"trash", "Delete layer", "layer.delete"},
    };
    for (const auto& b : buttons) {
        auto* btn = makeIconButton(QString::fromLatin1(b.icon), QString::fromLatin1(b.tip), this, false, 22);
        const QString id = QString::fromLatin1(b.action);
        connect(btn, &QToolButton::clicked, this, [this, id] { trigger(id); });
        bottom->addWidget(btn);
    }
    root->addLayout(bottom);

    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& cur) {
        if (m_syncing || !m_doc || !cur.isValid()) return;
        m_doc->setActiveIndex(m_model->docIndex(cur.row()));
    });
    connect(m_delegate, &LayerDelegate::doubleClicked, this, &LayersPanel::rowDoubleClicked);
    connect(m_mode, &BlendModeCombo::modeChanged, this, [this](BlendMode m) {
        if (!m_syncing && m_doc) Ops::setBlendMode(m_doc, m_doc->activeIndex(), m);
    });
    connect(m_opacity, &ValueField::valueChanged, this, [this](int v) {
        if (!m_syncing && m_doc) Ops::setOpacity(m_doc, m_doc->activeIndex(), v / 100.f);
    });
    connect(m_fill, &ValueField::valueChanged, this, [this](int v) {
        if (!m_syncing && m_doc) Ops::setFill(m_doc, m_doc->activeIndex(), v / 100.f);
    });
    auto applyLocks = [this] {
        if (m_syncing || !m_doc) return;
        Ops::setLocks(m_doc, m_doc->activeIndex(), m_lockTransparency->isChecked(), m_lockPixels->isChecked(),
                      m_lockPosition->isChecked(), m_lockAll->isChecked());
    };
    for (QToolButton* b : {m_lockTransparency, m_lockPixels, m_lockPosition, m_lockAll})
        connect(b, &QToolButton::toggled, this, applyLocks);

    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(250);
    connect(m_thumbTimer, &QTimer::timeout, this, [this] { m_list->viewport()->update(); });

    setDocument(nullptr);
}

void LayersPanel::trigger(const QString& id)
{
    if (!m_lookup) return;
    if (QAction* a = m_lookup(id); a && a->isEnabled()) a->trigger();
}

void LayersPanel::popupMenu(QToolButton* under, const QStringList& ids)
{
    if (!m_lookup) return;
    QMenu menu(this);
    for (const QString& id : ids) {
        if (id == QLatin1String("-")) menu.addSeparator();
        else if (QAction* a = m_lookup(id)) menu.addAction(a);
    }
    menu.exec(under->mapToGlobal(QPoint(0, under->height())));
}

void LayersPanel::rowDoubleClicked(int di, LayerDelegate::Part part)
{
    if (!m_doc || di < 0 || di >= m_doc->layerCount()) return;
    const Layer& l = m_doc->layerAt(di);
    if (l.isBackground) {
        trigger(QStringLiteral("layer.fromBackground"));
        return;
    }
    m_doc->setActiveIndex(di);
    using Part = LayerDelegate::Part;
    if (part == Part::Name) {
        m_list->edit(m_model->index(m_model->rowFor(di)));
        return;
    }
    if (part == Part::Thumbnail && l.kind == LayerKind::Adjustment) {
        trigger(QStringLiteral("layer.contentOptions"));
        return;
    }
    if (part == Part::Thumbnail && l.kind == LayerKind::Text) {
        trigger(QStringLiteral("type.edit"));
        return;
    }
    if (part == Part::Mask) return;
    // Anywhere else opens Layer Style, as in Photoshop.
    trigger(QStringLiteral("layer.styleBlending"));
}

void LayersPanel::setDocument(Document* doc)
{
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    m_model->setDocument(doc);
    if (doc) {
        connect(doc, &Document::layersChanged, this, [this] {
            m_model->reset();
            syncControls();
            syncSelection();
        });
        connect(doc, &Document::activeLayerChanged, this, [this] {
            syncControls();
            syncSelection();
            m_list->viewport()->update();
        });
        connect(doc, &Document::editTargetChanged, this, [this] { m_list->viewport()->update(); });
        connect(doc, &Document::layerPixelsChanged, this, [this] {
            if (!m_thumbTimer->isActive()) m_thumbTimer->start();
        });
    }
    for (QWidget* w : std::initializer_list<QWidget*>{m_mode, m_opacity, m_fill, m_lockTransparency, m_lockPixels,
                                                       m_lockPosition, m_lockAll})
        w->setEnabled(doc != nullptr);
    syncControls();
    syncSelection();
}

void LayersPanel::syncControls()
{
    if (!m_doc || !m_doc->activeLayer()) return;
    m_syncing = true;
    const Layer& l = *m_doc->activeLayer();
    m_mode->setPassThroughAllowed(l.isGroup());
    m_mode->setMode(l.mode);
    m_opacity->setValue(int(std::round(l.opacity * 100)));
    m_fill->setValue(int(std::round(l.fill * 100)));
    m_lockTransparency->setChecked(l.lockTransparency);
    m_lockPixels->setChecked(l.lockPixels);
    m_lockPosition->setChecked(l.lockPosition || l.isBackground);
    m_lockAll->setChecked(l.lockAll || l.isBackground);
    // The Background layer is fully locked and opaque, as in Photoshop.
    const bool bg = l.isBackground;
    for (QWidget* w : std::initializer_list<QWidget*>{m_mode, m_opacity, m_fill, m_lockTransparency,
                                                       m_lockPosition, m_lockAll})
        w->setEnabled(!bg);
    m_lockPixels->setEnabled(true);
    m_syncing = false;
}

void LayersPanel::syncSelection()
{
    if (!m_doc) return;
    // Open the groups around the selected layer so its row is visible.
    const QList<Layer>& layers = m_doc->layers();
    const int active = m_doc->activeIndex();
    if (active >= 0 && active < layers.size() && !Tree::shownInPanel(layers, active)) {
        for (int p = Tree::parentIndex(layers, active); p >= 0; p = Tree::parentIndex(layers, p)) m_doc->layerRef(p).expanded = true;
        m_model->reset();
    }
    m_syncing = true;
    const QModelIndex idx = m_model->index(m_model->rowFor(active));
    m_list->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    m_syncing = false;
}

void LayersPanel::contextMenuEvent(QContextMenuEvent* e)
{
    if (!m_doc || !m_lookup) return;
    QMenu menu(this);
    for (const char* id : {"layer.styleBlending", "-", "layer.duplicate", "layer.delete", "-", "layer.group", "layer.ungroup",
                           "-", "layer.fromBackground", "layer.maskAdd", "layer.maskToggle", "layer.maskApply", "layer.maskDelete",
                           "layer.clipping", "-", "layer.rasterizeLayer", "layer.styleCopy", "layer.stylePaste", "layer.styleClear",
                           "-", "layer.mergeDown", "layer.mergeVisible", "layer.flatten"}) {
        if (QLatin1String(id) == QLatin1String("-")) {
            menu.addSeparator();
            continue;
        }
        if (QAction* a = m_lookup(QString::fromLatin1(id)); a && a->isEnabled()) menu.addAction(a);
    }
    menu.exec(e->globalPos());
}
