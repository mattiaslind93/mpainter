// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only: compile with the exact frozen Cycles blender/session.cpp command.
#include "CyclesLiveDisplacementProbe.hh"
#include <Python.h>
#include "blender/session.h"
#include "session/session.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace bts {
namespace {
struct Gil {
  PyGILState_STATE state = PyGILState_Ensure();
  ~Gil()
  {
    PyGILState_Release(state);
  }
};
struct PyRef {
  PyObject *value = nullptr;
  explicit PyRef(PyObject *v = nullptr) : value(v) {}
  ~PyRef()
  {
    Py_XDECREF(value);
  }
  PyRef(const PyRef &) = delete;
};
std::string error_text()
{
  PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
  PyErr_Fetch(&type, &value, &trace);
  PyRef t(type), v(value), tb(trace);
  PyRef text(value ? PyObject_Str(value) : nullptr);
  const char *s = text.value ? PyUnicode_AsUTF8(text.value) : nullptr;
  return s ? s : "Python operation failed";
}
void checked(bool okay)
{
  if (!okay)
    throw std::runtime_error(error_text());
}
std::string quoted(const std::string &text)
{
  std::ostringstream out;
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      out << '\\' << char(c);
    else if (c < 32)
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
    else
      out << char(c);
  }
  out << '"';
  return out.str();
}
std::string geometry_copy(ccl::Session &session,
                          std::uint64_t generation,
                          std::uint64_t epoch,
                          std::uint64_t sequence)
{
  auto *scene = session.scene.get();
  // Progress::set_update already owns update_mutex. Scene update emits progress
  // while owning Scene::mutex: blocking here would invert that ordering.
  if (!scene || !scene->mutex.try_lock())
    return "{\"available\":false,\"reason\":\"scene-lock-busy\",\"generation\":" +
           std::to_string(generation) + ",\"epoch\":" + std::to_string(epoch) + "}";
  struct Unlock {
    ccl::Scene *scene;
    ~Unlock()
    {
      scene->mutex.unlock();
    }
  } unlock{scene};
  std::ostringstream out;
  out << std::setprecision(17) << "{\"available\":true,\"generation\":" << generation
      << ",\"epoch\":" << epoch << ",\"sequence\":" << sequence << ",\"meshes\":[";
  bool first = true;
  for (const auto &geometry : scene->geometry) {
    if (!geometry->is_mesh())
      continue;
    auto *mesh = static_cast<ccl::Mesh *>(geometry);
    if (!first)
      out << ',';
    first = false;
    const auto count = mesh->num_verts();
    const auto *positions = mesh->get_position();
    const auto *original = mesh->attributes.find(ccl::ATTR_STD_POSITION_UNDISPLACED);
    const auto *uv = mesh->attributes.find(ccl::ATTR_STD_UV);
    out << "{\"name\":" << quoted(mesh->name.c_str())
        << ",\"true_displacement\":" << (mesh->has_true_displacement() ? "true" : "false")
        << ",\"transform_applied\":" << (mesh->transform_applied ? "true" : "false")
        << ",\"vertex_count\":" << count << ",\"positions\":[";
    const auto write3 = [&](const ccl::packed_float3 *values, std::size_t size) {
      for (std::size_t i = 0; i < size; ++i) {
        if (i)
          out << ',';
        const auto p = values[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
          throw std::runtime_error("Nonfinite Cycles geometry witness");
        out << '[' << p.x << ',' << p.y << ',' << p.z << ']';
      }
    };
    if (!positions || !count)
      throw std::runtime_error("Missing live Cycles mesh positions");
    write3(positions, count);
    out << "],\"undisplaced\":[";
    if (original) {
      const auto n = original->buffer_size(mesh, ccl::ATTR_PRIM_GEOMETRY) /
                     sizeof(ccl::packed_float3);
      if (n != std::size_t(count))
        throw std::runtime_error("Wrong undisplaced attribute size");
      write3(original->data<ccl::packed_float3>(), n);
    }
    out << "],\"triangles\":[";
    for (std::size_t i = 0; i < mesh->num_triangles(); ++i) {
      if (i)
        out << ',';
      const auto triangle = mesh->get_triangle(i);
      out << '[' << triangle.v[0] << ',' << triangle.v[1] << ',' << triangle.v[2] << ']';
    }
    out << "],\"uv\":[";
    if (uv) {
      const auto n = uv->buffer_size(mesh, ccl::ATTR_PRIM_GEOMETRY) / sizeof(ccl::float2);
      const auto *values = uv->data<ccl::float2>();
      for (std::size_t i = 0; i < n; ++i) {
        if (i)
          out << ',';
        if (!std::isfinite(values[i].x) || !std::isfinite(values[i].y))
          throw std::runtime_error("Nonfinite Cycles UV witness");
        out << '[' << values[i].x << ',' << values[i].y << ']';
      }
    }
    out << "]}";
  }
  out << "],\"objects\":[";
  first = true;
  for (const auto &object : scene->objects) {
    if (!first)
      out << ',';
    first = false;
    const auto tfm = object->get_tfm();
    out << "{\"name\":" << quoted(object->name.c_str()) << ",\"geometry\":"
        << quoted(object->get_geometry() ? object->get_geometry()->name.c_str() : "")
        << ",\"transform\":[";
    for (int row = 0; row < 3; ++row) {
      if (row)
        out << ',';
      const auto p = row == 0 ? tfm.x : row == 1 ? tfm.y : tfm.z;
      out << '[' << p.x << ',' << p.y << ',' << p.z << ',' << p.w << ']';
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}
struct Event {
  std::uint64_t generation, epoch, sequence;
  int sample;
  double progress;
  long long ns;
  std::string status, substatus;
};
struct State {
  mutable std::mutex mutex;
  CyclesDisplacementCompletion latest;
  std::vector<Event> events;
  std::vector<std::string> sessions;
  std::atomic_bool failed = false;
  bool armed = false;
  std::uint64_t generations = 0;
  ccl::BlenderSession *active = nullptr;
  ccl::Session *session = nullptr;
  int previous = -1;
  std::string failure, initial_profile;
  bool geometry_armed = false;
  std::uint64_t geometry_generation = 0, geometry_epoch = 0;
  std::string geometry_record;
  void fail(const char *message) noexcept
  {
    failed.store(true);
    try {
      std::lock_guard lock(mutex);
      failure = message;
    }
    catch (...) {
    }
  }
  void update(ccl::BlenderSession *owner,
              ccl::Session *observed,
              std::uint64_t generation) noexcept
  {
    try {
      auto &progress = observed->progress;
      const int sample = progress.get_current_sample();
      const double value = progress.get_progress();
      std::string status, substatus;
      progress.get_status(status, substatus);
      bool capture = false;
      std::uint64_t epoch = 0, sequence = 0;
      {
        std::lock_guard lock(mutex);
        if (generation != latest.generation)
          throw std::runtime_error("Unexpected old Session callback after new generation");
        if (previous >= 0 && sample < previous)
          ++latest.epoch;
        previous = sample;
        latest.sample = sample;
        latest.progress = value;
        latest.status = status;
        ++latest.sequence;
        if (sample == 128 && value == 1 && status.find("Rendering Done") != std::string::npos)
          latest.terminal_epoch = latest.epoch;
        events.push_back({generation,
                          latest.epoch,
                          latest.sequence,
                          sample,
                          value,
                          std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count(),
                          status,
                          substatus});
        if (geometry_armed && generation == geometry_generation &&
            latest.epoch >= geometry_epoch && latest.terminal_epoch == latest.epoch &&
            sample == 128 && value == 1 && status.find("Rendering Done") != std::string::npos)
        {
          geometry_armed = false;
          capture = true;
          epoch = latest.epoch;
          sequence = latest.sequence;
        }
      }
      // No State mutex is held while trying Scene; no Qt, GIL or owner API here.
      // Session destruction joins this callback before destroying its Scene.
      if (capture) {
        auto copy = geometry_copy(*observed, generation, epoch, sequence);
        std::lock_guard lock(mutex);
        geometry_record = std::move(copy);
      }
    }
    catch (const std::exception &error) {
      fail(error.what());
    }
    catch (...) {
      fail("Completion callback recording failed");
    }
    // Match the original callback once. Never call it under our mutex.
    try {
      owner->tag_redraw();
    }
    catch (...) {
      fail("Original Cycles tag_redraw callback failed");
    }
  }
};
struct Bridge {
  std::shared_ptr<State> state;
  PyObject *original = nullptr;
  ~Bridge()
  {
    Py_XDECREF(original);
  }
};
PyObject *reset_wrapper(PyObject *capsule, PyObject *args) noexcept
{
  std::shared_ptr<State> failure_state;
  try {
    auto *bridge = static_cast<Bridge *>(
        PyCapsule_GetPointer(capsule, "bts.cycles.reset-observer"));
    if (!bridge)
      return nullptr;
    if (!PyTuple_Check(args) || PyTuple_Size(args) < 1) {
      PyErr_SetString(PyExc_RuntimeError, "Malformed Cycles reset arguments");
      return nullptr;
    }
    auto *owner = static_cast<ccl::BlenderSession *>(PyLong_AsVoidPtr(PyTuple_GET_ITEM(args, 0)));
    if (PyErr_Occurred())
      return nullptr;
    if (!owner) {
      PyErr_SetString(PyExc_RuntimeError, "Null Cycles BlenderSession");
      return nullptr;
    }
    auto state = bridge->state;
    failure_state = state;
    if (owner->background || !owner->b_v3d || !owner->b_rv3d)
      throw std::runtime_error(
          "Completion observer requires a real nonbackground viewport Session");
    auto *before = owner->session.get();
    {
      std::lock_guard lock(state->mutex);
      if (before && (state->active != owner || state->session != before))
        throw std::runtime_error("Refusing preexisting/unobserved live Cycles Session");
      if (!before && !state->armed)
        throw std::runtime_error("Unarmed new Cycles Session generation");
    }
    // Preserve the original result and exception without translation.
    PyObject *result = PyObject_CallObject(bridge->original, args);
    if (!result) {
      state->fail("Original _cycles.reset raised; Python exception preserved");
      return nullptr;
    }
    try {
      auto *after = owner->session.get();
      if (!after)
        throw std::runtime_error("Original first Cycles reset did not create Session");
      if (after->params.background || after->params.use_auto_tile)
        throw std::runtime_error(
            "Completion sample counter requires a single untiled viewport Session");
      if (!before) {
        std::uint64_t generation;
        {
          std::lock_guard lock(state->mutex);
          generation = ++state->generations;
          state->armed = false;
          state->active = owner;
          state->session = after;
          state->previous = -1;
          state->latest = {generation, 1, 0, 0, 0, 0, {}, {}};
          state->sessions.push_back(
              "{\"generation\":" + std::to_string(generation) +
              ",\"viewport\":true,\"background\":false,\"auto_tile\":false}");
        }
        // First reset_session returned for viewport; synchronize/start follows in
        // addon.view_update. No running callback is ever replaced.
        after->progress.set_update_callback([state, owner, after, generation]() noexcept {
          state->update(owner, after, generation);
        });
      }
      else if (after != before)
        throw std::runtime_error("Unexpected live Session replacement during reset");
    }
    catch (...) {
      Py_DECREF(result);
      throw;
    }
    return result;
  }
  catch (const std::exception &error) {
    if (failure_state)
      failure_state->fail(error.what());
    PyErr_SetString(PyExc_RuntimeError, error.what());
    return nullptr;
  }
  catch (...) {
    if (failure_state)
      failure_state->fail("Cycles reset observer failed");
    PyErr_SetString(PyExc_RuntimeError, "Cycles reset observer failed");
    return nullptr;
  }
}
PyMethodDef reset_method = {"reset",
                            reset_wrapper,
                            METH_VARARGS,
                            "Test-only first-reset observer; forwards original reset."};
void capsule_delete(PyObject *capsule)
{
  auto *bridge = static_cast<Bridge *>(PyCapsule_GetPointer(capsule, "bts.cycles.reset-observer"));
  if (bridge)
    delete bridge;
  else
    PyErr_Clear();
}
const char *profile_script = R"PY(
import bpy, json
_scenes=list(bpy.data.scenes)
assert len(_scenes)==1, 'Controlled BA fixture requires exactly one native Scene'
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
std::string profile(bool configure)
{
  PyRef globals(PyDict_New());
  checked(globals.value);
  checked(PyDict_SetItemString(globals.value, "__builtins__", PyEval_GetBuiltins()) == 0);
  PyRef start(PyRun_String(profile_script, Py_file_input, globals.value, globals.value));
  checked(start.value);
  PyRef old(PyRun_String(
      "json.dumps(_profile(),sort_keys=True)", Py_eval_input, globals.value, globals.value));
  checked(old.value);
  std::string before = PyUnicode_AsUTF8(old.value);
  if (configure) {
    PyRef set(PyRun_String(
        "_scene.frame_set(1)\nc=_scene.cycles\nc.preview_samples=128\nc.use_preview_adaptive_sampling=False\nc.use_animated_seed=False\nc.seed=0\nc.use_pixel_jitter=False\nc.use_preview_denoising=False\n",
        Py_file_input,
        globals.value,
        globals.value));
    checked(set.value);
  }
  PyRef current(PyRun_String(
      "json.dumps(_profile(),sort_keys=True)", Py_eval_input, globals.value, globals.value));
  checked(current.value);
  std::string now = PyUnicode_AsUTF8(current.value);
  return configure ? "{\"original\":" + before + ",\"controlled\":" + now + "}" : now;
}
}  // namespace
struct CyclesLiveDisplacementProbe::Impl {
  std::shared_ptr<State> state = std::make_shared<State>();
  PyObject *module = nullptr, *original = nullptr, *wrapper = nullptr;
};
CyclesLiveDisplacementProbe::CyclesLiveDisplacementProbe() : impl_(std::make_unique<Impl>()) {}
CyclesLiveDisplacementProbe::~CyclesLiveDisplacementProbe()
{
  try {
    restore();
  }
  catch (...) {
  }
}
void CyclesLiveDisplacementProbe::install_controlled_profile()
{
  Gil gil;
  if (impl_->wrapper)
    throw std::runtime_error("Completion observer already installed");
  impl_->state->initial_profile = profile(true);
  impl_->module = PyImport_ImportModule("_cycles");
  checked(impl_->module);
  impl_->original = PyObject_GetAttrString(impl_->module, "reset");
  checked(impl_->original);
  auto bridge = std::make_unique<Bridge>();
  bridge->state = impl_->state;
  bridge->original = Py_NewRef(impl_->original);
  PyRef capsule(PyCapsule_New(bridge.get(), "bts.cycles.reset-observer", capsule_delete));
  checked(capsule.value);
  bridge.release();
  impl_->wrapper = PyCFunction_New(&reset_method, capsule.value);
  checked(impl_->wrapper);
  checked(PyObject_SetAttrString(impl_->module, "reset", impl_->wrapper) == 0);
}
void CyclesLiveDisplacementProbe::arm_entry()
{
  std::lock_guard lock(impl_->state->mutex);
  if (impl_->state->armed)
    throw std::runtime_error("Completion entry already armed");
  impl_->state->armed = true;
}
void CyclesLiveDisplacementProbe::restore()
{
  if (!impl_->module && !impl_->original && !impl_->wrapper)
    return;
  Gil gil;
  // Terminally release our Python references while Python and its GIL are
  // alive, including mismatch/error paths. A later QWidget-owned destructor
  // must never retry Python access after host shutdown.
  struct Release {
    Impl *impl;
    ~Release()
    {
      Py_CLEAR(impl->wrapper);
      Py_CLEAR(impl->original);
      Py_CLEAR(impl->module);
    }
  } release{impl_.get()};
  try {
    if (!impl_->module)
      return;
    PyRef current(PyObject_GetAttrString(impl_->module, "reset"));
    checked(current.value);
    if (impl_->wrapper && current.value != impl_->wrapper)
      throw std::runtime_error("Cycles reset observer replaced by another writer");
    if (impl_->wrapper)
      checked(PyObject_SetAttrString(impl_->module, "reset", impl_->original) == 0);
  }
  catch (const std::exception &error) {
    impl_->state->fail(error.what());
    throw;
  }
  catch (...) {
    impl_->state->fail("Cycles reset observer restoration failed");
    throw;
  }
}
CyclesDisplacementCompletion CyclesLiveDisplacementProbe::snapshot() const
{
  std::lock_guard lock(impl_->state->mutex);
  auto out = impl_->state->latest;
  if (impl_->state->failed.load())
    out.error = impl_->state->failure.empty() ? "Completion observer failed" :
                                                impl_->state->failure;
  return out;
}
void CyclesLiveDisplacementProbe::arm_geometry_capture(std::uint64_t generation,
                                                       std::uint64_t epoch)
{
  auto &s = *impl_->state;
  std::lock_guard lock(s.mutex);
  if (s.geometry_armed || !generation || !epoch || generation < s.latest.generation ||
      (generation == s.latest.generation && epoch <= s.latest.epoch))
    throw std::runtime_error("Geometry witness must arm before a future render epoch");
  s.geometry_generation = generation;
  s.geometry_epoch = epoch;
  s.geometry_record.clear();
  s.geometry_armed = true;
}
std::string CyclesLiveDisplacementProbe::geometry_json() const
{
  std::lock_guard lock(impl_->state->mutex);
  return impl_->state->geometry_record;
}
std::string CyclesLiveDisplacementProbe::profile_json() const
{
  Gil gil;
  return profile(false);
}
std::string CyclesLiveDisplacementProbe::trace_json() const
{
  std::lock_guard lock(impl_->state->mutex);
  std::ostringstream out;
  out << std::setprecision(17) << "{\"profile\":"
      << (impl_->state->initial_profile.empty() ? "null" : impl_->state->initial_profile)
      << ",\"failed\":" << (impl_->state->failed.load() ? "true" : "false")
      << ",\"failure\":" << quoted(impl_->state->failure) << ",\"events\":[";
  bool first = true;
  for (const auto &e : impl_->state->events) {
    if (!first)
      out << ',';
    first = false;
    out << "{\"generation\":" << e.generation << ",\"epoch\":" << e.epoch
        << ",\"sequence\":" << e.sequence << ",\"sample\":" << e.sample
        << ",\"progress\":" << e.progress << ",\"steady_ns\":" << e.ns
        << ",\"status\":" << quoted(e.status) << ",\"substatus\":" << quoted(e.substatus) << '}';
  }
  out << "],\"sessions\":[";
  first = true;
  for (const auto &session : impl_->state->sessions) {
    if (!first)
      out << ',';
    first = false;
    out << session;
  }
  out << "]}";
  return out.str();
}
}  // namespace bts
