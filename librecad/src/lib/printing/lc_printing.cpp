/****************************************************************************
**
** This file is part of the LibreCAD project, a 2D CAD program
**
** Copyright (C) 2024 LibreCAD.org
** Copyright (C) 2024 Dongxu Li (dongxuli2011@gmail.com)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**********************************************************************/

#include "lc_printing.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QInputDialog>
#include <QRegularExpression>
#include <algorithm>

#include "lc_graphicviewport.h"
#include "lc_printpreviewview.h"
#include "lc_printviewportrenderer.h"
#include "qc_mdiwindow.h"
#include "qg_graphicview.h"
#include "rs_debug.h"
#include "rs_painter.h"
#include "rs_settings.h"
#include "rs_units.h"

// fixme - sand - files - this class should not be in /lib, it has outer dependencies. Review it!!!!
namespace {
    // supported paper formats should be added here
    const std::map<RS2::PaperFormat, QPageSize::PageSizeId> PAPER_TO_PAGE = {
        {
            {RS2::A0, QPageSize::A0},
            {RS2::A1, QPageSize::A1},
            {RS2::A2, QPageSize::A2},
            {RS2::A3, QPageSize::A3},
            {RS2::A4, QPageSize::A4},

            /* Removed ISO "B" and "C" series,  C5E,  Comm10E,  DLE,  (envelope sizes) */

            {RS2::Letter, QPageSize::Letter},
            {RS2::Legal, QPageSize::Legal},
            {RS2::Tabloid, QPageSize::Tabloid},

            //case RS2::Ansi_A, QPageSize::AnsiA},
            //case RS2::Ansi_B, QPageSize::AnsiB},
            {RS2::Ansi_C, QPageSize::AnsiC},
            {RS2::Ansi_D, QPageSize::AnsiD},
            {RS2::Ansi_E, QPageSize::AnsiE},
            {RS2::Arch_A, QPageSize::ArchA},
            {RS2::Arch_B, QPageSize::ArchB},
            {RS2::Arch_C, QPageSize::ArchC},
            {RS2::Arch_D, QPageSize::ArchD},
            {RS2::Arch_E, QPageSize::ArchE},
        }
    };

    /**
     * @brief setFileNameColor - get the printing output file name and black/white or color mode
     * @returns QString - the PDF file name to export to
     * @param printer - a QPrinter
     * @param graphic - the graphic to print
     */
    QString setFileNameColor(QPrinter& printer, const RS_Graphic& graphic) {
        QString pdfFileName = graphic.getFilename();
        if (pdfFileName.isEmpty()) {
            pdfFileName = graphic.getAutoSaveFileName();
        }
        static QRegularExpression reSuffix{R"(\.(dxf|dwg)$)", QRegularExpression::CaseInsensitiveOption};
        pdfFileName.replace(reSuffix, ".pdf");
        if (!pdfFileName.endsWith(".pdf", Qt::CaseInsensitive)) pdfFileName += ".pdf";

        // color or black/white mode
        LC_GROUP_GUARD("Print");
        printer.setColorMode(static_cast<QPrinter::ColorMode>(LC_GET_INT("ColorMode", QPrinter::Color)));
        return pdfFileName;
    }
}

QPageSize::PageSizeId LC_Printing::rsToQtPaperFormat(const RS2::PaperFormat paperFormat) {
    return (PAPER_TO_PAGE.count(paperFormat) == 1) ? PAPER_TO_PAGE.at(paperFormat) : QPageSize::Custom;
}

void LC_Printing::setupPageLayout(QPrinter& printer, bool landscape, QPageSize::PageSizeId paperSizeName,
                                  const RS_Vector& paperSize, RS2::Unit unit, const QMarginsF& paperMargins)
{
    QPageLayout layout;
    layout.setMode(QPageLayout::FullPageMode);
    layout.setUnits(QPageLayout::Millimeter);

    if (paperSizeName == QPageSize::Custom) {
        RS_Vector s = RS_Units::convert(paperSize, unit, RS2::Millimeter);
        if (landscape)
            s = s.flipXY();
        layout.setPageSize(QPageSize{QSizeF(s.x, s.y), QPageSize::Millimeter}, paperMargins);
    } else {
        layout.setPageSize(QPageSize{paperSizeName}, paperMargins);
    }
    layout.setOrientation(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    layout.setMargins(paperMargins);
    printer.setPageLayout(layout);
}

QMarginsF LC_Printing::printableMargins(QPageLayout layout, const QMarginsF& requested) {
    layout.setUnits(QPageLayout::Millimeter);
    const auto minimum = layout.minimumMargins();
    constexpr double defaultMarginMm = 2.5;
    return {std::max({defaultMarginMm, requested.left(), minimum.left()}),
            std::max({defaultMarginMm, requested.top(), minimum.top()}),
            std::max({defaultMarginMm, requested.right(), minimum.right()}),
            std::max({defaultMarginMm, requested.bottom(), minimum.bottom()})};
}

namespace {
void configurePaper(QPrinter& printer, RS_Graphic& graphic) {
    bool landscape = false;
    auto format = LC_Printing::rsToQtPaperFormat(graphic.getPlotSettings()->getPaperFormat(&landscape));
    auto size = RS_Units::convert(graphic.getPlotSettings()->getPaperSize(), graphic.getUnit(), RS2::Millimeter);
    if (landscape) size = size.flipXY();
    printer.setPageSize(format == QPageSize::Custom
        ? QPageSize(QSizeF(size.x, size.y), QPageSize::Millimeter) : QPageSize(format));
    printer.setPageOrientation(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
}
QString previewPrinterName() {
    auto name = LC_GET_ONE_STR("Print", "PrinterName", "");
    if (QPrinterInfo::printerInfo(name).isNull()) name = QPrinterInfo::defaultPrinterName();
    return name;
}
}

bool LC_Printing::applyPrinterMargins(RS_Graphic& graphic, QWidget* parent, bool choosePrinter) {
    QString name = previewPrinterName();
    if (choosePrinter) {
        const auto names = QPrinterInfo::availablePrinterNames();
        if (names.isEmpty()) {
            QMessageBox::information(parent, QObject::tr("Printer area"), QObject::tr("No printers are available. Drawing margins will be used for PDF export."));
            return false;
        }
        bool accepted = false;
        name = QInputDialog::getItem(parent, QObject::tr("Printer area"),
            QObject::tr("Printer (minimum margins are applied to the preview):"),
            names, std::max(0, int(names.indexOf(name))), false, &accepted);
        if (!accepted) return false;
    }
    auto info = QPrinterInfo::printerInfo(name);
    QPrinter printer(info, QPrinter::HighResolution);
    configurePaper(printer, graphic);
    auto* plot = graphic.getPlotSettings();
    const auto active = graphic.activeLayoutMargins();
    const auto margins = printableMargins(printer.pageLayout(),
        {std::max(active[0], plot->getMarginLeftMm()), std::max(active[1], plot->getMarginTopMm()),
         std::max(active[2], plot->getMarginRightMm()), std::max(active[3], plot->getMarginBottomMm())});
    const QSizeF actualPaper = printer.pageLayout().fullRect(QPageLayout::Millimeter).size();
    const RS_Vector size(actualPaper.width(), actualPaper.height());
    const RS_Vector paperInUnits = RS_Units::convert(size, RS2::Millimeter, graphic.getUnit());
    if (plot->getPaperSize().distanceTo(paperInUnits) > 0.01) plot->setPaperSize(paperInUnits);
    if (margins.left() + margins.right() >= size.x || margins.top() + margins.bottom() >= size.y) return false;
    graphic.setActiveLayoutMargins(margins.left(), margins.top(), margins.right(), margins.bottom());
    // The current fit/center renderer reads the document plot settings, while
    // imported DWGs also have independent layout records. Keep both in sync.
    plot->setMarginsInMm(margins.left(), margins.top(), margins.right(), margins.bottom());
    if (choosePrinter) LC_SET_ONE("Print", "PrinterName", name);
    return true;
}

void LC_Printing::print(QC_MDIWindow &mdiWindow, PrinterType printerType) {
    RS_Graphic* graphic = mdiWindow.getDocument()->getGraphic();

    if (graphic == nullptr) {
        RS_DEBUG->print(RS_Debug::D_WARNING, "QC_ApplicationWindow::slotFilePrint: " "no graphic");
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    // fullPage must be set to true to get full width and height
    // (without counting margins).
    printer.setFullPage(true);
    if (printerType == PrinterType::Printer) printer.setPrinterName(previewPrinterName());

    bool landscape = false;
    LC_PlotSettings* ps = graphic->getPlotSettings();
    RS2::PaperFormat paperFormat = ps->getPaperFormat(&landscape);
    QPageSize::PageSizeId paperSizeName = rsToQtPaperFormat(paperFormat);
    RS_Vector paperSize = ps->getPaperSize();
    RS2::Unit unit = graphic->getUnit();
    const auto printMargins = graphic->activeLayoutMargins();
    QMarginsF paperMargins{printMargins[0],   // left
                           printMargins[1],   // top
                           printMargins[2],   // right
                           printMargins[3]};  // bottom

    // Issue #2337: use QPageLayout to set page size, orientation, and margins
    // together. On Linux/CUPS, setting orientation via setPageOrientation()
    // after setPageSize() with a standard size ID may not propagate correctly,
    // resulting in portrait-only output regardless of the landscape setting.
    LC_Printing::setupPageLayout(printer, landscape, paperSizeName, paperSize, unit, paperMargins);

    // Issue #2130: populate the output file name for
    QString defaultFile = setFileNameColor(printer, *graphic);

    // printer setup:
    bool bStartPrinting = false;
    if (printerType == PrinterType::PDF) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setColorMode(QPrinter::Color);
        printer.setResolution(1200);
        // Issue #1897, exporting PDF margins to to follow the drawing settings
        QPageLayout pdfLayout = printer.pageLayout();
        pdfLayout.setMinimumMargins({});
        pdfLayout.setMargins(paperMargins);
        printer.setPageLayout(pdfLayout);
        QString pdfFie = QFileDialog::getSaveFileName(
            &mdiWindow,
            QObject::tr("Export to PDF"),
            defaultFile,
            QObject::tr("PDF files (*.pdf);;All files (*.*)"));
        if (pdfFie.isEmpty()) return;
        if (!pdfFie.endsWith(".pdf", Qt::CaseInsensitive)) pdfFie += ".pdf";
        printer.setOutputFileName(pdfFie);
        bStartPrinting = true;
    }
    else {
        printer.setOutputFileName(""); // uncheck 'Print to file' checkbox
        printer.setOutputFormat(QPrinter::NativeFormat);

        QPrintDialog printDialog(&printer, &mdiWindow);
        printDialog.setOption(QAbstractPrintDialog::PrintToFile);
        printDialog.setOption(QAbstractPrintDialog::PrintShowPageSize);
        bStartPrinting = (QDialog::Accepted == printDialog.exec());

        if (!bStartPrinting) return;
        // Keep physical-paper coordinates; constrain drawing to the driver area ourselves.
        printer.setFullPage(true);
        if (printer.outputFormat() == QPrinter::NativeFormat) {
            printer.setPageMargins(printableMargins(printer.pageLayout(),
                printer.pageLayout().margins(QPageLayout::Millimeter)), QPageLayout::Millimeter);
            LC_SET_ONE("Print", "PrinterName", printer.printerName());
        }

        auto equalPaperSize = [&printer](const RS_Vector &v0, const RS_Vector &v1) {
            // from DPI to pixel/mm
            auto resolution = RS_Units::convert(1., RS2::Millimeter, RS2::Inch) * printer.resolution();
            // ignore difference within two pixels
            return v0.distanceTo(v1) * resolution <= 2.;
        };
        auto equalMargins = [&printer](const QMarginsF &drawingMargins) {
            QMarginsF printerMarginsPixels = printer.pageLayout().marginsPixels(printer.resolution());
            // from DPI to pixel/mm
            auto resolution = RS_Units::convert(1., RS2::Millimeter, RS2::Inch) * printer.resolution();
            // assuming drawingMargins in mm
            QMarginsF drawingMarginsPixels = drawingMargins * resolution;
            QMarginsF diff = printerMarginsPixels - drawingMarginsPixels;
            // ignore difference within two pixels
            return std::max({std::abs(diff.left()), std::abs(diff.right()), std::abs(diff.top()), std::abs(diff.bottom())}) <= 2.;
        };

        RS_Vector paperSizeMm = RS_Units::convert(paperSize, unit, RS2::Millimeter);

        QRectF paperRect = printer.paperRect(QPrinter::Millimeter);
        RS_Vector printerSizeMm{paperRect.width(), paperRect.height()};
        if (bStartPrinting && (!equalPaperSize(printerSizeMm, paperSizeMm) || !equalMargins(paperMargins))) {
            QMarginsF printerMargins = printer.pageLayout().margins(QPageLayout::Millimeter);
            QMessageBox msgBox(&mdiWindow);
            // FIXME - SAND - localization
            msgBox.setWindowTitle(QObject::tr("Paper settings"));
            msgBox.setText(QObject::tr("The printer paper size or printable area differs from the drawing."));
            msgBox.setInformativeText(QObject::tr("Apply the printer settings and return to preview? Check the scale and position before printing again."));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            qreal printMarginsLeft = printerMargins.left();
            qreal printMarginsTop = printerMargins.top();
            qreal printMarginsRight = printerMargins.right();
            qreal printMarginsBottom = printerMargins.bottom();
            QString detailedText =
                QString(
                    "Drawing settings:\n" "\tsize: %1 x %2 (%3)\n" "\tmargins: %4, %5, %6, %7\n" "\n" "Printer settings:\n"
                    "\tsize: %8 x %9 (%10)\n" "\tmargins: %11, %12, %13, %14\n").arg(paperSize.x).arg(paperSize.y).
                                                                                 arg(RS_Units::paperFormatToString(paperFormat)).
                                                                                 arg(RS_Units::convert(
                                                                                     paperMargins.left(), RS2::Millimeter,
                                                                                     unit)).
                                                                                 arg(RS_Units::convert(
                                                                                     paperMargins.top(), RS2::Millimeter,
                                                                                     unit)).
                                                                                 arg(RS_Units::convert(
                                                                                     paperMargins.right(), RS2::Millimeter,
                                                                                     unit)).arg(RS_Units::convert(
                                                                                     paperMargins.bottom(), RS2::Millimeter,
                                                                                     unit)).arg(RS_Units::convert(
                                                                                     printerSizeMm.x, RS2::Millimeter,
                                                                                     unit)).arg(RS_Units::convert(
                                                                                     printerSizeMm.y, RS2::Millimeter,
                                                                                     unit)).arg(printer.pageLayout().pageSize().name()).
                                                                                 arg(RS_Units::convert(
                                                                                     printMarginsLeft, RS2::Millimeter,
                                                                                     unit)).arg(RS_Units::convert(
                                                                                     printMarginsTop, RS2::Millimeter, unit)).arg(
                                                                                     RS_Units::convert(
                                                                                         printMarginsRight, RS2::Millimeter, unit)).arg(
                                                                                     RS_Units::convert(
                                                                                         printMarginsBottom, RS2::Millimeter, unit));
            msgBox.setDetailedText(detailedText);
            int answer = msgBox.exec();
            switch (answer) {
                case QMessageBox::Yes:
                    ps->setPaperSize(RS_Units::convert(printerSizeMm, RS2::Millimeter, unit));
                    graphic->setActiveLayoutMargins(printMarginsLeft, printMarginsTop, printMarginsRight, printMarginsBottom);
                    ps->setMarginsInMm(printMarginsLeft, printMarginsTop, printMarginsRight, printMarginsBottom);
                    if (!ps->isPaperScaleFixed()) graphic->fitToPage();
                    else graphic->centerToPage();
                    mdiWindow.getGraphicView()->redraw();
                    bStartPrinting = false;
                    break;
                case QMessageBox::Cancel:
                    bStartPrinting = false;
                    break;
                default:
                    break;
            }
        }
    }

    if (bStartPrinting) {
        RS_DEBUG->print(RS_Debug::D_INFORMATIONAL, "QC_ApplicationWindow::slotFilePrint: resolution is %d", printer.resolution());
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        RS_Painter painter(&printer);
        // RAII style to restore cursor. Not really a shared pointer for ownership
        std::shared_ptr<RS_Painter> painterPtr{
            &painter,
            []([[maybe_unused]] RS_Painter* p) {
                QApplication::restoreOverrideCursor();
            }
        };

        // fixme - sand rework this later - it seems that printing in general should be refined.
        QG_GraphicView* graphicView = mdiWindow.getGraphicView();
        RS2::DrawingMode drawingMode = RS2::DrawingMode::ModeAuto;
        if (graphicView->isPrintPreview()) {
            auto printPreview = dynamic_cast<LC_PrintPreviewView*>(graphicView);
            if (printPreview != nullptr) {
                drawingMode = printPreview->getDrawingMode();
            }
        }
        painter.setDrawingMode(drawingMode);

        QMarginsF margins = printer.pageLayout().margins(QPageLayout::Millimeter);
        //        LC_ERR << "Printer margins (mm): " << margins.left()<<": "<<margins.top()<<" : "<<margins.right()<<" : "<<margins.bottom();

        double printerWidth = printer.width();
        double printerHeight = printer.height();

        double printerFx = printer.logicalDpiX() / 25.4;
        double printerFy = printer.logicalDpiY() / 25.4;

        painter.setClipRect(margins.left() * printerFx, margins.top() * printerFy,
                            printerWidth - (margins.left() + margins.right()) * printerFx,
                            printerHeight - (margins.top() + margins.bottom()) * printerFy);

        LC_GraphicViewport viewport;
        viewport.setDocument(graphic);
        viewport.setBorders(0, 0, 0, 0);
        viewport.setSize(printerWidth, printerHeight);

        LC_PrintViewportRenderer renderer(&viewport, &painter);
        viewport.loadSettings();
        renderer.loadSettings();
        renderer.setDrawingMode(drawingMode);
        renderer.setPaperScale(ps->getPaperScale());

        bool scaleLineWidth = mdiWindow.getGraphicView()->getLineWidthScaling();
        renderer.setLineWidthScaling(scaleLineWidth);

        double fx = printerFx * RS_Units::getFactorToMM(unit);
        double fy = printerFy * RS_Units::getFactorToMM(unit);
        //RS_DEBUG->print(RS_Debug::D_ERROR, "paper size=(%d, %d)\n",
        //                printer.widthMM(),printer.heightMM());

        double f = (fx + fy) / 2.0;

        double scale = ps->getPaperScale();
        double factor = f * scale;

        //RS_DEBUG->print(RS_Debug::D_ERROR, "PaperSize=(%d, %d)\n",printer.widthMM(), printer.heightMM());

        double baseX = graphic->getPaperInsertionBase().x;
        double baseY = graphic->getPaperInsertionBase().y;

        int numX = ps->getPagesNumHoriz();
        int numY = ps->getPagesNumVert();
        RS_Vector printArea = ps->getPrintAreaSize(false);

        for (int pY = 0; pY < numY; pY++) {
            double offsetY = printArea.y * pY;
            for (int pX = 0; pX < numX; pX++) {
                double offsetX = printArea.x * pX;
                // First page is created automatically.
                // Extra pages must be created manually.
                if (pX > 0 || pY > 0) {
                    printer.newPage();
                }

                viewport.justSetOffsetAndFactor(static_cast<int>((baseX - offsetX) * f), static_cast<int>((baseY - offsetY) * f), factor);

                painter.setViewPort(&viewport); // update offset
                renderer.render();

                //                painter.setDrawSelectedOnly(true);
                //                gv.drawEntity(&painter, graphic);
                //                painter.setDrawSelectedOnly(false);
                //                gv.drawEntity(&painter, graphic);
            }
        }

        // GraphicView deletes painter
        // Calling QPainter::end() is automatic at QPainter destructor
        // painter.end();

        LC_GROUP_GUARD("Print");
        {
            LC_SET("ColorMode", printer.colorMode());
            LC_SET("FileName", printer.outputFileName());
        }
    }
}
