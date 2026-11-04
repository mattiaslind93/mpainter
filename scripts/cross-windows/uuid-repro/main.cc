#include <QUuid>
int main() {
  QUuid id = QUuid::createUuid();
#ifdef EXPLICIT_UUID_RESET
  id = QUuid{};
#else
  id = {};
#endif
  return id.isNull() ? 0 : 1;
}
