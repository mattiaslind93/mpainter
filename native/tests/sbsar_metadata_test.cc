#include "Description.hh"
#include "../host/ResourceAvailability.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#if defined(__linux__)
#include <sys/inotify.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#endif
using namespace bts::sbsar_metadata;
namespace {
void check(bool b,const char *message) {if(!b)throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool threw=false;try{f();}catch(const std::exception &){threw=true;}check(threw,"Expected rejection");}
QByteArray load(const QString &path) {QFile f(path);check(f.open(QIODevice::ReadOnly),"Cannot read fixture");return f.readAll();}
QByteArray hash(const QString &path) {
  if(path.isEmpty())return {};
  QFile f(path);check(f.open(QIODevice::ReadOnly),"Cannot hash fixture");QCryptographicHash h(QCryptographicHash::Sha256);
  check(h.addData(&f),"Hash read failed");return h.result().toHex();
}
QByteArray basic() {return R"(<sbsdescription formatversion="2.1" asmuid="099"><graphs count="1"><graph label="Test"><sbspresets count="1"><unknownpreset custom="keep"><values>1<x/>two</values></unknownpreset></sbspresets><outputs count="2"><output uid="12" identifier="height" type="5" format="12"/><output uid="13" identifier="color"/></outputs><inputs count="1"><input uid="14" type="255" default="a,b" alteroutputs="13,12,13"><inputgui label="Future"><guislider min="-2" max="20" clamp="on"/></inputgui></input></inputs></graph></graphs></sbsdescription>)";}
std::uint64_t ram_tiny(){return 1024;}
std::uint64_t ram_none(){return 0;}
void generated() {
  const auto bytes=basic();const auto d=read_xml(bytes);
  check(d.original_xml==bytes&&d.known_schema,"Original XML lost");
  check(d.graphs.size()==1&&d.graphs[0].inputs.size()==1&&d.graphs[0].outputs.size()==2,"Typed lists incorrect");
  check(d.graphs[0].presets.size()==1&&d.graphs[0].output_references.size()==3,"Ordered presets/references lost");
  check(d.graphs[0].output_references[0].raw_uid==u"13"&&d.graphs[0].output_references[1].raw_uid==u"12","Reference order lost");
  check(attribute(d.tokens[d.graphs[0].inputs[0]],u"type")==u"255","Unknown ordinal changed");
  const auto json=dump_json(d);check(!QJsonDocument::fromJson(json).isNull(),"Dump invalid JSON");
  auto mixed=read_xml(R"(<?xml version="1.0"?><sbsdescription formatversion="2.1" xmlns:q="urn:test" z="2" a="1">a<!--note--><![CDATA[<&>]]><?keep hello?><q:future q:flag="v">b&amp;c&#65;</q:future>z</sbsdescription>)");
  const auto &root=mixed.tokens[mixed.root];
  check(root.attributes.size()==3&&root.attributes[1].name==u"z"&&root.attributes[2].name==u"a","Attribute order lost");
  check(root.namespaces.size()==1&&root.namespaces[0].value==u"urn:test","Namespace lost");
  bool cdata=false,pi=false,comment=false,ns=false,text=false;
  for(const auto &t:mixed.tokens) {
    cdata|=t.cdata&&t.text==u"<&>";pi|=t.kind==Kind::ProcessingInstruction&&t.text==u"hello";
    comment|=t.kind==Kind::Comment&&t.text==u"note";ns|=t.name==u"q:future"&&t.namespace_uri==u"urn:test";
    text|=t.text==u"b&cA";
  }
  check(cdata&&pi&&comment&&ns&&text,"Mixed content lost");
  auto unknown=read_xml("<sbsdescription formatversion=\"999\"><graphs count=\"bogus\"><mystery/></graphs></sbsdescription>");
  check(!unknown.known_schema&&unknown.graphs.empty()&&!unknown.diagnostics.empty(),"Unknown schema interpreted");
  check(!read_xml("<sbsdescription xmlns='urn:future' formatversion='2.1'/>").known_schema,"Unknown namespace interpreted");
  check(!read_xml("<q:sbsdescription xmlns:q='urn:future' formatversion='2.1'/>").known_schema,"Prefixed unknown namespace interpreted");
  check(read_xml("<sbsdescription formatversion='2.1'><graphs xmlns='urn:future'><graph xmlns=''/></graphs></sbsdescription>").graphs.empty(),"Foreign container interpreted");
  rejects([]{read_xml("<not-description/>");});
  rejects([]{read_xml("<sbsdescription>");});
  rejects([]{read_xml("<sbsdescription a='1' a='2'/>");});
  rejects([]{read_xml(QByteArray("<sbsdescription>\xff</sbsdescription>"));});
  rejects([]{read_xml(QByteArray("<sbsdescription/>\xc2"));});
  check(read_xml(QByteArray("\xef\xbb\xbf<sbsdescription/>")).root!=no_token,"UTF-8 BOM rejected");
  rejects([]{read_xml("<sbsdescription>&missing;</sbsdescription>");});
  for(const auto &from: {QByteArray("count=\"2\""),QByteArray("uid=\"14\""),QByteArray("13,12,13")}) {
    auto bad=bytes;
    if(from.startsWith("count"))bad.replace(from,"count=\"18446744073709551616\"");
    else if(from.startsWith("uid"))bad.replace(from,"uid=\"12\"");
    else bad.replace(from,"13,999");
    rejects([&]{read_xml(bad);});
  }
  for(const auto &bad: {"", "-1", "+1", "1 ", "1e2", "18446744073709551616"})rejects([&]{decimal(QString::fromLatin1(bad));});
  check(decimal(u"18446744073709551615")==std::numeric_limits<std::uint64_t>::max(),"UID64 boundary rejected");
  auto mismatch=bytes;mismatch.replace("outputs count=\"2\"","outputs count=\"1\"");rejects([&]{read_xml(mismatch);});
  auto badref=bytes;badref.replace("13,12,13","13,");rejects([&]{read_xml(badref);});
  check(read_xml(bytes,{}).original_xml==bytes,"Former configured limit blocked XML");
  {bts::TextureRamQueryScope s(ram_tiny);([&]{read_xml(bytes);})();}
  {bts::TextureRamQueryScope s(ram_none);read_xml(bytes,{});}
  rejects([]{bts::resource_sum(UINT64_MAX,1);});rejects([]{bts::resource_product(UINT64_MAX,2);});
  // No fixed depth/count cap; fixture sizes are test choices, not parser limits.
  QByteArray deep="<sbsdescription formatversion='2.1'>";
  for(int i=0;i<20000;++i)deep+="<x>";deep+="leaf";
  for(int i=0;i<20000;++i)deep+="</x>";deep+="</sbsdescription>";
  check(read_xml(deep).tokens.size()==40003,"Deep flat parse failed");
  QByteArray wide="<sbsdescription formatversion='2.1'><graphs count='0'/>";
  for(int i=0;i<20000;++i)wide+="<future/>";wide+="</sbsdescription>";
  check(read_xml(wide).tokens.size()==40004,"Wide parse failed");
  QByteArray many="<sbsdescription formatversion='2.1'><graphs count='1'><graph><outputs count='5000'>";
  for(int i=0;i<5000;++i)many+="<output uid='"+QByteArray::number(i)+"'/>";
  many+="</outputs></graph></graphs></sbsdescription>";
  check(read_xml(many).graphs[0].outputs.size()==5000,"Dynamic typed output count failed");
  QTemporaryDir filetmp;check(filetmp.isValid(),"File test temp failed");
  const auto file=filetmp.filePath("description.xml");
  {QFile f(file);check(f.open(QIODevice::WriteOnly),"Fixture create failed");check(f.write(bytes)==bytes.size(),"Fixture write failed");}
  check(read_xml_file(file).original_xml==bytes,"File reader changed bytes");
  check(read_xml_file(file,{}).original_xml==bytes,"Former configured limit blocked file");rejects([&]{read_xml_file(filetmp.filePath("missing"));});
  auto good=std::async(std::launch::async,[bytes]{for(int i=0;i<30;++i)check(read_xml(bytes).graphs.size()==1,"Parallel parse changed");});
  auto bad=std::async(std::launch::async,[]{for(int i=0;i<30;++i)rejects([]{read_xml("<bad>");});});good.get();bad.get();
}
void dtd() {
  QTemporaryDir tmp;check(tmp.isValid(),"No temporary directory");const auto sentinel=tmp.filePath("external.dtd");
  const QByteArray payload="<!ENTITY stolen 'DO_NOT_READ'>";
  {QFile f(sentinel);check(f.open(QIODevice::WriteOnly),"Sentinel open failed");check(f.write(payload)==payload.size(),"Sentinel write failed");}
  const auto before=hash(sentinel);
#if defined(__linux__)
  const int notify=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);check(notify>=0,"inotify init failed");
  check(inotify_add_watch(notify,sentinel.toUtf8(),IN_OPEN|IN_ACCESS)>=0,"inotify watch failed");
  const int listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  check(listener>=0,"Network sentinel socket failed");
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,4)==0,"Network sentinel bind failed");
  socklen_t address_size=sizeof(address);check(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&address_size)==0,"Network sentinel address failed");
  const auto network_id=QString("http://127.0.0.1:%1/never.dtd").arg(ntohs(address.sin_port));
#else
  const auto network_id=QString("https://127.0.0.1:1/must-never-fetch");
#endif
  for(const auto &id:{sentinel,network_id,QString("file:///nonexistent.dtd")}) {
    const auto xml=QByteArray("<!DOCTYPE sbsdescription SYSTEM '")+id.toUtf8()+"'><sbsdescription formatversion='2.1'/>";
    const auto d=read_xml(xml);check(d.original_xml==xml&&d.doctype_system_id==id,"External DOCTYPE lost");
    auto reference=xml;reference.replace("/>",">&stolen;</sbsdescription>");rejects([&]{read_xml(reference);});
    reference=xml;reference.replace("/>"," custom='&stolen;'/>");rejects([&]{read_xml(reference);});
  }
  const auto pub=read_xml("<!DOCTYPE sbsdescription PUBLIC '-//TEST//DTD metadata//EN' 'https://127.0.0.1:1/x'><sbsdescription/>");
  check(pub.doctype_public_id==u"-//TEST//DTD metadata//EN","Public identifier lost");
  check(read_xml("<!DOCTYPE sbsdescription SYSTEM 'quoted[identifier'><sbsdescription/>").doctype_system_id==u"quoted[identifier","Quoted DTD identifier mishandled");
  read_xml("<sbsdescription><!-- <!DOCTYPE x [ <!ENTITY x 'y'> ]> --><![CDATA[<!ENTITY x 'y'>]]><?note <!DOCTYPE x [?></sbsdescription>");
  read_xml(QByteArray("<!DOCTYPE sbsdescription PUBLIC '-//TEST//DTD metadata//EN' '")+network_id.toUtf8()+"'><sbsdescription/>");
  for(const auto &x:{
    "<!DOCTYPE sbsdescription [ ]><sbsdescription/>",
    "<!DOCTYPE sbsdescription [<!ENTITY a 'expanded'>]><sbsdescription>&a;</sbsdescription>",
    "<!DOCTYPE sbsdescription [<!ENTITY % a SYSTEM 'file:///etc/passwd'>%a;]><sbsdescription/>",
    "<!DOCTYPE sbsdescription [<!ENTITY x '1234567890'><!ENTITY y '&x;&x;&x;&x;'>]><sbsdescription>&y;</sbsdescription>",
    "<!ENTITY bad 'x'><sbsdescription/>"})rejects([&]{read_xml(x);});
  // UTF-16 cannot bypass lexical rejection; safe BOM-marked input remains valid.
  const QStringView utf16=u"<!DOCTYPE sbsdescription [<!ENTITY a 'x'>]><sbsdescription/>";
  QByteArray encoded("\xff\xfe",2);for(const auto c:utf16){encoded+=char(c.unicode()&255);encoded+=char(c.unicode()>>8);}
  rejects([&]{read_xml(encoded);});
  const QStringView safe=u"<!DOCTYPE sbsdescription SYSTEM 'never'><sbsdescription formatversion='2.1'/>";
  encoded=QByteArray("\xff\xfe",2);for(const auto c:safe){encoded+=char(c.unicode()&255);encoded+=char(c.unicode()>>8);}
  check(read_xml(encoded).known_schema,"UTF-16 valid XML rejected");
  encoded+='x';rejects([&]{read_xml(encoded);});
#if defined(__linux__)
  char events[4096];errno=0;const auto n=::read(notify,events,sizeof(events));
  check(n<0&&errno==EAGAIN,"External DTD sentinel was opened/read");close(notify);
  errno=0;const int connection=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);
  if(connection>=0)close(connection);
  check(connection<0&&(errno==EAGAIN||errno==EWOULDBLOCK),"External DTD network identifier was fetched");close(listener);
#endif
  check(hash(sentinel)==before,"DTD sentinel changed");
}
void fixture(const QString &xml,const QString &archive) {
  const auto xmlhash=hash(xml),archivehash=hash(archive);const auto bytes=load(xml);
  const auto d=read_xml_file(xml);
  check(d.original_xml==bytes&&d.known_schema,"Actual XML bytes/schema lost");
  check(d.doctype_name==u"sbsdescription"&&d.doctype_system_id==u"sbsdescription.dtd","Actual external DTD metadata lost");
  check(d.graphs.size()==1&&d.graphs[0].inputs.size()==19&&d.graphs[0].outputs.size()==10&&d.graphs[0].presets.empty(),"Actual counts changed");
  check(attribute(d.tokens[d.root],u"asmuid")==u"2338233685","Actual archive UID changed");
  const auto &g=d.graphs[0];check(attribute(d.tokens[g.token],u"pkgurl")==u"pkg://fabric_suit_vintage","Graph URL lost");
  check(attribute(d.tokens[g.inputs[0]],u"default")==u"8,8"&&attribute(d.tokens[g.inputs[0]],u"type")==u"8","Builtin default/ordinal changed");
  check(attribute(d.tokens[g.outputs[3]],u"width")==u"16"&&attribute(d.tokens[g.outputs[7]],u"format")==u"28","Output dimensions/format changed");
  check(attribute(d.tokens[g.outputs[8]],u"usertag")==u"disable=(true)","User tag lost");
  check(!g.output_references.empty(),"Actual UID references missing");
  auto parallel=std::async(std::launch::async,[bytes]{return read_xml(bytes).tokens.size();});
  check(parallel.get()==d.tokens.size(),"Actual parallel parse changed");
  check(hash(xml)==xmlhash&&hash(archive)==archivehash,"Original fixture changed");
  std::cout<<"actual XML bytes="<<bytes.size()<<" tokens="<<d.tokens.size()<<" refs="<<g.output_references.size()
    <<" XML_SHA256="<<xmlhash.constData()<<" SBSAR_SHA256="<<archivehash.constData()<<"\n";
}
}
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);try {
    const auto a=app.arguments();
    if(a.size()==4&&a[1]==u"--fixture")fixture(a[2],a[3]);
    else if(a.size()==1){generated();dtd();}
    else throw std::runtime_error("Unexpected test arguments");
    std::cout<<"PASS XML-only metadata\n";return 0;
  }catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
