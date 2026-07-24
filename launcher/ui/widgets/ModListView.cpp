/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ModListView.h"
#include <QDrag>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QStyle>
#include <QStyleOption>

#include "minecraft/mod/ModCategoryProxyModel.h"

ModListView::ModListView(QWidget* parent) : QTreeView(parent)
{
    setAllColumnsShowFocus(true);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(false);
    setSortingEnabled(true);
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHeaderHidden(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDropIndicatorShown(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    viewport()->setAcceptDrops(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

void ModListView::setModel(QAbstractItemModel* model)
{
    QTreeView::setModel(model);
    auto head = header();
    head->setStretchLastSection(false);
    // HACK: this is true for the checkbox column of mod lists
    auto string = model->headerData(0, head->orientation()).toString();
    if (head->count() < 1) {
        return;
    }
    if (!string.size()) {
        head->setSectionResizeMode(0, QHeaderView::Interactive);
        head->setSectionResizeMode(1, QHeaderView::Stretch);
        for (int i = 2; i < head->count(); i++)
            head->setSectionResizeMode(i, QHeaderView::Interactive);
    } else {
        head->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < head->count(); i++)
            head->setSectionResizeMode(i, QHeaderView::Interactive);
    }
}

void ModListView::setResizeModes(const QList<QHeaderView::ResizeMode>& modes)
{
    auto head = header();
    int count = qMin(modes.count(), head->count());
    for (int i = 0; i < count; i++) {
        head->setSectionResizeMode(i, modes[i]);
    }
}

void ModListView::drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const
{
    if (!index.data(ModCategoryProxyModel::CategoryHeaderRole).toBool()) {
        QTreeView::drawRow(painter, options, index);
        return;
    }

    painter->save();
    QRect rowRect = options.rect;
    rowRect.setLeft(0);
    rowRect.setRight(viewport()->width());

    QColor background = options.palette.alternateBase().color();
    if (options.state & QStyle::State_Selected) {
        background = options.palette.highlight().color();
    } else {
        const auto accent = options.palette.highlight().color();
        background = QColor::fromRgbF(accent.redF(), accent.greenF(), accent.blueF(), 0.22f);
    }
    painter->fillRect(rowRect, background);

    QFont font = options.font;
    font.setBold(true);
    painter->setFont(font);
    painter->setPen((options.state & QStyle::State_Selected) ? options.palette.highlightedText().color()
                                                            : options.palette.text().color());

    const int margin = 8;
    const int arrowSize = qMin(14, rowRect.height() - 6);
    QStyleOption arrowOption;
    arrowOption.rect = QRect(rowRect.left() + margin, rowRect.center().y() - arrowSize / 2, arrowSize, arrowSize);
    arrowOption.palette = options.palette;
    arrowOption.state = QStyle::State_Enabled;
    style()->drawPrimitive(index.data(ModCategoryProxyModel::CategoryCollapsedRole).toBool() ? QStyle::PE_IndicatorArrowRight
                                                                                             : QStyle::PE_IndicatorArrowDown,
                           &arrowOption, painter, this);

    const auto name = index.data(ModCategoryProxyModel::CategoryNameRole).toString();
    const auto count = index.data(ModCategoryProxyModel::CategoryCountRole).toInt();
    const auto text = tr("%1 (%2)").arg(name).arg(count);
    QRect textRect = rowRect.adjusted(margin * 2 + arrowSize, 0, -margin, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    const int textWidth = QFontMetrics(font).horizontalAdvance(text);
    const int lineStart = textRect.left() + textWidth + margin;
    if (lineStart < rowRect.right() - margin) {
        auto lineColor = painter->pen().color();
        lineColor.setAlphaF(0.25);
        painter->setPen(lineColor);
        painter->drawLine(lineStart, rowRect.center().y(), rowRect.right() - margin, rowRect.center().y());
    }
    painter->restore();
}
