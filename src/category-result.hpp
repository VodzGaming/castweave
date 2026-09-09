#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
inline constexpr int CategoryCountRole=Qt::UserRole+7;
class CategoryResultDelegate final : public QStyledItemDelegate {
public:
 using QStyledItemDelegate::QStyledItemDelegate;
 QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override { return {240,72}; }
 void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
  QStyleOptionViewItem background(option); initStyleOption(&background,index);
  background.text.clear(); background.icon={};
  auto *style=option.widget ? option.widget->style() : QApplication::style();
  painter->save();
  style->drawControl(QStyle::CE_ItemViewItem,&background,painter,option.widget);
  const QRect box(option.rect.left()+8,option.rect.top()+8,40,54);
  qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).paint(painter,box);
  QRect text(option.rect.left()+58,option.rect.top()+10,qMax(0,option.rect.width()-68),23);
  QFont title=option.font; title.setBold(true); painter->setFont(title); painter->setPen(QColor("#eeeef2"));
  painter->drawText(text,Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(title).elidedText(index.data().toString(),Qt::ElideRight,text.width()));
  text.translate(0,25); QFont detail=option.font; painter->setFont(detail); painter->setPen(QColor("#c2c2cd"));
  painter->drawText(text,Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(detail).elidedText(index.data(CategoryCountRole).toString(),Qt::ElideRight,text.width()));
  painter->restore();
 }
};
