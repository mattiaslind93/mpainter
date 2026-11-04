#include "FilterPanel.hh"
#include <QJsonObject>
#include "LevelsPanel.hh"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>
#include <cassert>
#include <cstdio>
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setStyle("Fusion");app.setStyleSheet("QWidget{background:#303030;color:#d0d0d0;font-size:12px;}QDoubleSpinBox,QComboBox{background:#252525;border:1px solid #515151;padding:3px;}");
 QWidget window;auto *layout=new QHBoxLayout(&window);auto *levels=new bts::LevelsPanel(&window);auto *filter=new bts::FilterPanel(&window);levels->setFixedWidth(320);filter->setFixedWidth(320);layout->addWidget(levels,0,Qt::AlignTop);layout->addWidget(filter,0,Qt::AlignTop);
 bts::TextureSet set;set.selected_layer=QUuid::createUuid();bts::LevelsEffect effect;effect.channel=2;bts::FilterSettings settings;settings.resource=6;
 bts::HistogramMetadata histogram;histogram.ticket=1;histogram.bins[0][12]=65536;histogram.bins[0][30]=1024;histogram.bins[0][50]=64;
 levels->histogram_requested=[](const auto &,const auto &){return 1;};levels->histogram_result=[&](const auto &){return std::optional(histogram);};
 levels->changed=[&](const auto &e){effect=e;levels->synchronize(&set,&effect);};filter->changed=[&](const auto &f,int,float,bts::LayerBlend){settings=f;filter->synchronize(&set,&settings,2);};
 levels->synchronize(&set,&effect);filter->synchronize(&set,&settings,2);window.resize(680,500);window.show();QEventLoop loop;QTimer::singleShot(40,&loop,&QEventLoop::quit);loop.exec();
 if(argc>1){QDir().mkpath(argv[1]);levels->grab().save(QString(argv[1])+"/levels.png");filter->grab().save(QString(argv[1])+"/filter-levels.png");window.grab().save(QString(argv[1])+"/panels.png");}
 auto *graph=dynamic_cast<bts::LevelsRampEditor *>(levels->findChild<QWidget *>("levelsHistogram"));auto *filter_graph=dynamic_cast<bts::LevelsRampEditor *>(filter->findChild<QWidget *>("filterLevelsRamp"));assert(graph&&filter_graph);
 const auto drag=[](bts::HandleRamp *ramp,int handle,float target){auto start=ramp->handle_position(handle);auto end=QPointF(9+target*(ramp->width()-18),32);QMouseEvent press(QEvent::MouseButtonPress,start,ramp->mapToGlobal(start.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(ramp,&press);QMouseEvent move(QEvent::MouseMove,end,ramp->mapToGlobal(end.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(ramp,&move);QMouseEvent release(QEvent::MouseButtonRelease,end,ramp->mapToGlobal(end.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(ramp,&release);};
 int starts=0,finishes=0;levels->edit_started=[&]{++starts;};levels->edit_finished=[&]{++finishes;};
 drag(graph->output,0,.23);assert(std::abs(effect.output_low-.23)<1e-5);drag(graph->output,1,.79);assert(std::abs(effect.output_high-.79)<1e-5&&starts==2&&finishes==2);assert(std::abs(graph->output->positions[0]-.23)<1e-5);
 drag(filter_graph->output,0,.16);drag(filter_graph->output,1,.84);assert(std::abs(settings.output_low-.16)<1e-5&&std::abs(settings.output_high-.84)<1e-5);assert(std::abs(filter->findChild<QDoubleSpinBox *>("filterOutputLow")->value()-.16)<1e-5);
 filter->findChild<QDoubleSpinBox *>("filterOutputLow")->setValue(.31);assert(std::abs(filter_graph->output->positions[0]-.31)<1e-5);
 assert(graph->property("gpuHistogramReady").toBool());const auto height=graph->histogram->plot_rect().height();assert(graph->histogram->bar_height(12)==height);assert(graph->histogram->bar_height(30)>height*.1&&graph->histogram->bar_height(50)>0);
 const auto rendered=graph->histogram->grab().toImage();const double dpr=rendered.devicePixelRatio();const auto plot=graph->histogram->plot_rect();const int px=int((plot.left()+plot.width()*12.5/64)*dpr);int bright=0;for(int y=int(plot.top()*dpr);y<int(plot.bottom()*dpr);++y){const auto color=rendered.pixelColor(px,y);bright+=color.red()>150&&color.blue()>180;}assert(bright>=height*dpr*.9);
 auto changed_histogram=histogram;changed_histogram.bins[0]={};changed_histogram.bins[0][20]=4000000000u;graph->set_histogram(changed_histogram.bins[0]);assert(graph->histogram->bar_height(20)==height&&graph->histogram->bar_height(12)==0);graph->set_histogram(histogram.bins[0]);
 for(int resource=0;resource<9;++resource){settings.resource=resource;filter->synchronize(&set,&settings,2);assert(filter_graph->isHidden()==(resource!=6));if(resource==4)assert(!filter->findChild<QWidget *>("filterHueRamp")->isHidden());if(resource==5)assert(!filter->findChild<QWidget *>("filterContrastRamp")->isHidden());if(resource==7||resource==8)assert(!filter->findChild<QWidget *>("filterRadiusRamp")->isHidden());}
 settings.resource=6;filter->synchronize(&set,&settings,2);assert(dynamic_cast<bts::HandleRamp *>(filter->findChild<QWidget *>("filterOpacityRamp"))->positions[0]==1);QCoreApplication::processEvents();
 if(argc>1){window.grab().save(QString(argv[1])+"/output-handles-dragged.png");QFile proof(QString(argv[1])+"/levels-editor.json");assert(proof.open(QIODevice::WriteOnly));proof.write(QJsonDocument(QJsonObject{{"output_low",effect.output_low},{"output_high",effect.output_high},{"filter_output_low",settings.output_low},{"filter_output_high",settings.output_high},{"normalized_peak_height",height},{"drag_starts",starts},{"drag_finishes",finishes},{"all_nine_filters_reviewed",true},{"large_counts_no_integer_overflow",true}}).toJson());}
 std::puts("Levels/ramp: output handles, numeric sync, normalized sqrt histogram, 4-billion bin overflow guard, shared controls across nine filters PASS");
}
