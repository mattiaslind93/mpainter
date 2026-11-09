// SPDX-License-Identifier: GPL-3.0-or-later
#include "FlattenExport.hh"
#include "FlattenLayer.hh"
#include "ExportNaming.hh"
#include "TexturePadding.hh"
#include "EditTiming.hh"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include "FileDialogs.hh"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QJsonObject>
#include <QJsonArray>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <mutex>
#include "TextureMemory.hh"
#include <thread>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "DNA_scene_types.h"
#include "IMB_colormanagement.hh"
namespace bts {
namespace {
class Wait final : public QDialog {
 public:
  std::atomic_bool cancelled = false;
  using QDialog::QDialog;
  void reject() override
  {
    cancelled = true;
  }
};
using Buffer = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
float background(const TextureSet &set, int c)
{
  return c == 1  ? set.base_values.metallic :
         c == 2  ? set.base_values.roughness :
         c == 4  ? set.base_values.bump :
         c == 5  ? set.base_values.ambient_occlusion :
         c == 14 ? set.displacement.midlevel :
                   0;
}
ChannelPrecision precision(const TextureSet &set, int c)
{
  return c < 6   ? set.precision[c] :
         c == 14 ? set.displacement.precision :
                   set.users[c - 6].precision;
}
bool floating(ChannelPrecision p)
{
  return p == ChannelPrecision::L16F || p == ChannelPrecision::RGB16F ||
         p == ChannelPrecision::L32F || p == ChannelPrecision::RGB32F;
}
bool high(ChannelPrecision p)
{
  return p != ChannelPrecision::L8 && p != ChannelPrecision::RGB8 && p != ChannelPrecision::SRGB8;
}
QString suffix(const TextureSet &set, int c)
{
  if (c == 0)
    return "color";
  if (c == 1)
    return "metallic";
  if (c == 2)
    return "roughness";
  if (c == 3)
    return "normal";
  if (c == 4)
    return "bump";
  if (c == 5)
    return "ambient_occlusion";
  if (c == 14)
    return "displacement";
  return canonical_export_suffix(QString::fromStdString(set.users[c - 6].name));
}
QString filename(QString pattern,
                 const TextureSet &set,
                 const TextureLayer &layer,
                 int c,
                 int tile,
                 ChannelPrecision p)
{
  const bool udim = set.tiles != std::set<int>{1001};
  pattern.replace("(.$udim)", udim ? ".$udim" : "");
  pattern.replace("$layerName", "$mesh");
  pattern.replace("$srcMap", "$channel");
  const auto name = export_output_name(pattern,
                                       QString::fromStdString(layer.name) + ".obj",
                                       QString::fromStdString(set.name),
                                       c < 0 ? "mask" : suffix(set, c),
                                       tile,
                                       floating(p) ? "exr" : "png");
  if (name.contains('/'))
    throw std::invalid_argument("Use a filename pattern without subdirectories");
  return name;
}
QByteArray encoded(const FlattenImage &source,
                   std::span<const unsigned char> coverage,
                   ChannelPrecision p,
                   bool color,
                   bool scalar,
                   float background = 0)
{
  using namespace blender;
  const auto count = std::size_t(source.width) * source.height;
  if (coverage.size() != count)
    throw std::invalid_argument("Flatten export UV coverage mismatch");
  Buffer image(IMB_allocImBuf(source.width, source.height, ImBufFlags::FloatData), IMB_freeImBuf);
  if (!image)
    throw std::bad_alloc();
  auto *data = image->float_data_for_write();
  for (std::size_t i = 0; i < count; ++i) {
    const auto &pixel = (*source.pixels)[i];
    const float alpha = coverage[i] ? pixel[3] : 0;
    for (int c = 0; c < 3; ++c)
      data[i * 4 + c] = pixel[c] * alpha;
    data[i * 4 + 3] = alpha;
  }
  // Protect every island texel, including transparent masked texels. Extend only
  // outside the UV islands, with exactly the documented one-pixel margin.
  std::vector<char> mask(count);
  for (std::size_t i = 0; i < count; ++i)
    mask[i] = coverage[i] ? FILTER_MASK_USED : 0;
  IMB_filter_extend(image.get(), mask.data(), 1);
  for (std::size_t i = 0; i < count; ++i) {
    const auto at = i * 4;
    const float alpha = data[at + 3];
    for (int c = 0; c < 3; ++c)
      data[at + c] = alpha > 1e-12f ? data[at + c] / alpha : 0;
    if (scalar) {
      const float visible = std::lerp(background, data[at], alpha);
      data[at] = data[at + 1] = data[at + 2] = visible;
      data[at + 3] = 1;
    }
    if (color && !floating(p))
      IMB_colormanagement_scene_linear_to_srgb_v3(data + at, data + at);
  }
  image->color_mode = scalar ? ImColorMode::BW : ImColorMode::RGBA;
  image->ftype = floating(p) ? IMB_FTYPE_OPENEXR : IMB_FTYPE_PNG;
  image->foptions.flag = floating(p) ?
                             (p == ChannelPrecision::L16F || p == ChannelPrecision::RGB16F ?
                                  OPENEXR_HALF :
                                  0) :
                             (high(p) ? PNG_16BIT : 0);
  if (floating(p))
    image->foptions.flag |= R_IMF_EXR_CODEC_ZIP;
  image->foptions.compress = 15;
  IMB_colormanagement_assign_float_colorspace(image.get(), "Non-Color");
  const auto bytes = IMB_save_image_to_buffer(
      image.get(), ImBufFlags::FloatData | ImBufFlags::AlphaChannelPacked);
  if (bytes.is_empty())
    throw std::runtime_error("Flatten export encoding failed");
  return QByteArray(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
bool enabled(const TextureSet &set, const TextureLayer &layer, int c)
{
  if (c == 3)
    return false;
  return c < 5 ?
             set.channels[c] && layer.channels[c] && layer.blending[c] != LayerBlend::Disabled :
         c == 5 ? set.ao_channel && layer.ao_enabled && layer.ao_blending != LayerBlend::Disabled :
         c == 14 ? set.displacement.channel && layer.displacement.enabled &&
                       layer.displacement.blending != LayerBlend::Disabled :
                   set.users[c - 6].present && layer.users[c - 6].enabled &&
                       layer.users[c - 6].blending != LayerBlend::Disabled;
}
}  // namespace
QJsonObject verify_flattened_export_codec()
{
  using namespace blender;
  const auto require = [](bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  FlattenImage source{128,
                      128,
                      std::make_shared<std::vector<FillTexture::Pixel>>(
                          128 * 128, FillTexture::Pixel{.18f, .18f, .18f, .35f})};
  std::vector<unsigned char> coverage(128 * 128);
  for (int y = 32; y < 96; ++y)
    for (int x = 32; x < 96; ++x)
      coverage[y * 128 + x] = 1;
  (*source.pixels)[64 * 128 + 64][3] = 0;
  const auto load = [&](const QByteArray &bytes) {
    Buffer image(IMB_load_image_from_memory(
                     reinterpret_cast<const unsigned char *>(bytes.constData()),
                     bytes.size(),
                     ImBufFlags::AlphaChannelPacked | ImBufFlags::NoColorspaceConvert,
                     "flatten-test"),
                 IMB_freeImBuf);
    require(image && image->x == 128 && image->y == 128, "Flatten bitmap cannot be decoded");
    return image;
  };
  const auto pixel = [](const ImBuf &image, int x, int y, int c) {
    const auto offset = std::size_t(y) * image.x + x;
    if (image.float_data()) {
      if (c == 3 && image.channels < 4)
        return 1.f;
      return image.float_data()[offset * image.channels + std::min(c, image.channels - 1)];
    }
    return image.byte_data()[offset * 4 + c] / 255.f;
  };
  auto color = load(encoded(source, coverage, ChannelPrecision::SRGB8, true, false));
  require(std::abs(pixel(*color, 50, 50, 0) * 255 - 118) <= 1,
          "Flatten color has missing/double sRGB conversion");
  require(std::abs(pixel(*color, 50, 50, 3) - .35f) < .005f, "Flatten alpha was lost");
  require(pixel(*color, 31, 50, 3) > 0 && pixel(*color, 30, 50, 3) == 0,
          "Flatten UV margin is not exactly one pixel");
  require(pixel(*color, 64, 64, 3) == 0,
          "UV margin grew into an authored transparent island texel");
  for (auto &p : *source.pixels)
    p = {-.25f, 1.125f, .5f, 1};
  auto exr = load(encoded(source, coverage, ChannelPrecision::RGB16F, false, false));
  require(exr->float_data() && pixel(*exr, 50, 50, 0) == -.25f && pixel(*exr, 50, 50, 1) == 1.125f,
          "Flatten EXR lost signed/HDR half data");
  for (auto &p : *source.pixels)
    p = {.37f, .37f, .37f, 1};
  auto raw = load(encoded(source, coverage, ChannelPrecision::L8, false, false));
  require(std::abs(pixel(*raw, 50, 50, 0) * 255 - 94) <= 1,
          "Flatten data received a color transform");
  auto mask = load(encoded(source, coverage, ChannelPrecision::L16, false, true));
  require(std::abs(pixel(*mask, 50, 50, 0) - .37f) < 2.f / 65535,
          "Flatten PNG16 mask precision changed");
  // A height/data export is genuinely single-channel, with coverage folded
  // once against its neutral value because the file has no alpha channel.
  const auto exr_types = [&](const QByteArray &bytes) {
    std::vector<int> types;
    int cursor = 8;
    const auto integer = [&](int at) {
      require(at >= 0 && at + 4 <= bytes.size(), "Truncated EXR header");
      unsigned value = 0;
      for (int i = 0; i < 4; ++i)
        value |= unsigned(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
      return value;
    };
    const auto text = [&](int &at) {
      QByteArray value;
      while (at < bytes.size() && bytes[at])
        value.append(bytes[at++]);
      require(at < bytes.size(), "Truncated EXR attribute");
      ++at;
      return value;
    };
    while (cursor < bytes.size() && bytes[cursor]) {
      const auto name = text(cursor), type = text(cursor);
      const int length = int(integer(cursor));
      cursor += 4;
      require(length >= 0 && cursor + length <= bytes.size(), "Invalid EXR attribute size");
      if (name == "channels" && type == "chlist") {
        const int end = cursor + length;
        while (cursor < end && bytes[cursor]) {
          text(cursor);
          types.push_back(int(integer(cursor)));
          cursor += 16;
        }
        return types;
      }
      cursor += length;
    }
    throw std::runtime_error("EXR channel header missing");
  };
  for (auto &p : *source.pixels)
    p = {-.25f, -.25f, -.25f, .4f};
  const auto gray32 = encoded(source, coverage, ChannelPrecision::L32F, false, true);
  require(exr_types(gray32) == std::vector<int>{2},
          "Bump/Displacement EXR must have one float32 channel");
  auto decoded = load(gray32);
  require(std::abs(pixel(*decoded, 50, 50, 0) + .1f) < 1e-6,
          "Scalar coverage was not folded exactly once");
  for (auto &p : *source.pixels)
    p = {1.125f, 1.125f, 1.125f, 1};
  const auto gray16 = encoded(source, coverage, ChannelPrecision::L16F, false, true);
  require(exr_types(gray16) == std::vector<int>{1}, "Scalar half EXR must have one channel");
  auto hdr = load(gray16);
  require(pixel(*hdr, 50, 50, 0) == 1.125f, "Scalar HDR was clipped");
  for (auto &p : *source.pixels)
    p = {.2f, .2f, .2f, .5f};
  const auto gray_png = encoded(source, coverage, ChannelPrecision::L16, false, true, .6f);
  require(gray_png.size() > 26 && static_cast<unsigned char>(gray_png[24]) == 16 &&
              gray_png[25] == 0,
          "Scalar PNG16 must be grayscale");
  auto visible = load(gray_png);
  require(std::abs(pixel(*visible, 50, 50, 0) - .4f) < 2.f / 65535,
          "Scalar neutral-value coverage fold differs");
  TextureSet policy;
  policy.displacement.channel = policy.displacement.allocated = true;
  auto layer = policy.layers.front();
  layer.displacement.enabled = true;
  require(!enabled(policy, layer, 3) && enabled(policy, layer, 4) && enabled(policy, layer, 14),
          "Public flatten outputs must exclude internal Normal only");
  require(
      filename(
          "$textureSet_$layerName_$srcMap(.$udim)", policy, layer, 4, 1001, ChannelPrecision::L32F)
              .endsWith("_bump.exr") &&
          filename("$textureSet_$layerName_$srcMap(.$udim)",
                   policy,
                   layer,
                   14,
                   1001,
                   ChannelPrecision::L32F)
              .endsWith("_displacement.exr"),
      "Height-map suffix policy changed");
  return {{"color_srgb_once", true},
          {"raw_scalar", true},
          {"one_pixel_uv_margin", true},
          {"transparent_island_preserved", true},
          {"signed_hdr_exr", true},
          {"mask_png16", true},
          {"single_channel_height_headers", true},
          {"scalar_coverage_fold_once", true},
          {"normal_export_disabled", true}};
}
void choose_flattened_export(QWidget *parent,
                             TextureSet source,
                             const std::vector<QUuid> &ids,
                             std::shared_ptr<TexturePadding> padding,
                             bool mask_only,
                             LayerCaptureBridge bridge)
{
  if (ids.empty() || !padding)
    throw std::invalid_argument("Select a layer or group to export");
  QDialog dialog(parent);
  dialog.setObjectName("flattenExportDialog");
  dialog.setWindowTitle(mask_only ? "Export flattened mask" : "Export flattened groups to files");
  dialog.resize(540, 230);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  QSettings settings;
  auto *directory = new QLineEdit(QDir::homePath(), &dialog);
  directory->setObjectName("flattenExportDirectory");
  form->addRow("Output directory", directory);
  auto *browse = new QPushButton("Choose directory…", &dialog);
  form->addRow(browse);
  QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto path = bts::FileDialog::getExistingDirectory(
        &dialog, "Export flattened maps", directory->text());
    if (!path.isEmpty())
      directory->setText(path);
  });
  auto *pattern = new QLineEdit(
      settings.value("flatten/outputPattern", "$textureSet_$layerName_$srcMap(.$udim)").toString(),
      &dialog);
  pattern->setObjectName("flattenExportPattern");
  form->addRow("Filename", pattern);
  auto *format = new QComboBox(&dialog);
  format->setObjectName("flattenMaskFormat");
  format->addItems({"PNG 8", "PNG 16", "EXR 16F", "EXR 32F"});
  form->addRow("Mask format", format);
  format->setVisible(mask_only);
  form->labelForField(format)->setVisible(mask_only);
  auto *status = new QLabel("Texture Set resolution/precision · 1px padding", &dialog);
  status->setObjectName("flattenExportStatus");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto *run = buttons->addButton("Export", QDialogButtonBox::ActionRole);
  run->setObjectName("runFlattenExport");
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
    try {
      QDir output(directory->text());
      if (!QDir::isAbsolutePath(directory->text()) || !output.exists())
        throw std::invalid_argument("Choose an existing absolute output directory");
      FileDialog::remember_folder(directory->text());
      const auto naming = pattern->text();
      const auto mask_precision = std::array{ChannelPrecision::L8,
                                             ChannelPrecision::L16,
                                             ChannelPrecision::L16F,
                                             ChannelPrecision::L32F}
                                      .at(format->currentIndex());
      std::set<QString> unique;
      for (const auto &id : ids) {
        const auto &layer = TextureDocument::layer(source, id);
        if (!layer.visible)
          continue;
        if (mask_only && !layer.mask)
          throw std::invalid_argument("Selected layer has no mask");
        for (int tile : source.tiles) {
          if (layer.geometry.type == 1 && !layer.geometry.includes_tile(tile))
            continue;
          for (int c = mask_only ? -1 : 0; c < (mask_only ? 0 : 15); ++c)
            if (mask_only || enabled(source, layer, c)) {
              const auto name = filename(naming,
                                         source,
                                         layer,
                                         c,
                                         tile,
                                         mask_only ? mask_precision : precision(source, c));
              if (!unique.insert(name).second)
                throw std::invalid_argument(
                    "Filename pattern produces duplicate files; include layer and channel names");
            }
        }
      }
      if (unique.empty())
        throw std::invalid_argument("No visible enabled channels to export");
      settings.setValue("flatten/outputPattern", naming);
      run->setEnabled(false);
      Wait wait(&dialog);
      wait.setObjectName("flattenExportProgress");
      wait.setWindowTitle("Export flattened maps");
      auto *wait_layout = new QVBoxLayout(&wait);
      wait_layout->addWidget(new QLabel("Exporting channel bitmaps…", &wait));
      auto *progress = new QProgressBar(&wait);
      progress->setRange(0, 0);
      wait_layout->addWidget(progress);
      auto *cancel = new QDialogButtonBox(QDialogButtonBox::Cancel, &wait);
      wait_layout->addWidget(cancel);
      QObject::connect(cancel, &QDialogButtonBox::rejected, &wait, &Wait::reject);
      std::atomic_bool done = false;
      std::string error;
      QStringList written;
      QJsonArray probes;
      struct Mailbox {
        std::mutex mutex;
        std::optional<LayerCaptureRequest> request;
        std::optional<LayerCaptureResult> result;
        std::uint64_t ticket = 0;
      };
      Mailbox mailbox;
      std::jthread worker([&] {
        try {
          EditTiming timing("BTS_FLATTEN_EXPORT_MS");
          std::uint64_t packets = 0;
          const auto capture = [&](const QUuid &layer,
                                   int channel,
                                   int tile,
                                   int width,
                                   int height,
                                   bool mask,
                                   bool contextual) {
            FlattenImage image{
                width,
                height,
                std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(width) * height)};
            for (int y = 0; y < height && !wait.cancelled; y += 256)
              for (int x = 0; x < width && !wait.cancelled; x += 256) {
                LayerCaptureRequest request{layer,
                                            channel,
                                            tile,
                                            x,
                                            y,
                                            std::min(256, width - x),
                                            std::min(256, height - y),
                                            width,
                                            height,
                                            mask,
                                            contextual};
                {
                  std::lock_guard lock(mailbox.mutex);
                  mailbox.result.reset();
                  mailbox.ticket = 0;
                  mailbox.request = request;
                }
                std::shared_ptr<std::vector<FillTexture::Pixel>> packet;
                while (!wait.cancelled) {
                  {
                    std::lock_guard lock(mailbox.mutex);
                    if (mailbox.result) {
                      if (!mailbox.result->valid)
                        throw std::runtime_error("GPU flattened export failed");
                      packet = mailbox.result->pixels;
                      break;
                    }
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (wait.cancelled)
                  return FlattenImage{};
                if (!packet || packet->size() != std::size_t(request.width) * request.height)
                  throw std::runtime_error("Incomplete flattened export packet");
                for (int row = 0; row < request.height; ++row)
                  std::copy_n(packet->data() + row * request.width,
                              request.width,
                              image.pixels->data() + std::size_t(y + row) * width + x);
                ++packets;
              }
            return image;
          };
          const bool spatial = std::any_of(evaluation_layers(source).begin(),
                                           evaluation_layers(source).end(),
                                           [](const auto &layer) {
                                             return layer.kind == LayerKind::Fill &&
                                                    fill_surface_projection(layer.fill);
                                           });
          for (int tile : source.tiles) {
            if (wait.cancelled)
              break;
            source.geometry_ownership[tile] = padding->mesh_ownership(
                source.width(tile), tile, source.rows(tile));
            if (spatial)
              source.projection_tiles[tile] = padding->projection_geometry(
                  source.width(tile), {tile}, false, source.rows(tile));
          }
          if (spatial && !source.projection_tiles.empty())
            source.projection_geometry = source.projection_tiles.begin()->second;
          for (const auto &id : ids) {
            if (wait.cancelled)
              break;
            const auto &original = TextureDocument::layer(source, id);
            if (!original.visible)
              continue;
            std::optional<FlattenPlan> plan;
            if (!mask_only)
              plan = prepare_flatten_layer(source, id);
            for (int tile : source.tiles) {
              if (wait.cancelled)
                break;
              if (original.geometry.type == 1 && !original.geometry.includes_tile(tile))
                continue;
              const int width = source.width(tile), height = source.rows(tile);
              const auto coverage = padding->uv_coverage(width, tile, height);
              std::optional<FlattenImage> mask;
              if (mask_only || (original.mask && original.mask->enabled))
                mask = capture(id, 0, tile, width, height, true, false);
              if (wait.cancelled)
                break;
              for (int c = mask_only ? -1 : 0; c < (mask_only ? 0 : 15); ++c) {
                if (wait.cancelled)
                  break;
                if (!mask_only && (!plan->enabled[c] || !enabled(source, original, c)))
                  continue;
                FlattenImage image;
                if (mask_only) {
                  image = *mask;
                  for (auto &pixel : *image.pixels)
                    pixel = {pixel[0], pixel[0], pixel[0], 1};
                }
                else {
                  image = capture(id, c, tile, width, height, false, plan->contextual[c]);
                  if (wait.cancelled)
                    break;
                  const auto &prepared = plan->result.layer;
                  const float opacity = c < 5   ? prepared.opacity[c] :
                                        c == 5  ? prepared.ao_opacity :
                                        c == 14 ? prepared.displacement.opacity :
                                                  prepared.users[c - 6].opacity;
                  for (std::size_t index = 0; index < image.pixels->size(); ++index)
                    (*image.pixels)[index][3] *= opacity * (mask ? (*mask->pixels)[index][0] : 1);
                }
                const auto p = mask_only ? mask_precision : precision(source, c);
                const auto name = filename(naming, source, original, c, tile, p);
                const auto path = output.filePath(name);
                if (QFileInfo(path).isSymLink())
                  throw std::invalid_argument("Output file is a symlink");
                const auto bytes = encoded(image,
                                           coverage,
                                           p,
                                           c == 0,
                                           mask_only || c != 0,
                                           mask_only ? 0 : background(source, c));
                if (wait.cancelled)
                  break;
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                    !file.commit())
                  throw std::runtime_error("Cannot save flattened map");
                written.append(name);
                if (qEnvironmentVariableIsSet("BTS_FLATTEN_VERIFY_OUTPUT")) {
                  Buffer decoded(blender::IMB_load_image_from_memory(
                                     reinterpret_cast<const unsigned char *>(bytes.constData()),
                                     bytes.size(),
                                     blender::ImBufFlags::AlphaChannelPacked |
                                         blender::ImBufFlags::NoColorspaceConvert,
                                     "flatten-output-proof"),
                                 blender::IMB_freeImBuf);
                  if (!decoded || decoded->x != width || decoded->y != height)
                    throw std::runtime_error("Cannot decode flatten verification output");
                  const bool scalar = mask_only || c != 0;
                  const float tolerance = floating(p) ? (p == ChannelPrecision::L16F ||
                                                                 p == ChannelPrecision::RGB16F ?
                                                             .002f :
                                                             2e-6f) :
                                          high(p)     ? 3.f / 65535 :
                                                        2.f / 255;
                  double maximum = 0;
                  std::uint64_t checked = 0;
                  // Compare every covered texel with the actual production GPU capture.
                  // Outside-island padding is tested separately by the codec oracle.
                  for (std::size_t index = 0; index < coverage.size(); ++index) {
                    if (!coverage[index])
                      continue;
                    const auto &raw = (*image.pixels)[index];
                    auto expected = raw;
                    if (scalar) {
                      expected.fill(
                          std::lerp(mask_only ? 0 : background(source, c), raw[0], raw[3]));
                      expected[3] = 1;
                    }
                    else {
                      if (raw[3] <= 1e-12f)
                        expected = {0, 0, 0, 0};
                      if (!floating(p))
                        blender::IMB_colormanagement_scene_linear_to_srgb_v3(expected.data(),
                                                                             expected.data());
                    }
                    if (!floating(p))
                      for (float &value : expected)
                        value = std::clamp(
                            value,
                            0.f,
                            1.f);  // Integer codecs represent only [0,1]; EXR retains signed/HDR.
                    for (int component = 0; component < (scalar ? 1 : 4); ++component) {
                      const float actual =
                          decoded->float_data() ?
                              (component == 3 && decoded->channels < 4 ?
                                   1.f :
                                   decoded->float_data()[index * decoded->channels +
                                                         std::min(component,
                                                                  decoded->channels - 1)]) :
                              decoded->byte_data()[index * 4 + component] / 255.f;
                      const float error = std::abs(actual - expected[component]);
                      maximum = std::max(maximum, double(error));
                      if (!std::isfinite(actual) || error > tolerance)
                        throw std::runtime_error(
                            "Flatten export pixel differs from GPU source channel=" +
                            std::to_string(c) + " x=" + std::to_string(index % width) +
                            " y=" + std::to_string(index / width));
                      ++checked;
                    }
                  }
                  if (!checked)
                    throw std::runtime_error("Flatten export verification has no covered texels");
                  probes.append(QJsonObject{
                      {"layer", id.toString()},
                      {"channel", c},
                      {"tile", tile},
                      {"covered_components", double(checked)},
                      {"maximum_error", maximum},
                      {"tolerance", tolerance},
                      {"reference", "actual production GPU capture before file encoding"}});
                }
              }
            }
          }
          std::printf(
              "BTS_GPU_FLATTEN_EXPORT packets=%llu cpu_stack_composites=0 worker_output_mask_weight=1\n",
              (unsigned long long)packets);
        }
        catch (const std::exception &e) {
          error = e.what();
        }
        done.store(true, std::memory_order_release);
      });
      QTimer poll;
      poll.setInterval(10);
      QObject::connect(&poll, &QTimer::timeout, &wait, [&] {
        if (!wait.cancelled) {
          std::lock_guard lock(mailbox.mutex);
          try {
            if (mailbox.request && !mailbox.ticket)
              mailbox.ticket = bridge.request(source, *mailbox.request);
            if (mailbox.ticket && !mailbox.result) {
              auto result = bridge.result();
              if (result && result->ticket == mailbox.ticket) {
                mailbox.result = std::move(result);
                mailbox.request.reset();
              }
            }
          }
          catch (...) {
            mailbox.result = LayerCaptureResult{mailbox.ticket, false, {}};
            mailbox.request.reset();
          }
        }
        if (done.load(std::memory_order_acquire))
          wait.done(wait.cancelled ? QDialog::Rejected : QDialog::Accepted);
      });
      poll.start();
      wait.exec();
      worker.join();
      run->setEnabled(true);
      if (!error.empty())
        throw std::runtime_error(error);
      dialog.setProperty("flattenExportProbes", probes);
      dialog.setProperty("flattenExportFiles", written);
      dialog.setProperty("flattenExportFinished", !wait.cancelled);
      status->setText(wait.cancelled ? "Cancelled; completed files retained" :
                                       QString("Exported %1 maps").arg(written.size()));
    }
    catch (const std::exception &e) {
      run->setEnabled(true);
      status->setText(QString::fromUtf8(e.what()));
      dialog.setProperty("flattenExportError", QString::fromUtf8(e.what()));
    }
  });
  dialog.exec();
}
}  // namespace bts
