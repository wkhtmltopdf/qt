/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
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
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QFONTSUBSET_P_H
#define QFONTSUBSET_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "private/qfontengine_p.h"

#ifndef QT_NO_PRINTER

QT_BEGIN_NAMESPACE

class QFontSubset
{
public:
    QFontSubset(QFontEngine *fe, int obj_id = 0)
        : new_index(0), object_id(obj_id), noEmbed(false), fontEngine(fe), downloaded_glyphs(0), standard_font(false)
        { fontEngine->ref.ref(); addGlyph(0); addGlyph(0,new_index,1);}
    ~QFontSubset() {
        if (!fontEngine->ref.deref() && fontEngine->cache_count == 0)
            delete fontEngine;
    }

    QByteArray toTruetype() const;
    QByteArray toType1() const;
    QByteArray type1AddedGlyphs() const;
    QByteArray widthArray() const;
    QByteArray createToUnicodeMap() const;
    QVector<int> getReverseMap() const;
    QByteArray glyphName(unsigned int glyph, const QVector<int> reverseMap) const;

    static QByteArray glyphName(unsigned short unicode, bool symbol);

    int new_index;
    int addGlyph(int index);
	int addGlyph(int index, int& new_index, int begin);
    const int object_id;
    bool noEmbed;
    QFontEngine *fontEngine;
    QList<int> glyph_indices;
    mutable int downloaded_glyphs;
    mutable bool standard_font;
    int nGlyphs() const { return glyph_indices.size(); }
    mutable QFixed emSquare;
    mutable QVector<QFixed> widths;
};

QT_END_NAMESPACE

#endif // QT_NO_PRINTER

#endif // QFONTSUBSET_P_H
