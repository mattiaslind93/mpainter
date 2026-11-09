// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace bts {
inline constexpr const char *python_extension_api_source = R"BTS_EXT(
import weakref as _weakref, uuid as _uuid
_current_plugin = ''
event = _module('event')
class Event:
    def __init__(self, **data): self.__dict__.update(data)
_event_types = {}
for _name in ('GraphicalUserInterfaceStarted', 'ProjectOpened', 'ProjectCreated',
              'ProjectEditionEntered', 'ProjectEditionLeft', 'ProjectClosed',
              'ProjectSaved', 'BusyStatusChanged', 'ExportTexturesAboutToStart',
              'ExportTexturesEnded', 'BakingProcessAboutToStart',
              'BakingProcessProgress', 'BakingProcessEnded'):
    _event_types[_name] = type(_name, (Event,), {'__module__': 'bts.event'})
    setattr(event, _name, _event_types[_name])
event.Event = Event
def _owned_call(callback, owner, *args):
    global _current_plugin
    previous, _current_plugin = _current_plugin, owner
    try: callback(*args)
    except Exception: _traceback.print_exc()
    finally: _current_plugin = previous
class Dispatcher:
    def __init__(self): self._connections = {}; self._next = 0
    def _watch(self): _call('events.watch', enabled=bool(self._connections))
    def _connect(self, event_cls, callback, strong):
        if event_cls not in _event_types.values() or not callable(callback): raise TypeError('Expected an event class and callable')
        _call("events.owner")
        self._next += 1; token = self._next
        if strong: reference = lambda: callback
        else:
            def collected(_):
                self._connections.pop(token, None)
            reference = _weakref.WeakMethod(callback, collected) if getattr(callback, '__self__', None) is not None else _weakref.ref(callback, collected)
        self._connections[token] = (event_cls, reference, _current_plugin)
        self._watch()
        return token
    def connect(self, event_cls, callback): return self._connect(event_cls, callback, False)
    def connect_strong(self, event_cls, callback): return self._connect(event_cls, callback, True)
    def disconnect(self, event_cls, callback):
        _call("events.owner")
        for token, (kind, reference, _) in list(self._connections.items()):
            if kind is event_cls and reference() == callback: self._connections.pop(token, None)
        self._watch()
    def _remove_owner(self, owner):
        for token, (_, _, parent) in list(self._connections.items()):
            if parent == owner: self._connections.pop(token, None)
        self._watch()
    def _emit(self, value):
        for token, (kind, reference, owner) in list(self._connections.items()):
            callback = reference()
            if callback is None: self._connections.pop(token, None)
            elif token in self._connections and isinstance(value, kind): _owned_call(callback, owner, value)
event.DISPATCHER = Dispatcher()
ui = _module('ui')
_ui_callbacks, _ui_handles = {}, {}
class UiHandle:
    def __init__(self, value): self.id = value['id']; self.object_name = value.get('object_name', ''); _ui_handles[self.id] = self
    def is_valid(self): return _call('ui.is_valid', id=self.id)
    def set_visible(self, visible): return _call('ui.set_visible', id=self.id, visible=visible)
    def show(self): return self.set_visible(True)
    def hide(self): return self.set_visible(False)
    def trigger(self): return _call('ui.trigger', id=self.id)
    def text(self): return _call('ui.get_text', id=self.id)
    def set_text(self, text): return _call('ui.set_text', id=self.id, text=text)
    def find_child(self, object_name): return UiHandle(_call('ui.find_child', id=self.id, object_name=object_name))
class Widget:
    def __init__(self, kind, text='', *, object_name='', callback=None, children=()):
        self.kind, self.text, self.object_name, self.callback, self.children = kind, text, object_name, callback, children
    def _encode(self, tokens):
        value = {'type': self.kind, 'text': self.text, 'object_name': self.object_name}
        if self.kind == 'panel': value['children'] = [child._encode(tokens) for child in self.children]
        if self.kind == 'button': value['callback'] = _ui_callback(self.callback, tokens)
        return value
class Panel(Widget):
    def __init__(self, *children, object_name=''): super().__init__('panel', object_name=object_name, children=children)
class Label(Widget):
    def __init__(self, text, *, object_name=''): super().__init__('label', text, object_name=object_name)
class Button(Widget):
    def __init__(self, text, callback, *, object_name=''): super().__init__('button', text, callback=callback, object_name=object_name)
class LineEdit(Widget):
    def __init__(self, text='', *, object_name=''): super().__init__('edit', text, object_name=object_name)
class TextView(Widget):
    def __init__(self, text='', *, object_name=''): super().__init__('text', text, object_name=object_name)
def _ui_callback(callback, tokens):
    if not callable(callback): raise TypeError('UI callback must be callable')
    token = str(_uuid.uuid4()); tokens.append(token)
    _ui_callbacks[token] = [callback, _current_plugin, None]
    return token
def _ui_create(kind, title='', parent=None, widget=None, callback=None, object_name=''):
    tokens = []
    data = {'kind': kind, 'title': title, 'parent': parent.id if parent else 'main',
            'owner': _current_plugin, 'object_name': object_name or 'bts_' + _uuid.uuid4().hex}
    try:
        if widget is not None:
            if not isinstance(widget, Widget): raise TypeError('Expected a bts.ui widget description')
            data['widget'] = widget._encode(tokens)
        if callback is not None: data['callback'] = _ui_callback(callback, tokens)
        handle = UiHandle(_call('ui.create', **data))
        for token in tokens: _ui_callbacks[token][2] = handle.id
        return handle
    except Exception:
        for token in tokens: _ui_callbacks.pop(token, None)
        raise
def _forget_ui(ids):
    for handle in ids: _ui_handles.pop(handle, None)
    for token, (_, _, root) in list(_ui_callbacks.items()):
        if root in ids: _ui_callbacks.pop(token, None)
def _delete_ui(handle): _forget_ui(_call('ui.delete', id=handle.id))
ui.UiHandle, ui.Panel, ui.Label, ui.Button, ui.LineEdit, ui.TextView = UiHandle, Panel, Label, Button, LineEdit, TextView
ui.get_main_window = lambda: UiHandle(_call('ui.main_window'))
ui.add_dock_widget = lambda widget, *, title='Plugin', object_name='': _ui_create('dock', title, widget=widget, object_name=object_name)
ui.add_menu = lambda title, *, parent=None, object_name='': _ui_create('menu', title, parent=parent, object_name=object_name)
ui.add_action = lambda parent, text, callback, *, object_name='': _ui_create('action', text, parent=parent, callback=callback, object_name=object_name)
ui.add_toolbar = lambda title, *, object_name='': _ui_create('toolbar', title, object_name=object_name)
ui.add_plugins_toolbar_widget = lambda widget, *, parent=None, object_name='': _ui_create('toolbar_widget', parent=parent, widget=widget, object_name=object_name)
ui.delete_ui_element = _delete_ui
ui.get_layout = lambda: _call('ui.get_layout')
ui.set_layout = lambda state: _call('ui.set_layout', **state)
ui.reset_layout = lambda: _call('ui.reset_layout')
def _deliver_events(values):
    for item in values:
        name, data = item['type'], item['data']
        if name == 'UiCallback':
            entry = _ui_callbacks.get(data['token'])
            if entry: _owned_call(entry[0], entry[1])
        elif name == 'UiDeleted': _forget_ui(data['ids'])
        elif name in _event_types:
            if name == 'BakingProcessAboutToStart': data['stop_source'] = jobs.Job(data)
            event.DISPATCHER._emit(_event_types[name](**data))
bts._deliver_events = _deliver_events
def _cleanup_plugin(name):
    event.DISPATCHER._remove_owner(name)
    _forget_ui(_call('ui.remove_owner', owner=name))
def _close_plugins():
    global _current_plugin
    for name, module in list(_plugins.items()):
        previous, _current_plugin = _current_plugin, name
        try:
            if hasattr(module, 'close_plugin'): module.close_plugin()
        except Exception: _traceback.print_exc()
        finally:
            _cleanup_plugin(name); _current_plugin = previous
            _sys.modules.pop(name, None)
    _plugins.clear()
def _reload_plugins(directory):
    global _current_plugin
    import importlib.util, pathlib
    _close_plugins()
    for path in sorted(pathlib.Path(directory).glob('*.py')):
        name = '_bts_plugin_' + path.stem
        previous, _current_plugin = _current_plugin, name
        try:
            spec = importlib.util.spec_from_file_location(name, str(path))
            module = importlib.util.module_from_spec(spec)
            _sys.modules[name] = module
            exec(compile(path.read_text(encoding='utf-8'), str(path), 'exec'), module.__dict__)
            _plugins[name] = module
            if hasattr(module, 'start_plugin'): module.start_plugin()
        except Exception:
            _traceback.print_exc(); _cleanup_plugin(name)
            _plugins.pop(name, None); _sys.modules.pop(name, None)
        finally: _current_plugin = previous
    print('Reloaded', len(_plugins), 'BTS Python plugins')
bts._reload_plugins, bts._shutdown = _reload_plugins, _close_plugins
bts.__version__ = '0.2.0'
)BTS_EXT";
}
