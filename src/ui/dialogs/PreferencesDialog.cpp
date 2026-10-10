#include "ui/dialogs/PreferencesDialog.h"

#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

const QStringList kUnits = {QStringLiteral("Pixels"), QStringLiteral("Inches"), QStringLiteral("Centimeters"),
                            QStringLiteral("Millimeters"), QStringLiteral("Percent")};

QLabel* note(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #9a9a9a;"));
    return l;
}

} // namespace

PreferencesDialog::PreferencesDialog(ViewOptions* view, const QString& gpuInfo, QWidget* parent, Page startPage)
    : QDialog(parent)
    , m_view(view)
{
    setWindowTitle(QStringLiteral("Preferences"));
    resize(720, 470);
    auto* root = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setFixedWidth(170);
    m_stack = new QStackedWidget(this);
    body->addWidget(m_list);
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);

    // ---- General ----
    {
        QWidget* w = page(QStringLiteral("General"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        m_home = new QCheckBox(QStringLiteral("Auto show the Home Screen"), w);
        m_wheelZoom = new QCheckBox(QStringLiteral("Zoom with Scroll Wheel"), w);
        lay->addWidget(m_home);
        lay->addWidget(m_wheelZoom);
        lay->addWidget(note(QStringLiteral("With Zoom with Scroll Wheel on, the wheel zooms and Alt (Option) + wheel scrolls."), w));
        auto* reset = new QPushButton(QStringLiteral("Reset Preferences to Defaults"), w);
        connect(reset, &QPushButton::clicked, this, [this] {
            load(PreferenceValues());
            m_rulerUnits->setCurrentIndex(int(ViewOptions::Units::Pixels));
            m_gridEvery->setValue(1.0);
            m_gridUnits->setCurrentIndex(int(ViewOptions::Units::Inches));
            m_subdivisions->setValue(4);
        });
        lay->addSpacing(12);
        lay->addWidget(reset, 0, Qt::AlignLeft);
        lay->addStretch();
    }
    // ---- Interface ----
    {
        QWidget* w = page(QStringLiteral("Interface"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* form = new QFormLayout;
        m_fontSize = new QComboBox(w);
        m_fontSize->addItems({QStringLiteral("Small"), QStringLiteral("Medium"), QStringLiteral("Large")});
        form->addRow(QStringLiteral("UI Font Size:"), m_fontSize);
        lay->addLayout(form);
        lay->addWidget(note(QStringLiteral("Changes to the font size take effect the next time you start PhotoSlop."), w));
        lay->addStretch();
    }
    // ---- Performance ----
    {
        QWidget* w = page(QStringLiteral("Performance"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* history = new QGroupBox(QStringLiteral("History & Cache"), w);
        auto* hf = new QFormLayout(history);
        m_history = new QSpinBox(history);
        m_history->setRange(1, 1000);
        hf->addRow(QStringLiteral("History States:"), m_history);
        lay->addWidget(history);
        lay->addWidget(note(QStringLiteral("The number of history states applies to documents opened from now on."), w));
        auto* gpu = new QGroupBox(QStringLiteral("Graphics Processor Settings"), w);
        auto* gl = new QVBoxLayout(gpu);
        gl->addWidget(new QLabel(gpuInfo.isEmpty() ? QStringLiteral("No compatible graphics processor was found.")
                                                   : QStringLiteral("Detected Graphics Processor:\n%1").arg(gpuInfo),
                                 gpu));
        m_gpu = new QCheckBox(QStringLiteral("Use Graphics Processor"), gpu);
        m_gpu->setEnabled(!gpuInfo.isEmpty());
        gl->addWidget(m_gpu);
        gl->addWidget(note(QStringLiteral("Draws the canvas with OpenGL, which makes zooming and panning large images smoother."), gpu));
        lay->addWidget(gpu);
        lay->addStretch();
    }
    // ---- Cursors ----
    {
        QWidget* w = page(QStringLiteral("Cursors"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* paint = new QGroupBox(QStringLiteral("Painting Cursors"), w);
        auto* pl = new QVBoxLayout(paint);
        const char* paintNames[] = {"Standard", "Precise", "Brush Tip"};
        auto* pg = new QButtonGroup(this);
        for (int i = 0; i < 3; ++i) {
            m_paint[i] = new QRadioButton(QString::fromLatin1(paintNames[i]), paint);
            pg->addButton(m_paint[i]);
            pl->addWidget(m_paint[i]);
        }
        m_crosshair = new QCheckBox(QStringLiteral("Show Crosshair in Brush Tip"), paint);
        pl->addWidget(m_crosshair);
        auto* other = new QGroupBox(QStringLiteral("Other Cursors"), w);
        auto* ol = new QVBoxLayout(other);
        auto* og = new QButtonGroup(this);
        const char* otherNames[] = {"Standard", "Precise"};
        for (int i = 0; i < 2; ++i) {
            m_other[i] = new QRadioButton(QString::fromLatin1(otherNames[i]), other);
            og->addButton(m_other[i]);
            ol->addWidget(m_other[i]);
        }
        lay->addWidget(paint);
        lay->addWidget(other);
        lay->addWidget(note(QStringLiteral("Brush Tip outlines the brush at its size; Precise shows a crosshair."), w));
        lay->addStretch();
    }
    // ---- Transparency ----
    {
        QWidget* w = page(QStringLiteral("Transparency"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* form = new QFormLayout;
        m_checkerSize = new QComboBox(w);
        m_checkerSize->addItems({QStringLiteral("None"), QStringLiteral("Small"), QStringLiteral("Medium"), QStringLiteral("Large")});
        m_checkerColors = new QComboBox(w);
        m_checkerColors->addItems({QStringLiteral("Light"), QStringLiteral("Medium"), QStringLiteral("Dark")});
        form->addRow(QStringLiteral("Grid Size:"), m_checkerSize);
        form->addRow(QStringLiteral("Grid Colors:"), m_checkerColors);
        lay->addLayout(form);
        lay->addStretch();
    }
    // ---- Units & Rulers ----
    {
        QWidget* w = page(QStringLiteral("Units & Rulers"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* form = new QFormLayout;
        m_rulerUnits = new QComboBox(w);
        m_rulerUnits->addItems(kUnits);
        form->addRow(QStringLiteral("Rulers:"), m_rulerUnits);
        lay->addLayout(form);
        lay->addStretch();
    }
    // ---- Guides & Grid ----
    {
        QWidget* w = page(QStringLiteral("Guides & Grid"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* guides = new QGroupBox(QStringLiteral("Guides"), w);
        auto* gf = new QFormLayout(guides);
        m_guideColor = new ColorButton(QStringLiteral("Select guide color:"), guides);
        m_guideColor->setFixedSize(48, 22);
        gf->addRow(QStringLiteral("Color:"), m_guideColor);
        auto* grid = new QGroupBox(QStringLiteral("Grid"), w);
        auto* rf = new QFormLayout(grid);
        m_gridColor = new ColorButton(QStringLiteral("Select grid color:"), grid);
        m_gridColor->setFixedSize(48, 22);
        m_gridStyle = new QComboBox(grid);
        m_gridStyle->addItems({QStringLiteral("Lines"), QStringLiteral("Dashed Lines"), QStringLiteral("Dots")});
        auto* every = new QHBoxLayout;
        m_gridEvery = new QDoubleSpinBox(grid);
        m_gridEvery->setRange(0.01, 10000);
        m_gridEvery->setDecimals(2);
        m_gridUnits = new QComboBox(grid);
        m_gridUnits->addItems(kUnits);
        every->addWidget(m_gridEvery);
        every->addWidget(m_gridUnits);
        m_subdivisions = new QSpinBox(grid);
        m_subdivisions->setRange(1, 100);
        rf->addRow(QStringLiteral("Color:"), m_gridColor);
        rf->addRow(QStringLiteral("Style:"), m_gridStyle);
        rf->addRow(QStringLiteral("Gridline Every:"), every);
        rf->addRow(QStringLiteral("Subdivisions:"), m_subdivisions);
        lay->addWidget(guides);
        lay->addWidget(grid);
        lay->addStretch();
    }
    // ---- File Handling ----
    {
        QWidget* w = page(QStringLiteral("File Handling"));
        auto* lay = static_cast<QVBoxLayout*>(w->layout());
        auto* form = new QFormLayout;
        m_recent = new QSpinBox(w);
        m_recent->setRange(0, 100);
        m_recent->setSuffix(QStringLiteral(" files"));
        form->addRow(QStringLiteral("Recent File List Contains:"), m_recent);
        lay->addLayout(form);
        lay->addWidget(note(QStringLiteral("Photoshop (.psd) files are always saved with a full composite, so other "
                                           "applications can read them."),
                            w));
        lay->addStretch();
    }

    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* prev = buttons->addButton(QStringLiteral("Prev"), QDialogButtonBox::ActionRole);
    auto* next = buttons->addButton(QStringLiteral("Next"), QDialogButtonBox::ActionRole);
    connect(prev, &QPushButton::clicked, this, [this] { m_list->setCurrentRow((m_list->currentRow() + m_list->count() - 1) % m_list->count()); });
    connect(next, &QPushButton::clicked, this, [this] { m_list->setCurrentRow((m_list->currentRow() + 1) % m_list->count()); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    load(Preferences::instance());
    m_rulerUnits->setCurrentIndex(int(view->units));
    m_gridEvery->setValue(view->gridEvery);
    m_gridUnits->setCurrentIndex(int(view->gridUnits));
    m_subdivisions->setValue(view->gridSubdivisions);
    if (gpuInfo.isEmpty()) m_gpu->setChecked(false);
    setPage(startPage);
}

QWidget* PreferencesDialog::page(const QString& title)
{
    auto* w = new QWidget(m_stack);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 4, 8, 4);
    auto* heading = new QLabel(title, w);
    heading->setStyleSheet(QStringLiteral("font-size: 15px; color: #f0f0f0; margin-bottom: 8px;"));
    lay->addWidget(heading);
    m_stack->addWidget(w);
    m_list->addItem(title);
    return w;
}

void PreferencesDialog::setPage(Page p) { m_list->setCurrentRow(int(p)); }

void PreferencesDialog::load(const PreferenceValues& v)
{
    m_home->setChecked(v.showHomeScreen);
    m_wheelZoom->setChecked(v.zoomWithScrollWheel);
    m_fontSize->setCurrentIndex(int(v.uiFontSize));
    m_history->setValue(v.historyStates);
    m_gpu->setChecked(v.useGraphicsProcessor && m_gpu->isEnabled());
    m_paint[int(v.paintingCursor)]->setChecked(true);
    m_crosshair->setChecked(v.brushCrosshair);
    m_other[int(v.otherCursors)]->setChecked(true);
    m_checkerSize->setCurrentIndex(int(v.checkerSize));
    m_checkerColors->setCurrentIndex(int(v.checkerColors));
    m_guideColor->setColor(v.guideColor);
    m_gridColor->setColor(v.gridColor);
    m_gridStyle->setCurrentIndex(int(v.gridStyle));
    m_recent->setValue(v.recentFileCount);
}

void PreferencesDialog::accept()
{
    Preferences& p = Preferences::instance();
    p.showHomeScreen = m_home->isChecked();
    p.zoomWithScrollWheel = m_wheelZoom->isChecked();
    p.uiFontSize = Preferences::FontSize(m_fontSize->currentIndex());
    p.historyStates = m_history->value();
    // Keep the saved choice when the GPU cannot be used on this machine.
    if (m_gpu->isEnabled()) p.useGraphicsProcessor = m_gpu->isChecked();
    for (int i = 0; i < 3; ++i)
        if (m_paint[i]->isChecked()) p.paintingCursor = Preferences::PaintingCursor(i);
    p.brushCrosshair = m_crosshair->isChecked();
    p.otherCursors = m_other[1]->isChecked() ? Preferences::OtherCursor::Precise : Preferences::OtherCursor::Standard;
    p.checkerSize = Preferences::CheckerSize(m_checkerSize->currentIndex());
    p.checkerColors = Preferences::CheckerColors(m_checkerColors->currentIndex());
    p.guideColor = m_guideColor->color();
    p.gridColor = m_gridColor->color();
    p.gridStyle = Preferences::GridStyle(m_gridStyle->currentIndex());
    p.recentFileCount = m_recent->value();
    p.notify();

    m_view->units = ViewOptions::Units(m_rulerUnits->currentIndex());
    m_view->gridEvery = m_gridEvery->value();
    m_view->gridUnits = ViewOptions::Units(m_gridUnits->currentIndex());
    m_view->gridSubdivisions = m_subdivisions->value();
    m_view->notify();
    QDialog::accept();
}
