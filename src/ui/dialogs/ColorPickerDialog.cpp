#include "ui/dialogs/ColorPickerDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

QColor hsvColor(double h, double s, double v)
{
    return QColor::fromHsvF(float(std::fmod(h, 360.0) / 360.0), float(s), float(v));
}

class Swatch : public QWidget {
public:
    Swatch(QWidget* parent) : QWidget(parent) { setFixedSize(64, 34); }
    QColor color;
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), color);
    }
};

} // namespace

// ---------------- SBField ----------------

SBField::SBField(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(258, 258);
    setCursor(Qt::CrossCursor);
}

void SBField::setHsv(double h, double s, double v)
{
    m_h = h;
    m_s = s;
    m_v = v;
    update();
}

void SBField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect area(1, 1, 256, 256);
    if (m_cacheHue != m_h) {
        m_cache = QImage(256, 256, QImage::Format_RGB32);
        for (int y = 0; y < 256; ++y) {
            auto* row = reinterpret_cast<QRgb*>(m_cache.scanLine(y));
            for (int x = 0; x < 256; ++x) row[x] = hsvColor(m_h, x / 255.0, 1.0 - y / 255.0).rgb();
        }
        m_cacheHue = m_h;
    }
    p.drawImage(area.topLeft(), m_cache);
    p.setPen(QColor(0x1e, 0x1e, 0x1e));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    // Ring marker, white on dark colours and black on light ones.
    QPointF c(1 + m_s * 255, 1 + (1 - m_v) * 255);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(area);
    p.setPen(QPen(m_v > 0.6 && m_s < 0.5 ? Qt::black : Qt::white, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, 5, 5);
}

void SBField::pick(const QPointF& pt)
{
    m_s = std::clamp((pt.x() - 1) / 255.0, 0.0, 1.0);
    m_v = std::clamp(1.0 - (pt.y() - 1) / 255.0, 0.0, 1.0);
    update();
    emit changed(m_s, m_v);
}

void SBField::mousePressEvent(QMouseEvent* e) { pick(e->position()); }
void SBField::mouseMoveEvent(QMouseEvent* e) { pick(e->position()); }

// ---------------- HueStrip ----------------

HueStrip::HueStrip(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(34, 258);
}

void HueStrip::setHue(double h)
{
    m_h = h;
    update();
}

void HueStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect bar(8, 1, 18, 256);
    for (int y = 0; y < 256; ++y) {
        p.setPen(hsvColor(360.0 * (1.0 - y / 256.0), 1, 1));
        p.drawLine(bar.left(), bar.top() + y, bar.right(), bar.top() + y);
    }
    p.setPen(QColor(0x1e, 0x1e, 0x1e));
    p.drawRect(bar.adjusted(-1, -1, 0, 0));
    // Triangular slider arrows on both sides.
    const double y = 1 + (1.0 - m_h / 360.0) * 255;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xe0, 0xe0, 0xe0));
    p.drawPolygon(QPolygonF{QPointF(0, y - 5), QPointF(7, y), QPointF(0, y + 5)});
    p.drawPolygon(QPolygonF{QPointF(34, y - 5), QPointF(27, y), QPointF(34, y + 5)});
}

void HueStrip::pick(double y)
{
    m_h = std::clamp((1.0 - (y - 1) / 255.0) * 360.0, 0.0, 360.0);
    if (m_h >= 360.0) m_h = 0.0;
    update();
    emit changed(m_h);
}

void HueStrip::mousePressEvent(QMouseEvent* e) { pick(e->position().y()); }
void HueStrip::mouseMoveEvent(QMouseEvent* e) { pick(e->position().y()); }

// ---------------- ColorPickerDialog ----------------

ColorPickerDialog::ColorPickerDialog(const QColor& initial, const QString& title, QWidget* parent)
    : QDialog(parent)
    , m_initial(initial)
{
    setWindowTitle(title);
    auto* root = new QHBoxLayout(this);
    m_field = new SBField(this);
    m_hue = new HueStrip(this);
    root->addWidget(m_field);
    root->addWidget(m_hue);

    auto* right = new QVBoxLayout;
    auto* top = new QHBoxLayout;
    auto* swatchCol = new QVBoxLayout;
    swatchCol->setSpacing(0);
    auto* newLabel = new QLabel(QStringLiteral("new"), this);
    newLabel->setAlignment(Qt::AlignCenter);
    swatchCol->addWidget(newLabel);
    m_newSwatch = new Swatch(this);
    m_currentSwatch = new Swatch(this);
    static_cast<Swatch*>(m_currentSwatch)->color = initial;
    swatchCol->addWidget(m_newSwatch);
    swatchCol->addWidget(m_currentSwatch);
    auto* curLabel = new QLabel(QStringLiteral("current"), this);
    curLabel->setAlignment(Qt::AlignCenter);
    swatchCol->addWidget(curLabel);
    swatchCol->addStretch();
    top->addLayout(swatchCol);
    top->addSpacing(12);

    auto* buttons = new QVBoxLayout;
    auto* ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    auto* addSwatch = new QPushButton(QStringLiteral("Add to Swatches"), this);
    auto* libraries = new QPushButton(QStringLiteral("Color Libraries"), this);
    libraries->setEnabled(false);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(addSwatch, &QPushButton::clicked, this, [this] { emit addToSwatches(color()); });
    for (QPushButton* b : {ok, cancel, addSwatch, libraries}) buttons->addWidget(b);
    buttons->addStretch();
    top->addLayout(buttons);
    right->addLayout(top);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    auto mkSpin = [this](int max, const QString& suffix) {
        auto* s = new QSpinBox(this);
        s->setRange(0, max);
        s->setSuffix(suffix);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(56);
        return s;
    };
    m_hSpin = mkSpin(360, QStringLiteral("°"));
    m_sSpin = mkSpin(100, QStringLiteral("%"));
    m_bSpin = mkSpin(100, QStringLiteral("%"));
    m_rSpin = mkSpin(255, QString());
    m_gSpin = mkSpin(255, QString());
    m_bbSpin = mkSpin(255, QString());
    m_cSpin = mkSpin(100, QStringLiteral("%"));
    m_mSpin = mkSpin(100, QStringLiteral("%"));
    m_ySpin = mkSpin(100, QStringLiteral("%"));
    m_kSpin = mkSpin(100, QStringLiteral("%"));
    const struct { const char* label; QSpinBox* spin; int row; int col; } fields[] = {
        {"H:", m_hSpin, 0, 0}, {"S:", m_sSpin, 1, 0}, {"B:", m_bSpin, 2, 0},
        {"R:", m_rSpin, 4, 0}, {"G:", m_gSpin, 5, 0}, {"B:", m_bbSpin, 6, 0},
        {"C:", m_cSpin, 0, 2}, {"M:", m_mSpin, 1, 2}, {"Y:", m_ySpin, 2, 2}, {"K:", m_kSpin, 3, 2},
    };
    for (const auto& f : fields) {
        grid->addWidget(new QLabel(QString::fromLatin1(f.label), this), f.row, f.col, Qt::AlignRight);
        grid->addWidget(f.spin, f.row, f.col + 1);
    }
    grid->setRowMinimumHeight(3, 8);
    m_hex = new QLineEdit(this);
    m_hex->setFixedWidth(70);
    m_hex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9a-fA-F]{0,6}")), m_hex));
    grid->addWidget(new QLabel(QStringLiteral("#"), this), 7, 0, Qt::AlignRight);
    grid->addWidget(m_hex, 7, 1, 1, 2);
    right->addLayout(grid);
    m_webOnly = new QCheckBox(QStringLiteral("Only Web Colors"), this);
    right->addWidget(m_webOnly);
    right->addStretch();
    root->addLayout(right);

    connect(m_field, &SBField::changed, this, [this](double s, double v) { setHsv(m_h, s, v, m_field); });
    connect(m_hue, &HueStrip::changed, this, [this](double h) { setHsv(h, m_s, m_v, m_hue); });
    for (QSpinBox* s : {m_hSpin, m_sSpin, m_bSpin})
        connect(s, &QSpinBox::valueChanged, this, [this, s] {
            if (!m_updating) setHsv(m_hSpin->value(), m_sSpin->value() / 100.0, m_bSpin->value() / 100.0, s);
        });
    for (QSpinBox* s : {m_rSpin, m_gSpin, m_bbSpin})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (!m_updating) fromRgbFields();
        });
    for (QSpinBox* s : {m_cSpin, m_mSpin, m_ySpin, m_kSpin})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (!m_updating) fromCmykFields();
        });
    connect(m_hex, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (t.size() != 6) return;
        QColor c(QLatin1Char('#') + t);
        if (!c.isValid()) return;
        float h, s, v;
        c.getHsvF(&h, &s, &v);
        setHsv(h < 0 ? m_h : h * 360.0, s, v, m_hex);
    });
    connect(m_webOnly, &QCheckBox::toggled, this, [this] { setHsv(m_h, m_s, m_v, nullptr); });

    float h, s, v;
    initial.getHsvF(&h, &s, &v);
    setHsv(h < 0 ? 0 : h * 360.0, s, v, nullptr);
}

QColor ColorPickerDialog::color() const
{
    QColor c = hsvColor(m_h, m_s, m_v).toRgb();
    if (m_webOnly->isChecked()) {
        auto snap = [](int x) { return int(std::round(x / 51.0)) * 51; };
        c = QColor(snap(c.red()), snap(c.green()), snap(c.blue()));
    }
    return c;
}

void ColorPickerDialog::setHsv(double h, double s, double v, QWidget* source)
{
    m_h = h;
    m_s = s;
    m_v = v;
    updateFields(source);
}

void ColorPickerDialog::fromRgbFields()
{
    QColor c(m_rSpin->value(), m_gSpin->value(), m_bbSpin->value());
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    setHsv(h < 0 ? m_h : h * 360.0, s, v, m_rSpin);
}

void ColorPickerDialog::fromCmykFields()
{
    QColor c = QColor::fromCmykF(m_cSpin->value() / 100.f, m_mSpin->value() / 100.f, m_ySpin->value() / 100.f,
                                 m_kSpin->value() / 100.f)
                   .toRgb();
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    setHsv(h < 0 ? m_h : h * 360.0, s, v, m_cSpin);
}

void ColorPickerDialog::updateFields(QWidget* source)
{
    m_updating = true;
    const QColor c = color();
    m_field->setHsv(m_h, m_s, m_v);
    if (source != m_hue) m_hue->setHue(m_h);
    if (source != m_hSpin && source != m_sSpin && source != m_bSpin) {
        m_hSpin->setValue(int(std::round(m_h)) % 360);
        m_sSpin->setValue(int(std::round(m_s * 100)));
        m_bSpin->setValue(int(std::round(m_v * 100)));
    }
    if (source != m_rSpin) {
        m_rSpin->setValue(c.red());
        m_gSpin->setValue(c.green());
        m_bbSpin->setValue(c.blue());
    }
    if (source != m_cSpin) {
        QColor k = c.toCmyk();
        m_cSpin->setValue(int(std::round(k.cyanF() * 100)));
        m_mSpin->setValue(int(std::round(k.magentaF() * 100)));
        m_ySpin->setValue(int(std::round(k.yellowF() * 100)));
        m_kSpin->setValue(int(std::round(k.blackF() * 100)));
    }
    if (source != m_hex) m_hex->setText(c.name().mid(1));
    static_cast<Swatch*>(m_newSwatch)->color = c;
    m_newSwatch->update();
    m_updating = false;
}

QColor ColorPickerDialog::getColor(const QColor& initial, const QString& title, QWidget* parent)
{
    ColorPickerDialog dlg(initial, title, parent);
    if (dlg.exec() != QDialog::Accepted) return QColor();
    return dlg.color();
}
