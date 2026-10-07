#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

// The Photoshop-style dark UI theme.
namespace Theme {

inline const QColor kPanel{0x32, 0x32, 0x32};
inline const QColor kPanelDark{0x28, 0x28, 0x28};
inline const QColor kHeader{0x2b, 0x2b, 0x2b};
inline const QColor kField{0x45, 0x45, 0x45};
inline const QColor kBorder{0x1e, 0x1e, 0x1e};
inline const QColor kText{0xd6, 0xd6, 0xd6};
inline const QColor kTextDim{0x8e, 0x8e, 0x8e};
inline const QColor kAccent{0x14, 0x73, 0xe6};
inline const QColor kPasteboard{0x28, 0x28, 0x28};
inline const QColor kRowSelected{0x4b, 0x4b, 0x4b};

void apply(QApplication& app);
QIcon icon(const QString& name);

} // namespace Theme
