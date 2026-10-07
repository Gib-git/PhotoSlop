#pragma once

#include <QColor>
#include <QDialog>

class QLineEdit;
class QSpinBox;
class QCheckBox;

// Saturation/brightness square for the current hue.
class SBField : public QWidget {
    Q_OBJECT
public:
    explicit SBField(QWidget* parent = nullptr);
    void setHsv(double h, double s, double v);
signals:
    void changed(double s, double v);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void pick(const QPointF& p);
    double m_h = 0, m_s = 0, m_v = 0;
    QImage m_cache;
    double m_cacheHue = -1;
};

// Vertical hue strip.
class HueStrip : public QWidget {
    Q_OBJECT
public:
    explicit HueStrip(QWidget* parent = nullptr);
    void setHue(double h);
signals:
    void changed(double h);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void pick(double y);
    double m_h = 0;
};

// Photoshop-style colour picker (HSB / RGB / CMYK / hex).
class ColorPickerDialog : public QDialog {
    Q_OBJECT
public:
    ColorPickerDialog(const QColor& initial, const QString& title, QWidget* parent = nullptr);
    QColor color() const;

    static QColor getColor(const QColor& initial, const QString& title, QWidget* parent);

signals:
    void addToSwatches(const QColor& c);

private:
    void setHsv(double h, double s, double v, QWidget* source);
    void updateFields(QWidget* source);
    void fromRgbFields();
    void fromCmykFields();

    double m_h = 0, m_s = 0, m_v = 0;
    QColor m_initial;
    SBField* m_field;
    HueStrip* m_hue;
    QWidget* m_newSwatch;
    QWidget* m_currentSwatch;
    QSpinBox *m_hSpin, *m_sSpin, *m_bSpin, *m_rSpin, *m_gSpin, *m_bbSpin;
    QSpinBox *m_cSpin, *m_mSpin, *m_ySpin, *m_kSpin;
    QLineEdit* m_hex;
    QCheckBox* m_webOnly;
    bool m_updating = false;
};
