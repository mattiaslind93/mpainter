#include "NativeMaterialImageRefresh.hh"
#include "NativeMaterialImagePanel.hh"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <iostream>
using namespace bts;
namespace {void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char **argv){
 QApplication app(argc,argv);
 try{
  auto result=std::make_shared<NativeImageBindingAvailability>();NativeImageBindingStatus color;color.binding.image="Original.png";color.binding.output="Color";color.binding.input="Color1";color.binding.consumer_type="ShaderNodeMixRGB";color.binding.consumer_identifier=123;color.binding.node_identifier=11;color.binding.source_settings=QJsonObject{{"image_source",1},{"image_type",0},{"alpha_mode",0},{"high_precision",true},{"multiview",false},{"source_space","sRGB"},{"interpolation",0},{"projection",0},{"projection_blend",0.0},{"extension",0}};color.active_channels=QJsonArray{QJsonObject{{"channel",0},{"uses",1}}};result->bindings.push_back(color);auto unsupported=color;unsupported.reason="Internal Normal is unsupported for this exact link";unsupported.active_channels={};result->bindings.push_back(unsupported);
  NativeMaterialImagePanel panel;panel.set_color_spaces({"sRGB","Non-Color"});panel.synchronize(result,true);
  auto *slot=panel.findChild<QComboBox *>("nativeImageSlot"),*space=panel.findChild<QComboBox *>("nativeImageSourceSpace");auto *file=panel.findChild<QPushButton *>("nativeImageFile"),*shelf=panel.findChild<QPushButton *>("nativeImageShelf"),*reset=panel.findChild<QPushButton *>("nativeImageResetAll");
  require(!file->isEnabled()&&!shelf->isEnabled(),"Unknown policy enabled image replacement");int choices=0,reset_count=0,target_changes=0,source_changes=0;panel.requested=[&](const auto &binding,bool from_file,const auto &policy){require(binding.image=="Original.png"&&from_file&&policy.source_space==NativeImageSourceSpace::OriginalDeclared,"Wrong native image request");++choices;};panel.reset_requested=[&]{++reset_count;};panel.target_changed=[&]{++target_changes;};panel.source_changed=[&]{++source_changes;};
  space->setCurrentIndex(1);file->click();require(choices==1,"File action missing");slot->setCurrentIndex(1);require(!file->isEnabled()&&!shelf->isEnabled()&&!panel.findChild<QLabel *>("nativeImageStatus")->text().isEmpty(),"Unavailable link was enabled or unexplained");slot->setCurrentIndex(0);reset->click();require(reset_count==1,"Section Reset missing");panel.set_busy(true);require(!file->isEnabled()&&!reset->isEnabled(),"Busy controls enabled");
  panel.target_context("set/layer");panel.target_context("set/layer");panel.target_context("set/other-layer");require(target_changes==2,"Target changes not tracked exactly");panel.source_context("source-uuid",true);panel.source_context("source-uuid",true);QApplication::processEvents();require(source_changes==1,"Refresh repeated unchanged source inspection");panel.source_context({},false);require(panel.isHidden(),"Native controls shown without a source");
  panel.set_busy(false);panel.source_context("source-uuid",true);panel.setFixedWidth(260);panel.show();QApplication::processEvents();require(panel.minimumSizeHint().width()<=300,"Image slot controls force narrow Properties growth");if(argc>1)require(panel.grab().save(QString::fromLocal8Bit(argv[1])),"Cannot save private panel screenshot");
  panel.synchronize(result,true);space->setCurrentIndex(1);require(file->isEnabled(),"Valid ordinary source unexpectedly disabled");
  const int choices_before=choices,resets_before=reset_count;int inspections=0;
  if(!restrict_native_image_refresh(panel,NativeHeightPolicy::RawAuthoredFloatV1))++inspections;
  require(inspections==0&&!panel.property("nativeImageInspecting").toBool()&&!file->isEnabled()&&!shelf->isEnabled()&&!reset->isEnabled(),"Passive RawHeight refresh enabled editing or inspection");
  require(panel.findChild<QLabel *>("nativeImageStatus")->text().contains("Disable raw authored Height"),"Passive restriction lacks a local explanation");file->click();shelf->click();reset->click();require(choices==choices_before&&reset_count==resets_before,"Passive restriction emitted an image mutation");
  bool rejected=false;try{require_native_height_image_replacement(NativeHeightPolicy::RawAuthoredFloatV1);}catch(const std::invalid_argument &){rejected=true;}require(rejected,"Actual RawHeight image mutation guard was relaxed");
  require(!restrict_native_image_refresh(panel,NativeHeightPolicy::None),"Ordinary image inspection became restricted");
  QApplication::processEvents();const int refreshes_before=source_changes;
  panel.loading("pending same-source inspection");require(panel.property("nativeImageInspecting").toBool(),"Pending-inspection fixture missing");
  panel.source_context("source-uuid",true,NativeHeightPolicy::RawAuthoredFloatV1);
  require(!panel.property("nativeImageInspecting").toBool()&&!file->isEnabled()&&!shelf->isEnabled()&&!reset->isEnabled(),"Same-source None-to-Raw left stale inspection or editable controls");
  QApplication::processEvents();require(source_changes==refreshes_before+1,"Same-source Height policy change did not invalidate inspection");
  panel.source_context("source-uuid",true,NativeHeightPolicy::RawAuthoredFloatV1);QApplication::processEvents();require(source_changes==refreshes_before+1,"Unchanged Raw policy repeated inspection");
  // An old completion must preserve the new local restriction, not revive slots.
  if(!restrict_native_image_refresh(panel,NativeHeightPolicy(panel.property("nativeImageHeightPolicy").toInt())))panel.set_busy(false);
  require(panel.findChild<QLabel *>("nativeImageStatus")->text().contains("Disable raw authored Height")&&!file->isEnabled(),"Old completion lost the current restriction");
  panel.source_context("source-uuid",true,NativeHeightPolicy::None);QApplication::processEvents();require(source_changes==refreshes_before+2,"Same-source Raw-to-None did not request fresh inspection");
  panel.synchronize(result,true);space->setCurrentIndex(1);require(file->isEnabled()&&reset->isEnabled(),"Disabling Raw Height failed to restore legitimate image controls");
  std::cout<<"NATIVE_IMAGE_PANEL_PASS\n";return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
