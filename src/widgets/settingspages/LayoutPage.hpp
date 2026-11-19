// SPDX-FileCopyrightText: 2026 Contributors to Solrino
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

#include <QString>

class QComboBox;
class QLabel;
class QPushButton;

namespace chatterino {

/// One arrangement of the app. The defaults are the basic layout.
struct SolrinoLayoutChoice {
    int tabs = 0;  // NotebookTabLocation
    QString chatBox = QStringLiteral("below");
    QString logo = QStringLiteral("left");
    QString gear = QStringLiteral("right");
    QString account = QStringLiteral("right");

    bool operator==(const SolrinoLayoutChoice &other) const = default;
};

class SolrinoLayoutMock;

/// Picks where things go. Choices only show on the small picture until
/// "Preview on my window" is pressed, and only stay once Save is pressed.
class LayoutPage : public SettingsPage
{
public:
    LayoutPage();
    ~LayoutPage() override;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void pick(const SolrinoLayoutChoice &choice);
    void readControls();
    void stopPreview();
    void refresh();

    SolrinoLayoutChoice saved_;
    SolrinoLayoutChoice pending_;
    bool previewing_ = false;

    SolrinoLayoutMock *mock_{};
    QComboBox *tabs_{};
    QComboBox *chatBox_{};
    QComboBox *logo_{};
    QComboBox *gear_{};
    QComboBox *account_{};
    QLabel *status_{};
    QPushButton *previewButton_{};
    QPushButton *saveButton_{};
    QPushButton *undoButton_{};
    QPushButton *basicButton_{};
};

}  // namespace chatterino
