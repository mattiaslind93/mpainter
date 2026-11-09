// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.hh"
#include "BlendLibraryPreferences.hh"
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cassert>
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir directory;assert(directory.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
 {QSettings settings("mPainter","Preferences");settings.setValue("Viewport/textureBudgetMB",1);settings.setValue("TextureImport/decodedBudgetMB",1);}
 QTimer::singleShot(0,&app,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());assert(dialog);for(const auto *name:{"viewportTextureBudgetMB","viewportTextureBudgetAuto","textureLoadingBudgetMB","viewportMaxTextureResolution"})assert(!dialog->findChild<QObject *>(name));dialog->findChild<QDoubleSpinBox *>("viewportClipStart")->setValue(.02);dialog->accept();});
 bts::show_preferences(nullptr,"",false,{},[](auto value){assert(value.valid());});assert(bts::load_viewport_clipping().near_plane==.02);
 QTimer::singleShot(0,&app,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());dialog->findChild<QPushButton *>("restoreViewportClipping")->click();dialog->accept();});
 bts::show_preferences(nullptr,"",false,{},[](auto value){assert(value.valid());});assert(bts::load_viewport_clipping().near_plane==.01);
}
