#include "ui/DocumentPage.h"

#include "core/Document.h"
#include "ui/CanvasView.h"

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

DocumentPage::DocumentPage(Document* doc, ToolManager* tools, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    doc->setParent(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_view = new CanvasView(doc, tools, this);
    root->addWidget(m_view, 1);

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
    updateStatus();
}

DocumentPage::~DocumentPage()
{
    // The view references the document, so it must go first.
    delete m_view;
}

QString DocumentPage::tabTitle() const
{
    const Layer* l = m_doc->activeLayer();
    return QStringLiteral("%1 @ %2 (%3, RGB/8)%4")
        .arg(m_doc->title(), formatZoom(m_view->zoom()), l ? l->name : QString(),
             m_doc->isModified() ? QStringLiteral(" *") : QString());
}

void DocumentPage::updateStatus()
{
    if (!m_zoom->hasFocus()) m_zoom->setText(formatZoom(m_view->zoom()));
    m_info->setText(QStringLiteral("%1 px x %2 px (%3 ppi)").arg(m_doc->width()).arg(m_doc->height()).arg(m_doc->dpi()));
}
