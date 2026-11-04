#pragma once
#include <QString>
namespace bts {
// These indices match Resource Import's existing Usage choices. Source preset
// families must enter preset hydration, never the image/alpha decode branch.
inline int resource_preset_usage(const QString &filename){
 if(filename.endsWith(".btsmaterial",Qt::CaseInsensitive)||filename.endsWith(".spsm",Qt::CaseInsensitive))return 3;
 if(filename.endsWith(".btsbrush",Qt::CaseInsensitive))return 5;
 if(filename.endsWith(".btsmask",Qt::CaseInsensitive)||filename.endsWith(".spmsk",Qt::CaseInsensitive))return 4;
 return -1;
}
inline bool resource_preset_usage_matches(const QString &filename,int usage){const int expected=resource_preset_usage(filename);return expected<0||expected==usage;}
}
