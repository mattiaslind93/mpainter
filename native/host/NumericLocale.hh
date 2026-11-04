#pragma once
#include <QLocale>
#include <QDoubleSpinBox>
#include <QEvent>
namespace bts {
inline QLocale numeric_locale(){auto locale=QLocale::c();locale.setNumberOptions(QLocale::OmitGroupSeparator|QLocale::RejectGroupSeparator);return locale;}
// Call before constructing QApplication or any UI; filenames, names and dates
// are not rewritten. C's LC_NUMERIC is separately pinned after QApplication.
inline void initialize_numeric_locale(){QLocale::setDefault(numeric_locale());}
inline QString numeric_input(QString text){return text.replace(',', '.');}
class NumericSpinBox final:public QDoubleSpinBox {
 public:
  explicit NumericSpinBox(QWidget *parent=nullptr):QDoubleSpinBox(parent){setLocale(numeric_locale());setGroupSeparatorShown(false);}
  void setSuffix(const QString &suffix){QDoubleSpinBox::setSuffix(suffix);if(suffix.trimmed()=="%")setDecimals(2);}
 protected:
  QValidator::State validate(QString &text,int &position)const override{
   // A comma is a decimal separator, never thousands grouping. Two separators
   // remain invalid; do not silently reinterpret 1,234.56 as another number.
   text=numeric_input(text);return QDoubleSpinBox::validate(text,position);
  }
  double valueFromText(const QString &text)const override{return QDoubleSpinBox::valueFromText(numeric_input(text));}
  QString textFromValue(double value)const override{return numeric_locale().toString(value,'f',suffix().trimmed()=="%"?2:decimals());}
  void fixup(QString &text)const override{text=numeric_input(text);QDoubleSpinBox::fixup(text);}
  bool event(QEvent *event)override{if(event->type()==QEvent::Polish&&suffix().trimmed()=="%"&&decimals()!=2)setDecimals(2);return QDoubleSpinBox::event(event);}
};
}
