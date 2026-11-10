// SPDX-License-Identifier: GPL-3.0-or-later
// Internal authoring-program group; no scheduler, public Host API or shader change.
using Prepared = std::unique_ptr<blender::GPUHostedShaderCompilation,
                                 decltype(&blender::GPU_shader_hosted_compilation_free)>;
using PreparedOwner = GpuPreparedProgramOwner<blender::gpu::shader::StageInterfaceInfo, Prepared>;
struct Program {
  PreparedOwner owner{Prepared{nullptr, blender::GPU_shader_hosted_compilation_free}};
};
std::array<Program, 14> programs_;
GpuProgramWarmup program_state_{14};
bool programs_started_ = false;
Shader &program_output(int index)
{
  if (index == 0)
    return filled_paths_.shader_;
  if (index == 1)
    return ribbon_paths_.shader_;
  if (index == 2)
    return polygons_.coverage_shader_;
  if (index == 3)
    return polygons_.pages_shader_;
  if (index >= 4 && index <= 6)
    return index == 4 ? raster_.stamp_shader_ :
           index == 5 ? raster_.over_shader_ :
                        raster_.contact_shader_;
  if (index >= 7 && index <= 9)
    return index == 7 ? payloads_.gather_shader_ :
           index == 8 ? payloads_.sample_shader_ :
                        payloads_.carry_shader_;
  if (index >= 10 && index <= 12)
    return index == 10 ? payloads_.raster_.stamp_shader_ :
           index == 11 ? payloads_.raster_.over_shader_ :
                         payloads_.raster_.contact_shader_;
  if (index == 13)
    return copy_;
  throw std::out_of_range("Unknown authoring shader slot");
}
blender::gpu::shader::ShaderCreateInfo program_info(int index)
{
  if (index == 0)
    return GpuFilledPath::program_info();
  if (index == 1)
    return GpuRibbonPath::program_info();
  if (index == 2) {
    auto &owner = programs_[index].owner;
    owner.retain(std::make_unique<blender::gpu::shader::StageInterfaceInfo>(
        GpuPolygonRaster::coverage_interface()));
    return GpuPolygonRaster::coverage_info(*owner.metadata());
  }
  if (index == 3)
    return GpuPolygonRaster::pages_info();
  if (index >= 4 && index <= 6)
    return GpuBrushRaster::program_info(index - 4);
  if (index >= 7 && index <= 9)
    return GpuSmudgePayload::program_info(index - 7);
  if (index >= 10 && index <= 12)
    return GpuBrushRaster::program_info(index - 10);
  if (index == 13)
    return copy_info();
  throw std::out_of_range("Unknown authoring shader slot");
}
bool poll_program(int index)
{
  using namespace blender;
  auto &owner = programs_[index].owner;
  if (!owner || !GPU_shader_hosted_compilation_ready(owner.get()))
    return true;
  performance::Scope timing("gpu_authoring_program_finalize");
  Shader completed{GPU_shader_hosted_compilation_finalize(owner.get()), GPU_shader_free};
  owner.reset();  // Ready handle/finalize first; interface metadata last.
  const bool okay = bool(completed);
  if (okay)
    program_output(index) = std::move(completed);
  return okay;
}

public:
bool programs_started() const
{
  return programs_started_;
}
bool programs_pending() const
{
  return std::any_of(
      programs_.begin(), programs_.end(), [](const auto &p) { return bool(p.owner); });
}
void poll_programs()
{
  if (programs_started_)
    program_state_.drain([&](int index) { return poll_program(index); });
}
void cancel_programs()
{
  for (auto &p : programs_)
    p.owner.reset();
}  // Terminal owner cleanup only.
std::string take_program_error()
{
  return program_state_.take_error();
}
GpuProgramWarmup::State prepare_worker_programs()
{
  using namespace blender;
  programs_started_ = true;
  return program_state_.prepare(
      [&](int index) {
        performance::Scope timing("gpu_authoring_program_submit");
        trace_authoring_submission();
        auto info = program_info(index);
        gpu_derived::stabilize_hosted_resource_owners(info);
        auto &owner = programs_[index].owner;
        owner.submit(Prepared{
            GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
            GPU_shader_hosted_compilation_free});
        if (!owner)
          throw std::runtime_error("Hosted authoring shader submission unavailable at slot " +
                                   std::to_string(index));
        std::printf(
            "BTS_GPU_AUTHORING_PROGRAM_SUBMITTED slot=%d route=THREAD python_preprocessed=1\n",
            index);
      },
      [&](int index) { return poll_program(index); },
      [&](int index) { return bool(program_output(index)); },
      [&](int index) { return bool(programs_[index].owner); });
}

private:
