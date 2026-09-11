#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "lc_printing.h"
#include "drw_objects.h"
#include <QApplication>
#include <QPrinter>
#include "rs_graphic.h"
#include "rs_line.h"
#include "rs_settings.h"
#include "rs_units.h"

TEST_CASE("Printer minima preserve asymmetric drawing margins in millimetres", "[printing]") {
    QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, {}, QPageLayout::Millimeter);
    layout.setMinimumMargins({3.4, 5., 6., 7.});
    CHECK(LC_Printing::printableMargins(layout, {1., 12., 8., 2.}) == QMarginsF(3.4, 12., 8., 7.));
    layout.setUnits(QPageLayout::Inch);
    layout.setMinimumMargins({0.25, 0.5, 0.75, 1.});
    const auto margins = LC_Printing::printableMargins(layout, {1., 12., 8., 2.});
    CHECK(margins.left() == Catch::Approx(6.35).margin(0.01));
    CHECK(margins.top() == Catch::Approx(12.7));
    CHECK(margins.right() == Catch::Approx(19.05));
    CHECK(margins.bottom() == Catch::Approx(25.4).margin(0.01));
    layout.setMode(QPageLayout::FullPageMode);
    CHECK(LC_Printing::printableMargins(layout, {}) .top() == Catch::Approx(12.7).margin(0.01));
}

TEST_CASE("Borderless layouts retain the default 2.5 mm safety margin", "[printing]") {
    QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, {}, QPageLayout::Millimeter);
    CHECK(LC_Printing::printableMargins(layout, {}) == QMarginsF(2.5, 2.5, 2.5, 2.5));
    CHECK(LC_Printing::printableMargins(layout, {2., 3., 4., 5.}) == QMarginsF(2.5, 3., 4., 5.));
}

TEST_CASE("Fit and center respect printable bounds for both orientations and units", "[printing]") {
    if (!RS_SETTINGS) RS_Settings::init("LibreCAD", "LibreCAD-print-tests");
    for (auto unit : {RS2::Millimeter, RS2::Inch}) {
        for (bool landscape : {false, true}) {
            RS_Graphic graphic;
            graphic.setUnit(unit);
            graphic.getPlotSettings()->setPaperFormat(RS2::A4, landscape);
            graphic.setActiveLayoutMargins(3.4, 5., 8., 12.);
            graphic.addEntity(new RS_Line(&graphic, RS_LineData({-100., -200.}, {400., 700.})));
            graphic.calculateBorders();
            graphic.getPlotSettings()->setPaperScaleFixed(false);
            REQUIRE(graphic.fitToPage());
            auto lower = RS_Units::convert(graphic.getMin()*graphic.getPlotSettings()->getPaperScale()+graphic.getPaperInsertionBase(), unit, RS2::Millimeter);
            auto upper = RS_Units::convert(graphic.getMax()*graphic.getPlotSettings()->getPaperScale()+graphic.getPaperInsertionBase(), unit, RS2::Millimeter);
            auto paper = RS_Units::convert(graphic.getPlotSettings()->getPaperSize(), unit, RS2::Millimeter);
            CHECK(lower.x >= 3.4-1e-6);
            CHECK(lower.y >= 12.-1e-6);
            CHECK(upper.x <= paper.x-8.+1e-6);
            CHECK(upper.y <= paper.y-5.+1e-6);
            graphic.getPlotSettings()->setPaperScaleFixed(true);
            const double scale = graphic.getPlotSettings()->getPaperScale();
            graphic.setActiveLayoutMargins(10., 15., 10., 15.);
            graphic.centerToPage();
            CHECK(graphic.getPlotSettings()->getPaperScale() == scale);
            graphic.getPlotSettings()->setPagesNum(2, 3);
            auto single = graphic.getPlotSettings()->getPrintAreaSize(false);
            auto tiled = graphic.getPlotSettings()->getPrintAreaSize();
            CHECK(tiled.x == Catch::Approx(single.x*2));
            CHECK(tiled.y == Catch::Approx(single.y*3));
        }
    }
}

TEST_CASE("Default safety margins keep fitted geometry inside every paper edge", "[printing]") {
    if (!RS_SETTINGS) RS_Settings::init("LibreCAD", "LibreCAD-print-tests");
    for (bool landscape : {false, true}) {
        RS_Graphic graphic;
        graphic.setUnit(RS2::Millimeter);
        graphic.getPlotSettings()->setPaperFormat(RS2::A4, landscape);
        QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, {}, QPageLayout::Millimeter);
        const auto margins = LC_Printing::printableMargins(layout, {});
        graphic.setActiveLayoutMargins(margins.left(), margins.top(), margins.right(), margins.bottom());
        graphic.addEntity(new RS_Line(&graphic, RS_LineData({-100., -200.}, {400., 700.})));
        graphic.calculateBorders();
        graphic.getPlotSettings()->setPaperScaleFixed(false);
        REQUIRE(graphic.fitToPage());
        const auto lower = graphic.getMin()*graphic.getPlotSettings()->getPaperScale()+graphic.getPaperInsertionBase();
        const auto upper = graphic.getMax()*graphic.getPlotSettings()->getPaperScale()+graphic.getPaperInsertionBase();
        const auto paper = graphic.getPlotSettings()->getPaperSize();
        CHECK(lower.x >= 2.5-1e-6);
        CHECK(lower.y >= 2.5-1e-6);
        CHECK(upper.x <= paper.x-2.5+1e-6);
        CHECK(upper.y <= paper.y-2.5+1e-6);
    }
}

TEST_CASE("Imported layout margins reach both page layout and fit calculations", "[printing]") {
    static int argc = 1;
    static char name[] = "print-layout-tests";
    static char* argv[] = {name, nullptr};
    if (!QCoreApplication::instance()) new QApplication(argc, argv);
    if (!RS_SETTINGS) RS_Settings::init("LibreCAD", "LibreCAD-print-tests");
    RS_Graphic graphic;
    graphic.setUnit(RS2::Millimeter);
    graphic.getPlotSettings()->setPaperFormat(RS2::A4, false);
    DRW_Layout layout;
    layout.handle = 0x20;
    layout.name = "Layout1";
    layout.marginLeft = 5.;
    layout.marginTop = 6.;
    layout.marginRight = 7.;
    layout.marginBottom = 8.;
    graphic.dwgAdvancedMetadata().addLayout(layout);
    graphic.setActiveLayoutHandle(layout.handle);
    REQUIRE(LC_Printing::applyPrinterMargins(graphic));
    const auto margins = graphic.activeLayoutMargins();
    auto* plot = graphic.getPlotSettings();
    CHECK(plot->getMarginLeftMm() == margins[0]);
    CHECK(plot->getMarginTopMm() == margins[1]);
    CHECK(plot->getMarginRightMm() == margins[2]);
    CHECK(plot->getMarginBottomMm() == margins[3]);
    graphic.addEntity(new RS_Line(&graphic, RS_LineData({-10., -20.}, {90., 180.})));
    graphic.calculateBorders();
    REQUIRE(graphic.fitToPage());
    const auto lower = graphic.getMin()*plot->getPaperScale()+graphic.getPaperInsertionBase();
    CHECK(lower.x >= margins[0] - 1e-6);
    CHECK(lower.y >= margins[3] - 1e-6);
    QPrinter printer(QPrinter::HighResolution);
    const QMarginsF requested(margins[0], margins[1], margins[2], margins[3]);
    LC_Printing::setupPageLayout(printer, false, QPageSize::A4, RS_Vector(210., 297.),
                                 RS2::Millimeter, requested);
    CHECK(printer.pageLayout().margins(QPageLayout::Millimeter) == requested);
}
