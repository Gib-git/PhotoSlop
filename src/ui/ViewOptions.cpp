#include "ui/ViewOptions.h"

#include <QSettings>

ViewOptions::ViewOptions(QObject* parent)
    : QObject(parent)
{
    QSettings s;
    s.beginGroup(QStringLiteral("view"));
    rulers = s.value(QStringLiteral("rulers"), rulers).toBool();
    snap = s.value(QStringLiteral("snap"), snap).toBool();
    snapGuides = s.value(QStringLiteral("snapGuides"), snapGuides).toBool();
    snapGrid = s.value(QStringLiteral("snapGrid"), snapGrid).toBool();
    snapBounds = s.value(QStringLiteral("snapBounds"), snapBounds).toBool();
    units = Units(s.value(QStringLiteral("units"), int(units)).toInt());
}

void ViewOptions::notify()
{
    QSettings s;
    s.beginGroup(QStringLiteral("view"));
    s.setValue(QStringLiteral("rulers"), rulers);
    s.setValue(QStringLiteral("snap"), snap);
    s.setValue(QStringLiteral("snapGuides"), snapGuides);
    s.setValue(QStringLiteral("snapGrid"), snapGrid);
    s.setValue(QStringLiteral("snapBounds"), snapBounds);
    s.setValue(QStringLiteral("units"), int(units));
    emit changed();
}
