// SPDX-License-Identifier: GPL-3.0-or-later
#include "UserSessionGuard.hh"
#include <QTemporaryDir>
#include <cassert>
int main(int argc,char **argv){QCoreApplication app(argc,argv);assert(!bts::gui_test_arguments({"app","--workspace","/artist/demo.blend"}));assert(bts::gui_test_arguments({"app","--gui-fix-demo","/test"}));QTemporaryDir root;assert(root.isValid());const auto add=[&](int id,const QByteArray &args,const QByteArray &config){const auto path=root.path()+"/"+QString::number(id);assert(QDir().mkpath(path));for(const auto &entry:{std::pair{QString("comm"),QByteArray("bts-host\n")},std::pair{QString("cmdline"),args},std::pair{QString("environ"),QByteArray("XDG_CONFIG_HOME=")+config+QByteArray(1,'\0')}}){QFile f(path+"/"+entry.first);assert(f.open(QIODevice::WriteOnly));f.write(entry.second);}};
 add(11,QByteArray("bts-host\0--workspace",20),"/tmp/mpainter-config");add(12,QByteArray("bts-host\0--workspace\0--gui-fix-demo",35),"/mpainter-test-sessions/demo/config");add(13,QByteArray("bts-host\0--workspace\0--gui-fix-demo",35),"/tmp/mpainter-config");auto pids=bts::user_session_pids(root.path());assert(pids.size()==2&&pids.contains(11)&&pids.contains(13));
#ifdef Q_OS_LINUX
 const auto server=root.path()+"/21",locks=root.path()+"/locks";assert(QDir().mkpath(server)&&QDir().mkpath(locks));const auto write=[&](const QString &path,const QByteArray &bytes){QFile f(path);assert(f.open(QIODevice::WriteOnly));f.write(bytes);};write(server+"/comm","Xvfb\n");QByteArray command("/usr/bin/Xvfb");command.append('\0');command.append(":97");command.append('\0');write(server+"/cmdline",command);write(server+"/status","PPid:\t"+QByteArray::number(::getppid())+"\n");write(locks+"/.X97-lock","21\n");qputenv("QT_QPA_PLATFORM","xcb");qunsetenv("WAYLAND_DISPLAY");qputenv("DISPLAY",":97");qputenv("BTS_PRIVATE_XVFB_PID","21");qputenv("XDG_CONFIG_HOME",(root.path()+"/mpainter-test-sessions/config").toUtf8());assert(bts::owned_private_xvfb(root.path(),locks));qputenv("QT_QPA_PLATFORM","offscreen");assert(bts::owned_private_xvfb(root.path(),locks));qputenv("QT_QPA_PLATFORM","wayland");assert(!bts::owned_private_xvfb(root.path(),locks));qputenv("QT_QPA_PLATFORM","xcb");qputenv("DISPLAY",":0");assert(!bts::owned_private_xvfb(root.path(),locks));qputenv("DISPLAY",":1");assert(!bts::owned_private_xvfb(root.path(),locks));qputenv("DISPLAY",":97");write(locks+"/.X97-lock","22");assert(!bts::owned_private_xvfb(root.path(),locks));write(locks+"/.X97-lock","21");write(server+"/status","PPid:\t1\n");assert(!bts::owned_private_xvfb(root.path(),locks));write(server+"/status","PPid:\t"+QByteArray::number(::getppid())+"\n");qputenv("WAYLAND_DISPLAY","wayland-0");assert(!bts::owned_private_xvfb(root.path(),locks));qunsetenv("WAYLAND_DISPLAY");qputenv("XDG_CONFIG_HOME","/artist/.config");assert(!bts::owned_private_xvfb(root.path(),locks));
#endif
 assert(bts::cpu_test_arguments({"app","--cpu-native-material-test","/tmp/proof"}));
 assert(!bts::cpu_test_arguments({"app","--cpu-unknown"}));
 assert(!bts::cpu_test_arguments({"app","--headless-viewport-test","/tmp/proof"}));
 assert(!bts::cpu_test_arguments({"app","--cpu-export-test","/tmp/proof","--workspace"}));
 assert(!bts::cpu_test_arguments({"app","--cpu-export-test","/tmp/proof","--gui-fix-demo","/tmp/proof"}));
 assert(bts::graphics_session_pids(root.path()).contains(12)); // A private display still shares VRAM/BAR.
 qputenv("BTS_NO_BACKGROUND_GPU","1");qputenv("QT_QPA_PLATFORM","offscreen");
 char executable[]="app",headless[]="--headless-viewport-test",directory[]="/tmp/proof";char *gpu_args[]{executable,headless,directory};
 assert(!bts::prepare_test_session(3,gpu_args));
 char cpu[]="--cpu-native-material-test";char *cpu_args[]{executable,cpu,directory};
 qputenv("BTS_ROOT","/read-only-install");qputenv("XDG_CACHE_HOME",(root.path()+"/local-cache").toUtf8());qputenv("XDG_CONFIG_HOME","/artist/config");qputenv("BTS_RECOVERY_DIR","/artist/recovery");
 assert(bts::prepare_test_session(3,cpu_args));
 assert(qEnvironmentVariable("XDG_CONFIG_HOME").startsWith(root.path()+"/local-cache/"));
 assert(qEnvironmentVariable("BTS_RECOVERY_DIR").startsWith(root.path()+"/local-cache/"));
 assert(!qEnvironmentVariable("BTS_RECOVERY_DIR").startsWith("/read-only-install"));
 qputenv("QT_QPA_PLATFORM","xcb");qunsetenv("QT_QPA_OFFSCREEN_NO_GLX");
 assert(bts::prepare_test_session(3,cpu_args));
 assert(qEnvironmentVariable("QT_QPA_PLATFORM")=="offscreen"&&qEnvironmentVariable("QT_QPA_OFFSCREEN_NO_GLX")=="1");
 add(14,QByteArray("mpainter\0",9),"/artist/config");
 {QFile name(root.path()+"/14/comm");assert(name.open(QIODevice::WriteOnly|QIODevice::Truncate));name.write("mpainter\n");}
 assert(bts::user_session_pids(root.path()).contains(14));assert(bts::graphics_session_pids(root.path()).contains(14));
 return 0;}
