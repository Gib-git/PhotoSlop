#pragma once

#include "app/Preferences.h"
#include "ui/ViewOptions.h"

#include <QDialog>

class ColorButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

// Edit > Preferences (Ctrl+K): a page list on the left, like Photoshop's. OK saves the values
// into Preferences and the shared ViewOptions.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    enum Page { General, Interface, Performance, Cursors, Transparency, Units, Guides, FileHandling };

    // `gpuInfo` describes the graphics processor (empty when none can be used).
    PreferencesDialog(ViewOptions* view, const QString& gpuInfo, QWidget* parent = nullptr, Page page = General);
    void setPage(Page page);

    void accept() override;

private:
    void load(const PreferenceValues& v);
    QWidget* page(const QString& title);

    ViewOptions* m_view;
    QListWidget* m_list;
    QStackedWidget* m_stack;

    QCheckBox* m_home;
    QCheckBox* m_wheelZoom;
    QComboBox* m_fontSize;
    QSpinBox* m_history;
    QCheckBox* m_gpu;
    QRadioButton* m_paint[3];
    QCheckBox* m_crosshair;
    QRadioButton* m_other[2];
    QComboBox* m_checkerSize;
    QComboBox* m_checkerColors;
    QComboBox* m_rulerUnits;
    ColorButton* m_guideColor;
    ColorButton* m_gridColor;
    QComboBox* m_gridStyle;
    QDoubleSpinBox* m_gridEvery;
    QComboBox* m_gridUnits;
    QSpinBox* m_subdivisions;
    QSpinBox* m_recent;
};
