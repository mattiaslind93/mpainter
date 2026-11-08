"""Fixtures for check-windows-uuid-resets.py: real QUuid resets fail, look-alikes pass."""
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

checker = Path(__file__).with_name('check-windows-uuid-resets.py')
model = '''
#pragma once
#include <QUuid>
#include <array>
#include <optional>
#include <vector>
namespace bts {
struct Effect {QUuid id=QUuid::createUuid();float fill=0;};
struct Layer {QUuid id=QUuid::createUuid(),parent;std::vector<Effect> effects;int depth=0;};
struct Set {std::vector<Layer> layers;QUuid selected_layer,selected_effect;bool selected_mask=false;
  std::array<QUuid,3> roots;std::optional<QUuid> anchor;std::vector<QUuid> order;
  void clear(){selected_effect=QUuid();anchor={};order={};}};
}
'''
cases = {
    'member': ('void f(bts::Set &set){set.selected_mask=false;set.selected_effect={};}', 'set.selected_effect'),
    'spaced': ('void f(bts::Set &set){set.selected_layer = { };}', 'set.selected_layer'),
    'auto-field': ('void f(bts::Set &set){auto &layer=set.layers.back();layer.parent={};}', 'layer.parent'),
    'subscript-chain': ('void f(bts::Set &set){set.layers[0].parent={};}', 'set.layers[0].parent'),
    'array-element': ('void f(bts::Set set){auto invalid=set;invalid.roots[0]={};}', 'invalid.roots[0]'),
    'local': ('void f(){QUuid id=QUuid::createUuid();id={};}', 'id'),
    'auto-local': ('void f(bts::Set &set){auto &id=set.selected_layer;id={};}', 'id'),
    'call-chain': ('struct H{bts::Set &set();};void f(H h){h.set().selected_effect={};}', 'h.set().selected_effect'),
    'alias': ('using LayerId=QUuid;void f(){LayerId id;id={};}', 'id'),
}
clean = '''
void ok(bts::Set &set,bts::Effect &effect){
  QUuid declared={};QUuid braced{};set.selected_effect=QUuid();
  set.anchor={};set.order={};set.roots={};effect.fill={};
  auto &first=set.layers.front().effects.front();first.fill={};
  std::vector<int> numbers;numbers={};
  const char *text="set.selected_effect={};"; // set.selected_effect={};
  /* set.layers[0].parent={}; */ bool same=set.selected_layer=={};(void)same;(void)text;
}
void defaults(QUuid id={},const QUuid &other={});
'''
with tempfile.TemporaryDirectory(prefix='uuid-resets-') as directory:
    root = Path(directory)
    (root / 'Model.hh').write_text(model)
    (root / 'clean.cc').write_text('#include "Model.hh"\n' + clean)
    result = subprocess.run([sys.executable, str(checker), str(root)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    for name, (body, expression) in cases.items():
        source = root / f'{name}.cc'
        source.write_text('#include "Model.hh"\n' + textwrap.dedent(body) + '\n')
        result = subprocess.run([sys.executable, str(checker), str(root)], capture_output=True, text=True)
        assert result.returncode == 1, (name, result.stdout, result.stderr)
        assert f'{source.name}:2: `{expression} = {{}}`' in result.stdout, (name, result.stdout)
        assert result.stdout.count('\n') == 1, (name, result.stdout)
        source.unlink()
    # An .inc without includes of its own resolves through the file including it.
    (root / 'Witness.inc').write_text('auto &set=document.set;set.selected_effect={};\n')
    (root / 'main.cc').write_text('#include "Model.hh"\nstruct D{bts::Set set;};D document;\nvoid g(){\n#include "Witness.inc"\n}\n')
    result = subprocess.run([sys.executable, str(checker), str(root)], capture_output=True, text=True)
    assert result.returncode == 1 and 'Witness.inc:1: `set.selected_effect = {}`' in result.stdout, result.stdout
print('Windows QUuid reset check fixtures passed.')
