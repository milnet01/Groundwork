// An on/off switch in place of a tick box (GRND-0069). It is a QCheckBox
// painted as a sliding switch, so the keyboard, the clicked and toggled
// signals and what a screen reader hears are a tick box's. The track is as
// tall as the text, so it grows with the font, and it paints with the
// palette, so it follows the colour theme.
#pragma once

#include <QCheckBox>

namespace gw {

class Switch : public QCheckBox
{
    Q_OBJECT
public:
    explicit Switch(const QString &text, QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool hitButton(const QPoint &pos) const override;
    void changeEvent(QEvent *event) override;

private:
    // Space around the track, so the focus ring has room.
    int margin() const;
    // Where the track sits, before mirroring for right-to-left text.
    QRect trackRect() const;
};

} // namespace gw
