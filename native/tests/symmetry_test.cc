#include "SymmetryJSON.hh"
#include <QJsonDocument>
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main()
{
  // New/New Project uses an empty SymmetrySettings; existing metadata retains
  // its explicitly authored axis rather than adopting the new default.
  const bts::SymmetrySettings fresh;
  assert(fresh.axis==0&&bts::symmetry_json(fresh)["axis"].toInt()==0);
  const auto new_project=bts::read_symmetry_json(bts::symmetry_json(fresh));
  assert(new_project.axis==0);
  auto legacy=bts::symmetry_json(fresh);legacy["axis"]=1;legacy["enabled"]=true;
  const auto restored=bts::read_symmetry_json(QJsonDocument::fromJson(QJsonDocument(legacy).toJson()).object());
  assert(restored.axis==1&&bts::symmetry_json(restored)==legacy);
  const auto mirrored=bts::symmetry_transforms(restored).front().point(std::array<double,3>{1,2,3});
  assert((mirrored==std::array<double,3>{1,-2,3}));
  auto invalid=legacy;invalid["axis"]=3;bool bad_axis=false;
  try{bts::read_symmetry_json(invalid);}catch(const std::invalid_argument &){bad_axis=true;}assert(bad_axis);
  bts::SymmetrySettings s;assert(bts::symmetry_transforms(s).empty());s.enabled=true;
  for(int axis=0;axis<3;++axis){s.axis=axis;s.offset=.75;const auto t=bts::symmetry_transforms(s).front();const std::array<double,3> p{.2,-.4,1.2};const auto mirrored=t.point(p),restored=t.point(mirrored);for(int i=0;i<3;++i){assert(std::abs(restored[i]-p[i])<1e-12);assert(std::abs(mirrored[i]-(i==axis?1.5-p[i]:p[i]))<1e-12);}assert(t.orientation==-1);}
  s.mode=bts::SymmetryMode::Radial;s.copies=4;s.center={1,2,3};
  for(int axis=0;axis<3;++axis){s.axis=axis;const auto transforms=bts::symmetry_transforms(s);assert(transforms.size()==3);for(const auto &t:transforms){const auto center=t.point(s.center);for(int i=0;i<3;++i)assert(std::abs(center[i]-s.center[i])<1e-12);const std::array<double,3> v{.4,.7,-.3};const auto rotated=t.vector(v);double before=0,after=0;for(int i=0;i<3;++i){before+=v[i]*v[i];after+=rotated[i]*rotated[i];}assert(std::abs(before-after)<1e-12&&t.orientation==1);}}
  s.copies=1;bool rejected=false;try{bts::symmetry_transforms(s);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
  std::puts("symmetry: new project X, legacy saved Y roundtrip, world-offset mirror involution on XYZ, reflected handedness, radial center invariance, rigid lengths, exact copy count and invalid-count rejection PASS");
}
