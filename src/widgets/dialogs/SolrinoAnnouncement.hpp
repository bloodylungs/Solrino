#pragma once

#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <functional>

namespace chatterino::solrino {

/// Called with the "announcement" from the badge list. A new one shows once,
/// as a banner across the top of Solrino.
void receiveAnnouncement(const QJsonObject &announcement);

/// The owner sends an announcement to everyone. An empty message takes the
/// current one down. The callback gets whether it worked and what to say.
void sendAnnouncement(const QString &message,
                      std::function<void(bool, const QString &)> done);

/// The strip across the top of the main window with the owner's message
class AnnouncementBanner : public QWidget
{
public:
    AnnouncementBanner(const QString &message, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();
    QRectF messageRect() const;

    QString message_;
    QRectF close_;
    bool overClose_ = false;
};

}  // namespace chatterino::solrino
