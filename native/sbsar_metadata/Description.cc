#include "Description.hh"
#include "../host/ResourceAvailability.hh"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <algorithm>
#include <map>
#include <stdexcept>

namespace bts::sbsar_metadata {
namespace {
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
struct Ledger {
  std::uint64_t used = 0;
  explicit Ledger(ReadOptions){}
  void add(std::uint64_t n) {
    const auto next=resource_sum(used,n);
    used=next;
  }
  void string(QStringView s) { add(resource_product(std::uint64_t(s.size())+1,2)); }
  template<class T> void push(std::vector<T> &v, T item) {
    if(v.size()==v.capacity()) {
      const auto cap=std::max<std::uint64_t>(1,resource_product(v.capacity(),2));
      if(cap>v.max_size()) throw std::length_error("XML vector exceeds native representation");
      add(resource_product(cap-v.capacity(),sizeof(T))); v.reserve(std::size_t(cap));
    }
    v.push_back(std::move(item));
  }
};
// Reject internal subsets BEFORE Qt processes DTD declarations. This scanner
// ignores comments, CDATA, PIs and quoted external identifiers. No file/network
// lookup occurs here or in the resolver. Unicode input is decoded first so UTF-16
// cannot bypass the check with interleaved NUL bytes.
void preflight(QStringView s) {
  qsizetype i=0;
  while(i<s.size()) {
    if(s[i]!=u'<') { ++i; continue; }
    auto skip=[&](QStringView prefix,QStringView end) {
      if(!s.mid(i).startsWith(prefix)) return false;
      const auto at=s.indexOf(end,i+prefix.size());
      if(at<0) fail("Unterminated XML markup");
      i=at+end.size(); return true;
    };
    if(skip(u"<!--",u"-->")||skip(u"<![CDATA[",u"]]>")||skip(u"<?",u"?>")) continue;
    if(s.mid(i).startsWith(u"<!ENTITY")) fail("XML entity declarations are forbidden");
    const bool dtd=s.mid(i).startsWith(u"<!DOCTYPE");
    char16_t quote=0; ++i;
    for(;i<s.size();++i) {
      const auto c=s[i].unicode();
      if(quote) { if(c==quote) quote=0; continue; }
      if(c==u'\''||c==u'\"') { quote=c; continue; }
      if(dtd&&c==u'[') fail("XML internal DTD subsets are forbidden");
      if(c==u'>') { ++i; break; }
    }
  }
}
struct DenyResolver final: QXmlStreamEntityResolver {
  bool attempted=false;
  QString resolveEntity(const QString &,const QString &) override { attempted=true; return {}; }
  QString resolveUndeclaredEntity(const QString &) override { attempted=true; return {}; }
};
QString copy(QStringView s, Ledger &l) { l.string(s); return s.toString(); }
void validate(Description &d,Ledger &l) {
  if(d.root==no_token) fail("Expected sbsdescription XML root");
  const auto root_name=QStringView(d.tokens[d.root].name);
  if(root_name.sliced(root_name.lastIndexOf(u':')+1)!=u"sbsdescription") fail("Expected sbsdescription XML root");
  d.format_version=attribute(d.tokens[d.root],u"formatversion"); l.string(d.format_version);
  d.known_schema=d.format_version==u"2.1"&&d.tokens[d.root].namespace_uri.isEmpty();
  if(!d.known_schema) {
    QString message="Unknown description format/namespace: raw XML retained; no typed graph interpretation";
    l.string(message);l.push(d.diagnostics,std::move(message));
    return;
  }
  std::vector<TokenId> graph_for; l.add(resource_product(d.tokens.size(),sizeof(TokenId)));
  graph_for.resize(d.tokens.size(),no_token);
  std::vector<std::uint64_t> children; l.add(resource_product(d.tokens.size(),sizeof(std::uint64_t)));
  children.resize(d.tokens.size());
  for(TokenId i=0;i<d.tokens.size();++i) {
    auto &t=d.tokens[i]; if(t.kind!=Kind::Start) continue;
    if(t.parent!=no_token) {
      children[t.parent]=resource_sum(children[t.parent],1);
      graph_for[i]=graph_for[t.parent];
    }
    if(!t.namespace_uri.isEmpty()) continue;
    if(t.name==u"graph"&&t.parent!=no_token&&d.tokens[t.parent].namespace_uri.isEmpty()&&d.tokens[t.parent].name==u"graphs"&&
       d.tokens[t.parent].parent==d.root) {
      graph_for[i]=d.graphs.size(); Graph g;g.token=i;l.push(d.graphs,std::move(g));
    }
    if(graph_for[i]!=no_token&&t.parent!=no_token) {
      auto &g=d.graphs[graph_for[i]]; const auto &p=d.tokens[t.parent];
      if(p.parent==g.token&&p.namespace_uri.isEmpty()) {
        if(t.name==u"input"&&p.name==u"inputs") l.push(g.inputs,i);
        if(t.name==u"output"&&p.name==u"outputs") l.push(g.outputs,i);
        if(p.name==u"sbspresets") l.push(g.presets,i);
      }
    }
  }
  for(TokenId i=0;i<d.tokens.size();++i) {
    const auto &t=d.tokens[i];if(t.kind!=Kind::Start||!t.namespace_uri.isEmpty()) continue;
    const bool graph_container=t.name==u"graphs"&&t.parent==d.root;
    const bool member_container=graph_for[i]!=no_token&&t.parent==d.graphs[graph_for[i]].token&&
      (t.name==u"inputs"||t.name==u"outputs"||t.name==u"sbspresets");
    if(graph_container||member_container) {
      for(const auto &a:t.attributes) if(a.name==u"count"&&decimal(a.value)!=children[i])
        fail("Declared XML count differs from direct element count at token "+QString::number(i));
    }
  }
  for(auto &g:d.graphs) {
    // Ordered maps give bounded comparisons even for adversarial UID values.
    std::map<std::uint64_t,TokenId> outputs,all;
    for(const auto *list:{&g.outputs,&g.inputs}) for(auto id:*list) {
      const auto uid=decimal(attribute(d.tokens[id],u"uid"));
      l.add(sizeof(std::pair<const std::uint64_t,TokenId>)+4*sizeof(void*));
      if(!all.emplace(uid,id).second) fail("Duplicate input/output UID within graph");
      if(list==&g.outputs) {
        l.add(sizeof(std::pair<const std::uint64_t,TokenId>)+4*sizeof(void*));outputs.emplace(uid,id);
      }
    }
    for(auto id:g.inputs) {
      const auto refs=attribute(d.tokens[id],u"alteroutputs");
      if(refs.isEmpty()) continue;
      qsizetype begin=0;
      while(begin<=refs.size()) {
        auto end=refs.indexOf(u',',begin);if(end<0)end=refs.size();
        const auto raw=QStringView(refs).mid(begin,end-begin);
        const auto uid=decimal(raw);const auto found=outputs.find(uid);
        if(found==outputs.end())fail("Unresolved alteroutputs UID "+raw.toString());
        Reference r{id,found->second,copy(raw,l)};l.push(g.output_references,std::move(r));
        if(end==refs.size())break;begin=end+1;
      }
    }
  }
}
}
std::uint64_t decimal(QStringView s) {
  if(s.isEmpty()) fail("Empty unsigned decimal metadata");
  std::uint64_t n=0;
  for(const auto c:s) {
    if(c<u'0'||c>u'9')fail("Invalid unsigned decimal metadata: "+s.toString());
    n=resource_sum(resource_product(n,10),c.unicode()-u'0');
  }
  return n;
}
QString attribute(const Token &t,QStringView name) {
  for(const auto &a:t.attributes)if(a.name==name&&a.namespace_uri.isEmpty())return a.value;
  return {};
}
Description read_xml(QByteArray bytes,ReadOptions options) {
  Ledger l(options);
  // Conservative source/decoder/parser scratch admission. Per-event arena,
  // strings, indexes and geometric vector growth are additionally charged.
  l.add(resource_product(std::uint64_t(bytes.size()),16));
  const auto encoding=QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
  if(encoding!=QStringConverter::Utf8&&encoding!=QStringConverter::Utf16LE&&encoding!=QStringConverter::Utf16BE)
    fail("XML metadata accepts UTF-8 or BOM-marked UTF-16 only");
  QStringDecoder decoder(encoding,QStringConverter::Flag::Stateless);const QString unicode=decoder(bytes);
  if(decoder.hasError())fail("Invalid XML Unicode encoding");
  preflight(unicode);
  Description d;d.original_xml=std::move(bytes);
  DenyResolver resolver;QXmlStreamReader xml(unicode);xml.setEntityResolver(&resolver);
  std::vector<TokenId> stack;
  while(!xml.atEnd()) {
    const auto type=xml.readNext();Token t;
    t.line=xml.lineNumber();t.column=xml.columnNumber();t.parent=stack.empty()?no_token:stack.back();
    if(type==QXmlStreamReader::StartElement) {
      t.kind=Kind::Start;t.name=copy(xml.qualifiedName(),l);t.namespace_uri=copy(xml.namespaceUri(),l);
      for(const auto &a:xml.attributes()) {
        Attribute a2{copy(a.qualifiedName(),l),copy(a.namespaceUri(),l),copy(a.value(),l)};
        l.push(t.attributes,std::move(a2));
      }
      for(const auto &n:xml.namespaceDeclarations()) {
        Attribute a{copy(n.prefix(),l),{},copy(n.namespaceUri(),l)};l.push(t.namespaces,std::move(a));
      }
      const auto id=d.tokens.size();l.push(d.tokens,std::move(t));l.push(stack,id);
      if(d.root==no_token)d.root=id;
      continue;
    }
    if(type==QXmlStreamReader::EndElement) {
      if(stack.empty())fail("Unbalanced XML end element");
      t.kind=Kind::End;t.matching=stack.back();d.tokens[stack.back()].matching=d.tokens.size();
      t.name=copy(xml.qualifiedName(),l);stack.pop_back();
    } else if(type==QXmlStreamReader::Characters) {
      t.kind=Kind::Text;t.text=copy(xml.text(),l);t.cdata=xml.isCDATA();
    } else if(type==QXmlStreamReader::Comment) { t.kind=Kind::Comment;t.text=copy(xml.text(),l);
    } else if(type==QXmlStreamReader::ProcessingInstruction) {
      t.kind=Kind::ProcessingInstruction;t.name=copy(xml.processingInstructionTarget(),l);
      t.text=copy(xml.processingInstructionData(),l);
    } else if(type==QXmlStreamReader::DTD) {
      if(!xml.entityDeclarations().isEmpty()||!xml.notationDeclarations().isEmpty())fail("XML DTD declarations forbidden");
      t.kind=Kind::Doctype;t.text=copy(xml.text(),l);
      d.doctype_name=copy(xml.dtdName(),l);d.doctype_public_id=copy(xml.dtdPublicId(),l);
      d.doctype_system_id=copy(xml.dtdSystemId(),l);
    } else if(type==QXmlStreamReader::EntityReference) fail("Unresolved XML entities forbidden");
    else continue;
    l.push(d.tokens,std::move(t));
  }
  if(resolver.attempted)fail("XML entity resolution forbidden");
  if(xml.hasError())fail("XML error at line "+QString::number(xml.lineNumber())+": "+xml.errorString());
  if(!stack.empty())fail("Incomplete XML document");
  validate(d,l);d.accounted_bytes=l.used;return d;
}
Description read_xml_file(const QString &path,ReadOptions options) {
  QFile f(path);if(!f.open(QIODevice::ReadOnly))fail(f.errorString());
  const auto size=f.size();if(size<0)fail("Cannot determine XML metadata length");
  Ledger l(options);l.add(resource_product(std::uint64_t(size),16));
  if(std::uint64_t(size)>std::uint64_t(QByteArray().max_size()))fail("XML exceeds QByteArray representation");
  QByteArray bytes;bytes.reserve(qsizetype(size));
  while(bytes.size()<size) {
    const auto old=bytes.size();bytes.resize(qsizetype(size));
    const auto n=f.read(bytes.data()+old,size-old);
    if(n<=0)fail("XML metadata changed or read failed");bytes.resize(old+n);
  }
  if(!f.atEnd()||f.size()!=size)fail("XML metadata length changed while reading");
  return read_xml(std::move(bytes),options);
}
QByteArray dump_json(const Description &d) {
  // Dump is a separate allocation: admit its conservative expansion estimate.

  auto attrs=[](const std::vector<Attribute> &as) {
    QJsonArray out;for(const auto &a:as)out.append(QJsonObject{{"name",a.name},{"namespace",a.namespace_uri},{"value",a.value}});return out;
  };
  QJsonArray tokens;
  auto index=[](TokenId id)->QJsonValue{return id==no_token?QJsonValue(QJsonValue::Null):QJsonValue(QString::number(id));};
  for(const auto &t:d.tokens) tokens.append(QJsonObject{{"kind",int(t.kind)},{"parent",index(t.parent)},
    {"matching",index(t.matching)},{"name",t.name},{"namespace",t.namespace_uri},{"text",t.text},
    {"attributes",attrs(t.attributes)},{"namespaces",attrs(t.namespaces)},{"cdata",t.cdata},
    {"line",QString::number(t.line)},{"column",QString::number(t.column)}});
  QJsonArray diagnostics;for(const auto &message:d.diagnostics)diagnostics.append(message);
  QJsonArray graphs;for(const auto &g:d.graphs) {
    auto ids=[&](const std::vector<TokenId> &v){QJsonArray a;for(auto id:v)a.append(index(id));return a;};
    QJsonArray refs;for(const auto &r:g.output_references)refs.append(QJsonObject{{"input",index(r.input)},
      {"output",index(r.output)},{"raw_uid",r.raw_uid}});
    graphs.append(QJsonObject{{"token",index(g.token)},{"inputs",ids(g.inputs)},{"outputs",ids(g.outputs)},
      {"presets",ids(g.presets)},{"output_references",refs}});
  }
  return QJsonDocument(QJsonObject{{"purpose","description XML metadata only; no archive/graph execution"},
    {"original_xml_bytes",QString::number(d.original_xml.size())},
    {"original_xml_sha256",QString(QCryptographicHash::hash(d.original_xml,QCryptographicHash::Sha256).toHex())},
    {"format_version",d.format_version},{"known_schema",d.known_schema},{"root",index(d.root)},
    {"doctype_name",d.doctype_name},{"doctype_public_id",d.doctype_public_id},{"doctype_system_id",d.doctype_system_id},
    {"tokens",tokens},{"graphs",graphs},{"diagnostics",diagnostics}}).toJson();
}
}
