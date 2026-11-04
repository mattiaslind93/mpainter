#pragma once
#include "MaterialLayeringAuthoring.hh"
#include <QDialog>
#include <functional>
class QCheckBox;class QComboBox;class QDoubleSpinBox;class QLabel;class QPushButton;class QTimer;
namespace bts {
class ColorPicker;
// All edits and decoded maps stay here until explicit Apply publication.
class MaterialLayeringPanel final:public QDialog {
 public:
 explicit MaterialLayeringPanel(QWidget *parent=nullptr);
 ~MaterialLayeringPanel()override;
 void retire_callbacks()noexcept;
 void reject()override;void accept()override;
 void load(const TextureSet &,const QString &working_space);
 void install_texture(int input,int channel,std::shared_ptr<const FillTexture>);
 void load_status(int input,int channel,const QString &,bool loading,bool error);
 void show_error(const QString &);
 std::function<void(bool,const std::array<MaterialValues,4>&)> apply_requested;
 std::function<bool(bool,const std::array<LayerMaterialInput,4>&)> apply_inputs_requested;
 std::function<void(int,int,bool,const std::string &)> source_requested;
 std::function<void(int,int)> source_cleared;
 // Owner cleanup runs once before retirement blocks Qt's finished signal.
 std::function<void()> completed;
 private:
 QCheckBox *enabled_;QComboBox *input_,*channel_,*space_;ColorPicker *color_;std::array<QDoubleSpinBox *,3> fields_;std::array<QDoubleSpinBox *,5> mapping_;
 QLabel *message_,*source_;QPushButton *apply_;QWidget *editor_;QTimer *apply_poll_;
 std::array<LayerMaterialInput,4> inputs_;std::array<std::string,4> spaces_;std::array<std::array<bool,6>,4> loading_{};
 int current_=0;bool syncing_=false,applying_=false,retired_=false;
 void store();void show_input(int);void show_source();void apply();void finish_apply();
};
}
