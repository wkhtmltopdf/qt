/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtSvg module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsvggenerator.h"

#ifndef QT_NO_SVGGENERATOR

#include "qpainterpath.h"

#include "private/qpaintengine_p.h"
#include "private/qtextengine_p.h"
#include "private/qdrawhelper_p.h"

#include "qfile.h"
#include "qtextcodec.h"
#include "qtextstream.h"
#include "qbuffer.h"
#include "qmath.h"

#include "qdebug.h"

QT_BEGIN_NAMESPACE

static void translate_color(const QColor &color, QString *color_string,
                            QString *opacity_string)
{
    Q_ASSERT(color_string);
    Q_ASSERT(opacity_string);

    *color_string =
        QString::fromLatin1("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'));
    *opacity_string = QString::number(color.alphaF());
}

static void translate_dashPattern(QVector<qreal> pattern, const qreal& width, QString *pattern_string)
{
    Q_ASSERT(pattern_string);

    // Note that SVG operates in absolute lengths, whereas Qt uses a length/width ratio.
    foreach (qreal entry, pattern)
        *pattern_string += QString::fromLatin1("%1,").arg(entry * width);

    pattern_string->chop(1);
}

class QSvgPaintEnginePrivate : public QPaintEnginePrivate
{
public:
    QSvgPaintEnginePrivate()
    {
        size = QSize();
        viewBox = QRectF();
        outputDevice = 0;
        resolution = 72;

        attributes.document_title = QLatin1String("Qt Svg Document");
        attributes.document_description = QLatin1String("Generated with Qt");
        attributes.font_family = QLatin1String("serif");
        attributes.font_size = QLatin1String("10pt");
        attributes.font_style = QLatin1String("normal");
        attributes.font_weight = QLatin1String("normal");

        afterFirstUpdate = false;
        numGradients = 0;
        clip = false;
    }

    QSize size;
    QRectF viewBox;
    QIODevice *outputDevice;
    QTextStream *stream;
    int resolution;

    QString header;
    QString defs;
    QString body;
    bool    afterFirstUpdate;

    QBrush brush;
    QPen pen;
    QMatrix matrix;
    QFont font;

    QString generateGradientName() {
        ++numGradients;
        currentGradientName = QString::fromLatin1("gradient%1").arg(numGradients);
        return currentGradientName;
    }

    QString currentGradientName;
    int numGradients;
    QString stateString;
    QString oldStateString;
    bool clip;

    struct _attributes {
        QString document_title;
        QString document_description;
        QString font_weight;
        QString font_size;
        QString font_family;
        QString font_style;
        QString stroke, strokeOpacity;
        QString dashPattern, dashOffset;
        QString fill, fillOpacity;
    } attributes;

    void emitState() {
        if (stateString == oldStateString) return;

        // close old state and start a new one...
        if (afterFirstUpdate)
            *stream << "</g>\n\n";
        
        *stream << stateString;
        afterFirstUpdate = true;
        oldStateString = stateString;
    }
};

static inline QPaintEngine::PaintEngineFeatures svgEngineFeatures()
{
    return QPaintEngine::PaintEngineFeatures(
        QPaintEngine::AllFeatures
        & ~QPaintEngine::PatternBrush
        & ~QPaintEngine::PerspectiveTransform
        & ~QPaintEngine::ConicalGradientFill
        & ~QPaintEngine::PorterDuff);
}

class QSvgPaintEngine : public QPaintEngine
{
    Q_DECLARE_PRIVATE(QSvgPaintEngine)
public:

    QSvgPaintEngine()
        : QPaintEngine(*new QSvgPaintEnginePrivate,
                       svgEngineFeatures())
    {
    }

    bool begin(QPaintDevice *device);
    bool end();

    void updateState(const QPaintEngineState &state);
    void popGroup();

    void drawPath(const QPainterPath &path);
    void drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr);
    void drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode);
    void drawTextItem(const QPointF &pt, const QTextItem &item);
    void drawImage(const QRectF &r, const QImage &pm, const QRectF &sr,
                   Qt::ImageConversionFlag = Qt::AutoColor);

    QPaintEngine::Type type() const { return QPaintEngine::SVG; }

    QSize size() const { return d_func()->size; }
    void setSize(const QSize &size) {
        Q_ASSERT(!isActive());
        d_func()->size = size;
    }

    QRectF viewBox() const { return d_func()->viewBox; }
    void setViewBox(const QRectF &viewBox) {
        Q_ASSERT(!isActive());
        d_func()->viewBox = viewBox;
    }

    QString documentTitle() const { return d_func()->attributes.document_title; }
    void setDocumentTitle(const QString &title) {
        d_func()->attributes.document_title = title;
    }

    QString documentDescription() const { return d_func()->attributes.document_description; }
    void setDocumentDescription(const QString &description) {
        d_func()->attributes.document_description = description;
    }

    QIODevice *outputDevice() const { return d_func()->outputDevice; }
    void setOutputDevice(QIODevice *device) {
        Q_ASSERT(!isActive());
        d_func()->outputDevice = device;
    }

    int resolution() { return d_func()->resolution; }
    void setResolution(int resolution) {
        Q_ASSERT(!isActive());
        d_func()->resolution = resolution;
    }
    void saveLinearGradientBrush(const QGradient *g)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        const QLinearGradient *grad = static_cast<const QLinearGradient*>(g);
        str << QLatin1String("<linearGradient ");
        saveGradientUnits(str, g);
        if (grad) {
            str << QLatin1String("x1=\"") <<grad->start().x()<< QLatin1String("\" ")
                << QLatin1String("y1=\"") <<grad->start().y()<< QLatin1String("\" ")
                << QLatin1String("x2=\"") <<grad->finalStop().x() << QLatin1String("\" ")
                << QLatin1String("y2=\"") <<grad->finalStop().y() << QLatin1String("\" ");
        }

        str << QLatin1String("id=\"") << d_func()->generateGradientName() << QLatin1String("\">\n");
        saveGradientStops(str, g);
        str << QLatin1String("</linearGradient>") <<endl;
    }
    void saveRadialGradientBrush(const QGradient *g)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        const QRadialGradient *grad = static_cast<const QRadialGradient*>(g);
        str << QLatin1String("<radialGradient ");
        saveGradientUnits(str, g);
        if (grad) {
            str << QLatin1String("cx=\"") <<grad->center().x()<< QLatin1String("\" ")
                << QLatin1String("cy=\"") <<grad->center().y()<< QLatin1String("\" ")
                << QLatin1String("r=\"") <<grad->radius() << QLatin1String("\" ")
                << QLatin1String("fx=\"") <<grad->focalPoint().x() << QLatin1String("\" ")
                << QLatin1String("fy=\"") <<grad->focalPoint().y() << QLatin1String("\" ");
        }
        str << QLatin1String("xml:id=\"") <<d_func()->generateGradientName()<< QLatin1String("\">\n");
        saveGradientStops(str, g);
        str << QLatin1String("</radialGradient>") << endl;
    }
    void saveConicalGradientBrush(const QGradient *)
    {
        qWarning("svg's don't support conical gradients!");
    }

    void saveGradientStops(QTextStream &str, const QGradient *g) {
        QGradientStops stops = g->stops();

        if (g->interpolationMode() == QGradient::ColorInterpolation) {
            bool constantAlpha = true;
            int alpha = stops.at(0).second.alpha();
            for (int i = 1; i < stops.size(); ++i)
                constantAlpha &= (stops.at(i).second.alpha() == alpha);

            if (!constantAlpha) {
                const qreal spacing = qreal(0.02);
                QGradientStops newStops;
                QRgb fromColor = PREMUL(stops.at(0).second.rgba());
                QRgb toColor;
                for (int i = 0; i + 1 < stops.size(); ++i) {
                    int parts = qCeil((stops.at(i + 1).first - stops.at(i).first) / spacing);
                    newStops.append(stops.at(i));
                    toColor = PREMUL(stops.at(i + 1).second.rgba());

                    if (parts > 1) {
                        qreal step = (stops.at(i + 1).first - stops.at(i).first) / parts;
                        for (int j = 1; j < parts; ++j) {
                            QRgb color = INV_PREMUL(INTERPOLATE_PIXEL_256(fromColor, 256 - 256 * j / parts, toColor, 256 * j / parts));
                            newStops.append(QGradientStop(stops.at(i).first + j * step, QColor::fromRgba(color)));
                        }
                    }
                    fromColor = toColor;
                }
                newStops.append(stops.back());
                stops = newStops;
            }
        }

        foreach(QGradientStop stop, stops) {
            QString color =
                QString::fromLatin1("#%1%2%3")
                .arg(stop.second.red(), 2, 16, QLatin1Char('0'))
                .arg(stop.second.green(), 2, 16, QLatin1Char('0'))
                .arg(stop.second.blue(), 2, 16, QLatin1Char('0'));
            str << QLatin1String("    <stop offset=\"")<< stop.first << QLatin1String("\" ")
                << QLatin1String("stop-color=\"") << color << QLatin1String("\" ")
                << QLatin1String("stop-opacity=\"") << stop.second.alphaF() <<QLatin1String("\" />\n");
        }
    }

    void saveGradientUnits(QTextStream &str, const QGradient *gradient)
    {
        str << QLatin1String("gradientUnits=\"");
        if (gradient && gradient->coordinateMode() == QGradient::ObjectBoundingMode)
            str << QLatin1String("objectBoundingBox");
        else
            str << QLatin1String("userSpaceOnUse");
        str << QLatin1String("\" ");
    }

    void generateQtDefaults()
    {
        *d_func()->stream << QLatin1String("fill=\"none\" ");
        *d_func()->stream << QLatin1String("stroke=\"black\" ");
        *d_func()->stream << QLatin1String("stroke-width=\"1\" ");
        *d_func()->stream << QLatin1String("fill-rule=\"evenodd\" ");
        *d_func()->stream << QLatin1String("stroke-linecap=\"square\" ");
        *d_func()->stream << QLatin1String("stroke-linejoin=\"bevel\" ");
        *d_func()->stream << QLatin1String(">\n");
    }
    inline QTextStream &stream()
    {
        return *d_func()->stream;
    }


    void qpenToSvg(const QPen &spen, QTextStream & s)
    {
        QString width;

        d_func()->pen = spen;

        switch (spen.style()) {
        case Qt::NoPen:
            s << QLatin1String("stroke=\"none\" ");

            d_func()->attributes.stroke = QLatin1String("none");
            d_func()->attributes.strokeOpacity = QString();
            return;
            break;
        case Qt::SolidLine: {
            QString color, colorOpacity;

            translate_color(spen.color(), &color,
                            &colorOpacity);
            d_func()->attributes.stroke = color;
            d_func()->attributes.strokeOpacity = colorOpacity;

            s << QLatin1String("stroke=\"")<<color<< QLatin1String("\" ");
            s << QLatin1String("stroke-opacity=\"")<<colorOpacity<< QLatin1String("\" ");
        }
            break;
        case Qt::DashLine:
        case Qt::DotLine:
        case Qt::DashDotLine:
        case Qt::DashDotDotLine:
        case Qt::CustomDashLine: {
            QString color, colorOpacity, dashPattern, dashOffset;

            qreal penWidth = spen.width() == 0 ? qreal(1) : spen.widthF();

            translate_color(spen.color(), &color, &colorOpacity);
            translate_dashPattern(spen.dashPattern(), penWidth, &dashPattern);

            // SVG uses absolute offset
            dashOffset = QString::number(spen.dashOffset() * penWidth);

            d_func()->attributes.stroke = color;
            d_func()->attributes.strokeOpacity = colorOpacity;
            d_func()->attributes.dashPattern = dashPattern;
            d_func()->attributes.dashOffset = dashOffset;

            s << QLatin1String("stroke=\"")<<color<< QLatin1String("\" ");
            s << QLatin1String("stroke-opacity=\"")<<colorOpacity<< QLatin1String("\" ");
            s << QLatin1String("stroke-dasharray=\"")<<dashPattern<< QLatin1String("\" ");
            s << QLatin1String("stroke-dashoffset=\"")<<dashOffset<< QLatin1String("\" ");
            break;
        }
        default:
            qWarning("Unsupported pen style");
            break;
        }

        if (spen.widthF() == 0)
            s <<"stroke-width=\"1\" ";
        else
            s <<"stroke-width=\"" << spen.widthF() << "\" ";

        switch (spen.capStyle()) {
        case Qt::FlatCap:
            s << "stroke-linecap=\"butt\" ";
            break;
        case Qt::SquareCap:
            s << "stroke-linecap=\"square\" ";
            break;
        case Qt::RoundCap:
            s << "stroke-linecap=\"round\" ";
            break;
        default:
            qWarning("Unhandled cap style");
        }
        switch (spen.joinStyle()) {
        case Qt::MiterJoin:
            s << "stroke-linejoin=\"miter\" "
                        "stroke-miterlimit=\""<<spen.miterLimit()<<"\" ";
            break;
        case Qt::BevelJoin:
            s << "stroke-linejoin=\"bevel\" ";
            break;
        case Qt::RoundJoin:
            s << "stroke-linejoin=\"round\" ";
            break;
        case Qt::SvgMiterJoin:
            s << "stroke-linejoin=\"miter\" "
                        "stroke-miterlimit=\""<<spen.miterLimit()<<"\" ";
            break;
        default:
            qWarning("Unhandled join style");
        }
    }
    void qbrushToSvg(const QBrush &sbrush, QTextStream & s)
    {
        d_func()->brush = sbrush;
        switch (sbrush.style()) {
        case Qt::SolidPattern: {
            QString color, colorOpacity;
            translate_color(sbrush.color(), &color, &colorOpacity);
            s << "fill=\"" << color << "\" "
                        "fill-opacity=\""
                     << colorOpacity << "\" ";
            d_func()->attributes.fill = color;
            d_func()->attributes.fillOpacity = colorOpacity;
        }
            break;
        case Qt::LinearGradientPattern:
            saveLinearGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
            s << QLatin1String("fill=\"url(#") << d_func()->currentGradientName << QLatin1String(")\" ");
            break;
        case Qt::RadialGradientPattern:
            saveRadialGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
            s << QLatin1String("fill=\"url(#") << d_func()->currentGradientName << QLatin1String(")\" ");
            break;
        case Qt::ConicalGradientPattern:
            saveConicalGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
            s << QLatin1String("fill=\"url(#") << d_func()->currentGradientName << QLatin1String(")\" ");
            break;
        case Qt::NoBrush:
            s << QLatin1String("fill=\"none\" ");
            d_func()->attributes.fill = QLatin1String("none");
            d_func()->attributes.fillOpacity = QString();
            return;
            break;
        default:
           break;
        }
    }
    void qfontToSvg(const QFont &sfont, QTextStream & s)
    {
        Q_D(QSvgPaintEngine);

        d->font = sfont;

        if (d->font.pixelSize() == -1)
            d->attributes.font_size = QString::number(d->font.pointSizeF() * d->resolution / 72);
        else
            d->attributes.font_size = QString::number(d->font.pixelSize());

        int svgWeight = d->font.weight();
        switch (svgWeight) {
        case QFont::Light:
            svgWeight = 100;
            break;
        case QFont::Normal:
            svgWeight = 400;
            break;
        case QFont::Bold:
            svgWeight = 700;
            break;
        default:
            svgWeight *= 10;
        }

        d->attributes.font_weight = QString::number(svgWeight);
        d->attributes.font_family = d->font.family();
        d->attributes.font_style = d->font.italic() ? QLatin1String("italic") : QLatin1String("normal");

        s << "font-family=\"" << d->attributes.font_family << "\" "
             "font-size=\"" << d->attributes.font_size << "\" "
             "font-weight=\"" << d->attributes.font_weight << "\" "
             "font-style=\"" << d->attributes.font_style << "\" "
             << endl;
    }

    void setViewBoxClip(bool clip) {
        Q_D(QSvgPaintEngine);
        d->clip = clip;
    }
    
    bool viewBoxClip() const {
        Q_D(const QSvgPaintEngine);
        return d->clip;
    }

};

class QSvgGeneratorPrivate
{
public:
    QSvgPaintEngine *engine;

    uint owns_iodevice : 1;
    QString fileName;
};

/*!
    \class QSvgGenerator
    \ingroup painting
    \since 4.3
    \brief The QSvgGenerator class provides a paint device that is used to create SVG drawings.
    \reentrant

    This paint device represents a Scalable Vector Graphics (SVG) drawing. Like QPrinter, it is
    designed as a write-only device that generates output in a specific format.

    To write an SVG file, you first need to configure the output by setting the \l fileName
    or \l outputDevice properties. It is usually necessary to specify the size of the drawing
    by setting the \l size property, and in some cases where the drawing will be included in
    another, the \l viewBox property also needs to be set.

    \snippet examples/painting/svggenerator/window.cpp configure SVG generator

    Other meta-data can be specified by setting the \a title, \a description and \a resolution
    properties.

    As with other QPaintDevice subclasses, a QPainter object is used to paint onto an instance
    of this class:

    \snippet examples/painting/svggenerator/window.cpp begin painting
    \dots
    \snippet examples/painting/svggenerator/window.cpp end painting

    Painting is performed in the same way as for any other paint device. However,
    it is necessary to use the QPainter::begin() and \l{QPainter::}{end()} to
    explicitly begin and end painting on the device.

    The \l{SVG Generator Example} shows how the same painting commands can be used
    for painting a widget and writing an SVG file.

    \sa QSvgRenderer, QSvgWidget, {About SVG}
*/

/*!
    Constructs a new generator.
*/
QSvgGenerator::QSvgGenerator()
    : d_ptr(new QSvgGeneratorPrivate)
{
    Q_D(QSvgGenerator);

    d->engine = new QSvgPaintEngine;
    d->owns_iodevice = false;
}

/*!
    Destroys the generator.
*/
QSvgGenerator::~QSvgGenerator()
{
    Q_D(QSvgGenerator);
    if (d->owns_iodevice)
        delete d->engine->outputDevice();
    delete d->engine;
}

/*!
    \property QSvgGenerator::title
    \brief the title of the generated SVG drawing
    \since 4.5
    \sa description
*/
QString QSvgGenerator::title() const
{
    Q_D(const QSvgGenerator);

    return d->engine->documentTitle();
}

void QSvgGenerator::setTitle(const QString &title)
{
    Q_D(QSvgGenerator);

    d->engine->setDocumentTitle(title);
}

/*!
    \property QSvgGenerator::description
    \brief the description of the generated SVG drawing
    \since 4.5
    \sa title
*/
QString QSvgGenerator::description() const
{
    Q_D(const QSvgGenerator);

    return d->engine->documentDescription();
}

void QSvgGenerator::setDescription(const QString &description)
{
    Q_D(QSvgGenerator);

    d->engine->setDocumentDescription(description);
}

/*!
    \property QSvgGenerator::size
    \brief the size of the generated SVG drawing
    \since 4.5

    By default this property is set to \c{QSize(-1, -1)}, which
    indicates that the generator should not output the width and
    height attributes of the \c<svg> element.

    \note It is not possible to change this property while a
    QPainter is active on the generator.

    \sa viewBox, resolution
*/
QSize QSvgGenerator::size() const
{
    Q_D(const QSvgGenerator);
    return d->engine->size();
}

void QSvgGenerator::setSize(const QSize &size)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setSize(), cannot set size while SVG is being generated");
        return;
    }
    d->engine->setSize(size);
}

/*!
    \property QSvgGenerator::viewBox
    \brief the viewBox of the generated SVG drawing
    \since 4.5

    By default this property is set to \c{QRect(0, 0, -1, -1)}, which
    indicates that the generator should not output the viewBox attribute
    of the \c<svg> element.

    \note It is not possible to change this property while a
    QPainter is active on the generator.

    \sa viewBox(), size, resolution
*/
QRectF QSvgGenerator::viewBoxF() const
{
    Q_D(const QSvgGenerator);
    return d->engine->viewBox();
}

/*!
    \since 4.5

    Returns viewBoxF().toRect().

    \sa viewBoxF()
*/
QRect QSvgGenerator::viewBox() const
{
    Q_D(const QSvgGenerator);
    return d->engine->viewBox().toRect();
}

void QSvgGenerator::setViewBox(const QRectF &viewBox)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setViewBox(), cannot set viewBox while SVG is being generated");
        return;
    }
    d->engine->setViewBox(viewBox);
}

void QSvgGenerator::setViewBox(const QRect &viewBox)
{
    setViewBox(QRectF(viewBox));
}

/*!
    \property QSvgGenerator::fileName
    \brief the target filename for the generated SVG drawing
    \since 4.5

    \sa outputDevice
*/
QString QSvgGenerator::fileName() const
{
    Q_D(const QSvgGenerator);
    return d->fileName;
}

void QSvgGenerator::setFileName(const QString &fileName)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setFileName(), cannot set file name while SVG is being generated");
        return;
    }

    if (d->owns_iodevice)
        delete d->engine->outputDevice();

    d->owns_iodevice = true;

    d->fileName = fileName;
    QFile *file = new QFile(fileName);
    d->engine->setOutputDevice(file);
}

/*!
    \property QSvgGenerator::outputDevice
    \brief the output device for the generated SVG drawing
    \since 4.5

    If both output device and file name are specified, the output device
    will have precedence.

    \sa fileName
*/
QIODevice *QSvgGenerator::outputDevice() const
{
    Q_D(const QSvgGenerator);
    return d->engine->outputDevice();
}

void QSvgGenerator::setOutputDevice(QIODevice *outputDevice)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setOutputDevice(), cannot set output device while SVG is being generated");
        return;
    }
    d->owns_iodevice = false;
    d->engine->setOutputDevice(outputDevice);
    d->fileName = QString();
}

/*!
    \property QSvgGenerator::resolution
    \brief the resolution of the generated output
    \since 4.5

    The resolution is specified in dots per inch, and is used to
    calculate the physical size of an SVG drawing.

    \sa size, viewBox
*/
int QSvgGenerator::resolution() const
{
    Q_D(const QSvgGenerator);
    return d->engine->resolution();
}

void QSvgGenerator::setResolution(int dpi)
{
    Q_D(QSvgGenerator);
    d->engine->setResolution(dpi);
}

/*!
    Returns the paint engine used to render graphics to be converted to SVG
    format information.
*/
QPaintEngine *QSvgGenerator::paintEngine() const
{
    Q_D(const QSvgGenerator);
    return d->engine;
}

/*!
    \reimp
*/
int QSvgGenerator::metric(QPaintDevice::PaintDeviceMetric metric) const
{
    Q_D(const QSvgGenerator);
    switch (metric) {
    case QPaintDevice::PdmDepth:
        return 32;
    case QPaintDevice::PdmWidth:
        return d->engine->size().width();
    case QPaintDevice::PdmHeight:
        return d->engine->size().height();
    case QPaintDevice::PdmDpiX:
        return d->engine->resolution();
    case QPaintDevice::PdmDpiY:
        return d->engine->resolution();
    case QPaintDevice::PdmHeightMM:
        return qRound(d->engine->size().height() * 25.4 / d->engine->resolution());
    case QPaintDevice::PdmWidthMM:
        return qRound(d->engine->size().width() * 25.4 / d->engine->resolution());
    case QPaintDevice::PdmNumColors:
        return 0xffffffff;
    case QPaintDevice::PdmPhysicalDpiX:
        return d->engine->resolution();
    case QPaintDevice::PdmPhysicalDpiY:
        return d->engine->resolution();
    default:
        qWarning("QSvgGenerator::metric(), unhandled metric %d\n", metric);
        break;
    }
    return 0;
}

/*!
    \property QSvgGenerator::resolution
    \brief do not draw objects outside the viewBox
    \since 4.7

    When specified objects drawn compleatly outsite the viewBox
    are not include in the output SVG.

    \sa viewBox
*/

bool QSvgGenerator::viewBoxClip() const {
    Q_D(const QSvgGenerator);
    return d->engine->viewBoxClip();
}

void QSvgGenerator::setViewBoxClip(bool clip) {
    Q_D(QSvgGenerator);
    d->engine->setViewBoxClip(clip);
}

/*****************************************************************************
 * class QSvgPaintEngine
 */

bool QSvgPaintEngine::begin(QPaintDevice *)
{
    Q_D(QSvgPaintEngine);
    if (!d->outputDevice) {
        qWarning("QSvgPaintEngine::begin(), no output device");
        return false;
    }

    if (!d->outputDevice->isOpen()) {
        if (!d->outputDevice->open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning("QSvgPaintEngine::begin(), could not open output device: '%s'",
                     qPrintable(d->outputDevice->errorString()));
            return false;
        }
    } else if (!d->outputDevice->isWritable()) {
        qWarning("QSvgPaintEngine::begin(), could not write to read-only output device: '%s'",
                 qPrintable(d->outputDevice->errorString()));
        return false;
    }

    d->stream = new QTextStream(&d->header);

    // stream out the header...
    *d->stream << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>" << endl << "<svg";

    if (d->size.isValid()) {
        qreal wmm = d->size.width() * 25.4 / d->resolution;
        qreal hmm = d->size.height() * 25.4 / d->resolution;
        *d->stream << " width=\"" << wmm << "mm\" height=\"" << hmm << "mm\"" << endl;
    }

    if (d->viewBox.isValid()) {
        *d->stream << " viewBox=\"" << d->viewBox.left() << ' ' << d->viewBox.top();
        *d->stream << ' ' << d->viewBox.width() << ' ' << d->viewBox.height() << '\"' << endl;
    }

    *d->stream << " xmlns=\"http://www.w3.org/2000/svg\""
                  " xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                  " version=\"1.2\" baseProfile=\"tiny\">" << endl;

    if (!d->attributes.document_title.isEmpty()) {
        *d->stream << "<title>" << d->attributes.document_title << "</title>" << endl;
    }

    if (!d->attributes.document_description.isEmpty()) {
        *d->stream << "<desc>" << d->attributes.document_description << "</desc>" << endl;
    }

    d->stream->setString(&d->defs);
    *d->stream << "<defs>\n";

    d->stream->setString(&d->body);
    // Start the initial graphics state...
    *d->stream << "<g ";
    generateQtDefaults();
    *d->stream << endl;

    return true;
}

bool QSvgPaintEngine::end()
{
    Q_D(QSvgPaintEngine);

    d->stream->setString(&d->defs);
    *d->stream << "</defs>\n";

    d->stream->setDevice(d->outputDevice);
#ifndef QT_NO_TEXTCODEC
    d->stream->setCodec(QTextCodec::codecForName("UTF-8"));
#endif

    *d->stream << d->header;
    *d->stream << d->defs;
    *d->stream << d->body;
    if (d->afterFirstUpdate)
        *d->stream << "</g>" << endl; // close the updateState

    *d->stream << "</g>" << endl // close the Qt defaults
               << "</svg>" << endl;

    delete d->stream;

    return true;
}

void QSvgPaintEngine::drawPixmap(const QRectF &r, const QPixmap &pm,
                                 const QRectF &sr)
{
    drawImage(r, pm.toImage(), sr);
}

void QSvgPaintEngine::drawImage(const QRectF &r, const QImage &image,
                                const QRectF &sr,
                                Qt::ImageConversionFlag flags)
{
    Q_D(QSvgPaintEngine);

    Q_UNUSED(sr);
    Q_UNUSED(flags);
    if (d->clip && !d->matrix.mapRect(r).intersects(d->viewBox)) return;
    d->emitState();

    stream() << "<image ";
    stream() << "x=\""<<r.x()<<"\" "
                "y=\""<<r.y()<<"\" "
                "width=\""<<r.width()<<"\" "
                "height=\""<<r.height()<<"\" "
                "preserveAspectRatio=\"none\" ";

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::ReadWrite);
    image.save(&buffer, "PNG");
    buffer.close();
    stream() << "xlink:href=\"data:image/png;base64,"
             << data.toBase64()
             <<"\" />\n";
}

void QSvgPaintEngine::updateState(const QPaintEngineState &state)
{
    Q_D(QSvgPaintEngine);

    d->stateString="";
    QTextStream stateStream(&d->stateString);
    stateStream << "<g ";
    qbrushToSvg(state.brush(), stateStream);
    qpenToSvg(state.pen(), stateStream);
    
    d->matrix = state.matrix();
    stateStream << "transform=\"matrix(" << d->matrix.m11() << ','
                << d->matrix.m12() << ','
                << d->matrix.m21() << ',' << d->matrix.m22() << ','
                << d->matrix.dx() << ',' << d->matrix.dy()
                << ")\""
                << endl;
    qfontToSvg(state.font(), stateStream);
    
    if (!qFuzzyIsNull(state.opacity() - 1))
        stateStream << "opacity=\""<<state.opacity()<<"\" ";
    
    stateStream << '>' << endl;
}

void QSvgPaintEngine::drawPath(const QPainterPath &p)
{
    Q_D(QSvgPaintEngine);

    if (d->clip && !d->matrix.mapRect(p.boundingRect()).intersects(d->viewBox)) return;
    d->emitState();
    
    *d->stream << "<path vector-effect=\""
               << (state->pen().isCosmetic() ? "non-scaling-stroke" : "none")
               << "\" fill-rule=\""
               << (p.fillRule() == Qt::OddEvenFill ? "evenodd" : "nonzero")
               << "\" d=\"";

    for (int i=0; i<p.elementCount(); ++i) {
        const QPainterPath::Element &e = p.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            *d->stream << 'M' << e.x << ',' << e.y;
            break;
        case QPainterPath::LineToElement:
            *d->stream << 'L' << e.x << ',' << e.y;
            break;
        case QPainterPath::CurveToElement:
            *d->stream << 'C' << e.x << ',' << e.y;
            ++i;
            while (i < p.elementCount()) {
                const QPainterPath::Element &e = p.elementAt(i);
                if (e.type != QPainterPath::CurveToDataElement) {
                    --i;
                    break;
                } else
                    *d->stream << ' ';
                *d->stream << e.x << ',' << e.y;
                ++i;
            }
            break;
        default:
            break;
        }
        if (i != p.elementCount() - 1) {
            *d->stream << ' ';
        }
    }

    *d->stream << "\"/>" << endl;
}

void QSvgPaintEngine::drawPolygon(const QPointF *points, int pointCount,
                                  PolygonDrawMode mode)
{
    Q_ASSERT(pointCount >= 2);

    Q_D(QSvgPaintEngine);

    QPainterPath path(points[0]);
    for (int i=1; i<pointCount; ++i)
        path.lineTo(points[i]);

    if (d->clip && !d->matrix.mapRect(path.boundingRect()).intersects(d->viewBox)) return;
    d->emitState();

    if (mode == PolylineMode) {
        stream() << "<polyline fill=\"none\" vector-effect=\""
                 << (state->pen().isCosmetic() ? "non-scaling-stroke" : "none")
                 << "\" points=\"";
        for (int i = 0; i < pointCount; ++i) {
            const QPointF &pt = points[i];
            stream() << pt.x() << ',' << pt.y() << ' ';
        }
        stream() << "\" />" <<endl;
    } else {
        path.closeSubpath();
        drawPath(path);
    }
}

void QSvgPaintEngine::drawTextItem(const QPointF &pt, const QTextItem &textItem)
{
    Q_D(QSvgPaintEngine);
    if (d->pen.style() == Qt::NoPen)
        return;

    if (d->clip) {
        QRectF b=painter()->boundingRect( QRectF(pt, QSize()) , Qt::AlignLeft, textItem.text());
        if (!d->matrix.mapRect(b).intersects(d->viewBox)) return;
    }
    d->emitState();

    const QTextItemInt &ti = static_cast<const QTextItemInt &>(textItem);
    QString s = QString::fromRawData(ti.chars, ti.num_chars);

    *d->stream << "<text "
                  "fill=\"" << d->attributes.stroke << "\" "
                  "fill-opacity=\"" << d->attributes.strokeOpacity << "\" "
                  "stroke=\"none\" "
                  "xml:space=\"preserve\" "
                  "x=\"" << pt.x() << "\" y=\"" << pt.y() << "\" ";
    qfontToSvg(textItem.font(), *d->stream);
    *d->stream << " >"
               << Qt::escape(s)
               << "</text>"
               << endl;
}

QT_END_NAMESPACE

#endif // QT_NO_SVGGENERATOR
