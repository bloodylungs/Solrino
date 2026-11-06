#pragma once

#include <QColor>
#include <QString>

namespace chatterino {

struct SettingsTheme {
    QColor background;
    QColor surface;
    QColor raisedSurface;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor selectionSurface;
    QString interfaceFontFamily;
    int interfaceFontSize;
};

inline const SettingsTheme &settingsTheme()
{
    static const SettingsTheme value{
        .background = QColor(QStringLiteral("#0e0808")),
        .surface = QColor(QStringLiteral("#1c1010")),
        .raisedSurface = QColor(QStringLiteral("#2a1515")),
        .text = QColor(QStringLiteral("#f1e8e6")),
        .mutedText = QColor(QStringLiteral("#a99391")),
        .accent = QColor(QStringLiteral("#e3101c")),
        .selectionSurface = QColor(QStringLiteral("#3a1518")),
        .interfaceFontFamily = QStringLiteral("Gabarito"),
        .interfaceFontSize = 10,
    };
    return value;
}

}
