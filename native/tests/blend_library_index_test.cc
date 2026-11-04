#include "BlendLibraryIndex.hh"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDirIterator>
#include <cassert>
#include <future>
#include <thread>
using namespace bts;
static void write(const QString &path,QByteArrayView data){QFile f(path);assert(f.open(QIODevice::WriteOnly|QIODevice::Truncate));assert(f.write(data.data(),data.size())==data.size());}
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir tmp;assert(tmp.isValid());const auto source=tmp.filePath("input.blend"),dir=tmp.filePath("cache");write(source,"0123456789");BlendLibraryIndex index(dir);int reads=0;
 BlendLibraryIndex::Reader reader=[&](const QString &,std::stop_token){++reads;BlendMaterialEntry e;e.name="Ö Roughness";e.marked_asset=true;e.catalog_id="catalog";e.catalog_name="Wood";e.description="Notes";e.tags={"one","two"};e.preview=QImage(128,96,QImage::Format_RGBA8888);e.preview.fill(QColor(50,100,150,200));return BlendLibraryIndex::Entries{e};};
 auto first=index.read(source,{},reader);auto second=index.read(source,{},reader);assert(reads==1&&first.size()==1&&second[0].name==first[0].name&&second[0].preview==first[0].preview&&second[0].tags==first[0].tags&&second[0].preview.width()==128&&second[0].preview.height()==96);
 write(source,"ABCDEFGHIJ");index.read(source,{},reader);assert(reads==2); // same size, timestamp/ctime changed
 const auto replacement=tmp.filePath("new.blend");write(replacement,"KLMNOPQRST");assert(QFile::remove(source)&&QFile::rename(replacement,source));index.read(source,{},reader);assert(reads==3); // inode replacement
 const auto cache=QDir(dir).entryList({"*.index"},QDir::Files).at(0);write(QDir(dir).filePath(cache),"broken");index.read(source,{},reader);assert(reads==4);
 // Recompute integrity after mutation, to exercise length/count validation too.
 const auto damaged=[&](bool count){QFile f(QDir(dir).filePath(cache));assert(f.open(QIODevice::ReadOnly));auto body=f.readAll().chopped(32);f.close();QBuffer buffer(&body);assert(buffer.open(QIODevice::ReadWrite));QDataStream stream(&buffer);assert(buffer.seek(8));if(count){for(int i=0;i<2;++i){quint64 length=0;stream>>length;assert(buffer.seek(buffer.pos()+length));}assert(buffer.seek(buffer.pos()+48));}stream<<quint64(~quint64(0));write(QDir(dir).filePath(cache),body+QCryptographicHash::hash(body,QCryptographicHash::Sha256));};
 damaged(false);index.read(source,{},reader);assert(reads==5);damaged(true);index.read(source,{},reader);assert(reads==6);
 {TextureRamQueryScope low(+[]()->std::uint64_t{return 0;});bool failed=false;try{index.read(source,{},reader);}catch(const std::length_error &e){failed=std::string(e.what()).find("available")!=std::string::npos;}assert(!failed&&reads==6);}
 std::stop_source stop;stop.request_stop();bool cancelled=false;try{index.read(source,stop.get_token(),reader);}catch(const std::runtime_error &){cancelled=true;}assert(cancelled&&reads==6);
 write(source,"UVWXYZabcd");bool changed=false;try{index.read(source,{},[&](const QString &p,std::stop_token s){auto r=reader(p,s);write(p,"different!");return r;});}catch(const std::runtime_error &){changed=true;}assert(changed);
 // A cancelled miss cannot replace the last valid cache.
 std::stop_source during;cancelled=false;try{index.read(source,during.get_token(),[&](const QString &p,std::stop_token s){auto r=reader(p,s);during.request_stop();return r;});}catch(const std::runtime_error &){cancelled=true;}assert(cancelled);
 index.read(source,{},reader);const auto before=reads;
 std::vector<std::future<BlendLibraryIndex::Entries>> parallel;for(int i=0;i<8;++i)parallel.push_back(std::async(std::launch::async,[&]{return index.read(source,{},reader);}));for(auto &r:parallel)assert(r.get()[0].name==first[0].name);assert(reads==before);
 // An unavailable cache is optional, not an admission failure.
 const auto blocked=tmp.filePath("blocked");write(blocked,"file");assert(BlendLibraryIndex(blocked).read(source,{},reader).size()==1);
 puts("PASS index hit/change/inode/corruption/length/budget/cancel/concurrent/optional-cache");
}
