#pragma once

#include <QColor>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace chatterino {

class SolrinoPaintTile;

/// The "Solrino Paints" tab in Vanity: every paint Solrino offers, shown on
/// your own name. Clicking one wears it.
class SolrinoPaintGallery final : public QWidget
{
public:
    explicit SolrinoPaintGallery(QWidget *parent = nullptr);

    /// Called with the paint id, or an empty string for no paint
    std::function<void(const QString &)> paintChosen;

    void setIdentity(const QString &displayName, const QColor &color);
    void setSelected(const QString &paintId);
    void setStatus(const QString &text, bool error = false);
    void setUiScale(float scale);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Filter { All, Animated, Still };

    void setFilter(Filter filter);
    void relayout();
    void animateTiles();

    QLabel *status_{};
    QScrollArea *scroll_{};
    QWidget *grid_{};
    QGridLayout *gridLayout_{};
    QVector<QPushButton *> filterButtons_;
    QVector<SolrinoPaintTile *> tiles_;
    QTimer animationTimer_;
    Filter filter_ = Filter::All;
    int columns_ = 0;
    float uiScale_ = 1.F;
};

}  // namespace chatterino
