#include "NumericLocale.hh"
#include "FilterPanel.hh"
#include "LevelsPanel.hh"
#include "DisplacementPanel.hh"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QLineEdit>
#include <QLabel>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QVBoxLayout>
#include <cassert>
#include <cstdio>
int main(int argc,char **argv){
 assert(QLocale::system().decimalPoint()==",");bts::initialize_numeric_locale();QApplication app(argc,argv);assert(QLocale().decimalPoint()==".");assert(!QLocale().toString(1234.5).contains(','));
 QWidget window;auto *layout=new QHBoxLayout(&window);auto *displacement=new bts::DisplacementPanel(&window);auto *filter=new bts::FilterPanel(&window);auto *levels=new bts::LevelsPanel(&window);layout->addWidget(displacement);layout->addWidget(filter);layout->addWidget(levels);displacement->setFixedWidth(300);filter->setFixedWidth(300);levels->setFixedWidth(300);
 bts::TextureSet set;bts::FilterSettings settings;settings.resource=6;bts::LevelsEffect effect;effect.channel=2;levels->synchronize(&set,&effect);filter->synchronize(&set,&settings,2);auto disp=bts::DisplacementSettings{};disp.channel=disp.allocated=true;displacement->synchronize(disp,true);
 auto *spin=new bts::NumericSpinBox(&window);spin->setObjectName("commaInput");spin->setRange(-100000,100000);spin->setDecimals(3);layout->addWidget(spin);auto *percent=new bts::NumericSpinBox(&window);percent->setObjectName("percentInput");percent->setRange(0,100);percent->setSuffix(" %");percent->setValue(100);layout->addWidget(percent);window.resize(1600,900);window.show();app.processEvents();
 auto type=[&](const QString &text){spin->setFocus();spin->selectAll();for(QChar c:text){QKeyEvent event(QEvent::KeyPress,0,Qt::NoModifier,QString(c));QCoreApplication::sendEvent(spin,&event);}QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QCoreApplication::sendEvent(spin,&enter);};
 type("0,25");assert(spin->value()==.25&&spin->text()=="0.250");type("0.75");assert(spin->value()==.75);type("-1234,5");assert(spin->value()==-1234.5&&!spin->text().contains(','));spin->selectAll();QApplication::clipboard()->setText("0,25");auto *line=spin->findChild<QLineEdit *>();line->paste();spin->interpretText();assert(spin->value()==.25&&spin->text()=="0.250");assert(percent->text()=="100.00 %"&&percent->decimals()==2);
 QJsonArray texts;for(auto *control:window.findChildren<QDoubleSpinBox *>()){assert(control->locale().decimalPoint()==".");assert(!control->text().contains(','));texts.append(QJsonObject{{"name",control->objectName()},{"text",control->text()}});}assert(texts.size()>12);
 if(argc>1){QDir().mkpath(argv[1]);QFile proof(QString(argv[1])+"/numeric-locale.json");assert(proof.open(QIODevice::WriteOnly));proof.write(QJsonDocument(QJsonObject{{"system_locale",QLocale::system().name()},{"ui_locale",QLocale().name()},{"comma_typed_and_pasted",true},{"texts",texts}}).toJson());window.grab().save(QString(argv[1])+"/numeric-locale.png");}std::printf("Numeric locale: comma/dot typing+paste, negative/no grouping, 100.00 %% and %lld numeric controls PASS\n",static_cast<long long>(texts.size()));
}
