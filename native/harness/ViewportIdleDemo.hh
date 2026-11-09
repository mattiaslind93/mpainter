// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
class QMainWindow;
namespace bts {
void start_viewport_idle_demo(QMainWindow *, std::function<bool()>);
}
