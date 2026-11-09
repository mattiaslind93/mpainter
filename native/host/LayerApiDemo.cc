#include "PythonConsole.hh"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include "ViewportWidget.hh"
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
namespace bts {namespace {
QString quoted(const QString &text){const auto bytes=QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);return QString::fromUtf8(bytes.mid(1,bytes.size()-2));}
class Demo:public QObject {
 QMainWindow *window;QString directory,mesh;std::function<bool()> busy;std::function<QJsonObject()> state;QTimer timer;int phase=0,ticks=0;QJsonArray captures;
 template<class T>T *get(const char *name){auto *value=window->findChild<T *>(name);if(!value)qFatal("Layer API GUI control missing: %s",name);return value;}
 void command(const QString &code){get<QLineEdit>("pythonCommand")->setText("exec("+quoted(code)+")");get<QPushButton>("executePythonCommand")->click();if(!get<QDockWidget>("pythonConsoleDock")->property("lastExecutionSucceeded").toBool())qFatal("Layer API script failed: %s",qPrintable(get<QPlainTextEdit>("pythonOutput")->toPlainText()));}
 void capture(const char *name){auto data=state();data["stage"]=name;captures.append(data);QFile proof(directory+"/layer-api.json");if(!proof.open(QIODevice::WriteOnly))qFatal("Cannot write layer API proof");proof.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());auto *view=window->findChild<bts::ViewportWidget *>();view->grabFramebuffer().save(directory+"/"+name+"-viewport.png");window->grab().save(directory+"/"+name+"-workspace.png");}
 void advance(){if(++ticks>1600)qFatal("Layer API demo timed out");auto *view=window->findChild<bts::ViewportWidget *>();if(!view||!view->isValid()||busy())return;
 switch(phase++){
 case 0:get<QAction>("showPythonConsole")->trigger();command("import bts, json, pathlib, os");break;
 case 1:command("root = pathlib.Path("+quoted(directory)+")\nC=bts.textureset.ChannelType\nP=bts.layerstack.InsertPosition\nS=bts.layerstack.NodeStack\nbts.project.create("+quoted(mesh)+", settings=bts.project.Settings(resolution=256, import_cameras=False))\nsets=bts.textureset.all_texture_sets()\nassert len(sets)==2\nstack=sets[0]['id']; other=sets[1]['id']\nbts.textureset.set_active_stack(stack)\nchanged=[]\ndef on_stack(event): changed.append(event.texture_set)\nbts.event.DISPATCHER.connect_strong(bts.event.LayerStacksModelDataChanged,on_stack)");break;
 case 2:capture("before");command("group=bts.layerstack.insert_group(P.from_textureset_stack(stack))\nfill=bts.layerstack.insert_fill(P.inside_node(group,S.Substack))\nfill.name='API Coat'\nfill.active_channels={C.BaseColor,C.Roughness}\nfill.set_source(C.BaseColor,[.8,.02,.03])\nfill.set_source(C.Roughness,.25)\nbts.layerstack.set_selected_nodes([fill])\nassert [node.uid for node in bts.layerstack.get_selected_nodes(stack)]==[fill.uid]\nassert group.sub_layers()[0].uid==fill.uid\nassert bts.layerstack.get_root_layer_nodes(stack)[0].uid==group.uid\nassert not fill.info()['paintable']\nprojection=fill.get_projection_parameters(); projection.linked=False; projection.scale=[2,1.5]; projection.offset=[.1,.2]; projection.rotation=30\nfill.set_projection_parameters(projection)");break;
 case 3:capture("color");command("fill.add_mask(1.)\nmask=bts.layerstack.insert_fill(P.inside_node(fill,S.Mask))\nmask.set_source(None,.35)\nmask.name='API mask fill'\nmask.set_opacity(.6,None)\nlevels=bts.layerstack.insert_levels(P.inside_node(fill,S.Content),C.BaseColor)\nlevels.name='API levels'\nparameters=levels.get_parameters(); parameters.gamma=1.3; levels.set_parameters(parameters)\nbts.layerstack.set_selected_nodes([mask])\nassert bts.layerstack.get_selection_target()=='mask'\nassert len(fill.content_effects())==len(fill.mask_effects())==1");break;
 case 4:capture("masked");command("errors=[]\nfor operation in [lambda:fill.get_opacity(None),lambda:mask.set_opacity(.2,C.BaseColor),lambda:bts.layerstack.insert_group(P.inside_node(fill,S.Mask)),lambda:bts.layerstack.insert_fill(P.inside_node(fill,S.Content)),lambda:setattr(fill,'active_channels',{C.Displacement})]:\n    try: operation()\n    except ValueError as error: errors.append(str(error))\nassert len(errors)==5\nbts.textureset.set_active_stack(other)\ntry:bts.layerstack.set_selected_nodes([fill])\nexcept ValueError:pass\nelse:raise AssertionError('cross-stack selection must fail')\nbts.textureset.set_active_stack(stack)\nbts.layerstack.set_selected_nodes([mask])");break;
 case 5:capture("invalid");command("asset=bts.resource.import_image(root/'texture.png',scope='project',usage='texture')\nfill.set_source(C.BaseColor,asset['id'])\nfill.set_source(C.Roughness,asset['id'])\nassert not fill.get_source(C.BaseColor)['raw'] and fill.get_source(C.Roughness)['raw']\nbts.layerstack.set_selected_nodes([fill])");break;
 case 6:capture("textured");command("projection=fill.get_projection_parameters();projection.offset=[.31,.17];fill.set_projection_parameters(projection)\nassert fill.get_source(C.BaseColor)['resource_id']==asset['id']\nassert fill.get_source(C.Roughness)['resource_id']==asset['id']");break;
 case 7:capture("projected");get<QAction>("undoDocument")->trigger();break;
 case 8:capture("undo");get<QAction>("redoDocument")->trigger();break;
 case 9:capture("redo");command("temporary=bts.layerstack.insert_paint(P.above_node(fill))\ntemporary.delete()\nassert not temporary.is_valid()\ntry:temporary.get_name()\nexcept ValueError:pass\nelse:raise AssertionError('deleted node accepted')\nbts.project.execute_when_not_busy(lambda:bts.project.save_as(root/'layers.blend'))");break;
 case 10:if(!QFileInfo::exists(directory+"/layers.blend")){--phase;return;}capture("saved");command("bts.project.close()");if(!QFile::remove(directory+"/texture.png"))qFatal("Cannot remove own test texture");command("bts.project.open(root/'layers.blend')");break;
 case 11:capture("opened");command("assert fill.name=='API Coat' and levels.name=='API levels' and mask.name=='API mask fill'\nassert fill.active_channels=={C.BaseColor,C.Roughness}\nassert fill.get_source(C.BaseColor)['resource_id']==asset['id']\nassert fill.get_source(C.Roughness)['raw']\nmaterial=next(asset for asset in bts.resource.list_resources() if asset['name']=='Matte Gray')\nfill.set_material_source(material)\nassert fill.source_mode=='material' and fill.get_material_source()\nbts.project.execute_when_not_busy(lambda:bts.project.save_as(root/'material.blend'))");break;
 case 12:if(!QFileInfo::exists(directory+"/material.blend")){--phase;return;}capture("material");command("bts.project.close()\nbts.project.open(root/'material.blend')");break;
 case 13:capture("material-open");command("assert fill.source_mode=='material'\nfill.set_source(C.Roughness,.68)\nassert fill.source_mode=='split'\nassert changed\n(root/'layer-script.json').write_text(json.dumps({'pid':os.getpid(),'errors':errors,'events':len(changed),'fill_id':fill.uid,'group_id':group.uid,'mask_id':mask.uid,'levels_id':levels.uid,'texture_resource':asset['id'],'deleted_node_rejected':True,'cross_stack_rejected':True,'material_source_packed':True},indent=2))");break;
 case 14:capture("split");command("bts.project.close(discard=True)");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }
 }
 public:Demo(QMainWindow *parent,std::function<bool()> pending,std::function<QJsonObject()> snapshot):QObject(parent),window(parent),directory(qEnvironmentVariable("BTS_LAYER_API_DEMO")),mesh(qEnvironmentVariable("BTS_LAYER_API_MESH")),busy(std::move(pending)),state(std::move(snapshot)){if(!state)qFatal("Layer API demo requires the native state hook");QDir().mkpath(directory);QImage image(16,16,QImage::Format_RGBA8888);for(int y=0;y<16;++y)for(int x=0;x<16;++x)image.setPixelColor(x,y,QColor(x*17,y*17,(x+y)%2?230:20));if(!image.save(directory+"/texture.png"))qFatal("Cannot save own test texture");timer.setInterval(300);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_layer_api_demo(QMainWindow *window,std::function<bool()> busy,std::function<QJsonObject()> state){new Demo(window,std::move(busy),std::move(state));}
}
