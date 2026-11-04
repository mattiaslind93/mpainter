#include "NativeHeightImageRetention.hh"
#include <QFile>
#include <QTemporaryDir>
#include <cassert>
#include <array>
#include <iostream>
namespace {
template<class Action>void rejects(Action &&action){bool rejected=false;try{action();}catch(const std::invalid_argument &){rejected=true;}assert(rejected);}
}
int main(){
 using namespace bts;
 struct Packed {int size;const void *data;};struct Part {const Packed *packedfile;};
 char byte=0;Packed empty{0,&byte},missing_data{1,nullptr},valid{1,&byte};
 assert(!native_height_has_packed_image_bytes(std::array<Part,0>{}));
 assert(!native_height_has_packed_image_bytes(std::array<Part,1>{{{nullptr}}}));
 assert(!native_height_has_packed_image_bytes(std::array<Part,1>{{{&empty}}}));
 assert(!native_height_has_packed_image_bytes(std::array<Part,1>{{{&missing_data}}}));
 assert(native_height_has_packed_image_bytes(std::array<Part,1>{{{&valid}}}));
 const QByteArray encoded("owned Height-only encoded image");
 const auto sha=QString::fromLatin1(QCryptographicHash::hash(encoded,QCryptographicHash::Sha256).toHex());
 const std::array<NativeMaterialImageReference,1> refs{{{"Height-only",sha,"height.exr"}}};
 int checks=0;const auto verify=[&](const QString &expected){++checks;return expected==QString::fromLatin1(QCryptographicHash::hash(encoded,QCryptographicHash::Sha256).toHex());};
 assert(require_native_height_image_retention("Height-only",true,{},verify)==NativeHeightImageRetention::PackedArchive&&checks==0);
 assert(require_native_height_image_retention("Height-only",false,refs,verify)==NativeHeightImageRetention::VerifiedBundleReference&&checks==1);
 rejects([&]{require_native_height_image_retention("Other image",false,refs,verify);});assert(checks==1);
 rejects([&]{require_native_height_image_retention("Height-only",false,refs,[](const auto &){return false;});});
 const auto changed=encoded+"changed";rejects([&]{require_native_height_image_retention("Height-only",false,refs,[&](const QString &wanted){return wanted==QString::fromLatin1(QCryptographicHash::hash(changed,QCryptographicHash::Sha256).toHex());});});
 auto malformed=refs;malformed[0].sha256="invalid";rejects([&]{require_native_height_image_retention("Height-only",false,malformed,verify);});
 const std::array<NativeMaterialImageReference,2> duplicate{refs[0],refs[0]};rejects([&]{require_native_height_image_retention("Height-only",false,duplicate,verify);});
 // Actual readable old path, with no archive retention. Its existence and
 // contents must not turn this otherwise rejected source into a portable one.
 QTemporaryDir directory;assert(directory.isValid());QFile original(directory.filePath("height.exr"));assert(original.open(QIODevice::WriteOnly));assert(original.write(encoded)==encoded.size());original.close();assert(original.exists());
 rejects([&]{require_native_height_image_retention("Height-only",false,{},verify);});assert(original.exists());
 rejects([&]{require_native_height_image_retention("Height-only",native_height_has_packed_image_bytes(std::array<Part,1>{{{&empty}}}),{},verify);});
 rejects([&]{require_native_height_image_retention({},true,{},verify);});
 std::cout<<"Height image packed/archive-reference/hash admission and readable-path rejection PASS\n";
}
