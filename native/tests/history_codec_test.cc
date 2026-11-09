// SPDX-License-Identifier: GPL-3.0-or-later
#include "HistoryArchiveCodec.hh"
#include <QDataStream>
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 const auto consume=[](HistoryArchiveReader &reader){while(!reader.atEnd()){const auto block=reader.read(65536);assert(!block.isEmpty());}};
 HistoryArchiveWriter writer;
 QByteArray pattern(65536,0);for(qsizetype i=0;i<pattern.size();++i)pattern[i]=char((i*37+i/257)%251);
 constexpr int repetitions=8193; // >512MiB logical, bounded codec buffers.
 for(int i=0;i<repetitions;++i)assert(writer.write(pattern)==pattern.size());
 const auto logical=quint64(pattern.size())*repetitions;assert(writer.logical_size()==logical);
 const auto bytes=writer.finish();assert(bytes.startsWith("BTSHIST2")&&bytes.size()<logical/10);
 HistoryArchiveReader reader(bytes);for(int i=0;i<repetitions;++i)assert(reader.read(pattern.size())==pattern);assert(reader.complete());
 const auto legacy=qCompress(pattern,1);HistoryArchiveReader old(legacy);assert(old.readAll()==pattern&&old.complete());
 auto corrupt=bytes;corrupt[30]^=char(0xff);bool rejected=false;try{HistoryArchiveReader invalid(corrupt);consume(invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 auto truncated=bytes.chopped(1);rejected=false;try{HistoryArchiveReader invalid(truncated);consume(invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 auto trailing=bytes+"x";HistoryArchiveReader extra(trailing);consume(extra);assert(!extra.complete());
 auto bomb=bytes;qToBigEndian<quint32>(0xffffffffu,reinterpret_cast<uchar *>(bomb.data()+24));rejected=false;try{HistoryArchiveReader invalid(bomb);invalid.read(1);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 std::cout<<"History block codec: logical="<<logical<<" compressed="<<bytes.size()<<" legacy=pass corruption=pass bounded_buffers=pass\n";
}
