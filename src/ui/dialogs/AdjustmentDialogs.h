#pragma once

#include "core/Adjustments.h"
#include "core/Filters.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QHash>
#include <QTimer>
#include <array>
#include <functional>
#include <memory>

class BlendModeCombo;
class Document;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QVBoxLayout;

// ---------------- Widgets ----------------

// Histogram bars for one channel, 256 px wide plus a 6 px margin each side.
class HistogramView : public QWidget {
    Q_OBJECT
public:
    explicit HistogramView(QWidget* parent = nullptr);
    void setData(const std::array<quint32, 256>& bins, const QColor& color);
    QSize sizeHint() const override { return QSize(268, 110); }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    std::array<quint32, 256> m_bins{};
    QColor m_color;
};

// A gradient strip with triangle handles (Levels input and output, Threshold).
class LevelsBar : public QWidget {
    Q_OBJECT
public:
    LevelsBar(int handles, QWidget* parent = nullptr);
    void setValues(const QList<double>& values); // 0..255, does not emit
    QList<double> values() const { return m_values; }
    QSize sizeHint() const override { return QSize(268, 22); }

signals:
    void moved(int handle, double value);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    double valueAt(int x) const;
    int xOf(double value) const;
    QList<double> m_values;
    int m_drag = -1;
};

// The Curves graph: drag points, click to add, drag off the graph or press Delete to remove.
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);
    void setPoints(const Adjust::CurvePoints& points); // does not emit
    Adjust::CurvePoints points() const { return m_points; }
    void setHistogram(const std::array<quint32, 256>& bins);
    void setCurveColor(const QColor& c);
    int selected() const { return m_selected; }
    // Moves the selected point (from the Input/Output fields).
    void setSelectedPoint(const QPointF& value);
    QSize sizeHint() const override { return QSize(266, 266); }

signals:
    void pointsChanged();
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRectF graph() const;
    QPointF toWidget(const QPointF& v) const;
    QPointF toValue(const QPointF& w) const;
    void moveSelected(QPointF v, bool fromMouse);

    Adjust::CurvePoints m_points = Adjust::identityCurve();
    std::array<quint32, 256> m_hist{};
    QColor m_color;
    int m_selected = -1;
    bool m_dragging = false;
    bool m_removed = false;
};

// Label, slider and number field in one row; the slider may cover less than the field's range.
class SliderField : public QWidget {
    Q_OBJECT
public:
    SliderField(const QString& label, double min, double max, double value, int decimals = 0,
                const QString& unit = QString(), double sliderMax = 0, QWidget* parent = nullptr);
    double value() const;
    void setValue(double v);              // does not emit
    void setRange(double min, double max); // does not emit
    QDoubleSpinBox* field() const { return m_spin; }
    QSlider* slider() const { return m_slider; }

signals:
    void valueChanged(double value);

private:
    QSlider* m_slider;
    QDoubleSpinBox* m_spin;
    double m_scale;
    double m_sliderMax;
};

// ---------------- Dialogs ----------------

// Base for adjustment and filter dialogs: controls on the left, OK / Cancel / Preview on the
// right, and a live preview on the canvas rendered on worker threads. OK commits one history
// state; Cancel restores the layer.
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    ~PreviewDialog() override;

    // False when the edit layer cannot be changed; show error() instead of exec().
    bool isReady() const { return m_session && m_session->isValid(); }
    QString error() const { return m_error; }
    // The spec for the current settings.
    virtual Filters::Spec spec() const = 0;

    bool previewEnabled() const;
    void setPreviewEnabled(bool on);
    // Blocks until the canvas shows the current settings (for tests).
    void waitForPreview();
    // What OK changed, for Edit > Fade.
    const Filters::Applied& applied() const { return m_applied; }

    void accept() override;
    void reject() override;

protected:
    PreviewDialog(Document* doc, const QString& title, bool spreads, QWidget* parent,
                  const QRect& target = QRect());
    QVBoxLayout* content() const { return m_content; }
    // Adds a button to the right-hand column, under OK and Cancel.
    QPushButton* addButton(const QString& text);
    Filters::Session* session() const { return m_session.get(); }
    // Call whenever a control changes.
    void settingsChanged();

private:
    void render();
    void finished();
    void stopWorker(bool keepCurrent);
    void finish(bool commit);

    std::unique_ptr<Filters::Session> m_session;
    QString m_error;
    QFutureWatcher<QImage> m_watcher;
    std::shared_ptr<Filters::CancelFlag> m_cancel;
    QTimer m_timer;
    bool m_stale = true;      // the canvas does not show the current settings
    bool m_rendering = false; // a job is out whose finished() has not run yet
    QVBoxLayout* m_content;
    QVBoxLayout* m_buttons;
    QCheckBox* m_preview;
    Filters::Applied m_applied;
};

// A dialog built from sliders, check boxes and choices whose values feed a spec builder.
// Used by the filters and the simpler adjustments. Values are remembered per title.
class ParamDialog : public PreviewDialog {
    Q_OBJECT
public:
    using Values = QHash<QString, double>;
    using Builder = std::function<Filters::Spec(const Values&)>;

    ParamDialog(Document* doc, const QString& title, bool spreads, Builder builder, QWidget* parent = nullptr);

    SliderField* addSlider(const QString& key, const QString& label, double min, double max, double value,
                           int decimals = 0, const QString& unit = QString(), double sliderMax = 0);
    QCheckBox* addCheck(const QString& key, const QString& label, bool value);
    void addChoice(const QString& key, const QString& title, const QStringList& options, int value);
    // A value that has no control (such as a noise seed).
    void addHidden(const QString& key, double value) { m_values.insert(key, value); }

    double value(const QString& key) const { return m_values.value(key); }
    void setValue(const QString& key, double v);
    Values values() const { return m_values; }
    // Restores the values last confirmed with OK in a dialog of this title.
    void useLastValues();
    // Call after adding the controls.
    void ready() { settingsChanged(); }

    Filters::Spec spec() const override { return m_builder(m_values); }
    void accept() override;

private:
    QString m_title;
    Builder m_builder;
    Values m_values;
    QHash<QString, std::function<void(double)>> m_setters;
    static QHash<QString, Values> s_last;
};

class LevelsDialog : public PreviewDialog {
    Q_OBJECT
public:
    LevelsDialog(Document* doc, bool useLast, QWidget* parent = nullptr);
    Filters::Spec spec() const override;
    Adjust::Levels levels() const { return m_levels; }
    void setLevels(const Adjust::Levels& levels);
    void setChannel(int channel);
    void autoLevels();
    void accept() override;

private:
    void sync();
    void inputMoved(int handle, double value);
    Adjust::Levels m_levels;
    Adjust::Histogram m_hist;
    int m_channel = 0;
    QComboBox* m_channelBox;
    HistogramView* m_histView;
    LevelsBar* m_input;
    LevelsBar* m_output;
    QSpinBox* m_inBlack;
    QDoubleSpinBox* m_gamma;
    QSpinBox* m_inWhite;
    QSpinBox* m_outBlack;
    QSpinBox* m_outWhite;
    static Adjust::Levels s_last;
};

class CurvesDialog : public PreviewDialog {
    Q_OBJECT
public:
    CurvesDialog(Document* doc, bool useLast, QWidget* parent = nullptr);
    Filters::Spec spec() const override;
    Adjust::Curves curves() const { return m_curves; }
    void setCurves(const Adjust::Curves& curves);
    void setChannel(int channel);
    CurveEditor* editor() const { return m_editor; }
    void accept() override;

private:
    void sync();
    void syncFields();
    Adjust::Curves m_curves;
    Adjust::Histogram m_hist;
    int m_channel = 0;
    QComboBox* m_channelBox;
    CurveEditor* m_editor;
    QSpinBox* m_inField;
    QSpinBox* m_outField;
    static Adjust::Curves s_last;
};

class HueSaturationDialog : public PreviewDialog {
    Q_OBJECT
public:
    HueSaturationDialog(Document* doc, bool useLast, QWidget* parent = nullptr);
    Filters::Spec spec() const override;
    Adjust::HueSaturation settings() const { return m_hs; }
    void setSettings(const Adjust::HueSaturation& hs);
    void setRange(int range);
    void accept() override;

private:
    void sync();
    void changed();
    Adjust::HueSaturation m_hs;
    int m_range = 0;
    QComboBox* m_rangeBox;
    SliderField* m_hue;
    SliderField* m_sat;
    SliderField* m_light;
    QCheckBox* m_colorize;
    class SpectrumBars* m_bars;
    static Adjust::HueSaturation s_last;
};

class ColorBalanceDialog : public PreviewDialog {
    Q_OBJECT
public:
    ColorBalanceDialog(Document* doc, bool useLast, QWidget* parent = nullptr);
    Filters::Spec spec() const override;
    Adjust::ColorBalance settings() const { return m_cb; }
    void setSettings(const Adjust::ColorBalance& cb);
    void setTone(int tone);
    void accept() override;

private:
    void sync();
    Adjust::ColorBalance m_cb;
    int m_tone = 1;
    std::array<QSlider*, 3> m_sliders{};
    std::array<QSpinBox*, 3> m_fields{};
    std::array<QRadioButton*, 3> m_tones{};
    QCheckBox* m_preserve;
    static Adjust::ColorBalance s_last;
};

class ThresholdDialog : public PreviewDialog {
    Q_OBJECT
public:
    ThresholdDialog(Document* doc, QWidget* parent = nullptr);
    Filters::Spec spec() const override;
    int level() const;
    void setLevel(int level);
    void accept() override;

private:
    LevelsBar* m_bar;
    QSpinBox* m_level;
    static int s_last;
};

// Edit > Fade: mixes the last filter or adjustment with the pixels it replaced.
class FadeDialog : public PreviewDialog {
    Q_OBJECT
public:
    FadeDialog(Document* doc, const QString& name, const Filters::Applied& faded, QWidget* parent = nullptr);
    Filters::Spec spec() const override;
    void setOpacity(int percent);

private:
    QString m_name;
    QImage m_before;
    SliderField* m_opacity;
    BlendModeCombo* m_mode;
};
