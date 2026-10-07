#include "ui/panels/LayersPanel.h"

#include "app/Theme.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
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
const QRect kThumbRect(kEyeWidth + 6, 5, 32, 32);
const char* kMime = "application/x-photoslop-layer-row";

QPixmap renderThumb(const Layer& l, const QSize& canvas)
{
    const qreal dpr = qApp->devicePixelRatio();
    const QSize box = (kThumbRect.size() * dpr);
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

} // namespace

// ---------------- LayerModel ----------------

LayerModel::LayerModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void LayerModel::setDocument(Document* doc)
{
    beginResetModel();
    m_doc = doc;
    m_thumbs.clear();
    endResetModel();
}

void LayerModel::reset()
{
    beginResetModel();
    endResetModel();
}

int LayerModel::docIndex(int row) const { return m_doc ? m_doc->layerCount() - 1 - row : -1; }
int LayerModel::rowFor(int docIndex) const { return m_doc ? m_doc->layerCount() - 1 - docIndex : -1; }

QPixmap LayerModel::thumbnail(int docIndex)
{
    const Layer& l = m_doc->layerAt(docIndex);
    auto it = m_thumbs.find(l.id);
    if (it != m_thumbs.end() && it->key == l.image.cacheKey() && it->offset == l.offset && it->canvas == m_doc->size())
        return it->pixmap;
    Thumb t{l.image.cacheKey(), l.offset, m_doc->size(), renderThumb(l, m_doc->size())};
    m_thumbs.insert(l.id, t);
    return t.pixmap;
}

int LayerModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !m_doc ? 0 : m_doc->layerCount();
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
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const Layer& l = m_doc->layerAt(docIndex(index.row()));
    if (!l.isBackground) f |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    return f;
}

QStringList LayerModel::mimeTypes() const { return {QString::fromLatin1(kMime)}; }

QMimeData* LayerModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty()) return nullptr;
    auto* md = new QMimeData;
    md->setData(QString::fromLatin1(kMime), QByteArray::number(indexes.first().row()));
    return md;
}

bool LayerModel::dropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent)
{
    if (!m_doc || !data->hasFormat(QString::fromLatin1(kMime))) return false;
    const int fromRow = data->data(QString::fromLatin1(kMime)).toInt();
    int toRow = row >= 0 ? row : (parent.isValid() ? parent.row() : rowCount());
    // Dropping "between" rows: the target index shifts when moving downwards.
    if (toRow > fromRow) --toRow;
    Ops::moveLayer(m_doc, docIndex(fromRow), docIndex(std::clamp(toRow, 0, rowCount() - 1)));
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

void LayerDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const
{
    Document* doc = m_model->document();
    if (!doc) return;
    const int di = m_model->docIndex(index.row());
    const Layer& l = doc->layerAt(di);
    const QRect r = opt.rect;
    const bool selected = di == doc->activeIndex();

    p->save();
    p->fillRect(r, selected ? Theme::kRowSelected : Theme::kPanel);
    p->setPen(QColor(0x26, 0x26, 0x26));
    p->drawLine(r.bottomLeft(), r.bottomRight());
    p->drawLine(QPoint(r.left() + kEyeWidth, r.top()), QPoint(r.left() + kEyeWidth, r.bottom()));

    if (l.visible) Theme::icon(QStringLiteral("eye")).paint(p, QRect(r.left() + 6, r.top() + 13, 16, 16));

    const QRect thumb = kThumbRect.translated(r.topLeft());
    QPixmap pm = m_model->thumbnail(di);
    QSize ps = pm.deviceIndependentSize().toSize();
    QRect pr(QPoint(), ps);
    pr.moveCenter(thumb.center());
    p->drawPixmap(pr, pm);
    p->setPen(selected ? QColor(0xd6, 0xd6, 0xd6) : QColor(0x1e, 0x1e, 0x1e));
    p->drawRect(pr.adjusted(-1, -1, 0, 0));

    QFont f = opt.font;
    f.setItalic(l.isBackground);
    p->setFont(f);
    p->setPen(selected ? Qt::white : Theme::kText);
    const int lockW = (l.isBackground || l.hasAnyLock()) ? 20 : 0;
    QRect textRect(thumb.right() + 10, r.top(), r.right() - thumb.right() - 14 - lockW, r.height());
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                p->fontMetrics().elidedText(l.name, Qt::ElideRight, textRect.width()));
    if (lockW) {
        const bool full = l.isBackground || l.lockAll;
        p->setOpacity(full ? 1.0 : 0.55);
        Theme::icon(QStringLiteral("lock")).paint(p, QRect(r.right() - 20, r.top() + 14, 14, 14));
    }
    p->restore();
}

bool LayerDelegate::editorEvent(QEvent* e, QAbstractItemModel*, const QStyleOptionViewItem& opt,
                                const QModelIndex& index)
{
    Document* doc = m_model->document();
    if (!doc || e->type() != QEvent::MouseButtonPress) return false;
    auto* me = static_cast<QMouseEvent*>(e);
    if (me->button() != Qt::LeftButton) return false;
    const QPoint pos = me->position().toPoint() - opt.rect.topLeft();
    const int di = m_model->docIndex(index.row());
    if (pos.x() < kEyeWidth) {
        if (me->modifiers() & Qt::AltModifier) Ops::soloVisibility(doc, di);
        else Ops::setVisible(doc, di, !doc->layerAt(di).visible);
        return true;
    }
    if (kThumbRect.contains(pos) && (me->modifiers() & Qt::ControlModifier)) {
        // Ctrl/Cmd-click the thumbnail loads the layer's transparency as a selection.
        Sel::Op op = Sel::Op::Replace;
        if (me->modifiers() & Qt::ShiftModifier) op = Sel::Op::Add;
        if (me->modifiers() & Qt::AltModifier) op = Sel::Op::Subtract;
        Ops::loadSelectionFromLayer(doc, di, op);
        return true;
    }
    return false;
}

void LayerDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& opt, const QModelIndex&) const
{
    const QRect r = opt.rect;
    const int left = kThumbRect.right() + 8;
    editor->setGeometry(r.left() + left, r.top() + 10, r.width() - left - 6, r.height() - 20);
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
    m_list->setItemDelegate(new LayerDelegate(m_model, this));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_list->setUniformItemSizes(true);
    m_list->setStyleSheet(QStringLiteral("QListView { border-top: 1px solid #262626; border-bottom: 1px solid #262626; }"));
    root->addWidget(m_list, 1);

    // Bottom button bar.
    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(6, 2, 6, 4);
    bottom->setSpacing(2);
    bottom->addStretch();
    const struct { const char* icon; const char* tip; const char* action; } buttons[] = {
        {"link", "Link layers", nullptr},
        {"fx", "Add a layer style", nullptr},
        {"mask", "Add layer mask", nullptr},
        {"adjustment", "Create new fill or adjustment layer", nullptr},
        {"folder", "Create a new group", nullptr},
        {"new-layer", "Create a new layer", "layer.newQuick"},
        {"trash", "Delete layer", "layer.delete"},
    };
    for (const auto& b : buttons) {
        auto* btn = makeIconButton(QString::fromLatin1(b.icon), QString::fromLatin1(b.tip), this, false, 22);
        if (b.action) {
            const QString id = QString::fromLatin1(b.action);
            connect(btn, &QToolButton::clicked, this, [this, id] { trigger(id); });
        } else {
            btn->setEnabled(false);
        }
        bottom->addWidget(btn);
    }
    root->addLayout(bottom);

    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& cur) {
        if (m_syncing || !m_doc || !cur.isValid()) return;
        m_doc->setActiveIndex(m_model->docIndex(cur.row()));
    });
    connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (m_doc && m_doc->layerAt(m_model->docIndex(idx.row())).isBackground) trigger(QStringLiteral("layer.fromBackground"));
    });
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
    m_syncing = true;
    const QModelIndex idx = m_model->index(m_model->rowFor(m_doc->activeIndex()));
    m_list->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    m_syncing = false;
}

void LayersPanel::contextMenuEvent(QContextMenuEvent* e)
{
    if (!m_doc || !m_lookup) return;
    QMenu menu(this);
    for (const char* id : {"layer.duplicate", "layer.delete", "-", "layer.fromBackground", "-", "layer.mergeDown",
                           "layer.mergeVisible", "layer.flatten"}) {
        if (QLatin1String(id) == QLatin1String("-")) {
            menu.addSeparator();
            continue;
        }
        if (QAction* a = m_lookup(QString::fromLatin1(id))) menu.addAction(a);
    }
    menu.exec(e->globalPos());
}
