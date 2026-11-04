#pragma once
#include <QString>
namespace bts {
// Test dispatcher only. Called by --cpu-native-material-test, never by editor
// callbacks, rendering, image imports or interactive composition.
void run_ordinary_color_source_cpu_regression(const QString &directory);
void run_spsm_color_lowering_cpu_regression();
}
