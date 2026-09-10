#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <QFile>
#include <QTemporaryDir>

#include "rs_block.h"
#include "rs_dimension.h"
#include "rs_filterdxfrw.h"
#include "rs_font.h"
#include "rs_fontlist.h"
#include "rs_graphic.h"
#include "rs_line.h"
#include "rs_mtext.h"
#include "rs_settings.h"

namespace {
// Exercise the same state as an installation with no external font files.
struct NoInstalledFonts {
    NoInstalledFonts() {
        if (!RS_SETTINGS) RS_Settings::init("LibreCAD", "LibreCAD-font-fallback-tests");
        RS_FONTLIST->clearFonts();
    }
    ~NoInstalledFonts() { RS_FONTLIST->init(); }
};
}

TEST_CASE("Missing CAD fonts retain dimension glyphs", "[fonts][fallback]") {
    NoInstalledFonts fonts;
    for (const QString name : {QString("missing-font"), QString("standard"),
                               QString("STANDARD"), QString()}) {
        CAPTURE(name.toStdString());
        auto* font = RS_FONTLIST->requestFont(name);
        REQUIRE(font != nullptr);
        CHECK(font->getFileName() == ":/fonts/standard.lff");
        for (const QChar ch : QString::fromUtf8("0123456789.R\u2300\u00b0\u00b1")) {
            auto* glyph = font->findLetter(QString(ch));
            REQUIRE(glyph != nullptr);
            CHECK(glyph != font->findLetter(QString(QChar(0xfffd))));
            CHECK(glyph->countDeep() > 0);
        }
    }
}

TEST_CASE("Missing font still generates dimension text geometry", "[fonts][fallback]") {
    NoInstalledFonts fonts;
    RS_MTextData data;
    data.insertionPoint = RS_Vector(0., 0.);
    data.height = 2.5;
    data.text = "12.5";
    data.style = "missing-font";
    RS_MText text(nullptr, data);
    text.update();
    CHECK(text.countDeep() > 0);
    CHECK(std::isfinite(text.getUsedTextWidth()));
    CHECK(text.getUsedTextWidth() > 0.);
    CHECK(text.getUsedTextHeight() > 0.);
    CHECK(text.getText() == "12.5");
}

TEST_CASE("Installed font takes priority and load failure falls back", "[fonts][fallback]") {
    RS_Settings::init("LibreCAD", "LibreCAD-font-fallback-tests");
    NoInstalledFonts fonts;
    REQUIRE(RS_FONTLIST->requestFont("standard") != nullptr);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath("fallback-test.lff");
    REQUIRE(QFile::copy(":/fonts/standard.lff", path));
    struct FontPathGuard {
        QString previous = LC_GET_ONE_STR("Paths", "Fonts", "");
        ~FontPathGuard() { LC_SET_ONE("Paths", "Fonts", previous); }
    } paths;
    LC_SET_ONE("Paths", "Fonts", directory.path());
    RS_FONTLIST->init();
    auto* installed = RS_FONTLIST->requestFont("fallback-test");
    REQUIRE(installed != nullptr);
    CHECK(installed->getFileName() == "fallback-test");
    REQUIRE(installed->findLetter("1") != nullptr);

    // Discover the file again, then remove it before lazy loading.
    RS_FONTLIST->clearFonts();
    RS_FONTLIST->init();
    REQUIRE(QFile::remove(path));
    auto* fallback = RS_FONTLIST->requestFont("fallback-test");
    REQUIRE(fallback != nullptr);
    CHECK(fallback->getFileName() != "fallback-test");
    REQUIRE(fallback->findLetter("1") != nullptr);
    CHECK(fallback->findLetter("1")->countDeep() > 0);
}

TEST_CASE("Blank MTEXT does not extend drawing bounds to the origin", "[fonts][fallback]") {
    NoInstalledFonts fonts;
    RS_Graphic graphic;
    graphic.addEntity(new RS_Line(&graphic, RS_LineData({15900., -37400.}, {16000., -37300.})));
    for (const QString value : {QString(), QString("   "), QString("\t\n")}) {
        RS_MTextData data;
        data.insertionPoint = RS_Vector(15950., -37350.);
        data.height = 3.;
        data.text = value;
        data.style = "missing-font";
        auto* text = new RS_MText(&graphic, data);
        text->update();
        graphic.addEntity(text);
        CHECK(text->count() == 0);
        CHECK(text->getText() == value);
    }
    graphic.calculateBorders();
    CHECK(graphic.getMin().x == 15900.);
    CHECK(graphic.getMax().y == -37300.);
}

// The user's drawing remains private and is supplied explicitly for integration
// testing: LIBRECAD_DIMENSION_DWG=/path/to/file.dwg librecad_tests '[.font-dwg]'
TEST_CASE("Wheel DWG dimensions render without installed fonts", "[.font-dwg]") {
    const QString path = qEnvironmentVariable("LIBRECAD_DIMENSION_DWG");
    REQUIRE_FALSE(path.isEmpty());
    RS_Settings::init("LibreCAD", "LibreCAD-font-fallback-tests");
    NoInstalledFonts fonts;
    RS_Graphic graphic;
    RS_FilterDXFRW filter;
    REQUIRE(filter.fileImport(graphic, path, RS2::FormatDWG));
    int dimensions = 0;
    int labels = 0;
    for (auto* entity : graphic) {
        auto* dimension = dynamic_cast<RS_Dimension*>(entity);
        if (!dimension) continue;
        ++dimensions;
        std::function<void(RS_EntityContainer*)> checkText = [&](auto* container) {
            for (auto* child : *container) {
                if (auto* text = dynamic_cast<RS_MText*>(child)) {
                    if (text->getText().trimmed().isEmpty()) continue;
                    ++labels;
                    CHECK(text->countDeep() > 0);
                    CHECK(std::isfinite(text->getUsedTextWidth()));
                    CHECK(text->getUsedTextWidth() > 0.);
                } else if (auto* nested = dynamic_cast<RS_EntityContainer*>(child)) {
                    checkText(nested);
                }
            }
        };
        checkText(dimension);
    }
    CAPTURE(dimensions, labels);
    CHECK(dimensions == 22);
    CHECK(labels == dimensions);
    graphic.calculateBorders();
    CHECK(graphic.getSize().x > 100.);
    CHECK(graphic.getSize().x < 1000.);
    CHECK(graphic.getSize().y > 200.);
    CHECK(graphic.getSize().y < 1000.);
}
