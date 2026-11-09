// SPDX-License-Identifier: GPL-3.0-or-later
// Link this driver with the host's native objects/libraries, replacing main.cc.o.
// It uses actual Qt widgets and embedded CPython; no Blender GPU or desktop window.
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "PythonConsole.hh"
#include "PythonUi.hh"
#include "ResourceStack.hh"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QTimer>
#include <QTextDocument>
#include <cassert>
#include <cstdio>

int main(int argc,char **argv){
  assert(argc==2);const QString directory=QString::fromLocal8Bit(argv[1]);QDir().mkpath(directory);
  qputenv("QT_QPA_PLATFORM","offscreen");qputenv("XDG_CONFIG_HOME",(directory+"/config").toUtf8());
  QApplication app(argc,argv);Py_Initialize();
  {
    QMainWindow window;
    bts::PythonConsole console(&window,{
      [](const QString &operation,const QJsonObject &data)->QJsonValue{
        if(operation=="echo")return data["payload"].toString().size();
        if(operation=="project.status")return QJsonObject{{"is_open",false},{"is_busy",false}};
        throw std::invalid_argument("Unexpected test hook");
      },[]{return false;},directory+"/plugins"});
    const auto run=[&](const QString &source){
      if(!console.execute(source)){const auto output=window.findChild<QPlainTextEdit *>("pythonOutput")->toPlainText();std::fprintf(stderr,"Python resource test failed: %s\n",qPrintable(output));std::abort();}
    };
    run(R"PY(import bts, _bts_host, json
assert json.loads(_bts_host.call('echo',json.dumps({'payload':'x'*(3*1024*1024)})))['result']==3*1024*1024
long_text='x'*(96*1024)
dock=bts.ui.add_dock_widget(bts.ui.Panel(bts.ui.LineEdit(long_text,object_name='large_edit'),bts.ui.TextView('\n'.join(str(n) for n in range(3001)),object_name='large_view')))
assert dock.find_child('large_edit').text()==long_text
assert dock.find_child('large_view').text().startswith('0\n1\n')
assert dock.find_child('large_view').text().endswith('\n3000')
wide=bts.ui.add_dock_widget(bts.ui.Panel(*(bts.ui.Label(str(n)) for n in range(256))))
nested=bts.ui.Label('leaf',object_name='nested_leaf')
for _ in range(48): nested=bts.ui.Panel(nested)
deep=bts.ui.add_dock_widget(nested)
assert deep.find_child('nested_leaf').text()=='leaf'
menu=bts.ui.add_menu('Many actions',object_name='a'*256)
actions=[bts.ui.add_action(menu,str(n),lambda:None) for n in range(600)]
assert all(item.is_valid() for item in actions)
deferred=[]
for n in range(256): bts.project.execute_when_not_busy(lambda n=n:deferred.append(n))
assert len(bts._pending)==256
bts._drain_ready()
assert deferred==list(range(256))
received=[]
bts.event.DISPATCHER.connect_strong(bts.event.ProjectSaved,lambda event:received.append(event.sequence))
)PY");
    for(int n=0;n<3000;++n)console.notify("ProjectSaved",{{"sequence",n}});
    QEventLoop loop;QTimer::singleShot(80,&loop,&QEventLoop::quit);loop.exec();
    run("assert received==list(range(3000))");
    auto *output=window.findChild<QPlainTextEdit *>("pythonOutput");
    QString lines;for(int n=0;n<3001;++n)lines+=QString::number(n)+'\n';console.output(lines);
    assert(output->toPlainText().startsWith("0\n1\n")&&output->document()->blockCount()>3000);
    bts::PythonUi ui(&window,[](const auto &,const auto &){});
    {
      bts::TextureRamQueryScope empty([]()->std::uint64_t{return 0;});
      ui.call("ui.create",{{"kind","menu"},{"title","test"},{"owner","test"},{"object_name","oom_test"}});
      console.notify("ProjectSaved",{{"sequence",3000}});console.output("Available memory does not block output");
      run("_bts_host.call('echo','{\"payload\":\"data\"}')");
    }
    const auto stack=bts::resource_stack_range();assert(stack.high>stack.low);
    bool rejected=false;try{bts::require_resource_stack(stack.high-stack.low,"Stack test");}catch(const std::length_error &){rejected=true;}assert(rejected);
  }
  Py_Finalize();std::puts("Python resources: 3 MiB JSON, 96 KiB edit, 256 children, 48 levels, 600 handles, 256 callbacks, 3000 events and full text; low-RAM admission and actual stack safety PASS");
}
