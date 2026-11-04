#include "Theme.hh"
#include "SliderHitArea.hh"
#include "NumericLocale.hh"
#include "ColorPicker.hh"
#include "PropertyLatency.hh"
#include "Performance.hh"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabletEvent>
#include <QThreadPool>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include "ReferenceImage.hh"
#include <QImageReader>
#ifndef BTS_STANDALONE_COLOR_PICKER
#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#endif

namespace bts {
namespace {
// Disk synchronization may take hundreds of milliseconds. Keep the latest palette
// in memory and serialize writes on a worker; application shutdown joins it.
class RecentColors final : public QObject {
 public:
  explicit RecentColors(QObject *parent) : QObject(parent) {
    QSettings settings("mPainter", "ColorPicker");
    entries_ = settings.value("recentACESCg").toStringList();
    writer_.setMaxThreadCount(1);
  }
  ~RecentColors() override { writer_.waitForDone(); }
  QStringList entries() {
    std::lock_guard lock(mutex_);
    return entries_;
  }
  void save(QStringList entries) {
    std::lock_guard lock(mutex_);
    entries_ = std::move(entries);
    pending_ = true;
    if (running_) return;
    running_ = true;
    writer_.start([this] {
      for (;;) {
        QStringList next;
        {
          std::lock_guard lock(mutex_);
          if (!pending_) { running_ = false; return; }
          next = entries_;
          pending_ = false;
        }
        performance::Scope timing("color_recent_settings_sync");
        QSettings settings("mPainter", "ColorPicker");
        settings.setValue("recentACESCg", next);
        settings.sync();
        if (settings.status() != QSettings::NoError)
          qWarning("Unable to save recent colors");
      }
    });
  }
 private:
  QThreadPool writer_;
  std::mutex mutex_;
  QStringList entries_;
  bool pending_ = false, running_ = false;
};
RecentColors &recent_colors() {
  static auto *store = new RecentColors(QCoreApplication::instance());
  return *store;
}
}
#ifndef BTS_STANDALONE_COLOR_PICKER
PickerColor picker_from_srgb(const QColor &color){float value[3]{float(color.redF()),float(color.greenF()),float(color.blueF())};blender::IMB_colormanagement_srgb_to_scene_linear_v3(value,value);return {value[0],value[1],value[2],float(color.alphaF())};}
QColor picker_to_srgb(const PickerColor &color){float value[3]{color[0],color[1],color[2]};blender::IMB_colormanagement_scene_linear_to_srgb_v3(value,value);return QColor::fromRgbF(std::clamp(value[0],0.f,1.f),std::clamp(value[1],0.f,1.f),std::clamp(value[2],0.f,1.f),std::clamp(color[3],0.f,1.f));}
#else
PickerColor picker_from_srgb(const QColor &color){
  const auto to_linear=[](float v){return v<=0.04045f?v/12.92f:std::pow((v+0.055f)/1.055f,2.4f);};
  const float r=to_linear(float(color.redF())),g=to_linear(float(color.greenF())),b=to_linear(float(color.blueF()));
  return {0.613097f*r+0.339523f*g+0.047379f*b,
          0.070194f*r+0.916354f*g+0.013452f*b,
          0.020616f*r+0.109570f*g+0.869815f*b,
          float(color.alphaF())};
}
QColor picker_to_srgb(const PickerColor &color){
  const auto to_srgb=[](float v){v=std::clamp(v,0.f,1.f);return v<=0.0031308f?v*12.92f:1.055f*std::pow(v,1.f/2.4f)-0.055f;};
  const float r=1.705051f*color[0]-0.621792f*color[1]-0.083259f*color[2];
  const float g=-0.130256f*color[0]+1.140805f*color[1]-0.010549f*color[2];
  const float b=-0.024003f*color[0]-0.128969f*color[1]+1.152972f*color[2];
  return QColor::fromRgbF(to_srgb(r),to_srgb(g),to_srgb(b),std::clamp(color[3],0.f,1.f));
}
#endif

LoadedReferenceImage load_reference_image(const QString &path) {
  if (path.isEmpty()) {
    return {};
  }
  const QString lower = path.toLower();
  const bool prefer_imbuf = lower.endsWith(".exr") || lower.endsWith(".hdr");
  if (!prefer_imbuf) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (!image.isNull()) {
      return make_reference_image(image, path);
    }
  }
#ifndef BTS_STANDALONE_COLOR_PICKER
  using namespace blender;
  constexpr auto flags = ImBufFlags::AlphaChannelPacked |
                         ImBufFlags::NoColorspaceConvert |
                         ImBufFlags::Metadata;
  const QByteArray utf8 = path.toUtf8();
  std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)> ibuf(
      IMB_load_image_from_filepath(utf8.constData(), flags), IMB_freeImBuf);
  if (ibuf && ibuf->x > 0 && ibuf->y > 0 &&
      (ibuf->float_data() || ibuf->byte_data())) {
    LoadedReferenceImage result;
    result.path = path;
    result.width = ibuf->x;
    result.height = ibuf->y;
    result.linear_rgba.resize(std::size_t(result.width) *
                              std::size_t(result.height) * 4);
    const float *floats = ibuf->float_data();
    const unsigned char *bytes = ibuf->byte_data();
    const int channels = ibuf->channels;
    for (int y = 0; y < result.height; ++y) {
      const int src_y = result.height - 1 - y;
      for (int x = 0; x < result.width; ++x) {
        const std::size_t src_i = std::size_t(src_y) * std::size_t(result.width) + std::size_t(x);
        const std::size_t dst_i = (std::size_t(y) * std::size_t(result.width) + std::size_t(x)) * 4;
        if (floats) {
          const float *v = floats + src_i * channels;
          if (channels < 3) {
            result.linear_rgba[dst_i + 0] = v[0];
            result.linear_rgba[dst_i + 1] = v[0];
            result.linear_rgba[dst_i + 2] = v[0];
            result.linear_rgba[dst_i + 3] = 1.0f;
          } else {
            result.linear_rgba[dst_i + 0] = v[0];
            result.linear_rgba[dst_i + 1] = v[1];
            result.linear_rgba[dst_i + 2] = v[2];
            result.linear_rgba[dst_i + 3] = 1.0f;
          }
        } else {
          const unsigned char *v = bytes + src_i * 4;
          result.linear_rgba[dst_i + 0] = v[0] / 255.0f;
          result.linear_rgba[dst_i + 1] = v[1] / 255.0f;
          result.linear_rgba[dst_i + 2] = v[2] / 255.0f;
          result.linear_rgba[dst_i + 3] = 1.0f;
        }
      }
    }
    const char *from = floats ? IMB_colormanagement_get_float_colorspace(ibuf.get())
                              : IMB_colormanagement_get_byte_colorspace(ibuf.get());
    if (from && *from && std::string(from) != "ACEScg") {
      IMB_colormanagement_transform_float(result.linear_rgba.data(),
                                          result.width,
                                          result.height,
                                          4,
                                          from,
                                          "ACEScg",
                                          false);
    }
    result.display_image =
        QImage(result.width, result.height, QImage::Format_ARGB32);
    for (int y = 0; y < result.height; ++y) {
      QRgb *line = reinterpret_cast<QRgb *>(result.display_image.scanLine(y));
      for (int x = 0; x < result.width; ++x) {
        const std::size_t idx =
            (std::size_t(y) * std::size_t(result.width) + std::size_t(x)) * 4;
        const QColor c = picker_to_srgb({result.linear_rgba[idx + 0],
                                         result.linear_rgba[idx + 1],
                                         result.linear_rgba[idx + 2],
                                         1.0f});
        line[x] = c.rgba();
      }
    }
    return result;
  }
#endif
  QImage fallback(path);
  return make_reference_image(fallback, path);
}

class ColorWheel final:public QWidget {
 public:
  QColor color{Qt::black};
  std::function<void()> started,finished;
  std::function<void(const QColor &)> changed;
  explicit ColorWheel(QWidget *parent):QWidget(parent){new SliderHandleGeometry(this,[this]{const double angle=hue_*2*3.14159265358979323846;const QPointF hue(110+98*std::cos(angle),110-98*std::sin(angle)),sv(50+120*color.hsvSaturationF(),170-120*color.valueF());return QList<QRectF>{QRectF(hue-QPointF(8.5,8.5),QSizeF(17,17)),QRectF(sv-QPointF(6.5,6.5),QSizeF(13,13))};});setObjectName("colorWheel");setFixedSize(220,220);setAttribute(Qt::WA_TabletTracking);}
  void set_color(QColor value){color=value;if(value.hsvHueF()>=0)hue_=value.hsvHueF();update();}
 protected:
  void paintEvent(QPaintEvent *)override {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QPointF center(110,110);
    QConicalGradient ring(center,0);for(int i=0;i<=12;++i)ring.setColorAt(i/12.,QColor::fromHsvF(i==12?0:i/12.,1,1));p.setPen(QPen(QBrush(ring),18));p.setBrush(Qt::NoBrush);p.drawEllipse(center,98,98);
    const QRectF square(50,50,120,120);QLinearGradient saturation(square.topLeft(),square.topRight());saturation.setColorAt(0,Qt::white);saturation.setColorAt(1,QColor::fromHsvF(hue_,1,1));p.fillRect(square,saturation);QLinearGradient value(square.topLeft(),square.bottomLeft());value.setColorAt(0,QColor(0,0,0,0));value.setColorAt(1,Qt::black);p.fillRect(square,value);
    p.setPen(QPen(Qt::black,3));p.setBrush(Qt::NoBrush);const QPointF sv(50+120*color.hsvSaturationF(),170-120*color.valueF());p.drawEllipse(sv,5,5);p.setPen(QPen(Qt::white,1));p.drawEllipse(sv,5,5);
    const double angle=hue_*2*3.14159265358979323846;const QPointF hue(center.x()+98*std::cos(angle),center.y()-98*std::sin(angle));p.setPen(QPen(Qt::black,3));p.drawEllipse(hue,7,7);p.setPen(QPen(Qt::white,1));p.drawEllipse(hue,7,7);
  }
  void mousePressEvent(QMouseEvent *event)override {if(event->button()!=Qt::LeftButton)return;press(event->position());event->accept();}
  void mouseMoveEvent(QMouseEvent *event)override {if(drag_)move(event->position());event->accept();}
  void mouseReleaseEvent(QMouseEvent *event)override {if(drag_){move(event->position());drag_=0;if(finished)finished();}event->accept();}
  void tabletEvent(QTabletEvent *event)override {if(event->type()==QEvent::TabletPress)press(event->position());else if(event->type()==QEvent::TabletMove&&drag_)move(event->position());else if(event->type()==QEvent::TabletRelease&&drag_){move(event->position());drag_=0;if(finished)finished();}event->accept();}
 private:
  double hue_=0;
  int drag_=0;
  void press(QPointF point){const double distance=QLineF(point,{110,110}).length();const auto handles=slider_handles(this);drag_=doubled_handle(handles[0]).contains(point)?1:doubled_handle(handles[1]).contains(point)?2:distance>=86&&distance<=110?1:QRectF(50,50,120,120).contains(point)?2:0;if(drag_){if(started)started();move(point);}}
  void move(QPointF point){double saturation=color.hsvSaturationF(),value=color.valueF();if(drag_==1){hue_=std::atan2(110-point.y(),point.x()-110)/(2*3.14159265358979323846);if(hue_<0)hue_+=1;}else {saturation=std::clamp((point.x()-50)/120.,0.,1.);value=std::clamp((170-point.y())/120.,0.,1.);}color=QColor::fromHsvF(hue_,saturation,value);if(changed)changed(color);update();}
};

ColorPicker::ColorPicker(QWidget *parent):QWidget(parent){
  auto *row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);swatch_=new QPushButton(this);swatch_->setAutoDefault(false);swatch_->setDefault(false);swatch_->setObjectName("colorSwatch");swatch_->setMinimumHeight(28);swatch_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);row->addWidget(swatch_,1);
  popup_=new QDialog(this,Qt::Tool);popup_->setObjectName("colorPickerDialog");popup_->setWindowTitle("Color — sRGB / scene-linear ACEScg");auto *layout=new QVBoxLayout(popup_);
  wheel_=new ColorWheel(popup_);layout->addWidget(wheel_,0,Qt::AlignHCenter);
  auto *swatches=new QHBoxLayout;old_swatch_=new QPushButton("Previous",popup_);new_swatch_=new QPushButton("New",popup_);old_swatch_->setMinimumHeight(35);new_swatch_->setMinimumHeight(35);swatches->addWidget(old_swatch_);swatches->addWidget(new_swatch_);layout->addLayout(swatches);
  auto *hex_row=new QHBoxLayout;hex_row->addWidget(new QLabel("Hex (sRGB)",popup_));hex_=new QLineEdit(popup_);hex_->setObjectName("colorHex");hex_->setMaxLength(7);hex_row->addWidget(hex_);auto *pick=new QPushButton("Pick albedo (P)",popup_);pick->setObjectName("colorEyedropper");pick->setShortcut(QKeySequence(Qt::Key_P));pick->setToolTip("Sample composited scene-linear Base Color at the surface/UV texel; lighting is excluded.");hex_row->addWidget(pick);layout->addLayout(hex_row);
  hdr_=new QLabel(popup_);hdr_->setWordWrap(true);layout->addWidget(hdr_);
  auto *recent_row=new QHBoxLayout;for(int i=0;i<8;++i){recent_buttons_[i]=new QPushButton(popup_);recent_buttons_[i]->setObjectName(QString("recentColor%1").arg(i));recent_buttons_[i]->setFixedSize(25,25);recent_row->addWidget(recent_buttons_[i]);connect(recent_buttons_[i],&QPushButton::clicked,this,[this,i]{if(i<int(recent_.size()))edit(recent_[i]);});}layout->addLayout(recent_row);
  auto *advanced=new QCheckBox("Advanced RGB / HSV / HDR",popup_);advanced->setObjectName("colorAdvanced");layout->addWidget(advanced);auto *fields=new QWidget(popup_);auto *form=new QFormLayout(fields);layout->addWidget(fields);fields->hide();connect(advanced,&QCheckBox::toggled,fields,&QWidget::setVisible);
  for(int group=0;group<3;++group){auto *numbers=new QWidget(fields);auto *line=new QHBoxLayout(numbers);line->setContentsMargins(0,0,0,0);for(int c=0;c<3;++c){auto *spin=new bts::NumericSpinBox(numbers);spin->setLocale(QLocale::c());spin->setRange(group==2?-65504:0,group==2?65504:group==1&&c==0?360:1);spin->setDecimals(group==2?6:4);spin->setSingleStep(group==1&&c==0?1:.01);spin->setKeyboardTracking(false);spin->setMinimumWidth(0);line->addWidget(spin);auto &array=group==0?rgb_:group==1?hsv_:linear_;array[c]=spin;spin->setObjectName(group==2?QString("baseColor%1").arg(c):QString(group==0?"displayRgb%1":"colorHsv%1").arg(c));connect(spin,&QDoubleSpinBox::valueChanged,this,[this,group]{if(updating_)return;if(group==2){auto value=color_;for(int i=0;i<3;++i)value[i]=linear_[i]->value();edit(value);}else if(group==0)edit(picker_from_srgb(QColor::fromRgbF(rgb_[0]->value(),rgb_[1]->value(),rgb_[2]->value())));else edit(picker_from_srgb(QColor::fromHsvF(std::fmod(hsv_[0]->value()/360,1.),hsv_[1]->value(),hsv_[2]->value())));});}form->addRow(group==0?"sRGB 0–1":group==1?"HSV":"Linear ACEScg / HDR",numbers);}
  for(const auto &entry:recent_colors().entries()){const auto parts=entry.split(',');if(parts.size()!=4)continue;PickerColor value;bool valid=true;for(int c=0;c<4;++c){bool ok=false;value[c]=QLocale::c().toFloat(parts[c],&ok);valid&=ok&&std::isfinite(value[c])&&std::abs(value[c])<=65504;}if(valid&&recent_.size()<8)recent_.push_back(value);}
  for(const auto &color:{QColor(Qt::black),QColor(Qt::white),QColor(Qt::red),QColor(Qt::green),QColor(Qt::blue),QColor(Qt::yellow),QColor(Qt::cyan),QColor(Qt::magenta)})if(recent_.size()<8)recent_.push_back(picker_from_srgb(color));
  // Submit the latest value before Qt schedules the next paint. The GPU owner
  // already coalesces requests; even a zero timer here can miss that frame.
  wheel_->started=[this]{begin();};wheel_->changed=[this](const QColor &value){property_event("color");color_=picker_from_srgb(value);synchronize();publish();};wheel_->finished=[this]{end();};
  connect(swatch_,&QPushButton::clicked,this,[this]{old_=color_;synchronize();popup_->show();popup_->raise();});connect(old_swatch_,&QPushButton::clicked,this,[this]{edit(old_);});connect(new_swatch_,&QPushButton::clicked,popup_,&QDialog::hide);
  connect(hex_,&QLineEdit::editingFinished,this,[this]{const QColor value(hex_->text());if(hex_->text().size()==7&&hex_->text().startsWith('#')&&value.isValid())edit(picker_from_srgb(value));else synchronize();});
  connect(pick,&QPushButton::clicked,this,[this]{popup_->hide();if(pick_requested)pick_requested();});
  // Return commits the focused numeric/hex editor. QDialog must not also
  // activate its first automatic default button (Previous) and undo that edit.
  for(auto *button:popup_->findChildren<QPushButton *>()){button->setAutoDefault(false);button->setDefault(false);}
  connect(popup_,&QDialog::finished,this,[this]{if(gesture_)end();});synchronize();
}
void ColorPicker::retire_callbacks()noexcept{
  const auto block=[](auto &&self,QObject *object)noexcept->void{object->blockSignals(true);for(QObject *child:object->children())self(self,child);};
  block(block,this);started={};finished={};pick_requested={};changed={};
  wheel_->started={};wheel_->changed={};wheel_->finished={};
}
ColorPicker::~ColorPicker(){retire_callbacks();}
void ColorPicker::set_working_space_label(const QString &space){
  if(space.isEmpty())throw std::invalid_argument("Color picker requires a recorded working-space label");
  if(property("workingSpaceLabel").toString()==space)return;setProperty("workingSpaceLabel",space);
  popup_->setWindowTitle(QString("Color — sRGB / scene-linear %1").arg(space));
  auto *form=qobject_cast<QFormLayout *>(linear_[0]->parentWidget()->parentWidget()->layout());
  auto *label=qobject_cast<QLabel *>(form->labelForField(linear_[0]->parentWidget()));label->setText(QString("Linear %1 / HDR").arg(space));
  synchronize();
}
void ColorPicker::set_color(const PickerColor &value){if(value==color_)return;color_=value;synchronize();}
void ColorPicker::begin(){if(gesture_)return;gesture_=true;old_=color_;if(started)started();}
void ColorPicker::publish(){if(changed)changed(color_);}
void ColorPicker::end(){performance::Scope timing("color_release");if(!gesture_)return;publish();gesture_=false;save_recent();if(finished){performance::Scope finish_timing("color_release_commit");finished();}}
void ColorPicker::edit(PickerColor value){begin();color_=value;synchronize();end();}
void ColorPicker::accept_sample(const PickerColor &value){edit(value);}
void ColorPicker::save_recent(){recent_.erase(std::remove(recent_.begin(),recent_.end(),color_),recent_.end());recent_.insert(recent_.begin(),color_);recent_.resize(8);QStringList entries;for(const auto &value:recent_){QStringList parts;for(float c:value)parts.append(QString::number(c,'g',9));entries.append(parts.join(','));}recent_colors().save(std::move(entries));{performance::Scope timing("color_recent_ui");synchronize();}}
void ColorPicker::synchronize(){performance::Scope timing("color_picker_sync");
  updating_=true;const auto requested=property("workingSpaceLabel").toString();const auto space=requested.isEmpty()?QStringLiteral("ACEScg"):requested;const auto display=picker_to_srgb(color_);const auto style=[](const QColor &color){return bts::theme::qss(QString("background:%1;color:%2;border:1px solid @Muted@;").arg(color.name(),color.lightnessF()>.5?bts::theme::Black:bts::theme::White));};swatch_->setText(display.name().toUpper());swatch_->setStyleSheet(style(display));new_swatch_->setStyleSheet(style(display));old_swatch_->setStyleSheet(style(picker_to_srgb(old_)));wheel_->set_color(display);{QSignalBlocker block(hex_);hex_->setText(display.name().toUpper());}
  for(int c=0;c<3;++c){QSignalBlocker a(rgb_[c]),b(hsv_[c]),d(linear_[c]);rgb_[c]->setValue(c==0?display.redF():c==1?display.greenF():display.blueF());hsv_[c]->setValue(c==0?std::max(0.,double(display.hsvHueF()))*360:c==1?display.hsvSaturationF():display.valueF());linear_[c]->setValue(color_[c]);}
  const bool hdr=std::any_of(color_.begin(),color_.begin()+3,[](float v){return v<0||v>1;});hdr_->setText((hdr?QString("HDR retained in scene-linear %1. Wheel/hex show a clipped sRGB preview; advanced linear fields retain the stored values."):QString("Wheel, hex and RGB are sRGB. Stored color is scene-linear %1.")).arg(space));for(int i=0;i<8;++i)if(i<int(recent_.size())){recent_buttons_[i]->setStyleSheet(style(picker_to_srgb(recent_[i])));recent_buttons_[i]->setToolTip(picker_to_srgb(recent_[i]).name());}updating_=false;
}
}
