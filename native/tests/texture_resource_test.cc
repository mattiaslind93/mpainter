#include "ImageSequence.hh"
#include "FillTextureJSON.hh"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir directory;assert(directory.isValid());const auto write=[&](const QString &name,const QByteArray &bytes){QFile file(directory.filePath(name));assert(file.open(QIODevice::WriteOnly));assert(file.write(bytes)==bytes.size());};
 for(const auto separator:{QString("."),QString("_"),QString("-")}){const auto prefix="texture"+separator;write(prefix+"1001.exr","one");write(prefix+"1002.exr","two");auto source=bts::reference_image_resource(directory.filePath(prefix+"1001.exr"));assert(source.bytes.isEmpty()&&source.files.size()==2&&source.files[0].tile==1001&&source.files[1].tile==1002);const auto hash=bts::texture_sources_hash(source.files);auto copy=source.files;copy[0].path="/relocated/texture.exr";assert(hash==bts::texture_sources_hash(copy));assert(bts::read_texture_sources(bts::texture_sources_json(source.files))==source.files);auto pattern=bts::reference_image_resource(directory.filePath(prefix+"<UDIM>.exr"));assert(pattern.files==source.files);}
 write("ordinary.png","single");auto one=bts::reference_image_resource(directory.filePath("ordinary.png"));assert(one.files.size()==1&&one.files[0].tile==0&&bts::texture_sources_hash(one.files)==bts::hash_texture_file(directory.filePath("ordinary.png")));
 // A sparse >256MiB fixture validates the descriptor, with no encoded bytes in RAM.
 QFile large(directory.filePath("large.exr"));assert(large.open(QIODevice::WriteOnly));assert(large.resize(268501438));large.close();auto big=bts::reference_image_resource(large.fileName());assert(big.bytes.isEmpty()&&big.files.front().size==268501438);
 auto image=std::make_shared<bts::FillTexture>();image->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();image->hash=std::string(64,'a');image->name="Uncapped descriptor";image->width=16384;image->height=8192;image->validate();auto members=std::make_shared<std::map<int,std::shared_ptr<const bts::FillTexture>>>();for(int tile=1001;tile<1011;++tile)members->emplace(tile,image);auto sequence=std::make_shared<bts::FillTexture>(*image);sequence->tiles=members;sequence->validate();assert(bts::read_fill_texture_json(bts::fill_texture_json(sequence))->tiles->size()==10);
 bool rejected=false;try{auto bad=big.files;bad[0].sha256="bad";bts::texture_sources_hash(bad);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);return 0;
}
