#pragma once

#include <QColor>
#include <QObject>

// Application-wide foreground / background colours (like Photoshop's toolbar swatches).
class ColorState : public QObject {
    Q_OBJECT
public:
    explicit ColorState(QObject* parent = nullptr) : QObject(parent) {}

    // In a Grayscale document the colours paint as their greys.
    QColor foreground() const { return m_gray ? grey(m_fg) : m_fg; }
    QColor background() const { return m_gray ? grey(m_bg) : m_bg; }
    bool grayscale() const { return m_gray; }
    void setGrayscale(bool on)
    {
        if (on == m_gray) return;
        m_gray = on;
        emit changed();
    }

    void setForeground(const QColor& c)
    {
        if (c == m_fg) return;
        m_fg = c;
        emit changed();
    }
    void setBackground(const QColor& c)
    {
        if (c == m_bg) return;
        m_bg = c;
        emit changed();
    }
    void swap()
    {
        std::swap(m_fg, m_bg);
        emit changed();
    }
    void reset()
    {
        m_fg = Qt::black;
        m_bg = Qt::white;
        emit changed();
    }

signals:
    void changed();

private:
    static QColor grey(const QColor& c)
    {
        const int v = (c.red() * 77 + c.green() * 151 + c.blue() * 28 + 128) >> 8;
        return QColor(v, v, v, c.alpha());
    }
    QColor m_fg = Qt::black;
    QColor m_bg = Qt::white;
    bool m_gray = false;
};
