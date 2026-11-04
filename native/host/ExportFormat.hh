#pragma once
#include <optional>
namespace bts {
// Only valid format/precision pairs. Null means inherit the global encoding.
enum class ExportFormat { Png8, Png16, Exr16, Exr32, Jpeg8 };
inline constexpr const char *export_format_labels[]{"PNG 8 bits","PNG 16 bits","EXR 16f (half)","EXR 32f (float)","JPEG 8 bits"};
}
