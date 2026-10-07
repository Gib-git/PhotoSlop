#include "ui/dialogs/Dialogs.h"

#include "app/Theme.h"
#include "core/ColorState.h"
#include "core/Document.h"
#include "io/DocumentIO.h"
#include "ui/Widgets.h"
#include "ui/dialogs/ColorPickerDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

QDoubleSpinBox* numberField(QWidget* parent, double max = 300000, int decimals = 0, int width = 90)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(0, max);
    s->setDecimals(decimals);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setFixedWidth(width);
    s->setKeyboardTracking(false);
    return s;
}

QString megabytes(const QSize& s)
{
    double mb = double(s.width()) * s.height() * 3 / (1024.0 * 1024.0);
    return mb >= 1.0 ? QStringLiteral("%1M").arg(mb, 0, 'f', 2) : QStringLiteral("%1K").arg(mb * 1024, 0, 'f', 0);
}

struct Preset {
    const char* name;
    int w, h;
    double dpi;
};

const QList<QList<Preset>>& presetCategories()
{
    static const QList<QList<Preset>> cats = {
        // Recent
        {{"Default Photoshop Size", 2100, 1500, 300}, {"Web Large", 1920, 1080, 72}, {"Square", 1080, 1080, 72}},
        // Saved
        {},
        // Photo
        {{"Landscape, 2 x 3", 3600, 2400, 300}, {"Portrait, 2 x 3", 2400, 3600, 300},
         {"Landscape, 4 x 6", 1800, 1200, 300}, {"Portrait, 4 x 6", 1200, 1800, 300},
         {"Landscape, 5 x 7", 2100, 1500, 300}, {"Portrait, 5 x 7", 1500, 2100, 300},
         {"Landscape, 8 x 10", 3000, 2400, 300}, {"Portrait, 8 x 10", 2400, 3000, 300}},
        // Print
        {{"Letter", 2550, 3300, 300}, {"Legal", 2550, 4200, 300}, {"Tabloid", 3300, 5100, 300},
         {"A4", 2480, 3508, 300}, {"A3", 3508, 4961, 300}, {"A5", 1748, 2480, 300}},
        // Art & Illustration
        {{"Poster", 5400, 7200, 300}, {"Postcard", 1800, 1200, 300}, {"Art Default", 1000, 1000, 72},
         {"Paper", 3300, 2550, 300}},
        // Web
        {{"Web Most Common", 1366, 768, 72}, {"Web Minimum", 1024, 768, 72}, {"Web Large", 1920, 1080, 72},
         {"MacBook Pro 16", 3456, 2234, 72}, {"Web Banner", 1200, 628, 72}},
        // Mobile
        {{"iPhone 15 Pro", 1179, 2556, 72}, {"iPhone 15 Pro Max", 1290, 2796, 72}, {"Android 1080p", 1080, 1920, 72},
         {"iPad Pro 12.9", 2048, 2732, 72}, {"Apple Watch 45mm", 396, 484, 72}},
        // Film & Video
        {{"HDTV 1080p", 1920, 1080, 72}, {"HDV/HDTV 720p", 1280, 720, 72}, {"UHD 4K", 3840, 2160, 72},
         {"8K", 7680, 4320, 72}, {"NTSC DV", 720, 480, 72}},
    };
    return cats;
}

QIcon presetIcon(int w, int h)
{
    QPixmap pm(90, 70);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    double s = std::min(70.0 / w, 54.0 / h);
    QRectF r(0, 0, w * s, h * s);
    r.moveCenter(QPointF(45, 35));
    p.fillRect(r, QColor(0x5a, 0x5a, 0x5a));
    p.setPen(QColor(0x80, 0x80, 0x80));
    p.drawRect(r);
    return QIcon(pm);
}

} // namespace

// ---------------- NewDocumentDialog ----------------

NewDocumentDialog::NewDocumentDialog(const QString& defaultName, const QSize& clipboardSize, QWidget* parent)
    : QDialog(parent)
    , m_clipboard(clipboardSize)
{
    setWindowTitle(QStringLiteral("New Document"));
    resize(900, 560);
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* left = new QVBoxLayout;
    left->setContentsMargins(16, 12, 16, 12);
    m_tabs = new QTabBar(this);
    m_tabs->setDrawBase(false);
    for (const char* t : {"Recent", "Saved", "Photo", "Print", "Art && Illustration", "Web", "Mobile", "Film && Video"})
        m_tabs->addTab(QString::fromLatin1(t));
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabBar { background: transparent; } QTabBar::tab { background: transparent; border: none; padding: 6px 10px; color: #a0a0a0; }"
        "QTabBar::tab:selected { color: white; border-bottom: 2px solid #1473e6; }"));
    left->addWidget(m_tabs);
    auto* heading = new QLabel(QStringLiteral("<b>BLANK DOCUMENT PRESETS</b>"), this);
    heading->setStyleSheet(QStringLiteral("color: #b0b0b0; margin-top: 10px;"));
    left->addWidget(heading);
    m_presets = new QListWidget(this);
    m_presets->setViewMode(QListView::IconMode);
    m_presets->setIconSize(QSize(90, 70));
    m_presets->setGridSize(QSize(150, 130));
    m_presets->setResizeMode(QListView::Adjust);
    m_presets->setMovement(QListView::Static);
    m_presets->setWordWrap(true);
    m_presets->setStyleSheet(QStringLiteral("QListWidget::item { color: #d0d0d0; } QListWidget::item:selected { background: #3d3d3d; border: 1px solid #1473e6; }"));
    left->addWidget(m_presets, 1);
    root->addLayout(left, 1);

    auto* panel = new QWidget(this);
    panel->setFixedWidth(300);
    panel->setStyleSheet(QStringLiteral("background: #2b2b2b;"));
    auto* form = new QVBoxLayout(panel);
    form->setContentsMargins(18, 16, 18, 16);
    auto* details = new QLabel(QStringLiteral("<b>PRESET DETAILS</b>"), panel);
    details->setStyleSheet(QStringLiteral("color: #b0b0b0;"));
    form->addWidget(details);
    m_name = new QLineEdit(defaultName, panel);
    m_name->setStyleSheet(QStringLiteral("font-size: 16px; padding: 4px;"));
    form->addWidget(m_name);
    form->addSpacing(10);

    auto* grid = new QGridLayout;
    grid->setVerticalSpacing(6);
    m_width = numberField(panel, 300000, 0, 110);
    m_height = numberField(panel, 300000, 0, 110);
    m_units = new QComboBox(panel);
    m_units->addItems({QStringLiteral("Pixels"), QStringLiteral("Inches"), QStringLiteral("Centimeters"), QStringLiteral("Millimeters")});
    grid->addWidget(new QLabel(QStringLiteral("Width"), panel), 0, 0, 1, 2);
    grid->addWidget(m_width, 1, 0);
    grid->addWidget(m_units, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("Height"), panel), 2, 0);
    grid->addWidget(new QLabel(QStringLiteral("Orientation"), panel), 2, 1);
    grid->addWidget(m_height, 3, 0);
    auto* orient = new QHBoxLayout;
    m_portrait = new QToolButton(panel);
    m_landscape = new QToolButton(panel);
    m_portrait->setText(QStringLiteral("▯"));
    m_landscape->setText(QStringLiteral("▭"));
    m_portrait->setToolTip(QStringLiteral("Portrait"));
    m_landscape->setToolTip(QStringLiteral("Landscape"));
    for (QToolButton* b : {m_portrait, m_landscape}) {
        b->setCheckable(true);
        b->setFixedSize(26, 24);
        orient->addWidget(b);
    }
    auto* og = new QButtonGroup(panel);
    og->addButton(m_portrait);
    og->addButton(m_landscape);
    orient->addStretch();
    grid->addLayout(orient, 3, 1);
    grid->addWidget(new QLabel(QStringLiteral("Resolution"), panel), 4, 0, 1, 2);
    m_resolution = numberField(panel, 10000, 0, 110);
    m_resUnits = new QComboBox(panel);
    m_resUnits->addItems({QStringLiteral("Pixels/Inch"), QStringLiteral("Pixels/Centimeter")});
    grid->addWidget(m_resolution, 5, 0);
    grid->addWidget(m_resUnits, 5, 1);
    grid->addWidget(new QLabel(QStringLiteral("Color Mode"), panel), 6, 0, 1, 2);
    auto* mode = new QComboBox(panel);
    mode->addItem(QStringLiteral("RGB Color"));
    auto* depth = new QComboBox(panel);
    depth->addItem(QStringLiteral("8 bit"));
    grid->addWidget(mode, 7, 0);
    grid->addWidget(depth, 7, 1);
    grid->addWidget(new QLabel(QStringLiteral("Background Contents"), panel), 8, 0, 1, 2);
    m_background = new QComboBox(panel);
    m_background->addItems({QStringLiteral("White"), QStringLiteral("Black"), QStringLiteral("Background Color"),
                            QStringLiteral("Transparent"), QStringLiteral("Custom...")});
    grid->addWidget(m_background, 9, 0, 1, 2);
    form->addLayout(grid);
    form->addStretch();
    auto* buttons = new QHBoxLayout;
    auto* close = new QPushButton(QStringLiteral("Close"), panel);
    auto* create = new QPushButton(QStringLiteral("Create"), panel);
    create->setDefault(true);
    buttons->addStretch();
    buttons->addWidget(close);
    buttons->addWidget(create);
    form->addLayout(buttons);
    root->addWidget(panel);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(create, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tabs, &QTabBar::currentChanged, this, &NewDocumentDialog::showCategory);
    connect(m_presets, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        QVariantList v = m_presets->item(row)->data(Qt::UserRole).toList();
        applyPreset(v[0].toInt(), v[1].toInt(), v[2].toDouble(), QString());
    });
    connect(m_presets, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this] {
        if (m_updating) return;
        m_pxW = std::max(1.0, std::round(toPixels(m_width->value())));
        setUnitsFields();
    });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this] {
        if (m_updating) return;
        m_pxH = std::max(1.0, std::round(toPixels(m_height->value())));
        setUnitsFields();
    });
    connect(m_units, &QComboBox::currentIndexChanged, this, [this] { setUnitsFields(); });
    connect(m_resolution, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_updating && m_units->currentIndex() != 0) {
            m_pxW = std::max(1.0, std::round(toPixels(m_width->value())));
            m_pxH = std::max(1.0, std::round(toPixels(m_height->value())));
        }
    });
    connect(m_portrait, &QToolButton::clicked, this, [this] {
        if (m_pxW > m_pxH) std::swap(m_pxW, m_pxH);
        setUnitsFields();
    });
    connect(m_landscape, &QToolButton::clicked, this, [this] {
        if (m_pxH > m_pxW) std::swap(m_pxW, m_pxH);
        setUnitsFields();
    });
    connect(m_background, &QComboBox::activated, this, [this](int i) {
        if (i != 4) return;
        QColor c = ColorPickerDialog::getColor(m_custom, QStringLiteral("Color Picker (Custom Background Color)"), this);
        if (c.isValid()) m_custom = c;
    });

    showCategory(0);
    m_presets->setCurrentRow(0);
}

void NewDocumentDialog::showCategory(int index)
{
    m_presets->clear();
    auto addItem = [this](const QString& name, int w, int h, double dpi) {
        auto* item = new QListWidgetItem(presetIcon(w, h),
                                         QStringLiteral("%1\n%2 x %3 px @ %4 ppi").arg(name).arg(w).arg(h).arg(dpi),
                                         m_presets);
        item->setData(Qt::UserRole, QVariantList{w, h, dpi});
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    };
    if (index == 0 && m_clipboard.isValid())
        addItem(QStringLiteral("Clipboard"), m_clipboard.width(), m_clipboard.height(), 72);
    for (const Preset& p : presetCategories().value(index)) addItem(QString::fromLatin1(p.name), p.w, p.h, p.dpi);
}

void NewDocumentDialog::applyPreset(int w, int h, double dpi, const QString&)
{
    m_pxW = w;
    m_pxH = h;
    m_updating = true;
    m_resolution->setValue(dpi);
    m_updating = false;
    setUnitsFields();
}

double NewDocumentDialog::toPixels(double v) const
{
    const double dpi = m_resolution->value() * (m_resUnits->currentIndex() == 1 ? 2.54 : 1.0);
    switch (m_units->currentIndex()) {
    case 1: return v * dpi;
    case 2: return v / 2.54 * dpi;
    case 3: return v / 25.4 * dpi;
    default: return v;
    }
}

double NewDocumentDialog::fromPixels(double px) const
{
    const double dpi = std::max(1.0, m_resolution->value() * (m_resUnits->currentIndex() == 1 ? 2.54 : 1.0));
    switch (m_units->currentIndex()) {
    case 1: return px / dpi;
    case 2: return px / dpi * 2.54;
    case 3: return px / dpi * 25.4;
    default: return px;
    }
}

void NewDocumentDialog::setUnitsFields()
{
    m_updating = true;
    const int decimals = m_units->currentIndex() == 0 ? 0 : 2;
    m_width->setDecimals(decimals);
    m_height->setDecimals(decimals);
    m_width->setValue(fromPixels(m_pxW));
    m_height->setValue(fromPixels(m_pxH));
    m_portrait->setChecked(m_pxH >= m_pxW);
    m_landscape->setChecked(m_pxW > m_pxH);
    m_updating = false;
}

QString NewDocumentDialog::name() const { return m_name->text().isEmpty() ? QStringLiteral("Untitled") : m_name->text(); }
QSize NewDocumentDialog::pixelSize() const { return QSize(int(m_pxW), int(m_pxH)); }
double NewDocumentDialog::resolution() const
{
    return m_resolution->value() * (m_resUnits->currentIndex() == 1 ? 2.54 : 1.0);
}
NewDocumentDialog::Background NewDocumentDialog::background() const { return Background(m_background->currentIndex()); }

// ---------------- ImageSizeDialog ----------------

ImageSizeDialog::ImageSizeDialog(Document* doc, QWidget* parent)
    : QDialog(parent)
    , m_orig(doc->size())
    , m_w(doc->width())
    , m_h(doc->height())
    , m_origDpi(doc->dpi())
{
    setWindowTitle(QStringLiteral("Image Size"));
    auto* root = new QHBoxLayout(this);

    auto* preview = new QLabel(this);
    preview->setFixedSize(240, 240);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QStringLiteral("background: #282828; border: 1px solid #1e1e1e;"));
    QImage thumb = doc->pyramidLevel(std::min(3, doc->pyramidLevelCount() - 1))
                       .scaled(236, 236, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview->setPixmap(QPixmap::fromImage(thumb));
    root->addWidget(preview);

    auto* right = new QVBoxLayout;
    m_info = new QLabel(this);
    right->addWidget(m_info);
    auto* grid = new QGridLayout;
    m_width = numberField(this, 1000000, 0);
    m_height = numberField(this, 1000000, 0);
    m_wUnits = new QComboBox(this);
    m_hUnits = new QComboBox(this);
    for (QComboBox* c : {m_wUnits, m_hUnits}) {
        c->addItems({QStringLiteral("Percent"), QStringLiteral("Pixels"), QStringLiteral("Inches"), QStringLiteral("Centimeters")});
        c->setCurrentIndex(1);
    }
    m_link = new QToolButton(this);
    m_link->setIcon(Theme::icon(QStringLiteral("link")));
    m_link->setCheckable(true);
    m_link->setChecked(true);
    m_link->setToolTip(QStringLiteral("Constrain aspect ratio"));
    m_res = numberField(this, 10000, 2);
    m_res->setValue(m_origDpi);
    grid->addWidget(new QLabel(QStringLiteral("Width:"), this), 0, 1, Qt::AlignRight);
    grid->addWidget(m_width, 0, 2);
    grid->addWidget(m_wUnits, 0, 3);
    grid->addWidget(m_link, 0, 0, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Height:"), this), 1, 1, Qt::AlignRight);
    grid->addWidget(m_height, 1, 2);
    grid->addWidget(m_hUnits, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("Resolution:"), this), 2, 1, Qt::AlignRight);
    grid->addWidget(m_res, 2, 2);
    grid->addWidget(new QLabel(QStringLiteral("Pixels/Inch"), this), 2, 3);
    m_resample = new QCheckBox(QStringLiteral("Resample:"), this);
    m_resample->setChecked(true);
    m_method = new QComboBox(this);
    m_method->addItems({QStringLiteral("Automatic"), QStringLiteral("Bicubic Smoother (enlargement)"),
                        QStringLiteral("Bicubic Sharper (reduction)"), QStringLiteral("Bicubic (smooth gradients)"),
                        QStringLiteral("Bilinear"), QStringLiteral("Nearest Neighbor (hard edges)")});
    grid->addWidget(m_resample, 3, 1, 1, 1, Qt::AlignRight);
    grid->addWidget(m_method, 3, 2, 1, 2);
    right->addLayout(grid);
    right->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(box);
    root->addLayout(right);

    auto onSize = [this](bool horizontal) {
        if (m_updating) return;
        QDoubleSpinBox* spin = horizontal ? m_width : m_height;
        int unit = (horizontal ? m_wUnits : m_hUnits)->currentIndex();
        if (!m_resample->isChecked() && unit >= 2) {
            // Without resampling, physical size changes the resolution.
            double inches = unit == 2 ? spin->value() : spin->value() / 2.54;
            if (inches > 0) m_res->setValue((horizontal ? m_w : m_h) / inches);
            return;
        }
        double px = std::max(1.0, std::round(unitToPx(spin->value(), unit, horizontal)));
        if (horizontal) {
            m_w = px;
            if (m_link->isChecked()) m_h = std::max(1.0, std::round(px * m_orig.height() / m_orig.width()));
        } else {
            m_h = px;
            if (m_link->isChecked()) m_w = std::max(1.0, std::round(px * m_orig.width() / m_orig.height()));
        }
        refresh(spin);
    };
    connect(m_width, &QDoubleSpinBox::valueChanged, this, [onSize] { onSize(true); });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [onSize] { onSize(false); });
    connect(m_wUnits, &QComboBox::currentIndexChanged, this, [this] { refresh(nullptr); });
    connect(m_hUnits, &QComboBox::currentIndexChanged, this, [this] { refresh(nullptr); });
    connect(m_res, &QDoubleSpinBox::valueChanged, this, [this] {
        if (m_updating) return;
        if (m_resample->isChecked()) {
            // Keep physical size; pixel count follows resolution.
            const double scale = m_res->value() / std::max(0.01, m_origDpi);
            m_w = std::max(1.0, std::round(m_orig.width() * scale));
            m_h = std::max(1.0, std::round(m_orig.height() * scale));
        }
        refresh(m_res);
    });
    connect(m_resample, &QCheckBox::toggled, this, [this](bool on) {
        m_method->setEnabled(on);
        if (!on) {
            m_w = m_orig.width();
            m_h = m_orig.height();
            m_link->setChecked(true);
        }
        m_link->setEnabled(on);
        refresh(nullptr);
    });
    refresh(nullptr);
}

double ImageSizeDialog::unitToPx(double v, int unit, bool horizontal) const
{
    const double dpi = std::max(0.01, m_res->value());
    switch (unit) {
    case 0: return v / 100.0 * (horizontal ? m_orig.width() : m_orig.height());
    case 2: return v * dpi;
    case 3: return v / 2.54 * dpi;
    default: return v;
    }
}

double ImageSizeDialog::pxToUnit(double px, int unit, bool horizontal) const
{
    const double dpi = std::max(0.01, m_res->value());
    switch (unit) {
    case 0: return px * 100.0 / (horizontal ? m_orig.width() : m_orig.height());
    case 2: return px / dpi;
    case 3: return px / dpi * 2.54;
    default: return px;
    }
}

void ImageSizeDialog::refresh(QObject* source)
{
    m_updating = true;
    for (bool horizontal : {true, false}) {
        QDoubleSpinBox* spin = horizontal ? m_width : m_height;
        int unit = (horizontal ? m_wUnits : m_hUnits)->currentIndex();
        spin->setDecimals(unit == 1 ? 0 : 2);
        spin->setEnabled(m_resample->isChecked() || unit >= 2);
        if (spin != source) spin->setValue(pxToUnit(horizontal ? m_w : m_h, unit, horizontal));
    }
    m_info->setText(QStringLiteral("Image Size: <b>%1</b> (was %2)<br>Dimensions: %3 px × %4 px")
                        .arg(megabytes(newSize()), megabytes(m_orig))
                        .arg(int(m_w))
                        .arg(int(m_h)));
    m_updating = false;
}

double ImageSizeDialog::resolution() const { return m_res->value(); }

Ops::Resample ImageSizeDialog::method() const
{
    switch (m_method->currentIndex()) {
    case 4: return Ops::Resample::Bilinear;
    case 5: return Ops::Resample::NearestNeighbor;
    default: return Ops::Resample::Bicubic;
    }
}

// ---------------- CanvasSizeDialog ----------------

CanvasSizeDialog::CanvasSizeDialog(Document* doc, ColorState* colors, QWidget* parent)
    : QDialog(parent)
    , m_orig(doc->size())
    , m_dpi(doc->dpi())
    , m_colors(colors)
{
    setWindowTitle(QStringLiteral("Canvas Size"));
    auto* root = new QVBoxLayout(this);
    auto* current = new QGroupBox(QStringLiteral("Current Size: %1").arg(megabytes(m_orig)), this);
    auto* cl = new QFormLayout(current);
    cl->addRow(QStringLiteral("Width:"), new QLabel(QStringLiteral("%1 Pixels").arg(m_orig.width())));
    cl->addRow(QStringLiteral("Height:"), new QLabel(QStringLiteral("%1 Pixels").arg(m_orig.height())));
    root->addWidget(current);

    auto* next = new QGroupBox(QStringLiteral("New Size"), this);
    auto* grid = new QGridLayout(next);
    m_width = numberField(this, 300000, 0);
    m_height = numberField(this, 300000, 0);
    m_width->setValue(m_orig.width());
    m_height->setValue(m_orig.height());
    m_units = new QComboBox(this);
    m_units->addItems({QStringLiteral("Pixels"), QStringLiteral("Percent"), QStringLiteral("Inches"), QStringLiteral("Centimeters")});
    grid->addWidget(new QLabel(QStringLiteral("Width:")), 0, 0, Qt::AlignRight);
    grid->addWidget(m_width, 0, 1);
    grid->addWidget(m_units, 0, 2, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Height:")), 1, 0, Qt::AlignRight);
    grid->addWidget(m_height, 1, 1);
    m_relative = new QCheckBox(QStringLiteral("Relative"), this);
    grid->addWidget(m_relative, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Anchor:")), 3, 0, Qt::AlignRight | Qt::AlignTop);
    auto* anchorGrid = new QGridLayout;
    anchorGrid->setSpacing(1);
    for (int i = 0; i < 9; ++i) {
        auto* b = new QToolButton(this);
        b->setFixedSize(24, 24);
        connect(b, &QToolButton::clicked, this, [this, i] {
            m_anchor = i;
            updateAnchors();
        });
        anchorGrid->addWidget(b, i / 3, i % 3);
        m_anchorButtons.append(b);
    }
    grid->addLayout(anchorGrid, 3, 1);
    root->addWidget(next);

    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(new QLabel(QStringLiteral("Canvas extension color:"), this));
    m_color = new QComboBox(this);
    m_color->addItems({QStringLiteral("Foreground"), QStringLiteral("Background"), QStringLiteral("White"),
                       QStringLiteral("Black"), QStringLiteral("Gray"), QStringLiteral("Other...")});
    m_color->setCurrentIndex(1);
    connect(m_color, &QComboBox::activated, this, [this](int i) {
        if (i != 5) return;
        QColor c = ColorPickerDialog::getColor(m_other, QStringLiteral("Color Picker (Canvas Extension Color)"), this);
        if (c.isValid()) m_other = c;
    });
    colorRow->addWidget(m_color);
    colorRow->addStretch();
    root->addLayout(colorRow);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    auto toUnits = [this](int unit, double px, bool horizontal) {
        switch (unit) {
        case 1: return px * 100.0 / (horizontal ? m_orig.width() : m_orig.height());
        case 2: return px / m_dpi;
        case 3: return px / m_dpi * 2.54;
        default: return px;
        }
    };
    connect(m_units, &QComboBox::currentIndexChanged, this, [this, toUnits](int unit) {
        const QSize s = m_relative->isChecked() ? QSize(0, 0) : m_orig;
        m_width->setDecimals(unit == 0 ? 0 : 2);
        m_height->setDecimals(unit == 0 ? 0 : 2);
        m_width->setValue(toUnits(unit, s.width(), true));
        m_height->setValue(toUnits(unit, s.height(), false));
    });
    connect(m_relative, &QCheckBox::toggled, this, [this, toUnits](bool rel) {
        m_width->setMinimum(rel ? -300000 : 0);
        m_height->setMinimum(rel ? -300000 : 0);
        const int unit = m_units->currentIndex();
        m_width->setValue(rel ? 0 : toUnits(unit, m_orig.width(), true));
        m_height->setValue(rel ? 0 : toUnits(unit, m_orig.height(), false));
    });
    updateAnchors();
}

void CanvasSizeDialog::updateAnchors()
{
    static const QString arrows[3][3] = {{QStringLiteral("↖"), QStringLiteral("↑"), QStringLiteral("↗")},
                                         {QStringLiteral("←"), QStringLiteral("●"), QStringLiteral("→")},
                                         {QStringLiteral("↙"), QStringLiteral("↓"), QStringLiteral("↘")}};
    const int ar = m_anchor / 3, ac = m_anchor % 3;
    for (int i = 0; i < 9; ++i) {
        const int dr = i / 3 - ar, dc = i % 3 - ac;
        m_anchorButtons[i]->setText(std::abs(dr) <= 1 && std::abs(dc) <= 1 ? arrows[dr + 1][dc + 1] : QString());
    }
}

QSize CanvasSizeDialog::newSize() const
{
    auto toPx = [this](double v, bool horizontal) {
        switch (m_units->currentIndex()) {
        case 1: return v / 100.0 * (horizontal ? m_orig.width() : m_orig.height());
        case 2: return v * m_dpi;
        case 3: return v / 2.54 * m_dpi;
        default: return v;
        }
    };
    double w = toPx(m_width->value(), true), h = toPx(m_height->value(), false);
    if (m_relative->isChecked()) {
        w += m_orig.width();
        h += m_orig.height();
    }
    return QSize(std::max(1, int(std::round(w))), std::max(1, int(std::round(h))));
}

QPoint CanvasSizeDialog::anchorOffset() const
{
    const QSize n = newSize();
    const int ar = m_anchor / 3, ac = m_anchor % 3;
    return QPoint((n.width() - m_orig.width()) * ac / 2, (n.height() - m_orig.height()) * ar / 2);
}

QColor CanvasSizeDialog::extensionColor() const
{
    switch (m_color->currentIndex()) {
    case 0: return m_colors->foreground();
    case 1: return m_colors->background();
    case 2: return Qt::white;
    case 3: return Qt::black;
    case 4: return QColor(128, 128, 128);
    default: return m_other;
    }
}

// ---------------- FillDialog ----------------

FillDialog::FillDialog(ColorState* colors, QWidget* parent)
    : QDialog(parent)
    , m_colors(colors)
{
    setWindowTitle(QStringLiteral("Fill"));
    auto* root = new QVBoxLayout(this);
    auto* contentsRow = new QHBoxLayout;
    contentsRow->addWidget(new QLabel(QStringLiteral("Contents:"), this));
    m_contents = new QComboBox(this);
    m_contents->addItems({QStringLiteral("Foreground Color"), QStringLiteral("Background Color"), QStringLiteral("Color..."),
                          QStringLiteral("Black"), QStringLiteral("50% Gray"), QStringLiteral("White")});
    connect(m_contents, &QComboBox::activated, this, [this](int i) {
        if (i != 2) return;
        QColor c = ColorPickerDialog::getColor(m_custom, QStringLiteral("Color Picker (Fill Color)"), this);
        if (c.isValid()) m_custom = c;
    });
    contentsRow->addWidget(m_contents, 1);
    root->addLayout(contentsRow);
    auto* opts = new QGroupBox(QStringLiteral("Options"), this);
    auto* form = new QFormLayout(opts);
    m_mode = new BlendModeCombo(this);
    m_opacity = new QSpinBox(this);
    m_opacity->setRange(1, 100);
    m_opacity->setValue(100);
    m_opacity->setSuffix(QStringLiteral("%"));
    m_opacity->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_preserve = new QCheckBox(QStringLiteral("Preserve Transparency"), this);
    form->addRow(QStringLiteral("Mode:"), m_mode);
    form->addRow(QStringLiteral("Opacity:"), m_opacity);
    form->addRow(QString(), m_preserve);
    root->addWidget(opts);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
}

QColor FillDialog::color() const
{
    switch (m_contents->currentIndex()) {
    case 0: return m_colors->foreground();
    case 1: return m_colors->background();
    case 2: return m_custom;
    case 3: return Qt::black;
    case 4: return QColor(128, 128, 128);
    default: return Qt::white;
    }
}

BlendMode FillDialog::mode() const { return m_mode->mode(); }
float FillDialog::opacity() const { return m_opacity->value() / 100.f; }
bool FillDialog::preserveTransparency() const { return m_preserve->isChecked(); }

// ---------------- NewLayerDialog ----------------

NewLayerDialog::NewLayerDialog(const QString& defaultName, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New Layer"));
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(defaultName, this);
    m_name->selectAll();
    form->addRow(QStringLiteral("Name:"), m_name);
    auto* clip = new QCheckBox(QStringLiteral("Use Previous Layer to Create Clipping Mask"), this);
    clip->setEnabled(false);
    form->addRow(QString(), clip);
    auto* color = new QComboBox(this);
    color->addItem(QStringLiteral("None"));
    form->addRow(QStringLiteral("Color:"), color);
    m_mode = new BlendModeCombo(this);
    m_opacity = new QSpinBox(this);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    m_opacity->setSuffix(QStringLiteral("%"));
    m_opacity->setButtonSymbols(QAbstractSpinBox::NoButtons);
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(m_mode);
    modeRow->addWidget(new QLabel(QStringLiteral("Opacity:"), this));
    modeRow->addWidget(m_opacity);
    form->addRow(QStringLiteral("Mode:"), modeRow);
    root->addLayout(form);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
}

QString NewLayerDialog::name() const { return m_name->text(); }
BlendMode NewLayerDialog::mode() const { return m_mode->mode(); }
float NewLayerDialog::opacity() const { return m_opacity->value() / 100.f; }

// ---------------- ExportDialog ----------------

ExportDialog::ExportDialog(Document* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    setWindowTitle(QStringLiteral("Export As"));
    auto* root = new QHBoxLayout(this);
    m_preview = new QLabel(this);
    m_preview->setFixedSize(420, 340);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background: #282828; border: 1px solid #1e1e1e;"));
    root->addWidget(m_preview);
    auto* right = new QVBoxLayout;
    auto* settings = new QGroupBox(QStringLiteral("File Settings"), this);
    auto* form = new QFormLayout(settings);
    m_format = new QComboBox(this);
    for (const QString& f : DocumentIO::exportFormats()) m_format->addItem(f == QLatin1String("JPEG") ? QStringLiteral("JPG") : f, f.toLower());
    form->addRow(QStringLiteral("Format:"), m_format);
    auto* qualityRow = new QHBoxLayout;
    m_quality = new QSlider(Qt::Horizontal, this);
    m_quality->setRange(1, 100);
    m_quality->setValue(90);
    m_qualityLabel = new QLabel(QStringLiteral("90%"), this);
    qualityRow->addWidget(m_quality);
    qualityRow->addWidget(m_qualityLabel);
    form->addRow(QStringLiteral("Quality:"), qualityRow);
    right->addWidget(settings);
    m_sizeLabel = new QLabel(QStringLiteral("Image Size: %1 × %2 px").arg(doc->width()).arg(doc->height()), this);
    right->addWidget(m_sizeLabel);
    right->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportBtn = box->addButton(QStringLiteral("Export"), QDialogButtonBox::AcceptRole);
    exportBtn->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(box);
    root->addLayout(right);

    connect(m_quality, &QSlider::valueChanged, this, [this](int v) { m_qualityLabel->setText(QStringLiteral("%1%").arg(v)); });
    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::updatePreview);
    updatePreview();
}

void ExportDialog::updatePreview()
{
    const QByteArray f = format();
    const bool lossy = f == "jpeg" || f == "webp";
    m_quality->setEnabled(lossy);
    m_qualityLabel->setEnabled(lossy);
    QImage img = m_doc->composite().scaled(m_preview->size() - QSize(8, 8), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage canvas(img.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter p(&canvas);
    if (f == "jpeg" || f == "bmp") {
        canvas.fill(Qt::white);
    } else {
        for (int y = 0; y < canvas.height(); y += 8)
            for (int x = 0; x < canvas.width(); x += 8)
                p.fillRect(x, y, 8, 8, ((x + y) / 8) % 2 ? QColor(0xcc, 0xcc, 0xcc) : Qt::white);
    }
    p.drawImage(0, 0, img);
    p.end();
    m_preview->setPixmap(QPixmap::fromImage(canvas));
}

QByteArray ExportDialog::format() const { return m_format->currentData().toString().toLatin1(); }
int ExportDialog::quality() const { return m_quality->isEnabled() ? m_quality->value() : -1; }

QString ExportDialog::extension() const
{
    const QByteArray f = format();
    if (f == "jpeg") return QStringLiteral("jpg");
    if (f == "tiff") return QStringLiteral("tif");
    return QString::fromLatin1(f);
}

// ---------------- ModifySelectionDialog ----------------

ModifySelectionDialog::ModifySelectionDialog(const QString& title, const QString& label, int initial,
                                             bool boundsOption, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(label, this));
    m_amount = new QSpinBox(this);
    m_amount->setRange(1, 500);
    m_amount->setValue(initial);
    m_amount->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_amount->setFixedWidth(64);
    row->addWidget(m_amount);
    row->addWidget(new QLabel(QStringLiteral("pixels"), this));
    row->addStretch();
    root->addLayout(row);
    if (boundsOption) {
        m_bounds = new QCheckBox(QStringLiteral("Apply effect at canvas bounds"), this);
        root->addWidget(m_bounds);
    }
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
    m_amount->selectAll();
}

int ModifySelectionDialog::amount() const { return m_amount->value(); }

bool ModifySelectionDialog::atCanvasBounds() const { return m_bounds && m_bounds->isChecked(); }

// ---------------- NewGuideDialog ----------------

NewGuideDialog::NewGuideDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New Guide"));
    auto* root = new QVBoxLayout(this);
    auto* group = new QGroupBox(QStringLiteral("Orientation"), this);
    auto* gl = new QVBoxLayout(group);
    m_horizontal = new QRadioButton(QStringLiteral("Horizontal"), group);
    auto* vertical = new QRadioButton(QStringLiteral("Vertical"), group);
    m_horizontal->setChecked(true);
    gl->addWidget(m_horizontal);
    gl->addWidget(vertical);
    root->addWidget(group);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Position:"), this));
    m_position = new QDoubleSpinBox(this);
    m_position->setRange(-30000, 30000);
    m_position->setDecimals(0);
    m_position->setSuffix(QStringLiteral(" px"));
    m_position->setButtonSymbols(QAbstractSpinBox::NoButtons);
    row->addWidget(m_position, 1);
    root->addLayout(row);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
}

Qt::Orientation NewGuideDialog::orientation() const
{
    return m_horizontal->isChecked() ? Qt::Horizontal : Qt::Vertical;
}

double NewGuideDialog::position() const { return m_position->value(); }
