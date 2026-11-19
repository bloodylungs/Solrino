#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <array>
#include <functional>

namespace chatterino {

/// The update card, drawn in the same style as the installer and the ping
/// popups. It appears as soon as an update is known about, while the download
/// runs behind it. It never takes the keyboard, because it can appear while
/// someone is typing in chat or playing a game.
///
/// Closing it with the X counts as "Remind me later".
class SolrinoUpdatePrompt : public QWidget
{
public:
    enum class Choice { UpdateNow, UpdateLater, RemindLater };

    /// One line of the update log: kind is "new", "fixed", "removed" or
    /// anything else (shown as "Changed")
    struct Change {
        QString kind;
        QString title;
        QString detail;
    };

    /// With changes, the card lists them in sections. Without, it shows the
    /// notes as one paragraph, like updates from before sections existed.
    SolrinoUpdatePrompt(const QString &version, const QString &notes,
                        std::function<void(Choice)> onChoice,
                        QVector<Change> changes = {});

    /// The download has finished, so "Update now" works straight away.
    void setReady();
    /// The download failed: says why, and "Update now" becomes "Try again".
    void showProblem(const QString &problem);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum Control { UpdateNow, UpdateLater, RemindLater, Close, ControlCount };

    int controlAt(const QPointF &position) const;
    void drawButton(QPainter &painter, const QRectF &area, const QString &text,
                    bool primary, double hover);
    void tick();
    /// Places the sections and changes, returning the height they need
    int layoutChanges(int width);
    void drawChanges(QPainter &painter);
    /// The notes or changes, faded at the edges while there's more to scroll
    void drawNotes(QPainter &painter);
    void drawScrollBar(QPainter &painter);
    double maxScroll() const;
    QRectF scrollTrack() const;
    QRectF scrollThumb() const;
    void scrollTo(double offset, bool instant = false);

    /// One thing to draw in the changes list, placed relative to notesArea_
    struct Piece {
        enum class Kind { Tag, Bullet, Title, Detail };
        Kind kind;
        QRectF rect;
        QString text;
        QString changeKind;
    };

    QString version_;
    QString notes_;
    QVector<Change> changes_;
    QVector<Piece> pieces_;
    bool ready_ = false;
    bool waiting_ = false;
    bool failed_ = false;
    std::function<void(Choice)> onChoice_;
    QPixmap logo_;
    QRectF card_;
    QRectF notesArea_;
    std::array<QRectF, ControlCount> controls_;
    std::array<double, ControlCount> hover_{};
    int hovered_ = -1;
    int pressed_ = -1;
    bool dragging_ = false;
    QPoint dragOffset_;
    /// How tall the whole list is, and how far down it's scrolled
    int contentHeight_ = 0;
    double scroll_ = 0;
    double scrollTarget_ = 0;
    bool draggingThumb_ = false;
    double thumbGrab_ = 0;
    bool thumbHovered_ = false;
    QTimer timer_;
    QElapsedTimer clock_;
};

}  // namespace chatterino
