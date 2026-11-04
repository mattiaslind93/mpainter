// Test-only default-observation-only-v2: compile with the exact frozen Cycles blender/session.cpp command.
#include "CyclesDefaultCompletionProbe.hh"
#include "CyclesDefaultCompletionEpoch.hh"
#include <Python.h>
#include "blender/session.h"
#include "session/session.h"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace bts { namespace {
struct Gil {PyGILState_STATE state=PyGILState_Ensure();~Gil(){PyGILState_Release(state);}};
struct PyRef {PyObject *value=nullptr;explicit PyRef(PyObject *v=nullptr):value(v){}~PyRef(){Py_XDECREF(value);}PyRef(const PyRef&)=delete;};
std::string error_text(){PyObject *type=nullptr,*value=nullptr,*trace=nullptr;PyErr_Fetch(&type,&value,&trace);PyRef t(type),v(value),tb(trace);PyRef text(value?PyObject_Str(value):nullptr);const char *s=text.value?PyUnicode_AsUTF8(text.value):nullptr;return s?s:"Python operation failed";}
void checked(bool okay){if(!okay)throw std::runtime_error(error_text());}
std::string quoted(const std::string &text){std::ostringstream out;out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else out<<char(c);}out<<'"';return out.str();}
struct Event {std::uint64_t generation,epoch,sequence;int sample;double progress;long long ns;std::string status,substatus;};
struct State {
 mutable std::mutex mutex;CyclesCompletion latest;std::vector<Event> events;std::vector<std::string> sessions;
 std::atomic_bool failed=false;bool armed=false;std::uint64_t generations=0;
 ccl::BlenderSession *active=nullptr;ccl::Session *session=nullptr;DefaultCompletionEpoch timeline;
 std::string failure,initial_profile;
 void fail(const char *message)noexcept {failed.store(true);try{std::lock_guard lock(mutex);failure=message;}catch(...) {}}
 void update(ccl::BlenderSession *owner,ccl::Session *observed,std::uint64_t generation)noexcept {
  try{
   auto &progress=observed->progress;const int sample=progress.get_current_sample();const double value=progress.get_progress();std::string status,substatus;progress.get_status(status,substatus);
   {std::lock_guard lock(mutex);
    if(generation!=latest.generation)throw std::runtime_error("Unexpected old Session callback after new generation");
    if(!timeline.observe(sample,value,status.find("Rendering Done")!=std::string::npos))throw std::runtime_error("Invalid actual default progress sample");
    latest.epoch=timeline.epoch;latest.terminal_epoch=timeline.terminal_epoch;
    latest.sample=sample;latest.progress=value;latest.status=status;++latest.sequence;
    events.push_back({generation,latest.epoch,latest.sequence,sample,value,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),status,substatus});
   }
  }catch(const std::exception &error){fail(error.what());}catch(...){fail("Completion callback recording failed");}
  // Match the original callback once. Never call it under our mutex.
  try{owner->tag_redraw();}catch(...){fail("Original Cycles tag_redraw callback failed");}
 }
};
struct Bridge {std::shared_ptr<State> state;PyObject *original=nullptr;~Bridge(){Py_XDECREF(original);}};
PyObject *reset_wrapper(PyObject *capsule,PyObject *args)noexcept {
 std::shared_ptr<State> failure_state;
 try{
  auto *bridge=static_cast<Bridge *>(PyCapsule_GetPointer(capsule,"bts.cycles.reset-observer"));if(!bridge)return nullptr;
  if(!PyTuple_Check(args)||PyTuple_Size(args)<1){PyErr_SetString(PyExc_RuntimeError,"Malformed Cycles reset arguments");return nullptr;}
  auto *owner=static_cast<ccl::BlenderSession *>(PyLong_AsVoidPtr(PyTuple_GET_ITEM(args,0)));if(PyErr_Occurred())return nullptr;
  if(!owner){PyErr_SetString(PyExc_RuntimeError,"Null Cycles BlenderSession");return nullptr;}
  auto state=bridge->state;failure_state=state;
  if(owner->background||!owner->b_v3d||!owner->b_rv3d)throw std::runtime_error("Completion observer requires a real nonbackground viewport Session");
  auto *before=owner->session.get();
  {std::lock_guard lock(state->mutex);if(before&&(state->active!=owner||state->session!=before))throw std::runtime_error("Refusing preexisting/unobserved live Cycles Session");if(!before&&!state->armed)throw std::runtime_error("Unarmed new Cycles Session generation");}
  // Preserve the original result and exception without translation.
  PyObject *result=PyObject_CallObject(bridge->original,args);if(!result){state->fail("Original _cycles.reset raised; Python exception preserved");return nullptr;}
  try{
   auto *after=owner->session.get();if(!after)throw std::runtime_error("Original first Cycles reset did not create Session");
   if(after->params.background||after->params.use_auto_tile)throw std::runtime_error("Completion sample counter requires a single untiled viewport Session");
   if(!before){
    std::uint64_t generation;
    {std::lock_guard lock(state->mutex);generation=++state->generations;state->armed=false;state->active=owner;state->session=after;state->timeline={};state->latest={generation,1,0,0,0,0,{},{}};state->sessions.push_back("{\"generation\":"+std::to_string(generation)+",\"viewport\":true,\"background\":false,\"auto_tile\":false}");}
    // First reset_session returned for viewport; synchronize/start follows in
    // addon.view_update. No running callback is ever replaced.
    after->progress.set_update_callback([state,owner,after,generation]()noexcept{state->update(owner,after,generation);});
   }else if(after!=before)throw std::runtime_error("Unexpected live Session replacement during reset");
  }catch(...){Py_DECREF(result);throw;}
  return result;
 }catch(const std::exception &error){if(failure_state)failure_state->fail(error.what());PyErr_SetString(PyExc_RuntimeError,error.what());return nullptr;}catch(...){if(failure_state)failure_state->fail("Cycles reset observer failed");PyErr_SetString(PyExc_RuntimeError,"Cycles reset observer failed");return nullptr;}
}
PyMethodDef reset_method={"reset",reset_wrapper,METH_VARARGS,"Test-only first-reset observer; forwards original reset."};
void capsule_delete(PyObject *capsule){auto *bridge=static_cast<Bridge *>(PyCapsule_GetPointer(capsule,"bts.cycles.reset-observer"));if(bridge)delete bridge;else PyErr_Clear();}
const char *profile_script=R"PY(
import bpy, json
_scenes=list(bpy.data.scenes)
assert len(_scenes)==1, 'Default observation requires exactly one native Scene'
_scene=_scenes[0]
def _value(v):
    if isinstance(v,(bool,int,float,str)) or v is None: return v
    try: return [_value(x) for x in v]
    except TypeError: return str(v)
def _tree(tree):
    if tree is None: return None
    return {'nodes': [{'name':n.name,'type':n.bl_idname,'inputs':[(s.identifier,_value(s.default_value)) for s in n.inputs if hasattr(s,'default_value')], 'image':getattr(getattr(n,'image',None),'filepath',None)} for n in tree.nodes], 'links':[(l.from_node.name,l.from_socket.identifier,l.to_node.name,l.to_socket.identifier) for l in tree.links]}
def _profile():
    c=_scene.cycles
    view=_scene.view_settings
    color_management={'view_transform':view.view_transform,'look':view.look,'exposure':view.exposure,'gamma':view.gamma,'display_device':_scene.display_settings.display_device,'dither':_scene.render.dither_intensity,'working_space':bpy.data.colorspace.working_space,'working_space_interop_id':bpy.data.colorspace.working_space_interop_id}
    return {'scene':_scene.name,'frame':_scene.frame_current,'samples':c.preview_samples,'sampling_pattern':c.sampling_pattern,'filter_type':c.pixel_filter_type,'filter_width':c.filter_width,'adaptive':c.use_preview_adaptive_sampling,'animated_seed':c.use_animated_seed,'seed':c.seed,'pixel_jitter':c.use_pixel_jitter,'denoise':c.use_preview_denoising,'color_management':color_management,'world':_tree(_scene.world.node_tree if _scene.world else None),'lights':[{'name':o.name,'matrix':_value(o.matrix_world),'type':o.data.type,'energy':o.data.energy,'color':_value(o.data.color),'nodes':_tree(o.data.node_tree)} for o in _scene.objects if o.type=='LIGHT']}
)PY";
std::string profile(){
 PyRef globals(PyDict_New());checked(globals.value);checked(PyDict_SetItemString(globals.value,"__builtins__",PyEval_GetBuiltins())==0);
 PyRef start(PyRun_String(profile_script,Py_file_input,globals.value,globals.value));checked(start.value);
 PyRef current(PyRun_String("json.dumps(_profile(),sort_keys=True)",Py_eval_input,globals.value,globals.value));checked(current.value);
 return PyUnicode_AsUTF8(current.value);
}
} // namespace
struct CyclesDefaultCompletionProbe::Impl {std::shared_ptr<State> state=std::make_shared<State>();PyObject *module=nullptr,*original=nullptr,*wrapper=nullptr;};
CyclesDefaultCompletionProbe::CyclesDefaultCompletionProbe():impl_(std::make_unique<Impl>()){}
CyclesDefaultCompletionProbe::~CyclesDefaultCompletionProbe(){try{restore();}catch(...) {}}
void CyclesDefaultCompletionProbe::install_observation_only(){
 Gil gil;if(impl_->wrapper)throw std::runtime_error("Completion observer already installed");
 impl_->state->initial_profile=profile();impl_->module=PyImport_ImportModule("_cycles");checked(impl_->module);impl_->original=PyObject_GetAttrString(impl_->module,"reset");checked(impl_->original);
 auto bridge=std::make_unique<Bridge>();bridge->state=impl_->state;bridge->original=Py_NewRef(impl_->original);PyRef capsule(PyCapsule_New(bridge.get(),"bts.cycles.reset-observer",capsule_delete));checked(capsule.value);bridge.release();
 impl_->wrapper=PyCFunction_New(&reset_method,capsule.value);checked(impl_->wrapper);checked(PyObject_SetAttrString(impl_->module,"reset",impl_->wrapper)==0);
}
void CyclesDefaultCompletionProbe::arm_entry(){std::lock_guard lock(impl_->state->mutex);if(impl_->state->armed)throw std::runtime_error("Completion entry already armed");impl_->state->armed=true;}
void CyclesDefaultCompletionProbe::restore(){
 if(!impl_->module&&!impl_->original&&!impl_->wrapper)return;Gil gil;
 // Terminally release our Python references while Python and its GIL are
 // alive, including mismatch/error paths. A later QWidget-owned destructor
 // must never retry Python access after host shutdown.
 struct Release {Impl *impl;~Release(){Py_CLEAR(impl->wrapper);Py_CLEAR(impl->original);Py_CLEAR(impl->module);}} release{impl_.get()};
 try{
  if(!impl_->module)return;PyRef current(PyObject_GetAttrString(impl_->module,"reset"));checked(current.value);
  if(impl_->wrapper&&current.value!=impl_->wrapper)throw std::runtime_error("Cycles reset observer replaced by another writer");
  if(impl_->wrapper)checked(PyObject_SetAttrString(impl_->module,"reset",impl_->original)==0);
 }catch(const std::exception &error){impl_->state->fail(error.what());throw;}catch(...){impl_->state->fail("Cycles reset observer restoration failed");throw;}
}
CyclesCompletion CyclesDefaultCompletionProbe::snapshot()const {std::lock_guard lock(impl_->state->mutex);auto out=impl_->state->latest;if(impl_->state->failed.load())out.error=impl_->state->failure.empty()?"Completion observer failed":impl_->state->failure;return out;}
std::string CyclesDefaultCompletionProbe::profile_json()const {Gil gil;return profile();}
std::string CyclesDefaultCompletionProbe::trace_json()const {
 std::lock_guard lock(impl_->state->mutex);std::ostringstream out;out<<std::setprecision(17)<<"{\"profile\":"<<(impl_->state->initial_profile.empty()?"null":impl_->state->initial_profile)<<",\"failed\":"<<(impl_->state->failed.load()?"true":"false")<<",\"failure\":"<<quoted(impl_->state->failure)<<",\"events\":[";bool first=true;
 for(const auto &e:impl_->state->events){if(!first)out<<',';first=false;out<<"{\"generation\":"<<e.generation<<",\"epoch\":"<<e.epoch<<",\"sequence\":"<<e.sequence<<",\"sample\":"<<e.sample<<",\"progress\":"<<e.progress<<",\"steady_ns\":"<<e.ns<<",\"status\":"<<quoted(e.status)<<",\"substatus\":"<<quoted(e.substatus)<<'}';}out<<"],\"sessions\":[";first=true;for(const auto &session:impl_->state->sessions){if(!first)out<<',';first=false;out<<session;}out<<"]}";return out.str();
}
}
