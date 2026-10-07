#pragma once

#include <QColor>
#include <QObject>

// Application-wide foreground / background colours (like Photoshop's toolbar swatches).
class ColorState : public QObject {
    Q_OBJECT
public:
    explicit ColorState(QObject* parent = nullptr) : QObject(parent) {}

    QColor foreground() const { return m_fg; }
    QColor background() const { return m_bg; }

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
    QColor m_fg = Qt::black;
    QColor m_bg = Qt::white;
};
