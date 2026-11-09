// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace bts {
inline constexpr const char *python_layer_api_source = R"BTS_LAYER(
import enum as _enum
layerstack = _module('layerstack')
class ChannelType(_enum.IntEnum):
    BaseColor=0; Metallic=1; Roughness=2; Normal=3; Bump=4; AmbientOcclusion=5
    User0=6; User1=7; User2=8; User3=9; User4=10; User5=11; User6=12; User7=13; Displacement=14
textureset.ChannelType = ChannelType
class NodeStack(str, _enum.Enum): Content='content'; Mask='mask'; Substack='substack'
class ProjectionMode(_enum.IntEnum): UV=0; Triplanar=1; Planar=2; Spherical=3; Cylindrical=4; Warp=5; MatchPerUVTile=6
class BlendingMode(_enum.IntEnum):
    Normal=0; Replace=1; Multiply=2; Screen=3; Disabled=4; Divide=5; InverseDivide=6; Darken=7; Lighten=8
    LinearDodge=9; Subtract=10; InverseSubtract=11; Difference=12; Exclusion=13; SignedAddition=14
    Overlay=15; SoftLight=16; HardLight=17; VividLight=18; LinearLight=19; PinLight=20; Tint=21
    Saturation=22; Color=23; Value=24; NormalCombine=25; NormalDetail=26; NormalInverseDetail=27; Passthrough=28
class Parameters(dict):
    def __getattr__(self, name):
        try: return self[name]
        except KeyError: raise AttributeError(name) from None
    def __setattr__(self, name, value): self[name] = value
def _stack(value):
    if isinstance(value, dict): return value['id']
    if isinstance(value, str): return value
    raise TypeError('Expected a Texture Set UUID or its discovery record')
class InsertPosition:
    def __init__(self, value): self.value = value
    @staticmethod
    def from_textureset_stack(stack): return InsertPosition({'kind':'root', 'set':_stack(stack)})
    @staticmethod
    def above_node(node): return InsertPosition({'kind':'above', 'reference':node._reference})
    @staticmethod
    def below_node(node): return InsertPosition({'kind':'below', 'reference':node._reference})
    @staticmethod
    def inside_node(node, stack): return InsertPosition({'kind':'inside', 'reference':node._reference, 'stack':str(stack.value if isinstance(stack, NodeStack) else stack)})
class Node:
    def __init__(self, reference): self._reference = dict(reference)
    @property
    def uid(self): return self._reference['node']
    def info(self): return _call('layers.info', reference=self._reference)
    def is_valid(self):
        try: self.info(); return True
        except (ValueError, RuntimeError): return False
    def get_type(self): return self.info()['kind']
    def get_stack(self): self.info(); return self._reference['set']
    def get_texture_set(self): return next(value for value in textureset.all_texture_sets() if value['id']==self.get_stack())
    def get_name(self): return self.info()['name']
    def set_name(self, name): return self._set('name', name)
    name = property(get_name, set_name)
    def is_visible(self): return self.info()['visible']
    def set_visible(self, visible): return self._set('visible', visible)
    visible = property(is_visible, set_visible)
    def is_in_mask_stack(self): return self.info()['in_mask']
    def has_blending(self): return self.info()['has_blending']
    def get_opacity(self, channel=None): return _call('layers.channel', reference=self._reference, channel=channel)['opacity']
    def set_opacity(self, opacity, channel=None): return self._set('opacity', opacity, channel)
    def get_blending_mode(self, channel=None): return BlendingMode(_call('layers.channel', reference=self._reference, channel=channel)['blending'])
    def set_blending_mode(self, mode, channel=None): return self._set('blending', mode, channel)
    def _set(self, property, value, channel=None): return _call('layers.set', reference=self._reference, property=property, value=value, channel=channel)
    def _children(self, stack): return [_node(value) for value in _call('layers.children', reference=self._reference, stack=stack)]
    def sub_layers(self): return self._children('substack')
    def content_effects(self): return self._children('content')
    def mask_effects(self): return self._children('mask')
    def has_mask(self): return self.info()['has_mask']
    def add_mask(self, background=1.): return _call('layers.mask', reference=self._reference, background=background)
    def remove_mask(self): return _call('layers.mask', reference=self._reference, remove=True)
    def delete(self): return _call('layers.remove', reference=self._reference)
    @property
    def active_channels(self): return {ChannelType(value) for value in self.info()['active_channels']}
    @active_channels.setter
    def active_channels(self, channels): self._set('active_channels', list(channels))
    @property
    def source_mode(self): return self.info()['source_mode']
    def get_source(self, channel=None): return _call('layers.source', reference=self._reference, channel=channel)
    def set_source(self, channel, source):
        if isinstance(source, dict) and source.get('type')!='atom': source = source.get('resource_id', source.get('id', source))
        return _call('layers.set_source', reference=self._reference, channel=channel, source=source)
    def get_material_source(self): return self.info()['material_source'] or None
    def set_material_source(self, resource): return _call('layers.set_material', reference=self._reference, resource=resource.get('id') if isinstance(resource, dict) else resource)
    def get_projection_parameters(self): return Parameters(_call('layers.projection', reference=self._reference))
    def set_projection_parameters(self, values): return _call('layers.set_projection', reference=self._reference, parameters=dict(values))
    def get_projection_mode(self): return ProjectionMode(self.get_projection_parameters()['mode'])
    def set_projection_mode(self, mode): return self.set_projection_parameters({'mode':mode})
    def get_parameters(self): return Parameters(_call('layers.parameters', reference=self._reference))
    def set_parameters(self, values): return _call('layers.set_parameters', reference=self._reference, parameters=dict(values))
class FillLayerNode(Node): pass
class PaintLayerNode(Node): pass
class GroupLayerNode(Node): pass
class InstanceLayerNode(Node): pass
class FillEffectNode(Node): pass
class LevelsEffectNode(Node): pass
class FilterEffectNode(Node): pass
class PaintEffectNode(Node): pass
class GeneratorEffectNode(Node): pass
class CompareMaskEffectNode(Node): pass
class ColorSelectionEffectNode(Node): pass
class AnchorPointEffectNode(Node): pass
_node_classes={'fill_layer':FillLayerNode,'paint_layer':PaintLayerNode,'group':GroupLayerNode,'instance':InstanceLayerNode,'mask_fill':FillEffectNode,'levels':LevelsEffectNode,'filter':FilterEffectNode,'paint_effect':PaintEffectNode,'generator':GeneratorEffectNode,'compare_mask':CompareMaskEffectNode,'color_selection':ColorSelectionEffectNode,'anchor':AnchorPointEffectNode}
def _node(reference):
    kind = _call('layers.info', reference=reference)['kind']
    return _node_classes[kind](reference)
def _insert(kind, position, **options):
    if not isinstance(position, InsertPosition): raise TypeError('Expected an explicit InsertPosition')
    return _node(_call('layers.insert', kind=kind, position=position.value, **options))
layerstack.Node, layerstack.NodeStack, layerstack.InsertPosition = Node, NodeStack, InsertPosition
layerstack.BlendingMode, layerstack.ProjectionMode, layerstack.Parameters = BlendingMode, ProjectionMode, Parameters
for _class in _node_classes.values(): setattr(layerstack, _class.__name__, _class)
layerstack.get_root_layer_nodes = lambda stack: [_node(ref) for ref in _call('layers.roots', stack=_stack(stack))]
layerstack.get_node_by_uid = lambda uid: _node(_call('layers.lookup', node=str(uid)))
layerstack.get_selected_nodes = lambda stack=None: [_node(ref) for ref in _call('layers.selected', stack=None if stack is None else _stack(stack))]
layerstack.set_selected_nodes = lambda nodes: _call('layers.select', references=[node._reference for node in nodes])
layerstack.get_selection_target = lambda: _call('layers.selection_target')
layerstack.insert_fill = lambda position: _insert('fill', position)
layerstack.insert_paint = lambda position: _insert('paint', position)
layerstack.insert_group = lambda position: _insert('group', position)
layerstack.insert_levels = lambda position, channel=ChannelType.BaseColor: _insert('levels', position, channel=channel)
layerstack.insert_filter = lambda position, channel=ChannelType.BaseColor: _insert('filter', position, channel=channel)
layerstack.insert_generator = lambda position: _insert('generator', position)
layerstack.insert_compare_mask = lambda position: _insert('compare_mask', position)
layerstack.insert_color_selection = lambda position: _insert('color_selection', position)
layerstack.insert_anchor = lambda position: _insert('anchor', position)
layerstack.insert_instance = lambda position, source: _insert('instance', position, source=source._reference)
layerstack.insert_smart_material = lambda position, resource: _insert('smart_material', position, resource=resource.get('id') if isinstance(resource,dict) else resource)
layerstack.apply_smart_mask = lambda node, resource, replace=True: _call('layers.smart_mask', reference=node._reference, resource=resource.get('id') if isinstance(resource,dict) else resource, replace=replace)
layerstack.group_nodes = lambda nodes: _node(_call('layers.group', references=[node._reference for node in nodes]))
layerstack.atom_source = lambda value=None: {'type':'atom', **({} if value is None else {'value':value})}
textureset.get_active_stack = lambda: _call('layers.active_stack')
textureset.set_active_stack = lambda stack: _call('layers.activate_stack', stack=_stack(stack))
_event_types['LayerStacksModelDataChanged'] = type('LayerStacksModelDataChanged', (Event,), {'__module__':'bts.event'})
event.LayerStacksModelDataChanged = _event_types['LayerStacksModelDataChanged']
bts.__version__ = '0.3.0'
)BTS_LAYER";
}
