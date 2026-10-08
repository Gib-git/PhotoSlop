#include "ui/DocumentPage.h"

#include "core/Document.h"
#include "ui/CanvasView.h"
#include "ui/Ruler.h"
#include "ui/ViewOptions.h"

#include <QGridLayout>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <iterator>

QString formatZoom(double zoom)
{
    const double pct = zoom * 100.0;
    if (std::fabs(pct - std::round(pct)) < 0.05) return QString::number(int(std::round(pct))) + QLatin1Char('%');
    return QString::number(pct, 'f', pct < 10 ? 2 : 1) + QLatin1Char('%');
}

DocumentPage::DocumentPage(Document* doc, ToolManager* tools, ViewOptions* options, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_options(options)
{
    doc->setParent(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_view = new CanvasView(doc, tools, options, this);
    m_hRuler = new Ruler(Qt::Horizontal, m_view, this);
    m_vRuler = new Ruler(Qt::Vertical, m_view, this);
    m_corner = new QWidget(this);
    m_corner->setFixedSize(Ruler::kThickness, Ruler::kThickness);
    m_corner->setStyleSheet(QStringLiteral("background: #323232; border-right: 1px solid #1e1e1e; border-bottom: 1px solid #1e1e1e;"));
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(m_corner, 0, 0);
    grid->addWidget(m_hRuler, 0, 1);
    grid->addWidget(m_vRuler, 1, 0);
    grid->addWidget(m_view, 1, 1);
    root->addLayout(grid, 1);
    connect(options, &ViewOptions::changed, this, &DocumentPage::updateRulers);
    updateRulers();

    auto* status = new QWidget(this);
    status->setObjectName(QStringLiteral("DocStatusBar"));
    status->setFixedHeight(22);
    auto* lay = new QHBoxLayout(status);
    lay->setContentsMargins(6, 0, 6, 0);
    m_zoom = new QLineEdit(status);
    m_zoom->setFixedWidth(64);
    m_zoom->setFrame(false);
    m_zoom->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; min-height: 0; padding: 0; }"
                                         "QLineEdit:focus { background: #454545; }"));
    m_info = new QLabel(status);
    lay->addWidget(m_zoom);
    lay->addWidget(m_info);
    lay->addStretch();
    root->addWidget(status);

    connect(m_zoom, &QLineEdit::editingFinished, this, [this] {
        QString t = m_zoom->text();
        t.remove(QLatin1Char('%'));
        bool ok = false;
        const double v = t.trimmed().toDouble(&ok);
        if (ok && v > 0) m_view->setZoom(v / 100.0);
        updateStatus();
        m_view->setFocus();
    });
    connect(m_view, &CanvasView::zoomChanged, this, [this] {
        updateStatus();
        emit titleChanged();
    });
    connect(doc, &Document::sizeChanged, this, &DocumentPage::updateStatus);
    connect(doc, &Document::titleChanged, this, &DocumentPage::titleChanged);
    connect(doc, &Document::modifiedChanged, this, &DocumentPage::titleChanged);
    connect(doc, &Document::activeLayerChanged, this, &DocumentPage::titleChanged);
    connect(doc, &Document::layersChanged, this, &DocumentPage::titleChanged);
    connect(doc, &Document::quickMaskChanged, this, &DocumentPage::titleChanged);
    connect(doc, &Document::editTargetChanged, this, &DocumentPage::titleChanged);
    updateStatus();
}

void DocumentPage::updateRulers()
{
    for (QWidget* w : {static_cast<QWidget*>(m_hRuler), static_cast<QWidget*>(m_vRuler), m_corner})
        w->setVisible(m_options->rulers);
}

DocumentPage::~DocumentPage()
{
    // The view references the document, so it must go first.
    delete m_view;
}

QString DocumentPage::tabTitle() const
{
    const Layer* l = m_doc->activeLayer();
    QString target;
    if (m_doc->inQuickMask()) target = QStringLiteral("Quick Mask/8");
    else if (m_doc->editingMask()) target = QStringLiteral("%1, Layer Mask/8").arg(l ? l->name : QString());
    else target = QStringLiteral("%1, RGB/8").arg(l ? l->name : QString());
    return QStringLiteral("%1 @ %2 (%3)%4")
        .arg(m_doc->title(), formatZoom(m_view->zoom()), target, m_doc->isModified() ? QStringLiteral(" *") : QString());
}

void DocumentPage::updateStatus()
{
    if (!m_zoom->hasFocus()) m_zoom->setText(formatZoom(m_view->zoom()));
    m_info->setText(QStringLiteral("%1 px x %2 px (%3 ppi)").arg(m_doc->width()).arg(m_doc->height()).arg(m_doc->dpi()));
}
