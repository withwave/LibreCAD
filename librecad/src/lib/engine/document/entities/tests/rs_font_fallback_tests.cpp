#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <functional>
#include <QFile>
#include <QTemporaryDir>
#include <QImage>

#include "rs_block.h"
#include "rs_dimension.h"
#include "rs_dimdiametric.h"
#include "rs_dimradial.h"
#include "rs_filterdxfrw.h"
#include "rs_font.h"
#include "rs_fontlist.h"
#include "rs_graphic.h"
#include "rs_line.h"
#include "rs_mtext.h"
#include "rs_settings.h"
#include "rs_text.h"
#include "rs_ellipse.h"
#include "rs_polyline.h"
#include "rs_painter.h"
#include "lc_graphicviewport.h"

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

TEST_CASE("Width-scaled font ellipse strokes render like standalone arcs", "[fonts][render]") {
    LC_GraphicViewport viewport;
    viewport.setSize(200, 200);
    viewport.setOffsetAndFactor(100, 100, 10.);
    for (bool reversed : {false, true}) {
        for (double rotation : {0., .6}) {
            RS_Ellipse arc(nullptr, {{0., 0.}, {5., 0.}, .6, .2, 2.5, reversed});
            arc.rotate({}, rotation);
            RS_Polyline polyline(nullptr, RS_PolylineData());
            polyline.RS_EntityContainer::addEntity(arc.clone());
            polyline.updateEndpoints();
            auto render = [&](bool asPolyline) {
                QImage result(200, 200, QImage::Format_ARGB32_Premultiplied);
                result.fill(Qt::white);
                RS_Painter painter(&result);
                painter.setViewPort(&viewport);
                painter.setPen(0, 0, 0);
                if (asPolyline) polyline.draw(&painter);
                else arc.draw(&painter);
                painter.end();
                return result;
            };
            const QImage expected = render(false);
            QImage blank(expected.size(), expected.format());
            blank.fill(Qt::white);
            REQUIRE(expected != blank);
            CHECK(render(true) == expected);
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

TEST_CASE("Missing TXT uses Simplex while an installed TXT retains priority", "[fonts][fallback]") {
    NoInstalledFonts fonts;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    struct Paths {
        QString previous = LC_GET_ONE_STR("Paths", "Fonts", "");
        ~Paths() { LC_SET_ONE("Paths", "Fonts", previous); }
    } paths;
    LC_SET_ONE("Paths", "Fonts", directory.path());
    REQUIRE(QFile::copy(":/fonts/standard.lff", directory.filePath("simplex.lff")));
    RS_FONTLIST->init();
    REQUIRE(RS_FONTLIST->requestFont("TXT") != nullptr);
    CHECK(RS_FONTLIST->requestFont("TXT")->getFileName() == "simplex");
    REQUIRE(QFile::copy(":/fonts/standard.lff", directory.filePath("txt.lff")));
    RS_FONTLIST->clearFonts();
    RS_FONTLIST->init();
    REQUIRE(RS_FONTLIST->requestFont("txt") != nullptr);
    CHECK(RS_FONTLIST->requestFont("txt")->getFileName() == "txt");
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
    graphic.onLoadingCompleted();
    int dimensions = 0;
    int labels = 0;
    int titles = 0;
    for (auto* entity : graphic) {
        if (auto* text = dynamic_cast<RS_Text*>(entity);
            text && text->getText() == "Rear_Wheel_10_12 Hole") {
            ++titles;
            // DWG evaluated left point: 15874.3; alignment center: 15922.2.
            // Preserve the roughly 96-unit footprint even without txt.shx.
            CHECK(text->getUsedTextWidth() > 95.);
            CHECK(text->getUsedTextWidth() < 97.);
            CHECK(text->getMin().x > 15874.);
            CHECK(text->getMin().x < 15875.);
            CHECK(text->getWidthRel() == Catch::Approx(1.));
            CHECK(text->getHeight() == Catch::Approx(5.));
        }
        auto* dimension = dynamic_cast<RS_Dimension*>(entity);
        if (!dimension) continue;
        ++dimensions;
        std::function<void(RS_EntityContainer*)> checkText = [&](auto* container) {
            for (auto* child : *container) {
                if (auto* text = dynamic_cast<RS_MText*>(child)) {
                    if (text->getText().trimmed().isEmpty()) continue;
                    ++labels;
                    CHECK(text->getHeight() == Catch::Approx(1.5));
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
    CHECK(titles == 3);
    graphic.calculateBorders();
    CHECK(graphic.getSize().x > 100.);
    CHECK(graphic.getSize().x < 1000.);
    CHECK(graphic.getSize().y > 200.);
    CHECK(graphic.getSize().y < 1000.);
}

TEST_CASE("Imported fallback text footprint preserves source parameters and resets on editing",
          "[fonts][fallback]") {
    NoInstalledFonts fonts;
    RS_TextData data({100., 200.}, {}, 5., 1., RS_TextData::VAMiddle,
                     RS_TextData::HACenter, RS_TextData::None, "ABC", "missing",
                     .4, RS2::Update);
    RS_Text text(nullptr, data);
    const double originalWidth = text.getUsedTextWidth();
    text.fitImportedDisplayWidth(originalWidth * 1.5);
    CHECK(text.getUsedTextWidth() == Catch::Approx(originalWidth * 1.5));
    CHECK(text.getWidthRel() == Catch::Approx(1.));
    CHECK(text.getHeight() == Catch::Approx(5.));
    CHECK(text.getAngle() == Catch::Approx(.4));
    CHECK(text.getInsertionPoint().x == Catch::Approx(100.));
    CHECK(text.getInsertionPoint().y == Catch::Approx(200.));
    text.update();
    CHECK(text.getUsedTextWidth() == Catch::Approx(originalWidth));
}

TEST_CASE("Document text styles resolve their font without losing style names", "[fonts][fallback]") {
    NoInstalledFonts fonts;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(QFile::copy(":/fonts/standard.lff", directory.filePath("drawing-font.lff")));
    struct Paths {
        QString previous = LC_GET_ONE_STR("Paths", "Fonts", "");
        ~Paths() { LC_SET_ONE("Paths", "Fonts", previous); }
    } paths;
    LC_SET_ONE("Paths", "Fonts", directory.path());
    RS_FONTLIST->init();
    RS_Graphic graphic;
    DRW_Textstyle style;
    style.name = "DS";
    style.font = "C:\\CAD\\DRAWING-FONT.shx";
    graphic.dwgAdvancedMetadata().addTextStyleName(style);
    auto* resolved = RS_FONTLIST->requestFontForStyle("ds", &graphic);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->getFileName() == "drawing-font");
    CHECK(RS_FONTLIST->requestFontForStyle("DS", nullptr)->getFileName() == ":/fonts/standard.lff");
    RS_TextData data({}, {}, 5., 1., RS_TextData::VABaseline, RS_TextData::HALeft,
                     RS_TextData::None, "ABC", "DS", 0., RS2::Update);
    RS_Text text(&graphic, data);
    CHECK(text.getStyle() == "DS");
    CHECK(text.getUsedTextWidth() > 0.);
}

TEST_CASE("Automatic dimension symbols preserve explicit labels", "[fonts][dimension]") {
    RS_DimensionData data;
    data.definitionPoint = RS_Vector(0., 0.);
    struct DiameterLabel : RS_DimDiametric {
        using RS_DimDiametric::RS_DimDiametric;
        QString measurement = "2.2";
        QString getMeasuredLabel() override { return measurement; }
    };
    struct RadiusLabel : RS_DimRadial {
        using RS_DimRadial::RS_DimRadial;
        QString getMeasuredLabel() override { return "2.2"; }
    };
    DiameterLabel diameter(nullptr, data, RS_DimDiametricData(RS_Vector(2.2, 0.), 0.));
    RadiusLabel radius(nullptr, data, RS_DimRadialData(RS_Vector(2.2, 0.), 0.));
    CHECK(diameter.getLabel() == QString::fromUtf8("⌀2.2"));
    CHECK(radius.getLabel() == "R2.2");
    diameter.setLabel("custom <>");
    CHECK(diameter.getLabel() == "custom 2.2");
    diameter.measurement = "2,2";
    CHECK(diameter.getLabel() == "custom 2,2");
    diameter.setLabel(" ");
    CHECK(diameter.getLabel().isEmpty());
}

TEST_CASE("MTEXT height controls scale glyphs without printing control text", "[fonts][mtext]") {
    NoInstalledFonts fonts;
    RS_MTextData data;
    data.insertionPoint = RS_Vector(0., 0.);
    data.height = 2.;
    data.style = "standard";
    data.text = "88";
    RS_MText normal(nullptr, data);
    normal.update();
    data.text = "{\\H0.5x;88}";
    RS_MText relative(nullptr, data);
    relative.update();
    data.text = "{\\H1;88}";
    RS_MText absolute(nullptr, data);
    absolute.update();
    CHECK(relative.getUsedTextWidth() == Catch::Approx(normal.getUsedTextWidth() * 0.5));
    CHECK(absolute.getUsedTextWidth() == Catch::Approx(relative.getUsedTextWidth()));
    CHECK(relative.countDeep() == normal.countDeep());
    CHECK(RS_FilterDXFRW::toNativeString(data.text, true) == data.text);
    CHECK(RS_FilterDXFRW::toNativeString(data.text) == "88");
}

TEST_CASE("Imported dimension display blocks retain producer geometry until edited", "[fonts][dimension][dxf]") {
    NoInstalledFonts fonts;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath("cached-dimension.dxf");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    const QByteArray dxf =
        "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n5\n20\n8\n0\n2\n*D1\n70\n1\n10\n0\n20\n0\n"
        "0\nLINE\n5\n21\n8\n0\n10\n10\n20\n20\n11\n30\n21\n40\n"
        "0\nENDBLK\n5\n22\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nDIMENSION\n5\n30\n8\n0\n2\n*D1\n70\n1\n"
        "10\n0\n20\n10\n11\n5\n21\n10\n13\n0\n23\n0\n14\n10\n24\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    REQUIRE(file.write(dxf) == dxf.size());
    file.close();
    RS_Graphic graphic;
    RS_FilterDXFRW filter;
    REQUIRE(filter.fileImport(graphic, path, RS2::FormatDXFRW));
    RS_Dimension* dimension = nullptr;
    for (auto* entity : graphic) {
        if (auto* candidate = dynamic_cast<RS_Dimension*>(entity)) dimension = candidate;
    }
    REQUIRE(dimension != nullptr);
    REQUIRE(dimension->count() == 1);
    graphic.onLoadingCompleted();
    REQUIRE(dimension->count() == 1);
    CHECK(dimension->getMin().x == Catch::Approx(10.));
    CHECK(dimension->getMax().y == Catch::Approx(40.));
    dimension->setLabel("edited");
    dimension->update();
    CHECK(dimension->count() > 1);
    CHECK_FALSE(dimension->hasImportedDisplay());
}
