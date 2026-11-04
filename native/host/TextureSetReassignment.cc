#include "TextureSetReassignment.hh"
#include "TextureSetAssignment.hh"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace bts {namespace {
constexpr auto mime="application/x-bts-texture-set";
class SetList:public QListWidget {public:using QListWidget::QListWidget;void startDrag(Qt::DropActions) override{if(!currentItem())return;auto *data=new QMimeData;data->setData(mime,currentItem()->data(Qt::UserRole).toString().toUtf8());auto *drag=new QDrag(this);drag->setMimeData(data);drag->exec(Qt::MoveAction);}};
class SetChoice:public QComboBox {public:using QComboBox::QComboBox;void dragEnterEvent(QDragEnterEvent *event)override{if(event->mimeData()->hasFormat(mime))event->acceptProposedAction();}void dragMoveEvent(QDragMoveEvent *event)override{if(event->mimeData()->hasFormat(mime))event->acceptProposedAction();}void dropEvent(QDropEvent *event)override{const int row=findData(QString::fromUtf8(event->mimeData()->data(mime)));if(row>=0){setCurrentIndex(row);event->acceptProposedAction();}}};
}
std::optional<std::vector<TextureSet>> choose_texture_set_assignment(QWidget *parent,const TextureDocument &document,const SceneSnapshot &snapshot){
  std::vector<std::uint32_t> order;for(const auto &material:snapshot.materials)order.push_back(material.session_id);TextureSetAssignment pending(document.texture_sets(),order);QDialog dialog(parent);dialog.setObjectName("textureSetReassignmentDialog");dialog.setWindowTitle("Texture Sets Reassignment");dialog.resize(650,430);auto *layout=new QVBoxLayout(&dialog);auto *grid=new QGridLayout;layout->addLayout(grid,1);grid->addWidget(new QLabel("Disabled Texture Sets",&dialog),0,0);grid->addWidget(new QLabel("Project Texture Sets",&dialog),0,1);grid->addWidget(new QLabel("Mesh Materials",&dialog),0,2);auto *disabled=new SetList(&dialog);disabled->setObjectName("disabledTextureSets");disabled->setDragEnabled(true);grid->addWidget(disabled,1,0,int(pending.state().size())+1,1);std::vector<SetChoice *> choices;
  for(std::size_t row=0;row<pending.state().size();++row){auto *choice=new SetChoice(&dialog);choice->setAcceptDrops(true);choice->setObjectName(QString("setAssignment%1").arg(row));for(const auto &set:document.texture_sets())choice->addItem(QString::fromStdString(set.name),set.id.toString());choices.push_back(choice);grid->addWidget(choice,int(row)+1,1);const auto uid=pending.destinations()[row];const auto material=std::find_if(snapshot.materials.begin(),snapshot.materials.end(),[&](const auto &m){return m.session_id==uid;});if(material==snapshot.materials.end())throw std::runtime_error("Assigned mesh material disappeared");auto *label=new QLabel(QString::fromStdString(material->name),&dialog);label->setObjectName(QString("assignmentMaterial%1").arg(row));grid->addWidget(label,int(row)+1,2);}
  auto *instruction=new QLabel("Drag a disabled Texture Set onto a project row, or choose a set from its list. Displaced stacks remain disabled. Apply changes the bindings; Cancel keeps the project unchanged.",&dialog);instruction->setWordWrap(true);layout->addWidget(instruction);auto *error=new QLabel(&dialog);error->setObjectName("reassignmentError");error->setWordWrap(true);layout->addWidget(error);auto *footer=new QHBoxLayout;layout->addLayout(footer);auto *undo=new QPushButton("Undo",&dialog),*redo=new QPushButton("Redo",&dialog);undo->setObjectName("undoSetAssignment");redo->setObjectName("redoSetAssignment");footer->addWidget(undo);footer->addWidget(redo);footer->addStretch();auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Apply)->setObjectName("applySetAssignment");footer->addWidget(buttons);
  const auto refresh=[&]{disabled->clear();for(const auto &set:document.texture_sets())if(std::find(pending.state().begin(),pending.state().end(),set.id)==pending.state().end()){auto *item=new QListWidgetItem(QString::fromStdString(set.name),disabled);item->setData(Qt::UserRole,set.id.toString());}for(std::size_t row=0;row<choices.size();++row){QSignalBlocker block(choices[row]);choices[row]->setCurrentIndex(choices[row]->findData(pending.state()[row].toString()));}undo->setEnabled(pending.can_undo());redo->setEnabled(pending.can_redo());};
  for(std::size_t row=0;row<choices.size();++row)QObject::connect(choices[row],&QComboBox::currentIndexChanged,&dialog,[&,row]{pending.assign(row,QUuid(choices[row]->currentData().toString()));refresh();});QObject::connect(undo,&QPushButton::clicked,&dialog,[&]{pending.undo();refresh();});QObject::connect(redo,&QPushButton::clicked,&dialog,[&]{pending.redo();refresh();});QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);std::optional<std::vector<TextureSet>> result;QObject::connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,[&]{try{result=pending.result();dialog.accept();}catch(const std::exception &failure){error->setText(QString::fromUtf8(failure.what()));}});refresh();dialog.exec();return result;
}
}
