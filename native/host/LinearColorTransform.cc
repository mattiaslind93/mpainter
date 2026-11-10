// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinearColorTransform.hh"
#include "IMB_colormanagement.hh"
#include <QCoreApplication>
#include <QThread>
#include <algorithm>

namespace bts {
std::shared_ptr<const LinearColorTransform> prepare_linear_color_transform(LinearColorInput input)
{
  if (auto *app = QCoreApplication::instance(); app && QThread::currentThread() == app->thread())
    throw std::logic_error("Named linear color preparation requires a worker");
  if (input != LinearColorInput::Rec709 && input != LinearColorInput::ACEScg)
    throw std::invalid_argument("Choose an explicit supported linear input space");
  using namespace blender;
  const auto named = [](const char *name, const char *interop) {
    auto *space = IMB_colormanagement_space_get_named(name);
    if (!space)
      space = IMB_colormanagement_space_from_interop_id(interop);
    if (!space || IMB_colormanagement_space_is_data(space))
      throw std::runtime_error("Required named linear color space is unavailable");
    return std::string(IMB_colormanagement_colorspace_get_name(space));
  };
  auto result = std::make_shared<LinearColorTransform>();
  result->input = input;
  result->input_space = input == LinearColorInput::Rec709 ?
                            named("Linear Rec.709", "lin_rec709_scene") :
                            named("ACEScg", "lin_ap1_scene");
  const auto target = named("ACEScg", "lin_ap1_scene");
  for (int column = 0; column < 3; ++column) {
    float basis[4]{0, 0, 0, 1};
    basis[column] = 1;
    IMB_colormanagement_transform_v4(basis, result->input_space.c_str(), target.c_str());
    for (int row = 0; row < 3; ++row)
      result->to_aces[column * 3 + row] = basis[row];
  }
  // Only a homogeneous linear processor fits this engine stage. The names are
  // explicit policy choices; reject a configuration that gives them other math.
  for (const auto &probe : {std::array<float, 3>{0, 0, 0},
                            std::array<float, 3>{-.5f, .125f, 2.5f},
                            std::array<float, 3>{3, -2, .25f}})
  {
    std::array<float, 4> actual{probe[0], probe[1], probe[2], 1};
    IMB_colormanagement_transform_v4(actual.data(), result->input_space.c_str(), target.c_str());
    const auto expected = linear_color_value(*result, probe);
    for (int c = 0; c < 3; ++c)
      if (!std::isfinite(actual[c]) ||
          std::abs(actual[c] - expected[c]) > 2e-5f * std::max(1.f, std::abs(actual[c])))
        throw std::runtime_error("Chosen color processor is not a homogeneous linear transform");
  }
  result->validate();
  return result;
}
}  // namespace bts
