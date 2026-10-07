#pragma once

#include "core/BlendMode.h"
#include "core/DocumentOps.h"

#include <QColor>
#include <QDialog>
#include <QImage>

class BlendModeCombo;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QTabBar;
class QToolButton;
class ColorState;
class Document;

// File > New: Photoshop's preset browser with "Preset Details" on the right.
class NewDocumentDialog : public QDialog {
    Q_OBJECT
public:
    enum class Background { White, Black, BackgroundColor, Transparent, Custom };
    NewDocumentDialog(const QString& defaultName, const QSize& clipboardSize, QWidget* parent = nullptr);

    QString name() const;
    QSize pixelSize() const;
    double resolution() const;
    Background background() const;
    QColor customColor() const { return m_custom; }

private:
    void showCategory(int index);
    void applyPreset(int w, int h, double dpi, const QString& name);
    void setUnitsFields();
    double toPixels(double v) const;
    double fromPixels(double px) const;

    QTabBar* m_tabs;
    QListWidget* m_presets;
    QLineEdit* m_name;
    QDoubleSpinBox* m_width;
    QDoubleSpinBox* m_height;
    QComboBox* m_units;
    QDoubleSpinBox* m_resolution;
    QComboBox* m_resUnits;
    QComboBox* m_background;
    QToolButton* m_portrait;
    QToolButton* m_landscape;
    QSize m_clipboard;
    double m_pxW = 1920, m_pxH = 1080;
    QColor m_custom = Qt::white;
    int m_lastUnits = 0;
    bool m_updating = false;
};

class ImageSizeDialog : public QDialog {
    Q_OBJECT
public:
    ImageSizeDialog(Document* doc, QWidget* parent = nullptr);
    QSize newSize() const { return QSize(int(m_w), int(m_h)); }
    double resolution() const;
    Ops::Resample method() const;

private:
    void refresh(QObject* source);
    double unitToPx(double v, int unit, bool horizontal) const;
    double pxToUnit(double px, int unit, bool horizontal) const;

    QSize m_orig;
    double m_w, m_h;
    double m_origDpi;
    QLabel* m_info;
    QDoubleSpinBox* m_width;
    QDoubleSpinBox* m_height;
    QComboBox* m_wUnits;
    QComboBox* m_hUnits;
    QDoubleSpinBox* m_res;
    QToolButton* m_link;
    QCheckBox* m_resample;
    QComboBox* m_method;
    bool m_updating = false;
};

class CanvasSizeDialog : public QDialog {
    Q_OBJECT
public:
    CanvasSizeDialog(Document* doc, ColorState* colors, QWidget* parent = nullptr);
    QSize newSize() const;
    QPoint anchorOffset() const;
    QColor extensionColor() const;

private:
    void updateAnchors();
    QSize m_orig;
    double m_dpi;
    QDoubleSpinBox* m_width;
    QDoubleSpinBox* m_height;
    QComboBox* m_units;
    QCheckBox* m_relative;
    QComboBox* m_color;
    QList<QToolButton*> m_anchorButtons;
    int m_anchor = 4;
    ColorState* m_colors;
    QColor m_other = Qt::white;
};

class FillDialog : public QDialog {
    Q_OBJECT
public:
    FillDialog(ColorState* colors, QWidget* parent = nullptr);
    QColor color() const;
    BlendMode mode() const;
    float opacity() const;
    bool preserveTransparency() const;

private:
    ColorState* m_colors;
    QComboBox* m_contents;
    BlendModeCombo* m_mode;
    QSpinBox* m_opacity;
    QCheckBox* m_preserve;
    QColor m_custom = Qt::black;
};

class NewLayerDialog : public QDialog {
    Q_OBJECT
public:
    NewLayerDialog(const QString& defaultName, QWidget* parent = nullptr);
    QString name() const;
    BlendMode mode() const;
    float opacity() const;

private:
    QLineEdit* m_name;
    BlendModeCombo* m_mode;
    QSpinBox* m_opacity;
};

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(Document* doc, QWidget* parent = nullptr);
    QByteArray format() const;
    int quality() const;
    QString extension() const;

private:
    void updatePreview();
    Document* m_doc;
    QComboBox* m_format;
    QSlider* m_quality;
    QLabel* m_qualityLabel;
    QLabel* m_preview;
    QLabel* m_sizeLabel;
};

// Select > Modify > Border / Smooth / Expand / Contract.
class ModifySelectionDialog : public QDialog {
    Q_OBJECT
public:
    ModifySelectionDialog(const QString& title, const QString& label, int initial, bool boundsOption,
                          QWidget* parent = nullptr);
    int amount() const;
    bool atCanvasBounds() const;

private:
    QSpinBox* m_amount;
    QCheckBox* m_bounds = nullptr;
};

// View > New Guide.
class NewGuideDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewGuideDialog(QWidget* parent = nullptr);
    Qt::Orientation orientation() const;
    double position() const;

private:
    class QRadioButton* m_horizontal;
    QDoubleSpinBox* m_position;
};
