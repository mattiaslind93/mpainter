#include "Theme.hh"
#pragma once
#include "FillProjection.hh"
#include "SmartMaterialPreset.hh"
#include "TextureDocument.hh"
#include <QColor>
#include <QImage>
#include <QResource>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

inline void init_shaderball_preview_resources() {
  // Preview geometry is generated mathematically.
}

inline void init_mask_preview_resources() { Q_INIT_RESOURCE(builtin_mask_preview); }

namespace bts {
namespace detail {

struct ShaderBallSample {
  double cov = 0.0;
  double nx = 0.0, ny = 0.0, nz = 1.0;
  double wnx = 0.0, wny = 1.0, wnz = 0.0;
  double px = 0.0, py = 0.0, pz = 0.0;
  double u = 0.5, v = 0.5;
  double ao = 1.0;
  double curv = 0.5;
  double thick = 0.5;
  double body_mask = 1.0;
};

struct ShaderBallGbuffer {
  int w = 0, h = 0;
  QImage norm_ao;
  QImage pos_curv;
  QImage uv_mask;
  QImage wsn_thick;

  bool valid() const { return w > 0 && h > 0; }

  ShaderBallSample sample(double u_scr, double v_scr) const {
    ShaderBallSample out;
    if (!valid()) return out;
    const double fx = std::clamp(u_scr * w - 0.5, 0.0, double(w - 1));
    const double fy = std::clamp(v_scr * h - 0.5, 0.0, double(h - 1));
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const double tx = fx - x0, ty = fy - y0;
    const double w00 = (1.0 - tx) * (1.0 - ty);
    const double w10 = tx * (1.0 - ty);
    const double w01 = (1.0 - tx) * ty;
    const double w11 = tx * ty;

    const auto fetch = [&](const QImage &img, int x, int y) {
      const auto p = reinterpret_cast<const QRgba64 *>(img.constScanLine(y))[x];
      return std::array<double, 4>{p.red() / 65535.0, p.green() / 65535.0,
                                   p.blue() / 65535.0, p.alpha() / 65535.0};
    };
    const auto u00 = fetch(uv_mask, x0, y0), u10 = fetch(uv_mask, x1, y0),
               u01 = fetch(uv_mask, x0, y1), u11 = fetch(uv_mask, x1, y1);
    out.cov = w00 * u00[3] + w10 * u10[3] + w01 * u01[3] + w11 * u11[3];
    if (out.cov <= 1e-4) return out;

    const auto n00 = fetch(norm_ao, x0, y0), n10 = fetch(norm_ao, x1, y0),
               n01 = fetch(norm_ao, x0, y1), n11 = fetch(norm_ao, x1, y1);
    const auto p00 = fetch(pos_curv, x0, y0), p10 = fetch(pos_curv, x1, y0),
               p01 = fetch(pos_curv, x0, y1), p11 = fetch(pos_curv, x1, y1);
    const bool has_wsn = !wsn_thick.isNull() && wsn_thick.width() == w && wsn_thick.height() == h;
    const auto w00_s = has_wsn ? fetch(wsn_thick, x0, y0) : n00,
               w10_s = has_wsn ? fetch(wsn_thick, x1, y0) : n10,
               w01_s = has_wsn ? fetch(wsn_thick, x0, y1) : n01,
               w11_s = has_wsn ? fetch(wsn_thick, x1, y1) : n11;

    const double c00 = w00 * u00[3], c10 = w10 * u10[3],
                 c01 = w01 * u01[3], c11 = w11 * u11[3];
    const double csum = c00 + c10 + c01 + c11;
    const auto blend = [&](const auto &a00, const auto &a10, const auto &a01,
                           const auto &a11, int ch) {
      return (c00 * a00[ch] + c10 * a10[ch] + c01 * a01[ch] + c11 * a11[ch]) /
             csum;
    };

    out.nx = blend(n00, n10, n01, n11, 0) * 2.0 - 1.0;
    out.ny = blend(n00, n10, n01, n11, 1) * 2.0 - 1.0;
    out.nz = blend(n00, n10, n01, n11, 2) * 2.0 - 1.0;
    const double nlen =
        std::sqrt(out.nx * out.nx + out.ny * out.ny + out.nz * out.nz);
    if (nlen > 1e-6) {
      out.nx /= nlen;
      out.ny /= nlen;
      out.nz /= nlen;
    } else {
      out.nx = 0.0;
      out.ny = 0.0;
      out.nz = 1.0;
    }
    out.ao = std::clamp(blend(n00, n10, n01, n11, 3), 0.05, 1.0);
    out.px = blend(p00, p10, p01, p11, 0) * 2.0 - 1.0;
    out.py = blend(p00, p10, p01, p11, 1) * 2.0 - 1.0;
    out.pz = blend(p00, p10, p01, p11, 2) * 2.0 - 1.0;
    out.curv = std::clamp(blend(p00, p10, p01, p11, 3), 0.0, 1.0);
    out.wnx = blend(w00_s, w10_s, w01_s, w11_s, 0) * 2.0 - 1.0;
    out.wny = blend(w00_s, w10_s, w01_s, w11_s, 1) * 2.0 - 1.0;
    out.wnz = blend(w00_s, w10_s, w01_s, w11_s, 2) * 2.0 - 1.0;
    const double wnlen = std::sqrt(out.wnx * out.wnx + out.wny * out.wny + out.wnz * out.wnz);
    if (wnlen > 1e-6) {
      out.wnx /= wnlen;
      out.wny /= wnlen;
      out.wnz /= wnlen;
    } else {
      out.wnx = out.nx;
      out.wny = out.ny;
      out.wnz = out.nz;
    }
    out.thick = has_wsn ? std::clamp(blend(w00_s, w10_s, w01_s, w11_s, 3), 0.0, 1.0) : 0.5;
    out.u = std::clamp(blend(u00, u10, u01, u11, 0), 0.0, 1.0);
    out.v = std::clamp(blend(u00, u10, u01, u11, 1), 0.0, 1.0);
    // UVs are discontinuous at island boundaries. Interpolating across one
    // samples a completely unrelated texture region between the islands.
    const std::array<double,4> weights{c00,c10,c01,c11};
    const std::array<std::array<double,4>,4> texels{u00,u10,u01,u11};
    const auto nearest=std::max_element(weights.begin(),weights.end())-weights.begin();
    for(int i=0;i<4;++i)if(weights[i]>0 &&
        (std::abs(texels[i][0]-texels[nearest][0])>.25 || std::abs(texels[i][1]-texels[nearest][1])>.25)){
      out.u=texels[nearest][0];out.v=texels[nearest][1];break;
    }
    out.body_mask = std::clamp(blend(u00, u10, u01, u11, 2), 0.0, 1.0);
    return out;
  }
};

inline const ShaderBallGbuffer &sphere_preview_gbuffer() {
 static const ShaderBallGbuffer gb=[] {
  ShaderBallGbuffer out;out.w=out.h=256;
  for(auto *image:{&out.norm_ao,&out.pos_curv,&out.uv_mask,&out.wsn_thick}){*image=QImage(256,256,QImage::Format_RGBA64);image->fill(Qt::transparent);}
  for(int y=0;y<256;++y)for(int x=0;x<256;++x){
   const double nx=(x+.5-128)/119.,ny=(128-y-.5)/119.,d=nx*nx+ny*ny;if(d>=1)continue;const double nz=std::sqrt(1-d);
   const double u=.5+std::atan2(nx,nz)/(2*std::numbers::pi),v=.5+std::asin(ny)/std::numbers::pi;
   const auto normal=QColor::fromRgbF(nx*.5+.5,ny*.5+.5,nz*.5+.5,1);
   out.norm_ao.setPixelColor(x,y,normal);out.wsn_thick.setPixelColor(x,y,normal);out.pos_curv.setPixelColor(x,y,QColor::fromRgbF(nx*.5+.5,ny*.5+.5,nz*.5+.5,.5));out.uv_mask.setPixelColor(x,y,QColor::fromRgbF(u,v,1,1));
  }return out;
 }();return gb;
}
struct ShaderBallUvSurface {MeshMaps maps;FillSurfaceAtlas atlas;};
inline const ShaderBallGbuffer &shaderball_jl01_gbuffer() {
  static const ShaderBallGbuffer gb = [] {
    init_mask_preview_resources();
    ShaderBallGbuffer out;
    out.norm_ao =
        QImage(QStringLiteral(":/builtin-mask-preview/shaderball-jl01-norm-ao.png"))
            .convertToFormat(QImage::Format_RGBA64);
    out.pos_curv =
        QImage(QStringLiteral(":/builtin-mask-preview/shaderball-jl01-pos-curv.png"))
            .convertToFormat(QImage::Format_RGBA64);
    out.uv_mask =
        QImage(QStringLiteral(":/builtin-mask-preview/shaderball-jl01-uv-mask.png"))
            .convertToFormat(QImage::Format_RGBA64);
    out.wsn_thick =
        QImage(QStringLiteral(":/builtin-mask-preview/shaderball-jl01-wsn-thick.png"))
            .convertToFormat(QImage::Format_RGBA64);
    if (!out.norm_ao.isNull() &&
        out.norm_ao.size() == out.pos_curv.size() &&
        out.norm_ao.size() == out.uv_mask.size()) {
      out.w = out.norm_ao.width();
      out.h = out.norm_ao.height();
    }
    return out;
  }();
  return gb;
}

inline ShaderBallUvSurface shaderball_uv_surface(int size){
  init_mask_preview_resources();ShaderBallUvSurface result;
  result.atlas.resolution=result.atlas.height=size;result.atlas.extent=2;
  auto &points=result.atlas.tiles[1001];points.resize(std::size_t(size)*size);
  for(int role:{1,3,4,5,6}){
    const auto image=QImage(QString(":/builtin-mask-preview/shaderball-jl01-map-%1.png").arg(role)).convertToFormat(QImage::Format_RGBA64).scaled(size,size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(image.isNull())throw std::runtime_error("Shaderball UV map missing");
    std::vector<float> data(std::size_t(size)*size*4);
    for(int y=0;y<size;++y){const auto *row=reinterpret_cast<const QRgba64 *>(image.constScanLine(size-1-y));for(int x=0;x<size;++x){
      const auto i=std::size_t(y)*size+x;auto &point=points[i];point.valid=true;point.footprint=2.f/size;
      const std::array<float,3> v{row[x].red()/65535.f,row[x].green()/65535.f,row[x].blue()/65535.f};
      for(int c=0;c<3;++c){data[i*4+c]=v[c];if(role==1)point.normal[c]=v[c]*2-1;if(role==5)point.position[c]=v[c]*2-1;}data[i*4+3]=1;
    }}
    auto pixels=std::make_shared<const MeshMapPixels>(size,std::move(data));MeshMap map;map.name=mesh_map_names[role];map.tiles[1001]={size,pixels->hash,pixels};result.maps[role]=std::move(map);
  }
  return result;
}

inline std::array<double, 3>
env_studio_reflection(double rx, double ry, double rz, double rough) {
  const double blur = std::clamp(rough * rough, 0.0, 1.0);
  // Note: in generated sphere G-buffer, ny > 0 is UPWARD, ny < 0 is DOWNWARD
  const double sky_t = std::clamp(ry * 0.5 + 0.5, 0.0, 1.0);
  std::array<double, 3> dome{0.15 + 0.44 * sky_t, 0.17 + 0.48 * sky_t,
                             0.21 + 0.55 * sky_t};
  const double horizon =
      std::exp(-std::pow((ry - 0.05) / (0.16 + 0.35 * blur), 2.0));
  const double floor_mask =
      std::clamp((-ry - 0.12) / (0.22 + 0.30 * blur), 0.0, 1.0);

  // Studio softbox 1 (upper left key panel)
  const double d_key = rx * (-0.46) + ry * 0.60 + rz * 0.65;
  const double sb_key =
      std::pow(std::max(0.0, d_key), 18.0 / (1.0 + 14.0 * blur));
  // Studio softbox 2 (right fill strip)
  const double d_fill = rx * 0.64 + ry * 0.24 + rz * 0.73;
  const double sb_fill =
      std::pow(std::max(0.0, d_fill), 22.0 / (1.0 + 16.0 * blur));
  // Studio top rim light bar
  const double d_rim = rx * 0.10 + ry * 0.88 + rz * (-0.46);
  const double sb_rim =
      std::pow(std::max(0.0, d_rim), 16.0 / (1.0 + 12.0 * blur));

  std::array<double, 3> env;
  for (int c = 0; c < 3; ++c) {
    const double base_env =
        dome[c] * (1.0 - 0.68 * floor_mask) +
        horizon * (c == 0 ? 0.42 : (c == 1 ? 0.44 : 0.48));
    const double boxes =
        sb_key * (c == 0 ? 1.85 : (c == 1 ? 1.80 : 1.72)) +
        sb_fill * (c == 0 ? 0.78 : (c == 1 ? 0.86 : 0.98)) + sb_rim * 0.92;
    env[c] = base_env + boxes * (1.0 - 0.55 * rough);
  }
  return env;
}

inline std::array<double, 3>
shade_shaderball_pixel(const MaterialValues &material, double nx, double ny,
                       double nz, double ao_factor = 1.0, double curv = 0.5,
                       double micro_cavity = 1.0) {
  const double rough = std::clamp(double(material.roughness), 0.03, 1.0);
  const double metal = std::clamp(double(material.metallic), 0.0, 1.0);
  const double mat_ao =
      std::clamp(double(material.ambient_occlusion), 0.05, 1.0) * ao_factor *
      micro_cavity;
  const double occ = std::clamp(mat_ao, 0.05, 1.15);
  const double bevel_boost = 1.0 + std::max(0.0, curv - 0.5) * 0.38;

  struct Light {
    double lx, ly, lz, r, g, b;
  };
  static constexpr std::array<Light, 3> lights{{
      {-0.46, 0.60, 0.65, 1.25, 1.22, 1.17}, // Key studio softbox (top-left)
      {0.64, 0.24, 0.73, 0.46, 0.52, 0.62},  // Cool fill light (right)
      {0.10, 0.88, -0.46, 0.48, 0.50, 0.56}, // Top rim light
  }};

  const double ndotv = std::max(0.001, nz);
  const double fresnel_v = std::pow(1.0 - ndotv, 5.0);
  const double rx = 2.0 * ndotv * nx;
  const double ry = 2.0 * ndotv * ny;
  const double rz = 2.0 * ndotv * nz - 1.0;
  const auto env_spec = env_studio_reflection(rx, ry, rz, rough);

  // Detect fabric/velvet sheen or car-paint/marble clearcoat from material properties
  const bool is_cloth_like =
      metal < 0.05 && rough >= 0.72 && material.ambient_occlusion <= 0.965f;
  const bool is_clearcoated =
      (metal > 0.1 && metal < 0.6 && rough < 0.30);
  const auto env_cc = env_studio_reflection(rx, ry, rz, 0.04);

  std::array<double, 3> linear{0.0, 0.0, 0.0};
  for (int c = 0; c < 3; ++c) {
    const double base_c = std::clamp(double(material.base_color[c]), 0.0, 1.0);
    const double f0 = 0.04 * (1.0 - metal) + base_c * metal;
    const double f_env = f0 + (std::max(1.0 - rough, f0) - f0) * fresnel_v;

    const double sky = 0.5 + 0.5 * ny;
    double diff_sum = (0.14 + 0.24 * sky) * occ;
    double spec_sum = env_spec[c] * f_env * bevel_boost * (0.42 + 0.58 * ao_factor);
    double cc_sum =
        is_clearcoated ? env_cc[c] * (0.04 + 0.96 * fresnel_v) * 0.45 : 0.0;

    for (const auto &l : lights) {
      const double lc = (&l.r)[c];
      const double ndotl = std::max(0.0, nx * l.lx + ny * l.ly + nz * l.lz);
      diff_sum += ndotl * lc * (0.35 + 0.65 * occ);
      if (ndotl > 0.0) {
        double hx = l.lx, hy = l.ly, hz = l.lz + 1.0;
        const double hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
        hx /= hlen;
        hy /= hlen;
        hz /= hlen;
        const double ndoth = std::max(0.0, nx * hx + ny * hy + nz * hz);
        const double a2 = std::pow(std::max(0.035, rough), 4.0);
        const double denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
        const double d_term = a2 / std::max(1e-4, 3.14159 * denom * denom);
        const double vdoth = std::max(0.0, hz);
        const double f_light = f0 + (1.0 - f0) * std::pow(1.0 - vdoth, 5.0);
        spec_sum +=
            std::min(16.0, d_term) * f_light * ndotl * lc * 0.24 * bevel_boost;
      }
    }

    const double sheen_term =
        is_cloth_like
            ? std::pow(1.0 - ndotv, 3.0) * (0.22 + 0.68 * base_c) * 0.48
            : 0.0;
    linear[c] = base_c * (1.0 - metal) * diff_sum + spec_sum + cc_sum + sheen_term;
  }
  // Working colors are ACEScg; display transfer below expects linear Rec.709.
  // Matrix from the bundled OCIO configuration, including D60/D65 adaptation.
  return {1.705050945*linear[0]-.621792138*linear[1]-.083258927*linear[2],
          -.130256400*linear[0]+1.140804768*linear[1]-.010548267*linear[2],
          -.024003355*linear[0]-.128968999*linear[1]+1.152972341*linear[2]};
}

inline std::array<double,3> preview_environment(double x,double y,double z,double rough,bool diffuse=false){
  if(diffuse){
    const double sky=std::clamp(y*.5+.5,0.0,1.0);
    return {.28+.42*sky,.29+.44*sky,.32+.46*sky};
  }
  return env_studio_reflection(x,y,z,rough);
}
inline std::array<double,3> shade_material_environment(const MaterialValues &material,double nx,double ny,double nz,double ao){
  const double metal=std::clamp(double(material.metallic),0.0,1.0),rough=std::clamp(double(material.roughness),0.0,1.0),nv=std::max(0.0,nz);
  const auto &b=material.base_color;
  const std::array<double,3> base{1.705050945*b[0]-.621792138*b[1]-.083258927*b[2],-.130256400*b[0]+1.140804768*b[1]-.010548267*b[2],-.024003355*b[0]-.128968999*b[1]+1.152972341*b[2]};
  const auto irradiance=preview_environment(nx,ny,nz,1,true);
  const auto reflected=preview_environment(2*nv*nx,2*nv*ny,2*nv*nz-1,rough);
  std::array<double,3> result;
  for(int c=0;c<3;++c){
    const double color=std::max(0.0,base[c]),f0=std::lerp(.04,color,metal);
    const double fresnel=f0+(std::max(1-rough,f0)-f0)*std::pow(1-nv,5);
    result[c]=(color*(1-metal)*(1-fresnel)*irradiance[c]+fresnel*reflected[c])*std::clamp(ao*material.ambient_occlusion,0.0,1.0);
  }
  return result;
}

inline std::array<double, 3>
shade_forest_agx_pixel(const MaterialValues &material, double nx, double ny,
                       double nz, double ao_factor = 1.0) {
  const double rough = std::clamp(double(material.roughness), 0.03, 1.0);
  const double metal = std::clamp(double(material.metallic), 0.0, 1.0);
  const double ao =
      std::clamp(double(material.ambient_occlusion) * ao_factor, 0.05, 1.0);

  const double ndotl = std::max(0.0, nx * (-0.42) + ny * 0.58 + nz * 0.70);
  const double sky = 0.5 + 0.5 * ny;
  const double diff_spatial =
      (0.52 + 0.28 * sky + 0.58 * ndotl) * (0.25 + 0.75 * ao) * 1.0417;

  double hx = -0.42, hy = 0.58, hz = 1.70;
  const double hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
  hx /= hlen;
  hy /= hlen;
  hz /= hlen;
  const double ndoth = std::max(0.0, nx * hx + ny * hy + nz * hz);
  const double spec_spatial =
      (0.55 + 0.35 * sky + 1.85 * std::pow(ndoth, 12.0)) / 1.7913;

  constexpr double irr_d[3] = {1.50, 1.35, 1.12};
  constexpr double spec_tint[3] = {1.00, 0.99, 0.95};
  constexpr double met_tint[3] = {1.04, 1.00, 0.88};
  const double spec_amp =
      (0.012 * (1.0 - 0.75 * rough) + 0.030 * (1.0 - rough) * (1.0 - rough)) *
      spec_spatial;
  const double met_spatial =
      (1.0 - rough) * ((0.32 + 0.38 * sky + 2.40 * std::pow(ndoth, 14.0)) / 1.82) +
      rough * diff_spatial;
  const double met_amp = (0.40 + 1.55 * rough) * met_spatial;

  std::array<double, 3> L{0.0, 0.0, 0.0};
  for (int c = 0; c < 3; ++c) {
    const double col = std::clamp(double(material.base_color[c]), 0.0, 1.0);
    const double L_d = col * irr_d[c] * diff_spatial + spec_amp * spec_tint[c];
    const double L_m = col * met_amp * met_tint[c] + 0.003 * spec_spatial;
    L[c] = (1.0 - metal) * L_d + metal * L_m;
  }
  const double lum = 0.2126 * L[0] + 0.7152 * L[1] + 0.0722 * L[2];
  std::array<double, 3> mapped{0.0, 0.0, 0.0};
  for (int c = 0; c < 3; ++c) {
    const double L_in = 0.985 * L[c] + 0.015 * lum;
    mapped[c] = L_in / (1.0 + 0.95 * std::max(0.0, L_in));
  }
  return mapped;
}

inline std::array<float, 3>
apply_surface_grain_color(const MaterialValues &base,
                          const std::array<float, 4> &base_color,
                          double delta_h, double delta_r,
                          double &micro_cavity) {
  const double t = std::clamp(delta_h / 0.048, -1.35, 1.35);
  const double r_shift = std::clamp(delta_r / 0.12, -1.1, 1.1);
  micro_cavity = std::clamp(1.0 + t * 0.36 - r_shift * 0.20, 0.54, 1.24);

  std::array<float, 3> out{base_color[0], base_color[1], base_color[2]};

  const bool is_marble =
      base.metallic < 0.1f && base.roughness < 0.25f && base.base_color[0] > 0.80f;
  const bool is_carbon =
      base.metallic > 0.05f && base.metallic < 0.30f && base.base_color[0] < 0.20f;
  const bool is_denim =
      base.metallic < 0.05f && base.roughness > 0.72f &&
      base.base_color[2] > base.base_color[0] * 1.4f;
  const bool is_wood =
      base.metallic < 0.05f && base.roughness >= 0.24f && base.roughness <= 0.72f &&
      base.base_color[0] > base.base_color[1] * 1.22f &&
      base.base_color[1] > base.base_color[2] * 1.18f &&
      base.base_color[0] > 0.30f;

  if (is_marble) {
    // Carrara white marble with dark blue-gray veins
    const double vein =
        std::clamp(-t * 0.88 + std::max(0.0, r_shift) * 0.45, 0.0, 1.0);
    const double vp = std::pow(vein, 1.3);
    out[0] = float(out[0] * (1.0 - 0.62 * vp) + 0.18 * vp);
    out[1] = float(out[1] * (1.0 - 0.60 * vp) + 0.20 * vp);
    out[2] = float(out[2] * (1.0 - 0.54 * vp) + 0.26 * vp);
  } else if (is_carbon) {
    // Woven carbon fiber diagonal twill contrast
    const double tow = 0.5 + 0.5 * std::tanh(t * 2.1 - r_shift * 0.9);
    out[0] = float(0.035 + 0.22 * tow);
    out[1] = float(0.040 + 0.23 * tow);
    out[2] = float(0.048 + 0.25 * tow);
  } else if (is_denim) {
    // Indigo warp + lighter cotton weft threads in denim twill
    const double weft = std::clamp(t * 0.72, 0.0, 1.0);
    out[0] = float(std::clamp(out[0] * (0.72 + 0.55 * (t + 0.5)) + 0.24 * weft,
                              0.02, 1.0));
    out[1] = float(std::clamp(out[1] * (0.76 + 0.52 * (t + 0.5)) + 0.26 * weft,
                              0.02, 1.0));
    out[2] = float(std::clamp(out[2] * (0.80 + 0.48 * (t + 0.5)) + 0.28 * weft,
                              0.04, 1.0));
  } else if (is_wood) {
    // Rich earlywood / latewood ring contrast
    const double ring = t * 0.48 - r_shift * 0.24;
    out[0] = float(std::clamp(out[0] * (1.0 + ring * 0.95), 0.02, 1.0));
    out[1] = float(std::clamp(out[1] * (1.0 + ring * 1.08), 0.015, 1.0));
    out[2] = float(std::clamp(out[2] * (1.0 + ring * 1.22), 0.01, 1.0));
  } else {
    // Leather pores, woven canvas/velvet/tactical cloth, brushed/forged metals, concrete, rubber
    const double grain = t * 0.38 - r_shift * 0.22;
    for (int c = 0; c < 3; ++c)
      out[c] = float(std::clamp(out[c] * (1.0 + grain), 0.01, 1.0));
  }
  return out;
}

} // namespace detail

inline QImage material_sphere_preview(const MaterialValues &base,
                                      const FillProjection *mapping = nullptr,
                                      int size = 58, bool sphere = true) {
  QImage image(size, size, QImage::Format_RGB32);
  image.fill(QColor(bts::theme::Surface));
  const auto &gbuffer = detail::sphere_preview_gbuffer();

  const auto surface_sample = [&](double u, double v) {
    if (!sphere) return gbuffer.sample(u, v);
    detail::ShaderBallSample gb;
    const double nx = (u - .5) / .46, ny = (.5 - v) / .46;
    const double r2 = nx * nx + ny * ny;
    if (r2 >= 1.0) return gb;
    gb.cov = 1; gb.nx = gb.px = nx; gb.ny = gb.py = ny;
    gb.nz = gb.pz = std::sqrt(1.0 - r2);
    gb.wnx = nx; gb.wny = ny; gb.wnz = gb.nz;
    gb.u = .5 + std::atan2(nx, gb.nz) / (2 * std::numbers::pi);
    gb.v = .5 + std::asin(ny) / std::numbers::pi;
    return gb;
  };

  const auto sample = [&](int channel, double nx, double ny, double nz,
                          double px, double py, double pz, double u,
                          double v) -> std::optional<std::array<float, 4>> {
    if (!mapping) return {};
    const auto &source = fill_channel_texture(*mapping, channel);
    if (!source || !source->pixels) return {};
    auto projected = *mapping;
    projected.texture = source;
    projected.channel_textures.reset();
    projected.sample_alpha = false;
    if (projected.mode == 1) {
      FillSurfacePoint surface;
      surface.valid = true;
      surface.position = {float(px), float(py), float(pz)};
      surface.normal = {float(nx), float(ny), float(nz)};
      surface.footprint = float(2.0 / size);
      return fill_texture_triplanar_sample(projected, surface, 2);
    }
    const auto uv = fill_uv_map(projected, u, v);
    return source->sample(uv[0], uv[1], true, false);
  };

  MaterialValues calib_base;
  calib_base.base_color = {0.22f, 0.23f, 0.25f, 1.0f};
  calib_base.metallic = 0.15f;
  calib_base.roughness = 0.42f;

  constexpr double bg_lin[3] = {0.022, 0.022, 0.022};
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const auto center_gb =
          surface_sample((x + 0.5) / double(size), (y + 0.5) / double(size));
      if (center_gb.cov <= 0.005) continue;

      double acc_srgb[3] = {0.0, 0.0, 0.0};
      for (int sy = 0; sy < 2; ++sy) {
        for (int sx = 0; sx < 2; ++sx) {
          const double u_scr = (x + 0.25 + 0.5 * sx) / double(size);
          const double v_scr = (y + 0.25 + 0.5 * sy) / double(size);
          const auto gb = surface_sample(u_scr, v_scr);
          if (gb.cov <= 0.005) {
            for (int c = 0; c < 3; ++c) acc_srgb[c] += 0.1608;
            continue;
          }

          MaterialValues material = base;
          double nx = gb.nx, ny = gb.ny, nz = gb.nz;

          if (mapping && mapping->atom) {
            const int ax = int(std::fmod(gb.u * 3.0 + 10.0, 1.0) * 127.0);
            const int ay = int(std::fmod(gb.v * 3.0 + 10.0, 1.0) * 127.0);
            const float atom_v = fill_uv_sample(*mapping, ax, ay, 128);
            for (int c = 0; c < 3; ++c)
              material.base_color[c] =
                  std::clamp(material.base_color[c] * (0.25f + 0.75f * atom_v),
                             0.0f, 1.0f);
            material.roughness =
                std::clamp(0.68f - 0.42f * atom_v, 0.05f, 0.95f);
          }

          const double u_sph = gb.u; // Mesh UV: no thumbnail-only stretch or clamp.
          if (mapping) {
            if (auto color = sample(0, gb.nx, gb.ny, gb.nz, gb.px, gb.py,
                                    gb.pz, u_sph, gb.v)) {
              for (int c = 0; c < 3; ++c)
                material.base_color[c] *= (*color)[c];
            }
            if (auto metal = sample(1, gb.nx, gb.ny, gb.nz, gb.px, gb.py,
                                    gb.pz, u_sph, gb.v)) {
              material.metallic = (*metal)[0];
            }
            if (auto rough = sample(2, gb.nx, gb.ny, gb.nz, gb.px, gb.py,
                                    gb.pz, u_sph, gb.v)) {
              material.roughness = (*rough)[0];
            }
            if (auto bump = sample(4, gb.nx, gb.ny, gb.nz, gb.px, gb.py,
                                   gb.pz, u_sph, gb.v)) {
              material.bump = (*bump)[0];
              // Differentiate along the visible surface, including the mapping.
              // A UV derivative is not a screen-space normal component.
              const auto gx = surface_sample(u_scr + 1.0 / size, v_scr);
              const auto gy = surface_sample(u_scr, v_scr + 1.0 / size);
              const auto height_at = [&](const auto &g) {
                if (g.cov <= .005) return double(material.bump);
                const auto h = sample(4,g.nx,g.ny,g.nz,g.px,g.py,g.pz,g.u,g.v);
                return h ? double((*h)[0]) : double(material.bump);
              };
              nx -= (height_at(gx) - material.bump) * 1.6;
              ny += (height_at(gy) - material.bump) * 1.6;
              const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
              if (length > 1e-6) { nx /= length; ny /= length; nz /= length; }

            }
          }

          if (mapping) {
            if (auto normal = sample(3,gb.nx,gb.ny,gb.nz,gb.px,gb.py,gb.pz,gb.u,gb.v)) {
              const auto gx=surface_sample(u_scr+1.0/size,v_scr);
              const auto gy=surface_sample(u_scr,v_scr+1.0/size);
              const double ux=gx.u-gb.u, uy=gy.u-gb.u;
              const double vx=gx.v-gb.v, vy=gy.v-gb.v, det=ux*vy-uy*vx;
              if (gx.cov>.005 && gy.cov>.005 && std::abs(det)>1e-10 && gb.nz>.02) {
                std::array<double,3> tangent{vy/det,vx/det,(-gb.nx*vy-gb.ny*vx)/(gb.nz*det)};
                const double tl=std::hypot(tangent[0],tangent[1],tangent[2]);
                for(auto &c:tangent)c/=tl;
                std::array<double,3> bitangent{gb.ny*tangent[2]-gb.nz*tangent[1],gb.nz*tangent[0]-gb.nx*tangent[2],gb.nx*tangent[1]-gb.ny*tangent[0]};
                const double handed=det<0?1:-1;
                const double tx=(*normal)[0]*2-1,ty=((*normal)[1]*2-1)*handed,tz=(*normal)[2]*2-1;
                nx=tangent[0]*tx+bitangent[0]*ty+gb.nx*tz;
                ny=tangent[1]*tx+bitangent[1]*ty+gb.ny*tz;
                nz=tangent[2]*tx+bitangent[2]*ty+gb.nz*tz;
                const double length=std::hypot(nx,ny,nz);
                if(length>1e-6){nx/=length;ny/=length;nz/=length;}
              }
            }
          }

          {
            auto linear = detail::shade_material_environment(
                material, nx, ny, nz, gb.ao);
            if (gb.body_mask < 0.999) {
              const auto ring_lin = detail::shade_material_environment(
                  calib_base, gb.nx, gb.ny, gb.nz, gb.ao);
              for (int c = 0; c < 3; ++c)
                linear[c] = linear[c] * gb.body_mask +
                            ring_lin[c] * (1.0 - gb.body_mask);
            }
            for (int c = 0; c < 3; ++c) {
              const double L =
                  linear[c] * gb.cov + bg_lin[c] * (1.0 - gb.cov);
              const double mapped = L / (1.0 + 0.52 * std::max(0.0, L));
              acc_srgb[c] +=
                  std::pow(std::clamp(mapped, 0.0, 1.0), 1.0 / 2.2);
            }
          }
        }
      }
      int rgb[3];
      for (int c = 0; c < 3; ++c)
        rgb[c] = qRound(255.0 * std::clamp(acc_srgb[c] * 0.25, 0.0, 1.0));
      image.setPixel(x, y, qRgb(rgb[0], rgb[1], rgb[2]));
    }
  }
  return image;
}

inline QImage smart_material_sphere_preview(const SmartMaterialPreset *preset,
                                            int size = 58) {
  QImage image(size, size, QImage::Format_RGB32);
  image.fill(QColor(bts::theme::Surface));
  const auto &gbuffer = detail::sphere_preview_gbuffer();

  MaterialValues calib_base;
  calib_base.base_color = {0.22f, 0.23f, 0.25f, 1.0f};
  calib_base.metallic = 0.15f;
  calib_base.roughness = 0.42f;

  constexpr double bg_lin[3] = {0.022, 0.022, 0.022};
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const auto center_gb =
          gbuffer.sample((x + 0.5) / double(size), (y + 0.5) / double(size));
      if (center_gb.cov <= 0.005) continue;

      double acc_srgb[3] = {0.0, 0.0, 0.0};
      for (int sy = 0; sy < 2; ++sy) {
        for (int sx = 0; sx < 2; ++sx) {
          const double u_scr = (x + 0.25 + 0.5 * sx) / double(size);
          const double v_scr = (y + 0.25 + 0.5 * sy) / double(size);
          const auto gb = gbuffer.sample(u_scr, v_scr);
          if (gb.cov <= 0.005) {
            for (int c = 0; c < 3; ++c) acc_srgb[c] += 0.1608;
            continue;
          }

          // Real 3D mesh maps from Mattias's generated sphere
          const double ao = gb.ao;
          const double curvature = gb.curv;
          const double edge_peak =
              std::clamp((gb.curv - 0.50) * 2.4 +
                             std::pow(1.0 - gb.nz, 2.4) * 0.35,
                         0.0, 1.0);
          const double cavity = std::clamp(
              std::max(0.0, 0.50 - gb.curv) * 2.4 + (1.0 - gb.ao) * 0.75, 0.0,
              1.0);
          const double pos_bottom =
              std::clamp((0.48 - gb.py) * 0.85, 0.0, 1.0);
          const double upward =
              std::clamp(gb.ny * 0.78 + gb.nz * 0.38, 0.0, 1.0);

          MaterialValues accum;
          bool initialized = false;
          double total_bump_dx = 0.0, total_bump_dy = 0.0;

          if (!preset || preset->set.layers.empty()) {
            MaterialValues steel;
            steel.base_color = {.72f, .74f, .78f, 1};
            steel.metallic = 1.f;
            steel.roughness = .24f;
            MaterialValues coat;
            coat.base_color = {.19f, .32f, .46f, 1};
            coat.metallic = .08f;
            coat.roughness = .42f;
            MaterialValues soil;
            soil.base_color = {.22f, .17f, .13f, 1};
            soil.metallic = 0.f;
            soil.roughness = .84f;
            const double coat_w = std::clamp(1.0 - edge_peak * 0.85, 0.0, 1.0);
            const double soil_w =
                std::clamp(cavity * 0.82 + pos_bottom * 0.25, 0.0, 1.0);
            accum = steel;
            for (int c = 0; c < 3; ++c)
              accum.base_color[c] = float(std::lerp(
                  double(accum.base_color[c]), double(coat.base_color[c]),
                  coat_w));
            accum.metallic = float(
                std::lerp(double(accum.metallic), double(coat.metallic), coat_w));
            accum.roughness = float(std::lerp(double(accum.roughness),
                                              double(coat.roughness), coat_w));
            for (int c = 0; c < 3; ++c)
              accum.base_color[c] = float(std::lerp(
                  double(accum.base_color[c]), double(soil.base_color[c]),
                  soil_w));
            accum.metallic = float(
                std::lerp(double(accum.metallic), double(soil.metallic), soil_w));
            accum.roughness = float(std::lerp(double(accum.roughness),
                                              double(soil.roughness), soil_w));
          } else {
            auto sample_layer_ch = [&](const FillProjection &mapping, int channel,
                                       double nx_s, double ny_s, double nz_s,
                                       double px_s, double py_s, double pz_s,
                                       double u_s, double v_s)
                -> std::optional<FillTexture::Pixel> {
              const auto &tex = fill_channel_texture(mapping, channel);
              if (!tex || !tex->pixels || tex->width <= 0 || tex->height <= 0 ||
                  tex->pixels->empty())
                return std::nullopt;
              if (mapping.mode == 1) {
                FillSurfacePoint surf;
                surf.valid = true;
                surf.position = {float(px_s), float(py_s), float(pz_s)};
                surf.normal = {float(nx_s), float(ny_s), float(nz_s)};
                surf.footprint = float(2.0 / size);
                return fill_texture_triplanar_sample(mapping, surf, channel);
              }
              FillProjection uv = mapping;
              uv.scale[0] = std::clamp(mapping.scale[0], 1.0, 4.0);
              uv.scale[1] = std::clamp(mapping.scale[1], 1.0, 4.0);
              const auto t = fill_uv_map(uv, u_s, v_s);
              return tex->sample(t[0], t[1], true, false);
            };

            auto eval_effects = [&](auto &self, const std::vector<MaskEffect> &effects,
                                    double m, double &tex_relief) -> double {
              for (const auto &eff : effects) {
                if (eff.opacity <= 0.f) continue;
                double g = m;
                if (eff.kind == MaskEffectKind::Group) {
                  const double child_bg = std::clamp(double(eff.bias), 0.0, 1.0);
                  g = eff.group_effects ? self(self, *eff.group_effects, child_bg, tex_relief) : child_bg;
                  if (eff.invert) g = 1.0 - g;
                } else if (eff.kind == MaskEffectKind::Generator) {
                  g = 0.5;
                  if (eff.generator == 8 || eff.generator == 2) {
                    g = std::clamp((curvature - 0.48) *
                                           double(eff.contrast) * 1.45 +
                                       double(eff.bias) + edge_peak * 0.48,
                                   0.0, 1.0);
                    if (eff.generator == 2 && eff.bias > 0.05f)
                      g = std::clamp(1.0 - edge_peak * 0.88, 0.0, 1.0);
                  } else if (eff.generator == 9 || eff.generator == 3) {
                    g = std::clamp(((1.0 - ao) * double(eff.ao_influence) +
                                    cavity * 0.68 +
                                    pos_bottom *
                                        double(eff.position_influence)) *
                                           double(eff.contrast) +
                                       double(eff.bias),
                                   0.0, 1.0);
                  } else if (eff.generator == 6 || eff.generator == 4) {
                    g = std::clamp(
                        upward * double(eff.contrast) + double(eff.bias),
                        0.0, 1.0);
                  } else if (eff.generator == 1) {
                    g = eff.invert ? pos_bottom : (1.0 - pos_bottom);
                  } else if (eff.generator == 5) {
                    g = std::clamp((1.0 - cavity * 0.65) * double(eff.contrast) + double(eff.bias), 0.0, 1.0);
                  }
                  if (eff.invert && eff.generator != 1) g = 1.0 - g;
                } else if (eff.kind == MaskEffectKind::Fill &&
                           eff.mapping.projection.texture &&
                           eff.mapping.projection.texture->pixels) {
                  FillSurfacePoint surf;
                  surf.valid = true;
                  surf.position = {float(gb.px), float(gb.py), float(gb.pz)};
                  surf.normal = {float(gb.nx), float(gb.ny), float(gb.nz)};
                  surf.footprint = float(2.0 / size);
                  const double tex_v =
                      fill_texture_triplanar_sample(eff.mapping.projection,
                                                    surf, 0)[0];
                  tex_relief = tex_v - 0.5;
                  g = eff.invert ? (1.0 - tex_v) : tex_v;
                } else if (eff.kind == MaskEffectKind::Levels) {
                  const double lo = eff.levels.input_low,
                               hi = std::max(lo + .01,
                                             double(eff.levels.input_high));
                  double lv = std::clamp((m - lo) / (hi - lo), 0.0, 1.0);
                  lv = std::pow(lv, 1.0 / std::max(0.1, double(eff.levels.gamma)));
                  lv = std::lerp(double(eff.levels.output_low), double(eff.levels.output_high), lv);
                  m = std::lerp(m, std::clamp(lv, 0.0, 1.0), double(eff.opacity));
                  continue;
                } else if (eff.kind == MaskEffectKind::Filter) {
                  double fv = std::clamp((m - 0.5) * double(eff.contrast) + 0.5 + double(eff.bias), 0.0, 1.0);
                  if (eff.invert) fv = 1.0 - fv;
                  m = std::lerp(m, fv, double(eff.opacity));
                  continue;
                } else {
                  continue;
                }
                if (eff.blending == LayerBlend::Multiply)
                  m = std::lerp(m, m * g, double(eff.opacity));
                else if (eff.blending == LayerBlend::LinearDodge)
                  m = std::lerp(m, std::clamp(m + g, 0.0, 1.0), double(eff.opacity));
                else if (eff.blending == LayerBlend::Subtract)
                  m = std::lerp(m, std::clamp(m - g, 0.0, 1.0), double(eff.opacity));
                else if (eff.blending == LayerBlend::LinearLight)
                  m = std::lerp(m, std::clamp(m + 2.0 * g - 1.0, 0.0, 1.0), double(eff.opacity) * 0.75);
                else if (eff.blending == LayerBlend::Overlay) {
                  const double ov = m < 0.5 ? (2.0 * m * g) : (1.0 - 2.0 * (1.0 - m) * (1.0 - g));
                  m = std::lerp(m, std::clamp(ov, 0.0, 1.0), double(eff.opacity));
                } else
                  m = std::lerp(m, g, double(eff.opacity));
              }
              return std::clamp(m, 0.0, 1.0);
            };

            for (const auto &layer : preset->set.layers) {
              if (layer.kind != LayerKind::Fill || !layer.visible) continue;
              double w = std::clamp(double(layer.opacity[0]), 0.0, 1.0);
              double tex_relief = 0.0;
              if (layer.mask) {
                double m = std::clamp(double(layer.mask->background), 0.0, 1.0);
                m = eval_effects(eval_effects, layer.mask->effects, m, tex_relief);
                w *= m;
              }
              if (!layer.parent.isNull()) {
                for (const auto &parent_layer : preset->set.layers) {
                  if (parent_layer.id == layer.parent && parent_layer.kind == LayerKind::Folder) {
                    if (!parent_layer.visible) w = 0.0;
                    else if (parent_layer.mask) {
                      double pm = std::clamp(double(parent_layer.mask->background), 0.0, 1.0);
                      pm = eval_effects(eval_effects, parent_layer.mask->effects, pm, tex_relief);
                      w *= pm;
                    }
                    break;
                  }
                }
              }
              if (w <= 1e-5) continue;

              MaterialValues lval = layer.values;
              if (auto c0 = sample_layer_ch(layer.fill, 0, gb.nx, gb.ny, gb.nz, gb.px, gb.py, gb.pz, gb.u, gb.v)) {
                for (int c = 0; c < 3; ++c) lval.base_color[c] *= (*c0)[c];
              }
              if (auto c1 = sample_layer_ch(layer.fill, 1, gb.nx, gb.ny, gb.nz, gb.px, gb.py, gb.pz, gb.u, gb.v)) {
                lval.metallic = (*c1)[0];
              }
              if (auto c2 = sample_layer_ch(layer.fill, 2, gb.nx, gb.ny, gb.nz, gb.px, gb.py, gb.pz, gb.u, gb.v)) {
                lval.roughness = (*c2)[0];
              }
              if (auto c4 = sample_layer_ch(layer.fill, 4, gb.nx, gb.ny, gb.nz, gb.px, gb.py, gb.pz, gb.u, gb.v)) {
                lval.bump = (*c4)[0];
                const double step_p = 1.6 / double(size);
                const double step_uv = 0.6 / double(size);
                const auto bx_s = sample_layer_ch(layer.fill, 4, gb.nx, gb.ny, gb.nz, gb.px + step_p, gb.py, gb.pz, gb.u + step_uv, gb.v);
                const auto by_s = sample_layer_ch(layer.fill, 4, gb.nx, gb.ny, gb.nz, gb.px, gb.py - step_p, gb.pz, gb.u, gb.v + step_uv);
                const double dhx = double(bx_s ? (*bx_s)[0] : lval.bump) - double(lval.bump);
                const double dhy = double(by_s ? (*by_s)[0] : lval.bump) - double(lval.bump);
                total_bump_dx += dhx * w * 1.6;
                total_bump_dy -= dhy * w * 1.6;
              }

              if (!initialized) {
                accum = lval;
                initialized = true;
              } else {
                for (int c = 0; c < 3; ++c)
                  accum.base_color[c] = float(
                      std::lerp(double(accum.base_color[c]),
                                double(lval.base_color[c]), w));
                accum.metallic = float(std::lerp(
                    double(accum.metallic), double(lval.metallic), w));
                accum.roughness = float(
                    std::lerp(double(accum.roughness),
                              double(lval.roughness), w));
                const double layer_b =
                    double(lval.bump) + tex_relief * 0.04;
                total_bump_dx += (layer_b - double(accum.bump)) * w * 1.6;
                total_bump_dy += (layer_b - double(accum.bump)) * w * 1.6;
                accum.bump =
                    float(std::lerp(double(accum.bump), layer_b, w));
                accum.ambient_occlusion = float(std::lerp(
                    double(accum.ambient_occlusion),
                    double(lval.ambient_occlusion), w));
              }
            }
          }

          double nx = gb.nx - total_bump_dx;
          double ny = gb.ny + total_bump_dy;
          double nz = gb.nz;
          const double nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
          if (nlen > 1e-6) {
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;
          }

          auto linear = detail::shade_shaderball_pixel(
              accum, nx, ny, nz, ao, gb.curv, 1.0 - cavity * 0.25);
          if (gb.body_mask < 0.999) {
            const auto ring_lin = detail::shade_shaderball_pixel(
                calib_base, gb.nx, gb.ny, gb.nz, gb.ao, gb.curv, 1.0);
            for (int c = 0; c < 3; ++c)
              linear[c] = linear[c] * gb.body_mask +
                          ring_lin[c] * (1.0 - gb.body_mask);
          }
          for (int c = 0; c < 3; ++c) {
            const double L =
                linear[c] * gb.cov + bg_lin[c] * (1.0 - gb.cov);
            const double mapped = L / (1.0 + 0.52 * std::max(0.0, L));
            acc_srgb[c] +=
                std::pow(std::clamp(mapped, 0.0, 1.0), 1.0 / 2.2);
          }
        }
      }
      int rgb[3];
      for (int c = 0; c < 3; ++c)
        rgb[c] = qRound(255.0 * std::clamp(acc_srgb[c] * 0.25, 0.0, 1.0));
      image.setPixel(x, y, qRgb(rgb[0], rgb[1], rgb[2]));
    }
  }
  return image;
}

inline QImage smart_mask_sphere_preview(const LayerMask &mask, int size = 58, bool material_view = false) {
  // Evaluate in the mesh UV domain. Screen coordinates belong only to display.
  thread_local int cached_size=0;thread_local detail::ShaderBallUvSurface surface;
  int canvas=32;while(canvas<size)canvas*=2;
  if(cached_size!=canvas){surface=detail::shaderball_uv_surface(canvas);cached_size=canvas;}
  // Fixed reference raster background, independent of the surrounding UI theme.
  QImage image(size,size,QImage::Format_RGB32);image.fill(qRgb(30,30,30));MaskCompareInputs context;context.projection=&surface.atlas;
  const auto encode=[](double v){return v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;};
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto gb=detail::shaderball_jl01_gbuffer().sample((x+.5)/size,(y+.5)/size);if(gb.cov<=.005)continue;
    const float value=mask.enabled?mask.value(std::clamp(int(gb.u*canvas),0,canvas-1),std::clamp(int(gb.v*canvas),0,canvas-1),1001,canvas,canvas,nullptr,&surface.maps,&context):1.f;
    const double shading=.76+.24*std::max(0.0,gb.nx*.3+gb.ny*.6+gb.nz*.74162);
    if(material_view){MaterialValues material;material.base_color={std::lerp(.025f,.9f,value),std::lerp(.025f,.12f,value),std::lerp(.025f,.015f,value),1};material.metallic=0;material.roughness=.35f;const auto rgb=detail::shade_shaderball_pixel(material,gb.nx,gb.ny,gb.nz,gb.ao,gb.curv,1);int channels[3];for(int c=0;c<3;++c)channels[c]=qRound(255*std::clamp(encode(std::clamp(rgb[c],0.0,1.0))*gb.cov+encode(.013)*(1-gb.cov),0.0,1.0));image.setPixel(x,y,qRgb(channels[0],channels[1],channels[2]));continue;}
    const double linear=(.003+.897*value)*shading;const int v=qRound(255*std::clamp(encode(linear)*gb.cov+encode(.013)*(1-gb.cov),0.0,1.0));image.setPixel(x,y,qRgb(v,v,v));
  }
  return image;
}

} // namespace bts
