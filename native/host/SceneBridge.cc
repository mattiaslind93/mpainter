#include "ResourceBudget.hh"
#include "SceneBridge.hh"
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>

namespace bts {
ConvertedScene::ConvertedScene() = default;
ConvertedScene::~ConvertedScene() = default;
namespace {
aiMatrix4x4 world(const aiNode *node)
{
  return node->mParent ? world(node->mParent) * node->mTransformation : node->mTransformation;
}
std::array<float, 3> native_axes(const aiVector3D &v)
{
  // Converted OBJ is imported with -Z-forward/Y-up into Blender Z-up.
  return {v.x, -v.z, v.y};
}
void write_bytes(const QString &path, const char *bytes, const std::uint64_t size)
{
  if (size > std::uint64_t(std::numeric_limits<qint64>::max()))
    throw std::runtime_error("Embedded scene image exceeds Qt file offset capacity");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes, qint64(size)) != qint64(size))
    throw std::runtime_error("Cannot stage embedded scene image: " + path.toStdString());
}
std::string stage_image(const aiTexture &texture, ConvertedScene &result, const unsigned index)
{
  QString extension = QString::fromLatin1(texture.achFormatHint, int(strnlen(texture.achFormatHint, sizeof(texture.achFormatHint)))).toLower();
  // Format hints become filenames, never directory components.
  if (std::any_of(extension.begin(),extension.end(),[](QChar ch){return !ch.isLetterOrNumber();})) extension.clear();
  if (extension.isEmpty()) extension = "image";
  if (texture.mHeight) extension = "png";
  const QString path = result.directory->filePath(QString("embedded-%1.%2").arg(index).arg(extension));
  if (!texture.mHeight) {
    require_texture_ram(texture.mWidth, "Embedded compressed scene image");
    write_bytes(path, reinterpret_cast<const char *>(texture.pcData), texture.mWidth);
  }
  else {
    const auto pixels = resource_product(texture.mWidth, texture.mHeight);
    require_texture_ram(resource_product(pixels, 8), "Embedded scene image conversion");
    if (texture.mWidth > unsigned(std::numeric_limits<int>::max()) || texture.mHeight > unsigned(std::numeric_limits<int>::max()))
      throw std::runtime_error("Embedded scene image dimensions exceed QImage capacity");
    QImage image(int(texture.mWidth), int(texture.mHeight), QImage::Format_RGBA8888);
    if (image.isNull()) throw std::runtime_error("Cannot allocate embedded scene image");
    for (unsigned y = 0; y < texture.mHeight; ++y) {
      auto *row = image.scanLine(int(y));
      for (unsigned x = 0; x < texture.mWidth; ++x) {
        const auto &pixel = texture.pcData[std::size_t(y) * texture.mWidth + x];
        row[x * 4] = pixel.r; row[x * 4 + 1] = pixel.g;
        row[x * 4 + 2] = pixel.b; row[x * 4 + 3] = pixel.a;
      }
    }
    if (!image.save(path, "PNG")) throw std::runtime_error("Cannot encode embedded scene image");
  }
  result.embedded_images.push_back(path.toStdString());
  return path.toStdString();
}
} // namespace

std::unique_ptr<ConvertedScene> convert_mesh_scene(const std::string &path, bool import_cameras)
{
  const QFileInfo source(QString::fromStdString(path));
  const auto suffix = source.suffix().toLower();
  const bool collada = suffix == "dae";
  if (!source.isAbsolute() || !source.isFile() || !source.isReadable() || source.size() <= 0 ||
      (!collada && suffix != "gltf" && suffix != "glb"))
    throw std::invalid_argument("Select a readable Collada or glTF scene");
  const auto original_size = source.size();
  const auto original_time = source.lastModified();
  require_texture_ram(resource_product(source.size(), 2), "Scene source reader");
  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_USE_COLLADA_NAMES, true);
  const auto *scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ValidateDataStructure);
  if (!scene || !scene->mRootNode)
    throw std::runtime_error(std::string("Scene import failed: ") + importer.GetErrorString());
  std::uint64_t vertices = 0, faces = 0;
  if (!scene->mNumMeshes) throw std::runtime_error("Scene contains no meshes");
  for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
    vertices = resource_sum(vertices, scene->mMeshes[i]->mNumVertices);
    faces = resource_sum(faces, scene->mMeshes[i]->mNumFaces);
  }
  ResourceBudget{}.require(resource_sum(resource_product(vertices, sizeof(aiVector3D) * 4),
      resource_product(faces, sizeof(aiFace) + 3 * sizeof(unsigned))),
      resource_product(faces, 3 * 8 * sizeof(float)), "Converted scene geometry");
  auto result = std::make_unique<ConvertedScene>();
  const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/mPainter/import-cache";
  if (!QDir().mkpath(root)) throw std::runtime_error("Cannot create private scene import cache");
  result->directory = std::make_unique<QTemporaryDir>(root + "/scene-XXXXXX");
  if (!result->directory->isValid()) throw std::runtime_error("Cannot stage converted scene");
  if (import_cameras) for (unsigned i = 0; i < scene->mNumCameras; ++i) {
    const auto &input = *scene->mCameras[i];
    const auto *node = scene->mRootNode->FindNode(input.mName);
    if (!node) throw std::runtime_error("Scene camera node is missing");
    const auto transform = world(node);
    const auto rotation = aiMatrix3x3(transform);
    SceneCamera camera;
    camera.name = input.mName.C_Str(); camera.position = native_axes(transform * input.mPosition);
    camera.forward = native_axes(rotation * input.mLookAt); camera.up = native_axes(rotation * input.mUp);
    camera.aspect = input.mAspect > 0 ? input.mAspect : 0;
    const float aspect = camera.aspect > 0 ? camera.aspect : 1.5f;
    // Both Assimp 6 readers store full horizontal FOV (glTF2Importer::ImportCameras).
    // Calibrated against source yfov=45 and Collada fixtures, rather than camera.h wording.
    const auto half_horizontal = input.mHorizontalFOV / 2;
    camera.vertical_fov = float(2 * std::atan(std::tan(half_horizontal) / aspect) * 180 / 3.14159265358979323846);
    auto direction = input.mLookAt; direction.Normalize();
    const float scale = (rotation * direction).Length();
    camera.near_clip = input.mClipPlaneNear * scale; camera.far_clip = input.mClipPlaneFar * scale;
    camera.orthographic = input.mOrthographicWidth > 0;
    camera.orthographic_extent = 2 * input.mOrthographicWidth * scale / aspect;
    for (int axis = 0; axis < 3; ++axis)
      if (!std::isfinite(camera.position[axis]) || !std::isfinite(camera.forward[axis]) || !std::isfinite(camera.up[axis]))
        throw std::runtime_error("Nonfinite imported camera transform");
    if ((!camera.orthographic && (!std::isfinite(camera.vertical_fov) || camera.vertical_fov < 1 || camera.vertical_fov > 175)) ||
        !std::isfinite(camera.near_clip) || !std::isfinite(camera.far_clip) || camera.near_clip <= 0 || camera.far_clip <= camera.near_clip)
      throw std::runtime_error("Invalid imported camera optics");
    result->cameras.push_back(camera);
  }
  std::map<const aiTexture *, std::string> embedded;
  for (unsigned m = 0; m < scene->mNumMaterials; ++m) {
    auto *material = scene->mMaterials[m];
    for (int kind = int(aiTextureType_DIFFUSE); kind <= int(aiTextureType_UNKNOWN); ++kind) {
      for (unsigned index = 0; index < material->GetTextureCount(aiTextureType(kind)); ++index) {
        aiString name;
        if (material->GetTexture(aiTextureType(kind), index, &name) != AI_SUCCESS) continue;
        auto file = QString::fromUtf8(name.C_Str());
        if (const auto *texture = scene->GetEmbeddedTexture(name.C_Str())) {
          auto [found, inserted] = embedded.try_emplace(texture);
          if (inserted) found->second = stage_image(*texture, *result, unsigned(embedded.size() - 1));
          file = QString::fromStdString(found->second);
        }
        else {
          const QUrl url(file);
          if (url.isLocalFile()) file = url.toLocalFile();
          if (QDir::isRelativePath(file)) file = source.dir().absoluteFilePath(file);
        }
        const auto bytes = QDir::cleanPath(file).toUtf8();
        aiString absolute(bytes.constData());
        material->AddProperty(&absolute, AI_MATKEY_TEXTURE(aiTextureType(kind), index));
      }
    }
  }
  scene = importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
  if (!scene) throw std::runtime_error(std::string("Scene geometry transform failed: ") + importer.GetErrorString());
  result->obj_path = result->directory->filePath("scene.obj").toStdString();
  Assimp::Exporter exporter;
  if (exporter.Export(scene, "obj", result->obj_path) != AI_SUCCESS)
    throw std::runtime_error(std::string("In-process scene conversion failed: ") + exporter.GetErrorString());
  const QFileInfo after(source.filePath());
  if (after.size() != original_size || after.lastModified() != original_time)
    throw std::runtime_error("Scene source changed during import");
  return result;
}
std::unique_ptr<ConvertedScene> convert_collada_scene(const std::string &path, bool import_cameras)
{
  if (QFileInfo(QString::fromStdString(path)).suffix().compare("dae", Qt::CaseInsensitive))
    throw std::invalid_argument("Select a readable Collada scene");
  return convert_mesh_scene(path, import_cameras);
}
} // namespace bts
