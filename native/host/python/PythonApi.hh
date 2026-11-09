// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace bts {
inline constexpr const char *python_api_source = R"BTS_PY(
__name__ = "_bts_runtime"
import sys as _sys, types as _types, json as _json, traceback as _traceback
import _bts_host as _native
bts = _types.ModuleType('bts', 'mPainter native automation API (not Painter ABI).')
bts.__version__ = '0.1.0'
bts.api_version = (1, 0)
bts._pending = []
def _call(operation, **parameters):
    return _json.loads(_native.call(operation, _json.dumps(parameters, allow_nan=False)))['result']
def _module(name):
    module = _types.ModuleType('bts.' + name)
    setattr(bts, name, module)
    _sys.modules[module.__name__] = module
    return module
project = _module('project')
class Settings:
    def __init__(self, *, resolution=None, import_cameras=None, normal_map_format=None,
                 preserve_tiles=None, tangent_per_fragment=None, auto_unwrap=None):
        self.values = {key: value for key, value in locals().copy().items()
                       if key != 'self' and value is not None}
def _create(mesh_file_path, *, settings=None, template_file_path=None):
    if settings is not None and not isinstance(settings, Settings):
        raise TypeError('settings must be bts.project.Settings')
    return _call('project.create', mesh_file_path=str(mesh_file_path),
                 settings=settings.values if settings else {},
                 template_file_path=str(template_file_path) if template_file_path else '')
def _defer(callback):
    if not callable(callback): raise TypeError('callback must be callable')
    bts._pending.append(callback)
    _call('project.defer')
def _drain():
    pending, bts._pending = bts._pending, []
    for index, callback in enumerate(pending):
        if project.is_busy():
            bts._pending.extend(pending[index:]); _call('project.defer'); break
        try: callback()
        except Exception: _traceback.print_exc()
project.Settings = Settings
project.create = _create
project.status = lambda: _call('project.status')
project.is_open = lambda: project.status()['is_open']
project.is_busy = lambda: project.status()['is_busy']
project.needs_saving = lambda: project.status()['needs_saving']
project.file_path = lambda: project.status()['file_path']
project.is_in_edition_state = lambda: project.is_open() and not project.is_busy()
project.open = lambda path: _call('project.open', path=str(path))
project.save = lambda: _call('project.save')
project.save_as = lambda path: _call('project.save_as', path=str(path))
project.save_as_copy = lambda path: _call('project.save_as_copy', path=str(path))
project.close = lambda *, discard=False: _call('project.close', discard=discard)
project.execute_when_not_busy = _defer
textureset = _module('textureset')
textureset.all_texture_sets = lambda: _call('textureset.list')
resource = _module('resource')
resource.list_resources = lambda: _call('resource.list')
display = _module('display')
display.settings = lambda: _call('display.settings')
display.texture_residency = lambda: _call('display.residency')
resource.import_image = lambda path, *, usage='texture', scope='project', name=None: _call('resource.import', path=str(path), usage=usage, scope=scope, name=name)
jobs = _module('jobs')
class Job:
    def __init__(self, state): self.id = state['id']; self.initial_state = dict(state)
    def status(self): return _call('jobs.status', id=self.id)
    def is_done(self): return self.status()['done']
    def cancel(self): return _call('jobs.cancel', id=self.id)
    def result(self): return _call('jobs.result', id=self.id)
jobs.Job = Job
export = _module('export')
export.list_project_textures = lambda config=None: _call('export.list', config={} if config is None else config)
export.export_async = lambda config=None: Job(_call('export.start', config={} if config is None else config))
export.export_project_textures = lambda config=None: _call('export.run', config={} if config is None else config)
baking = _module('baking')
class BakingParameters:
    def __init__(self, **common): self.common = dict(common); self.per_map = {}
    def set_baker(self, role, **options):
        if isinstance(role, bool) or not isinstance(role, int) or not 0 <= role <= 6: raise ValueError('Invalid baker role')
        self.per_map[str(role)] = dict(options)
        return self
    def to_config(self): return dict(self.common, map_options=dict(self.per_map))
baking.BakingParameters = BakingParameters
def _bake(config=None):
    if isinstance(config, BakingParameters): config = config.to_config()
    return Job(_call('baking.start', config={} if config is None else config))
baking.bake_async = _bake
baking.bake_selected_textures_async = baking.bake_async
bts._drain_ready = _drain
_sys.modules['bts'] = bts
# Console globals survive commands. Redirect only during execution, never replace
# Blender's process-global streams while ordinary rendering/baking is running.
class _Output:
    def write(self, text):
        if text: _native.output(str(text))
        return len(text)
    def flush(self): pass
_output = _Output()
_console_globals = {'__name__': '__console__', 'bts': bts}
def _execute(source):
    import contextlib, codeop
    with contextlib.redirect_stdout(_output), contextlib.redirect_stderr(_output):
        try:
            try: code = compile(source, '<BTS Python Console>', 'eval')
            except SyntaxError:
                exec(compile(source, '<BTS Python Console>', 'exec'), _console_globals)
            else:
                value = eval(code, _console_globals)
                if value is not None: print(repr(value))
            return True
        except BaseException:
            _traceback.print_exc()
            return False
_plugins = {}
def _close_plugins():
    for name, module in list(_plugins.items()):
        try:
            if hasattr(module, 'close_plugin'): module.close_plugin()
        except Exception: _traceback.print_exc()
        _sys.modules.pop(name, None)
    _plugins.clear()
def _reload_plugins(directory):
    import importlib.util, pathlib
    _close_plugins()
    for path in sorted(pathlib.Path(directory).glob('*.py')):
        name = '_bts_plugin_' + path.stem
        try:
            spec = importlib.util.spec_from_file_location(name, str(path))
            module = importlib.util.module_from_spec(spec)
            _sys.modules[name] = module
            exec(compile(path.read_text(encoding='utf-8'), str(path), 'exec'), module.__dict__)
            _plugins[name] = module
            if hasattr(module, 'start_plugin'): module.start_plugin()
        except Exception:
            _traceback.print_exc()
    print('Reloaded', len(_plugins), 'BTS Python plugins')
bts._reload_plugins = _reload_plugins
bts._shutdown = _close_plugins
)BTS_PY";
}
