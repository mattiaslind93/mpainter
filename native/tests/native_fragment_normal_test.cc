// Thin standalone entry for the same permanent native CPU graph regression.
#include "BlenderHost.hh"
#include "NativeMaterialFragmentNormalGraphTest.hh"
#include "NativeMaterialSourceContextGraphTest.hh"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <iostream>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  try {
    bts::native_fragment_normal_test::require(argc == 2, "Expected private proof directory");
    const QString directory = argv[1];
    QDir().mkpath(directory);
    bts::BlenderHost host(QCoreApplication::applicationFilePath().toStdString(), "");
    struct Close {
      bts::BlenderHost &host;
      ~Close()
      {
        host.shutdown();
      }
    } close{host};
    auto proof = bts::verify_native_fragment_normal_graph();
    proof["source_context_graph"] = bts::verify_native_material_source_context_graph();
    QFile file(directory + "/fragment-normal-cpu.json");
    bts::native_fragment_normal_test::require(file.open(QIODevice::WriteOnly),
                                              "Cannot write private CPU proof");
    file.write(QJsonDocument(proof).toJson());
    std::cout << "NATIVE_FRAGMENT_NORMAL_CPU PASS\n";
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "NATIVE_FRAGMENT_NORMAL_CPU FAIL " << error.what() << '\n';
    return 1;
  }
}
