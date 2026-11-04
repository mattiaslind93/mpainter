#pragma once
#include "ShaderInstances.hh"
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>
class QCheckBox;class QComboBox;class QDialog;class QLineEdit;class QDoubleSpinBox;class QPushButton;
namespace bts {
class OpenPbrPanel;class ColorPicker;class WorkspaceShell;class TextureDocument;struct TextureSet;
struct ShaderHooks {
 std::function<TextureDocument *()> document;
 std::function<std::uint32_t()> material;
 std::function<void(const TextureSet &)> apply;
 std::function<void()> dirty,repaint,end_input;
 std::function<void(const QString &)> log;
};
// Shader history is independent of painting history; snapshots contain no pixels.
class ShaderController final:public QObject {
 public:
 ShaderController(WorkspaceShell *,ShaderHooks);
 void sync();void finish();void reset_document();
 private:
 WorkspaceShell *shell_;ShaderHooks hooks_;QDialog *dialog_;QLineEdit *name_;QDoubleSpinBox *normal_,*ao_;QPushButton *undo_,*redo_;QComboBox *workflow_;OpenPbrPanel *openpbr_;QCheckBox *subsurface_enabled_;QComboBox *subsurface_type_;QDoubleSpinBox *subsurface_scale_;ColorPicker *subsurface_color_;QWidget *subsurface_fields_;QTimer frame_,idle_;
 QUuid project_,selected_;std::optional<ShaderInstances> before_;std::vector<ShaderInstances> undo_history_,redo_history_;bool syncing_=false;
 const TextureSet *selected()const;void apply();void change(const std::function<void(ShaderInstances &)> &,bool immediate);void history(bool redo);void menu(int row);void rows();
};
}
