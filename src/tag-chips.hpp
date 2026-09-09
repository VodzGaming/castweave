#pragma once
#include <QLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QRegularExpression>

class ChipFlow final : public QLayout {
 QList<QLayoutItem*> items;
 int arrange(const QRect &r,bool test) const {
  int x=r.x(),y=r.y(),height=0;
  for(auto *item:items) {
   const QSize size=item->sizeHint();
   if(x>r.x() && x+size.width()>r.right()+1) { x=r.x(); y+=height+5; height=0; }
   if(!test) item->setGeometry(QRect(QPoint(x,y),size));
   x+=size.width()+5; height=qMax(height,size.height());
  }
  return y+height-r.y();
 }
public:
 explicit ChipFlow(QWidget *parent):QLayout(parent) { setContentsMargins(0,0,0,0); }
 ~ChipFlow() override { while(auto *item=takeAt(0)) delete item; }
 void addItem(QLayoutItem *item) override { items.append(item); }
 int count() const override { return int(items.size()); }
 QLayoutItem *itemAt(int i) const override { return items.value(i); }
 QLayoutItem *takeAt(int i) override { return i>=0 && i<items.size() ? items.takeAt(i) : nullptr; }
 bool hasHeightForWidth() const override { return true; }
 int heightForWidth(int width) const override { return arrange(QRect(0,0,width,0),true); }
 Qt::Orientations expandingDirections() const override { return {}; }
 QSize sizeHint() const override { return minimumSize(); }
 QSize minimumSize() const override { QSize size; for(auto *i:items) size=size.expandedTo(i->minimumSize()); return size; }
 void setGeometry(const QRect &r) override { QLayout::setGeometry(r); arrange(r,false); }
};

class TagChips final : public QWidget {
 QLineEdit *model;
 QLineEdit *entry;
 QLabel *error;
 ChipFlow *flow;
 void render() {
  while(auto *item=flow->takeAt(0)) { delete item->widget(); delete item; }
  for(const auto &part:model->text().split(',',Qt::SkipEmptyParts)) {
   const QString tag=part.trimmed();
   auto *button=new QPushButton(tag+"  \u00d7");
   button->setAccessibleName("Remove tag "+tag);
   button->setStyleSheet("QPushButton { background:#36363c; border:0; border-radius:10px; padding:3px 8px; font-size:11px; }");
   flow->addWidget(button);
   connect(button,&QPushButton::clicked,this,[this,tag] {
    QStringList remaining;
    for(const auto &part:model->text().split(',',Qt::SkipEmptyParts))
     if(part.trimmed()!=tag) remaining<<part.trimmed();
    // Delay rebuilding until the clicked button's signal returns.
    QTimer::singleShot(0,this,[this,remaining] { model->setText(remaining.join(", ")); });
   });
  }
  flow->invalidate(); updateGeometry();
 }
public:
 explicit TagChips(QLineEdit *storage,QWidget *parent=nullptr):QWidget(parent),model(storage) {
  auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
  entry=new QLineEdit; entry->setPlaceholderText("Press Enter after each tag"); entry->setMaxLength(25);
  layout->addWidget(entry);
  auto *chips=new QWidget; flow=new ChipFlow(chips); layout->addWidget(chips);
  error=new QLabel; error->setWordWrap(true); error->setStyleSheet("color:#ffaaaa; font-size:11px;"); error->hide(); layout->addWidget(error);
  connect(entry,&QLineEdit::returnPressed,this,[this] { commit(); });
  connect(model,&QLineEdit::textChanged,this,[this] { render(); });
  render();
 }
#ifdef STREAMDOCK_PREVIEW
 bool runOfflineChecks() {
  const QString original=model->text();
  model->clear(); entry->setText("English");
  if(!commit() || model->text()!="English") return false;
  entry->setText("english"); if(!commit() || model->text()!="English") return false;
  entry->setText("bad tag"); if(commit()) return false;
  resetEntry();
  model->setText("a,b,c,d,e,f,g,h,i,j"); entry->setText("eleven"); if(commit()) return false;
  resetEntry(); model->setText(original); return true;
 }
#endif
 void resetEntry() { entry->clear(); error->hide(); }
 bool commit() {
  const QString tag=entry->text().trimmed();
  if(tag.isEmpty()) return true;
  QStringList values;
  for(const auto &part:model->text().split(',',Qt::SkipEmptyParts)) values<<part.trimmed();
  QString problem;
  if(!QRegularExpression("^[\\p{L}\\p{N}]+$").match(tag).hasMatch()) problem="Tags can contain letters and numbers, without spaces.";
  else if(values.contains(tag,Qt::CaseInsensitive)) { resetEntry(); return true; }
  else if(values.size()>=10) problem="You can have up to 10 Twitch tags.";
  if(!problem.isEmpty()) { error->setText(problem); error->show(); return false; }
  values<<tag; model->setText(values.join(", ")); resetEntry(); return true;
 }
};
