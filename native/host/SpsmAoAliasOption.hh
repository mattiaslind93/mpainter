#pragma once

#include <QCheckBox>
#include <QStringList>

namespace bts {
// Opt-in native interpretation of direct aliases only; procedural graphs stay
// unsupported. This choice is independent of embedded bitmap decoding.
class SpsmAoAliasOption final : public QCheckBox {
 public:
  explicit SpsmAoAliasOption(QWidget *parent = nullptr)
      : QCheckBox("Direct AO aliases to Color — native interpretation", parent)
  {
    setObjectName("spsmNativeAoAlias");
    setChecked(true);
    setToolTip(
        "Requires baked Ambient Occlusion for every destination tile. "
        "Uses the current texture set's native AO generator with identity "
        "UVs and nearest sampling. Named procedural generators use our native approximations. Source alpha/filter/color semantics and Painter "
        "appearance are unverified.");
  }

  QStringList policy_tags() const
  {
    return isChecked() ?
               QStringList{
                   "spsm:policy:meshmap-ao-color=native-ao-generator-uv-identity-nearest"} :
               QStringList{};
  }
};
}  // namespace bts
