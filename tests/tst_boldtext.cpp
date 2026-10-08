// Bold Chinese text, on Qt's offscreen display: drawn with the font's own
// Bold, not thickened a second time by Qt (GRND-0042).
#include "gui/wizard.h"

#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QtTest>

namespace {

const QString kFamily = QStringLiteral("Noto Sans CJK SC");

// How much black a title in this weight puts on a white strip.
double ink(QFont::Weight weight)
{
    QFont font(kFamily);
    font.setPixelSize(13);
    font.setWeight(weight);
    QImage image(400, 60, QImage::Format_Grayscale8);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(10, 40, QString::fromUtf8("媒体解码器 音频固件"));
    painter.end();
    double total = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
            total += 255 - line[x];
    }
    return total / 255;
}

} // namespace

class TstBoldText : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { gw::useFontsOwnBold(); }

    // Measured 2026-10-08 with Qt 6.11.2: the font's own Bold has about
    // 1.4 times Regular's ink; thickened again by Qt, about 1.85.
    void boldIsTheFontsOwn()
    {
        if (!QFontDatabase::families().contains(kFamily))
            QSKIP("Noto Sans CJK SC is not installed");
        const double ratio = ink(QFont::Bold) / ink(QFont::Normal);
        QVERIFY2(ratio < 1.6, qPrintable(QStringLiteral("bold/regular ink %1").arg(ratio)));
    }
};

QTEST_MAIN(TstBoldText)
#include "tst_boldtext.moc"
