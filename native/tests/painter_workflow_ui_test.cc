#include "PainterLayerDelegate.hh"
#include "NameListShortcuts.hh"
#include <QAction>
#include <QPushButton>
#include "LayerStackList.hh"
#include "ShelfCardDelegate.hh"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <cassert>
#include <cstdio>
#include <set>
using namespace bts;
namespace {
void click(QWidget *widget,QPoint point,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
 for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,QPointF(point),QPointF(widget->mapToGlobal(point)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);QCoreApplication::sendEvent(widget,&event);}QCoreApplication::processEvents();
}
void verify_name_shortcuts(){
 QWidget window;QVBoxLayout layout(&window);QListWidget layers,shelf,sets;QPushButton viewport("Viewport");viewport.setFocusPolicy(Qt::StrongFocus);layout.addWidget(&layers);layout.addWidget(&shelf);layout.addWidget(&sets);layout.addWidget(&viewport);
 int layouts=0;QAction action(&window);action.setShortcut(QKeySequence(Qt::Key_F2));window.addAction(&action);QObject::connect(&action,&QAction::triggered,&window,[&]{++layouts;});
 for(auto *list:{&layers,&shelf,&sets}){new NameListShortcuts(list);list->setEditTriggers(QAbstractItemView::EditKeyPressed);list->setItemDelegate(new InlineNameDelegate(list));auto *item=new QListWidgetItem("Editable name",list);item->setFlags(item->flags()|Qt::ItemIsEditable);list->setCurrentItem(item);}
 auto *set_row=new QWidget;sets.setItemWidget(sets.currentItem(),set_row);QHBoxLayout set_layout(set_row);InlineNameField field(set_row);field.setObjectName("textureSetName");QPushButton shader("Shader");set_layout.addWidget(&field);set_layout.addWidget(&shader);field.synchronize("Texture Set");
 window.resize(400,450);window.show();window.activateWindow();QApplication::processEvents();assert(window.isActiveWindow());
 const auto f2=[](QWidget *target,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QKeyEvent key(QEvent::KeyPress,Qt::Key_F2,modifiers);QCoreApplication::sendEvent(target,&key);QApplication::processEvents();};
 for(auto *list:{&layers,&shelf}){list->setFocus();f2(list);auto *editor=list->findChild<QLineEdit *>("inlineNameEditor");assert(editor&&editor->isVisible()&&editor->hasFocus()&&layouts==0);f2(editor);assert(layouts==0);QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(editor,&escape);QApplication::processEvents();}
 sets.setFocus();f2(&sets);assert(!field.isReadOnly()&&field.hasFocus()&&layouts==0);f2(&field);assert(layouts==0);viewport.setFocus();f2(&viewport);assert(layouts==1);shader.setFocus();f2(&shader);assert(layouts==2);shelf.currentItem()->setFlags(shelf.currentItem()->flags()&~Qt::ItemIsEditable);shelf.setFocus();f2(&shelf);assert(layouts==3);
 QKeyEvent shifted(QEvent::ShortcutOverride,Qt::Key_F2,Qt::ShiftModifier);shifted.setAccepted(false);QCoreApplication::sendEvent(&layers,&shifted);assert(!shifted.isAccepted());
}
QListWidgetItem *row(LayerStackList &list,const QString &name,const QUuid &id){auto *item=new QListWidgetItem(name,&list);item->setData(Qt::UserRole,id.toString());item->setData(InlineNameDelegate::name_role,name);item->setData(Qt::UserRole+1,100.);item->setData(Qt::UserRole+5,int(LayerBlend::Normal));item->setFlags(item->flags()|Qt::ItemIsUserCheckable|Qt::ItemIsEditable);item->setCheckState(Qt::Checked);item->setSizeHint(QSize(0,44));return item;}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setStyle("Fusion");verify_name_shortcuts();
 assert(compact_layer_opacity(37.49)=="37"&&compact_layer_opacity(37.51)=="38"&&compact_layer_opacity(101)=="100");
 assert(compact_layer_blend(int(LayerBlend::Normal))=="Norm"&&compact_layer_blend(int(LayerBlend::Overlay))=="Ovrl"&&compact_layer_blend(int(LayerBlend::Multiply))=="Mult"&&compact_layer_blend(int(LayerBlend::LinearDodge))=="Add"&&compact_layer_blend(int(LayerBlend::Passthrough))=="PThr");
 std::set<QByteArray> glyphs;for(int i=0;i<int(PainterGlyph::Count);++i){const auto image=painter_workflow_icon(PainterGlyph(i)).pixmap(32,32).toImage().convertToFormat(QImage::Format_ARGB32);assert(!image.isNull());bool visible=false;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)visible|=qAlpha(image.pixel(x,y))!=0;assert(visible);glyphs.insert(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256));}assert(glyphs.size()==int(PainterGlyph::Count));
 QWidget window;auto *layout=new QVBoxLayout(&window);auto *list=new LayerStackList(&window);layout->addWidget(list);auto *delegate=new PainterLayerDelegate(list);list->setItemDelegate(delegate);list->setEditTriggers(QAbstractItemView::EditKeyPressed);list->setSelectionMode(QAbstractItemView::ExtendedSelection);
 const QUuid id=QUuid::createUuid(),effect_id=QUuid::createUuid();auto *layer=row(*list,"Worn painted metal with a meaningful long layer name",id);layer->setData(Qt::UserRole+6,true);layer->setData(Qt::UserRole+22,(1u<<0)|(1u<<4)|(1u<<14));layer->setData(Qt::UserRole+9,true);layer->setIcon(painter_workflow_icon(PainterGlyph::Fill));layer->setData(Qt::UserRole+7,painter_workflow_icon(PainterGlyph::Mask));
 auto *effect=row(*list,"Native edge wear",id);effect->setData(Qt::UserRole+10,effect_id.toString());effect->setData(Qt::UserRole+2,1);effect->setData(Qt::UserRole+23,int(PainterGlyph::Generator));effect->setSizeHint(QSize(0,26));auto *folder=row(*list,"Folder",QUuid::createUuid());folder->setData(Qt::UserRole+3,true);
 bool material=false,mask=false,bypassed=false,isolated=false,removed=false,blend=false,opacity=false,collapsed=false;delegate->target_selected=[&](QUuid target,bool value){assert(target==id);(value?mask:material)=true;};delegate->mask_bypassed=[&](QUuid target){assert(target==id);bypassed=true;};delegate->mask_isolated=[&](QUuid target){assert(target==id);isolated=true;};delegate->effect_removed=[&](QUuid parent,QUuid child){assert(parent==id&&child==effect_id);removed=true;};delegate->blend_requested=[&](const auto &index){assert(index==list->indexFromItem(effect));blend=true;};delegate->opacity_requested=[&](const auto &index){assert(index==list->indexFromItem(effect));opacity=true;};delegate->folder_toggled=[&](QUuid target){assert(target==QUuid(folder->data(Qt::UserRole).toString()));collapsed=true;};
 window.resize(560,300);window.show();app.processEvents();const auto layer_index=list->indexFromItem(layer);auto box=list->visualItemRect(layer);assert(box.height()==44);click(list->viewport(),QPoint(layer_thumbnail_x(layer_index,box)+12,box.center().y()));click(list->viewport(),layer_mask_rect(layer_index,box).center());click(list->viewport(),layer_mask_rect(layer_index,box).center(),Qt::ShiftModifier);click(list->viewport(),layer_mask_rect(layer_index,box).center(),Qt::AltModifier);assert(material&&mask&&bypassed&&isolated);
 click(list->viewport(),layer_eye_rect(box).center());assert(layer->checkState()==Qt::Unchecked);assert(layer->text().startsWith("Worn painted"));
 box=list->visualItemRect(effect);assert(box.height()==26);click(list->viewport(),layer_eye_rect(box,20).center());assert(effect->checkState()==Qt::Unchecked);click(list->viewport(),layer_effect_blend_rect(box).center());click(list->viewport(),layer_effect_opacity_rect(box).center());click(list->viewport(),layer_effect_delete_rect(box).center());assert(blend&&opacity&&removed);effect->setData(Qt::UserRole+24,false);blend=opacity=false;click(list->viewport(),layer_effect_blend_rect(box).center());click(list->viewport(),layer_effect_opacity_rect(box).center());assert(!blend&&!opacity);effect->setData(Qt::UserRole+24,true);box=list->visualItemRect(folder);click(list->viewport(),QPoint(box.left()+40,box.center().y()));assert(collapsed);
 QString renamed;delegate->renamed=[&](const auto &index,const QString &name){assert(index==layer_index);renamed=name;};list->editItem(layer);app.processEvents();auto *editor=list->findChild<QLineEdit *>("inlineNameEditor");assert(editor);editor->setText("  Artist name  ");delegate->setModelData(editor,list->model(),layer_index);assert(renamed=="Artist name"&&layer->text().startsWith("Worn painted"));list->closePersistentEditor(layer);editor->hide();
 QListWidget shelf;auto *cards=new ShelfCardDelegate(&shelf);shelf.setItemDelegate(cards);shelf.setViewMode(QListView::IconMode);shelf.setGridSize(QSize(96,108));shelf.setIconSize(QSize(64,64));auto *card=new QListWidgetItem(painter_workflow_icon(PainterGlyph::Fill),"Weathered painted steel material",&shelf);card->setToolTip(card->text()+"\nweathered-painted-steel.spsm");card->setFlags(card->flags()|Qt::ItemIsEditable);shelf.resize(320,150);shelf.show();app.processEvents();QString shelf_name;cards->renamed=[&](const auto &,const QString &name){shelf_name=name;};shelf.editItem(card);app.processEvents();auto *card_editor=shelf.findChild<QLineEdit *>("inlineNameEditor");assert(card_editor);card_editor->setText("Updated shelf asset");cards->setModelData(card_editor,shelf.model(),shelf.indexFromItem(card));assert(shelf_name=="Updated shelf asset"&&card->toolTip().contains("weathered-painted-steel.spsm"));card_editor->hide();
 if(argc>1){QDir().mkpath(argv[1]);window.resize(560,440);app.processEvents();assert(window.grab().save(QString(argv[1])+"/painter-layer-controls.png"));assert(shelf.grab().save(QString(argv[1])+"/painter-shelf-cards.png"));}
 std::puts("Painter workflow UI: compact labels, distinct icons, targets, eye/blend/opacity/delete, rename and shelf cards PASS");
}
