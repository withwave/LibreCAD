#include <catch2/catch_test_macros.hpp>

#include <QTemporaryFile>
#include <QFontDatabase>
#include <QFontMetricsF>

#include "intern/drw_textcodec.h"
#include "rs_block.h"
#include "rs_filterdxfrw.h"
#include "rs_font.h"
#include "rs_fontlist.h"
#include "rs_graphic.h"
#include "rs_mtext.h"
#include "rs_text.h"
#include "rs_settings.h"

namespace {
const std::string korean = u8"한글 똠 제품문의";
const std::string cp949 = "\xc7\xd1\xb1\xdb \x8c\x63 \xc1\xa6\xc7\xb0\xb9\xae\xc0\xc7";

struct EncodingSetting {
    QString previous = LC_GET_ONE_STR("Defaults", "LegacyTextEncoding", "auto");
    ~EncodingSetting() { LC_SET_ONE("Defaults", "LegacyTextEncoding", previous); }
};
}

TEST_CASE("Legacy Korean codec honors read overrides", "[encoding]") {
    for (bool dxf : {true, false}) {
        DRW_TextCodec codec;
        codec.setReadCodePage("CP949");
        codec.setVersion(DRW::AC1015, dxf);
        codec.setCodePage("ANSI_1252", dxf);
        CHECK(codec.toUtf8(cp949) == korean);
        CHECK(codec.getCodePage() == "ANSI_949");

        codec.setReadCodePage("UTF-8");
        codec.setVersion(DRW::AC1015, dxf);
        codec.setCodePage("ANSI_949", dxf);
        CHECK(codec.toUtf8(korean) == korean);
        CHECK(codec.getCodePage() == "UTF-8");

        codec.setReadCodePage("");
        codec.setVersion(DRW::AC1015, dxf);
        codec.setCodePage("CP949", dxf);
        CHECK(codec.toUtf8(cp949) == korean);
        codec.setCodePage("ANSI_1252", dxf);
        CHECK(codec.toUtf8(std::string("caf\xe9")) == u8"café");
    }
}

TEST_CASE("Legacy encoding override preserves modern Unicode", "[encoding]") {
    for (const std::string encoding : {"CP949", "UTF-8"}) {
        DRW_TextCodec codec;
        codec.setReadCodePage(encoding);
        codec.setVersion(DRW::AC1021, true);
        CHECK(codec.toUtf8(korean) == korean);
        codec.setVersion(DRW::AC1021, false);
        CHECK(codec.toUtf8(std::string("\x5c\xd5\x00\xae", 4)) == u8"한글");
    }
}

TEST_CASE("Drawing import uses the configured text encoding", "[encoding]") {
    RS_Settings::init("LibreCAD", "LibreCAD-encoding-tests");
    EncodingSetting restore;
    // Deliberately label both files ANSI_1252 to verify a user override wins.
    const QByteArray prefix = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\nANSI_1252\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nMTEXT\n8\n0\n10\n10\n20\n20\n40\n2.5\n1\n";
    for (const QString encoding : {QString("CP949"), QString("UTF-8")}) {
        LC_SET_ONE("Defaults", "LegacyTextEncoding", encoding);
        QTemporaryFile file;
        REQUIRE(file.open());
        const auto& content = encoding == "CP949" ? cp949 : korean;
        const QByteArray drawing = prefix + QByteArray::fromStdString(content) + "\n0\nENDSEC\n0\nEOF\n";
        REQUIRE(file.write(drawing) == drawing.size());
        REQUIRE(file.flush());
        RS_Graphic graphic;
        RS_FilterDXFRW filter;
        REQUIRE(filter.fileImport(graphic, file.fileName(), RS2::FormatDXFRW));
        int texts = 0;
        for (auto* entity : graphic) {
            if (auto* text = dynamic_cast<RS_MText*>(entity)) {
                ++texts;
                CHECK(text->getText().toStdString() == korean);
                CHECK(text->getUsedTextWidth() > 0.);
            }
        }
        CHECK(texts == 1);
    }
}

TEST_CASE("System font supplies missing Hangul CAD glyphs", "[encoding][fonts]") {
    const QFontMetricsF metrics(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    if (!metrics.inFontUcs4(0xd55c)) {
        SKIP("The system has no font containing Hangul");
    }
    auto* font = RS_FONTLIST->requestFont("standard");
    REQUIRE(font != nullptr);
    for (const QChar ch : QString::fromUtf8(u8"한글똠제품문의")) {
        auto* glyph = font->findLetter(QString(ch));
        REQUIRE(glyph != nullptr);
        CHECK(glyph != font->findLetter(QString(QChar(0xfffd))));
        CHECK(glyph->countDeep() > 0);
        CHECK(glyph->getSize().x > 0.);
        CHECK(glyph->getSize().y > 0.);
        // The nominal CAD cap height is 9. Hangul uses a shared reference cell,
        // not the smaller Latin cap-height metric or a per-syllable ink height.
        CHECK(glyph->getSize().x <= 9. + 1.e-9);
        CHECK(glyph->getSize().y <= 9. + 1.e-9);
        CHECK(font->findLetter(QString(ch)) == glyph); // cached outlines
    }
}

TEST_CASE("Wheel DWG Korean annotations decode in auto and CP949 modes", "[.encoding-dwg]") {
    const QString path = qEnvironmentVariable("LIBRECAD_DIMENSION_DWG");
    REQUIRE_FALSE(path.isEmpty());
    RS_Settings::init("LibreCAD", "LibreCAD-encoding-tests");
    EncodingSetting restore;
    QStringList automatic;
    for (const QString encoding : {QString("auto"), QString("CP949")}) {
        LC_SET_ONE("Defaults", "LegacyTextEncoding", encoding);
        RS_Graphic graphic;
        RS_FilterDXFRW filter;
        REQUIRE(filter.fileImport(graphic, path, RS2::FormatDWG));
        QStringList texts;
        for (auto* entity : graphic) {
            if (auto* text = dynamic_cast<RS_MText*>(entity)) {
                texts.append(text->getText());
            } else if (auto* text = dynamic_cast<RS_Text*>(entity)) {
                texts.append(text->getText());
            }
        }
        const QString joined = texts.join("").remove(' ');
        CHECK(joined.contains(QString::fromUtf8(u8"게이트도피불가")));
        CHECK(joined.contains(QString::fromUtf8(u8"치수변경검토")));
        CHECK_FALSE(joined.contains(QChar(0xfffd)));
        if (encoding == "auto") automatic = texts;
        else CHECK(texts == automatic);
    }
    // A deliberately wrong override must actually reach the DWG reader.
    LC_SET_ONE("Defaults", "LegacyTextEncoding", QString("UTF-8"));
    RS_Graphic wrongEncoding;
    RS_FilterDXFRW filter;
    REQUIRE(filter.fileImport(wrongEncoding, path, RS2::FormatDWG));
    QString joined;
    for (auto* entity : wrongEncoding) {
        if (auto* text = dynamic_cast<RS_Text*>(entity)) joined += text->getText();
    }
    CHECK_FALSE(joined.remove(' ').contains(QString::fromUtf8(u8"게이트도피불가")));
}
