#include "tools/Tool.h"

#include "app/Theme.h"
#include "tools/ToolManager.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QWidget>
#include <algorithm>
#include <iterator>

Tool::Tool(ToolManager* manager)
    : QObject(manager)
    , m_manager(manager)
{
}

QWidget* Tool::createOptions(QWidget* parent) { return new QWidget(parent); }

ColorState* Tool::colors() const { return m_manager->colors(); }

void Tool::alert(const QString& message) const { emit m_manager->alertRequested(message); }

QCursor makeIconCursor(const QString& iconName, const QPoint& hotspot, int size)
{
    static QHash<QString, QCursor> cache;
    const QString key = iconName + QString::number(size);
    auto it = cache.find(key);
    if (it != cache.end()) return *it;

    const qreal dpr = 2.0;
    QPixmap icon = Theme::icon(iconName).pixmap(QSize(size, size), dpr);
    auto tinted = [&](const QColor& c) {
        QPixmap t(icon.size());
        t.setDevicePixelRatio(dpr);
        t.fill(Qt::transparent);
        QPainter p(&t);
        p.drawPixmap(0, 0, icon);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(t.rect(), c);
        return t;
    };
    QPixmap black = tinted(Qt::black), white = tinted(Qt::white);
    QPixmap out(QSize(size + 2, size + 2) * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) p.drawPixmap(1 + dx, 1 + dy, white);
    p.drawPixmap(1, 1, black);
    p.end();
    QCursor c(out, hotspot.x() + 1, hotspot.y() + 1);
    cache.insert(key, c);
    return c;
}
