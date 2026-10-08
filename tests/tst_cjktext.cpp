// Chinese, Japanese and Korean text, on Qt's offscreen display: bold is the
// font's own Bold, not thickened a second time by Qt (GRND-0042), and each
// language draws shared characters with its own face (GRND-0044).
#include "gui/wizard.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <QTextLayout>
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

// The face that draws text in font.
QString faceDrawing(const QString &text, const QFont &font)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    const QList<QGlyphRun> runs = layout.glyphRuns();
    return runs.size() == 1 ? runs.first().rawFont().familyName() : QString();
}

} // namespace

class TstCjkText : public QObject
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

    // Qt picks the face for a shared character from the system's language,
    // so with no hint every one of these drew with the Korean face. Taken
    // in turn in one run, as when the user changes language.
    void eachLanguageDrawsWithItsOwnFace()
    {
        const QFont base = QGuiApplication::font();
        const QList<std::pair<QString, QString>> faces{
            {QStringLiteral("zh_TW"), QStringLiteral("Noto Sans CJK TC")},
            {QStringLiteral("ja"), QStringLiteral("Noto Sans CJK JP")},
            {QStringLiteral("zh_CN"), QStringLiteral("Noto Sans CJK SC")},
            {QStringLiteral("ko"), QStringLiteral("Noto Sans CJK KR")},
        };
        int checked = 0;
        for (const auto &[code, face] : faces) {
            if (!QFontDatabase::families().contains(face))
                continue;
            QCOMPARE(faceDrawing(QString::fromUtf8("骨"), gw::fontFor(code, base)), face);
            ++checked;
        }
        if (checked == 0)
            QSKIP("no Noto Sans CJK face is installed");
    }

    void otherLanguagesKeepTheSystemFont()
    {
        const QFont base = QGuiApplication::font();
        QCOMPARE(gw::fontFor(QStringLiteral("de"), base), base);
        QCOMPARE(gw::fontFor(QStringLiteral("ar"), base), base);
    }
};

QTEST_MAIN(TstCjkText)
#include "tst_cjktext.moc"
