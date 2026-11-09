#pragma once
#include <QMessageBox>
#include <QPushButton>
namespace bts {
enum class ProjectSaveChoice { Overwrite, SaveAs, Cancel };
inline ProjectSaveChoice ask_project_save_conflict(QWidget *parent, const QString &path)
{
  QMessageBox box(
      QMessageBox::Warning,
      "Project changed on disk",
      path +
          "\n\nThe file was changed, replaced or deleted since Open or the last Save. Overwriting may replace another artist's work.",
      QMessageBox::NoButton,
      parent);
  box.setObjectName("projectSaveConflict");
  auto *overwrite = box.addButton("Overwrite", QMessageBox::DestructiveRole);
  auto *save_as = box.addButton("Save As…", QMessageBox::ActionRole);
  auto *cancel = box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(cancel);
  box.setEscapeButton(cancel);
  box.exec();
  return box.clickedButton() == overwrite ? ProjectSaveChoice::Overwrite :
         box.clickedButton() == save_as   ? ProjectSaveChoice::SaveAs :
                                            ProjectSaveChoice::Cancel;
}
}  // namespace bts
