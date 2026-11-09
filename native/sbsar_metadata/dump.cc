// SPDX-License-Identifier: GPL-3.0-or-later
#include "Description.hh"
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);const auto args=app.arguments();
  if(args.size()!=3||args[1]!=u"--xml") {
    std::fprintf(stderr,"Usage: bts-sbsar-metadata-dump --xml description.xml\nXML metadata only; no SBSAR archive/graph support.\n");return 2;
  }
  try {
    const auto json=bts::sbsar_metadata::dump_json(bts::sbsar_metadata::read_xml_file(args[2]));
    QFile out;if(!out.open(stdout,QIODevice::WriteOnly)||out.write(json)!=json.size())return 1;
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
