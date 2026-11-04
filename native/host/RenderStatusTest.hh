#pragma once
#include "RenderController.hh"
#include "WorkspaceShell.hh"
#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QStatusBar>
#include <stdexcept>
namespace bts {
inline int run_render_status_test() {
 QSettings().setValue("render/force_cpu",false);
 WorkspaceShell shell(new QWidget);
 QJsonObject state{{"available",true},{"device","CUDA"},{"samples",8},{"status","Rendering"}};
 bool fail_start=false;
 RenderHooks hooks;
 hooks.document=[] {return true;};hooks.end_input=[]{};
 hooks.backend=[&](bool enabled,bool cpu){if(fail_start)throw std::runtime_error("CUDA initialization rejected");if(enabled&&cpu){state["device"]="CPU";state["cpu_fallback"]=false;state["error"]="";state["status"]="Rendering on CPU";}};
 hooks.state=[&]{return state;};hooks.quality=[](int,bool){};
 hooks.layout=[] {return ViewLayoutState{};};hooks.set_layout=[](ViewLayoutState){};
 hooks.rendering=[](bool){};hooks.repaint=[]{};hooks.dirty=[]{};hooks.log=[](const QString &){};
 RenderController controller(&shell,std::move(hooks));
 auto require=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
 controller.set_active(true);
 auto *warning=shell.findChild<QLabel *>("cyclesCpuFallbackWarning");
 require(warning&&warning->isHidden(),"Unexpected initial warning");
 state["error"]="Failed to load CUDA kernel: CUDA error 222";
 controller.set_active(true);
 require(!warning->isHidden()&&warning->text().contains("222"),"CUDA error missing from rendering panel");
 require(shell.statusBar()->currentMessage().contains("222"),"CUDA error missing from status bar");
 shell.findChild<QComboBox *>("renderDevice")->setCurrentIndex(1);
 require(warning->isHidden(),"Recovered CPU selection retained a stale error");
 state["cpu_fallback"]=true;state["device_message"]="CUDA unavailable; using CPU (slower). No CUDA GPU was detected.";
 controller.set_active(true);
 require(!warning->isHidden()&&warning->text().contains("using CPU"),"CPU fallback missing from rendering panel");
 require(shell.statusBar()->currentMessage().contains("using CPU"),"CPU fallback missing from status bar");
 controller.set_active(false);fail_start=true;controller.set_active(true);
 require(!controller.active()&&shell.statusBar()->currentMessage().contains("initialization rejected"),"Mode-switch exception was only logged");
 QSettings().setValue("render/force_cpu",false);
 std::fprintf(stdout,"Cycles render status / error / CPU fallback: PASS\n");
 return 0;
}
}
