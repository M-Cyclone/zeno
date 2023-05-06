#include "Structures.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <random>
#include <zeno/types/DummyObject.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/UserData.h>
#include <zeno/utils/log.h>
#include <zeno/utils/parallel_reduce.h>
#include <zeno/utils/vec.h>
#include <zeno/zeno.h>

namespace zs {

template <typename T, zs::enable_if_t<std::is_floating_point_v<T>> = 0>
__forceinline__ __device__ T clamp(T v, const T vmin, const T vmax) {
    return zs::max(vmin, zs::min(v, vmax));
}

template <typename TOut, typename T, zs::enable_if_t<std::is_integral_v<TOut> && std::is_integral_v<T>> = 0>
__forceinline__ __device__ TOut clamp(TOut v, const T vmin, const T vmax) {
    v = v < vmin ? vmin : v;
    v = v > vmax ? vmax : v;
    return v;
}

__forceinline__ __device__ zs::vec<float, 3> normalizeSafe(const zs::vec<float, 3> &a,
                                                           float b = zs::limits<float>::epsilon()) {
    return a * (1 / zs::max(b, a.length()));
}

template <typename T, execspace_e space = deduce_execution_space(), enable_if_t<std::is_floating_point_v<T>> = 0>
constexpr T tan(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if ZS_ENABLE_CUDA && defined(__CUDACC__)
        if constexpr (is_same_v<T, float>)
            return ::tanf(v);
        else
            return ::tan((double)v);
#else
        static_assert(space != execspace_e::cuda, "cuda implementation of [tan] is missing!");
        return 0;
#endif
    } else
        return std::tan(v);
}

} // namespace zs

namespace zeno {

template <typename T>
auto to_device_vector(const std::vector<T> &hv, bool copy = true) {
    using namespace zs;
    if constexpr (zs::is_vec<T>::value) {
        zs::Vector<zs::vec<typename T::value_type, std::tuple_size_v<T>>> dv{hv.size(), memsrc_e::device, 0};
        if (copy) {
            Resource::copy(MemoryEntity{dv.memoryLocation(), (void *)dv.data()},
                           MemoryEntity{MemoryLocation{memsrc_e::host, -1}, (void *)hv.data()}, hv.size() * sizeof(T));
        }
        return dv;
    } else {
        zs::Vector<T> dv{hv.size(), memsrc_e::device, 0};
        if (copy) {
            Resource::copy(MemoryEntity{dv.memoryLocation(), (void *)dv.data()},
                           MemoryEntity{MemoryLocation{memsrc_e::host, -1}, (void *)hv.data()}, hv.size() * sizeof(T));
        }
        return dv;
    }
}

template <typename T0, typename T1, zs::enable_if_t<sizeof(T0) == sizeof(T1)> = 0>
void retrieve_device_vector(std::vector<T0> &hv, const zs::Vector<T1> &dv) {
    using namespace zs;
    Resource::copy(MemoryEntity{MemoryLocation{memsrc_e::host, -1}, (void *)hv.data()},
                   MemoryEntity{dv.memoryLocation(), (void *)dv.data()}, dv.size() * sizeof(T1));
}

__forceinline__ __device__ int Pos2Idx(const int x, const int z, const int nx) {
    return z * nx + x;
}

__forceinline__ __device__ unsigned int erode_random(float seed, int idx) {
    unsigned int s = *(unsigned int *)(&seed);
    s ^= idx << 3;
    s *= 179424691; // a magic prime number
    s ^= s << 13 | s >> (32 - 13);
    s ^= s >> 17 | s << (32 - 17);
    s ^= s << 23;
    s *= 179424691;
    return s;
}

// 降水/蒸发
struct zs_erode_value2cond : INode {
    void apply() override {
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);
        // 获取面板参数
        auto value = get_input2<float>("value");
        auto seed = get_input2<float>("seed");

        // 初始化网格属性
        if (!terrain->verts.has_attr("cond")) {
            auto &_cond = terrain->verts.add_attr<float>("cond");
            std::fill(_cond.begin(), _cond.end(), 0.0);
        }
        auto &attr_cond = terrain->verts.attr<float>("cond");
        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////
        /// @brief  accelerate cond computation using cuda
        auto pol = cuda_exec();

        auto zs_attr_cond = to_device_vector(attr_cond, false);

        pol(range((std::size_t)nz * (std::size_t)nx),
            [=, attr_cond = view<space>(zs_attr_cond)] __device__(std::size_t idx) mutable {
                if (value >= 1.0f) {
                    attr_cond[idx] = 1;
                } else {
                    value = value < 0 ? 0 : (value > 1 ? 1 : value);
                    unsigned int cutoff = (unsigned int)(value * 4294967295.0);
                    unsigned int randval = erode_random(seed, idx + nx * nz);
                    attr_cond[idx] = randval < cutoff;
                }
            });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(attr_cond, zs_attr_cond);

        set_output("prim_2DGrid", std::move(terrain));
    }
};

ZENDEFNODE(zs_erode_value2cond, {/* inputs: */ {
                                     "prim_2DGrid",
                                     {"float", "value", "1.0"}, // 0.0 ~ 1.0
                                     {"float", "seed", "0.0"},
                                 },
                                 /* outputs: */
                                 {
                                     "prim_2DGrid",
                                 },
                                 /* params: */ {}, /* category: */
                                 {
                                     "erode",
                                 }});

struct zs_erode_smooth_flow : INode {
    void apply() override {
        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);
        // 获取面板参数
        auto smooth_rate = get_input2<float>("smoothRate");
        auto flowName = get_input2<std::string>("flowName");
        // 初始化网格属性
        auto &flow = terrain->verts.attr<float>(flowName);
        auto &_lap = terrain->verts.add_attr<float>("_lap");
        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////
        /// @brief  accelerate cond computation using cuda
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        auto pol = cuda_exec();
        /// @brief  copy host-side attribute
        auto zs_flow = to_device_vector(flow);
        auto zs_lap = to_device_vector(_lap);

        ///
        pol(range((std::size_t)nz * (std::size_t)nx),
            [flow = view<space>(zs_flow), _lap = view<space>(zs_lap), nx, nz] __device__(std::size_t idx) mutable {
                auto id_z = idx / nx; // outer index
                auto id_x = idx % nx; // inner index
                float net_diff = 0.0f;
                net_diff += flow[idx - 1 * (id_x > 0)];
                net_diff += flow[idx + 1 * (id_x < nx - 1)];
                net_diff += flow[idx - nx * (id_z > 0)];
                net_diff += flow[idx + nx * (id_z < nz - 1)];
                net_diff *= 0.25f;
                net_diff -= flow[idx];
                _lap[idx] = net_diff;
            });

        pol(range((std::size_t)nz * (std::size_t)nx), [flow = view<space>(zs_flow), _lap = view<space>(zs_lap),
                                                       smooth_rate, nx, nz] __device__(std::size_t idx) mutable {
            auto id_z = idx / nx; // outer index
            auto id_x = idx % nx; // inner index
            float net_diff = 0.0f;
            net_diff += _lap[idx - 1 * (id_x > 0)];
            net_diff += _lap[idx + 1 * (id_x < nx - 1)];
            net_diff += _lap[idx - nx * (id_z > 0)];
            net_diff += _lap[idx + nx * (id_z < nz - 1)];
            net_diff *= 0.25f;
            net_diff -= _lap[idx];
            flow[idx] -= smooth_rate * 0.5f * net_diff;
        });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(flow, zs_flow);
        retrieve_device_vector(_lap, zs_lap);

        terrain->verts.erase_attr("_lap");
        set_output("prim_2DGrid", std::move(terrain));
    }
};

ZENDEFNODE(zs_erode_smooth_flow, {/* inputs: */ {
                                      "prim_2DGrid",
                                      {"float", "smoothRate", "1.0"},
                                      {"string", "flowName", "flow"},
                                  },
                                  /* outputs: */
                                  {
                                      "prim_2DGrid",
                                  },
                                  /* params: */ {}, /* category: */
                                  {
                                      "erode",
                                  }});

// 热侵蚀
struct zs_erode_tumble_material_v0 : INode {
    void apply() override {

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////

        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);

        // 获取面板参数
        auto gridbias = get_input<NumericObject>("gridbias")->get<float>();
        auto cut_angle = get_input<NumericObject>("cut_angle")->get<float>();
        auto global_erosionrate = get_input<NumericObject>("global_erosionrate")->get<float>();
        auto erosionrate = get_input<NumericObject>("erosionrate")->get<float>();
        auto erodability = get_input<NumericObject>("erodability")->get<float>();
        auto removalrate = get_input<NumericObject>("removalrate")->get<float>();
        auto maxdepth = get_input<NumericObject>("maxdepth")->get<float>();

        std::uniform_real_distribution<float> distr(0.0, 1.0); // 设置随机分布
        auto seed = get_input<NumericObject>("seed")->get<float>();

        auto iterations = get_input<NumericObject>("iterations")->get<int>(); // 外部迭代总次数      10
        auto iter = get_input<NumericObject>("iter")->get<int>();             // 外部迭代当前次数    1~10
        auto i = get_input<NumericObject>("i")->get<int>();                   // 内部迭代当前次数    0~7
        auto openborder = get_input<NumericObject>("openborder")->get<int>(); // 获取边界标记

        auto perm = get_input<ListObject>("perm")->get2<int>(); //std::vector<int>
        auto p_dirs = get_input<ListObject>("p_dirs")->get2<int>();
        auto x_dirs = get_input<ListObject>("x_dirs")->get2<int>();

        // 初始化网格属性
        if (!terrain->verts.has_attr("_height") || !terrain->verts.has_attr("_debris") ||
            !terrain->verts.has_attr("_temp_height") || !terrain->verts.has_attr("_temp_debris")) {
            zeno::log_error("Node [erode_tumble_material_v0], no such data layer named '{}' or '{}' or '{}' or '{}'.",
                            "_height", "_debris", "_temp_height", "_temp_debris");
        }
        auto &_height = terrain->verts.attr<float>("_height"); // 计算用的临时属性
        auto &_debris = terrain->verts.attr<float>("_debris");
        auto &_temp_height = terrain->verts.attr<float>("_temp_height"); // 备份用的临时属性
        auto &_temp_debris = terrain->verts.attr<float>("_temp_debris");

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////
        /// @brief  accelerate cond computation using cuda
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        auto pol = cuda_exec();
        /// @brief  copy host-side attribute
        auto zs_height = to_device_vector(_height, false);
        auto zs_debris = to_device_vector(_debris, false);
        auto zs_temp_height = to_device_vector(_temp_height);
        auto zs_temp_debris = to_device_vector(_temp_debris);
        auto zs_perm = to_device_vector(perm);
        auto zs_p_dirs = to_device_vector(p_dirs);
        auto zs_x_dirs = to_device_vector(x_dirs);

        pol(range((std::size_t)nz * (std::size_t)nx),
            [=, _height = view<space>(zs_height), _debris = view<space>(zs_debris),
             _temp_height = view<space>(zs_temp_height), _temp_debris = view<space>(zs_temp_debris),
             perm = view<space>(zs_perm), p_dirs = view<space>(zs_p_dirs),
             x_dirs = view<space>(zs_x_dirs)] __device__(std::size_t idx) mutable {
                auto id_z = idx / nx; // outer index
                auto id_x = idx % nx; // inner index

                int iterseed = iter * 134775813;
                int color = perm[i];

                int is_red = ((id_z & 1) == 1) && (color == 1);
                int is_green = ((id_x & 1) == 1) && (color == 2);
                int is_blue = ((id_z & 1) == 0) && (color == 3);
                int is_yellow = ((id_x & 1) == 0) && (color == 4);
                int is_x_turn_x = ((id_x & 1) == 1) && ((color == 5) || (color == 6));
                int is_x_turn_y = ((id_x & 1) == 0) && ((color == 7) || (color == 8));
                int dxs[] = {0, p_dirs[0], 0, p_dirs[0], x_dirs[0], x_dirs[1], x_dirs[0], x_dirs[1]};
                int dzs[] = {p_dirs[1], 0, p_dirs[1], 0, x_dirs[0], -x_dirs[1], x_dirs[0], -x_dirs[1]};

                if (is_red || is_green || is_blue || is_yellow || is_x_turn_x || is_x_turn_y) {
                    int idx = Pos2Idx(id_x, id_z, nx);
                    int dx = dxs[color - 1];
                    int dz = dzs[color - 1];
                    int bound_x = nx;
                    int bound_z = nz;
                    int clamp_x = bound_x - 1;
                    int clamp_z = bound_z - 1;

                    float i_debris = _temp_debris[idx];
                    float i_height = _temp_height[idx];

                    int samplex = zs::clamp(id_x + dx, 0, clamp_x);
                    int samplez = zs::clamp(id_z + dz, 0, clamp_z);
                    int validsource = (samplex == id_x + dx) && (samplez == id_z + dz);
                    if (validsource) {
                        validsource = validsource || !openborder;
                        int j_idx = Pos2Idx(samplex, samplez, nx);
                        float j_debris = validsource ? _temp_debris[j_idx] : 0.0f;
                        float j_height = _temp_height[j_idx];

                        int cidx = 0;
                        int cidz = 0;

                        float c_height = 0.0f;
                        float c_debris = 0.0f;
                        float n_debris = 0.0f;

                        int c_idx = 0;
                        int n_idx = 0;

                        int dx_check = 0;
                        int dz_check = 0;

                        float h_diff = 0.0f;

                        if ((j_height - i_height) > 0.0f) {
                            cidx = samplex;
                            cidz = samplez;

                            c_height = j_height;
                            c_debris = j_debris;
                            n_debris = i_debris;

                            c_idx = j_idx;
                            n_idx = idx;

                            dx_check = -dx;
                            dz_check = -dz;

                            h_diff = j_height - i_height;
                        } else {
                            cidx = id_x;
                            cidz = id_z;

                            c_height = i_height;
                            c_debris = i_debris;
                            n_debris = j_debris;

                            c_idx = idx;
                            n_idx = j_idx;

                            dx_check = dx;
                            dz_check = dz;

                            h_diff = i_height - j_height;
                        }

                        float max_diff = 0.0f;
                        float dir_prob = 0.0f;

                        for (int tmp_dz = -1; tmp_dz <= 1; tmp_dz++) {
                            for (int tmp_dx = -1; tmp_dx <= 1; tmp_dx++) {
                                if (!tmp_dx && !tmp_dz)
                                    continue;

                                int tmp_samplex = zs::clamp(cidx + tmp_dx, 0, clamp_x);
                                int tmp_samplez = zs::clamp(cidz + tmp_dz, 0, clamp_z);
                                int tmp_validsource =
                                    (tmp_samplex == (cidx + tmp_dx)) && (tmp_samplez == (cidz + tmp_dz));
                                tmp_validsource = tmp_validsource || !openborder;
                                int tmp_j_idx = Pos2Idx(tmp_samplex, tmp_samplez, nx);

                                float n_height = _temp_height[tmp_j_idx];

                                float tmp_diff = n_height - (c_height);

                                float _gridbias = zs::clamp(gridbias, -1.0f, 1.0f);

                                if (tmp_dx && tmp_dz)
                                    tmp_diff *= zs::clamp(1.0f - _gridbias, 0.0f, 1.0f) / 1.4142136f;
                                else
                                    tmp_diff *= zs::clamp(1.0f + _gridbias, 0.0f, 1.0f);

                                if (tmp_diff <= 0.0f) {
                                    if ((dx_check == tmp_dx) && (dz_check == tmp_dz))
                                        dir_prob = tmp_diff;
                                    if (tmp_diff < max_diff)
                                        max_diff = tmp_diff;
                                }
                            }
                        }
                        if (max_diff > 0.001f || max_diff < -0.001f)
                            dir_prob = dir_prob / max_diff;

                        int cond = 0;
                        if (dir_prob >= 1.0f)
                            cond = 1;
                        else {
                            dir_prob = dir_prob * dir_prob * dir_prob * dir_prob;
                            unsigned int cutoff = (unsigned int)(dir_prob * 4294967295.0);
                            unsigned int randval = erode_random(seed, (idx + nx * nz) * 8 + color + iterseed);
                            cond = randval < cutoff;
                        }

                        if (cond) {
                            float abs_h_diff = h_diff < 0.0f ? -h_diff : h_diff;
                            float _cut_angle = zs::clamp(cut_angle, 0.0f, 90.0f);
                            float delta_x = cellSize * (dx && dz ? 1.4142136f : 1.0f);
                            float height_removed =
                                _cut_angle < 90.0f ? zs::tan(_cut_angle * M_PI / 180) * delta_x : 1e10f;
                            float height_diff = abs_h_diff - height_removed;
                            if (height_diff < 0.0f)
                                height_diff = 0.0f;
                            float prob = ((n_debris + c_debris) != 0.0f)
                                             ? zs::clamp((height_diff / (n_debris + c_debris)), 0.0f, 1.0f)
                                             : 1.0f;
                            unsigned int cutoff = (unsigned int)(prob * 4294967295.0);
                            unsigned int randval = erode_random(seed * 3.14, (idx + nx * nz) * 8 + color + iterseed);
                            int do_erode = randval < cutoff;

                            float height_removal_amt =
                                do_erode * zs::clamp(global_erosionrate * erosionrate * erodability, 0.0f, height_diff);

                            _height[c_idx] -= height_removal_amt;

                            float bedrock_density = 1.0f - (removalrate);
                            if (bedrock_density > 0.0f) {
                                float newdebris = bedrock_density * height_removal_amt;
                                if (n_debris + newdebris > maxdepth) {
                                    float rollback = n_debris + newdebris - maxdepth;
                                    rollback = zs::min(rollback, newdebris);
                                    _height[c_idx] += rollback / bedrock_density;
                                    newdebris -= rollback;
                                }
                                _debris[c_idx] += newdebris;
                            }
                        }
                    }
                }
            });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(_height, zs_height);
        retrieve_device_vector(_debris, zs_debris);

        set_output("prim_2DGrid", std::move(terrain));
    }
};

ZENDEFNODE(zs_erode_tumble_material_v0, {/* inputs: */ {
                                             "prim_2DGrid",

                                             {"ListObject", "perm"},
                                             {"ListObject", "p_dirs"},
                                             {"ListObject", "x_dirs"},

                                             {"float", "seed", "9676.79"},
                                             {"int", "iterations", "0"},
                                             {"int", "iter", "0"},
                                             {"int", "i", "0"},

                                             {"int", "openborder", "0"},
                                             {"float", "gridbias", "0.0"},

                                             {"float", "cut_angle", "35"},
                                             {"float", "global_erosionrate", "1.0"},
                                             {"float", "erosionrate", "0.03"},
                                             {"float", "erodability", "0.4"},
                                             {"float", "removalrate", "0.7"},
                                             {"float", "maxdepth", "5.0"},
                                         },
                                         /* outputs: */
                                         {
                                             "prim_2DGrid",
                                         },
                                         /* params: */
                                         {

                                         },
                                         /* category: */
                                         {
                                             "erode",
                                         }});

// 崩塌
struct zs_erode_tumble_material_v2 : INode {
    void apply() override {

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////

        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);

        // 获取面板参数
        auto gridbias = get_input<NumericObject>("gridbias")->get<float>();
        auto repose_angle = get_input<NumericObject>("repose_angle")->get<float>();
        auto quant_amt = get_input<NumericObject>("quant_amt")->get<float>();
        auto flow_rate = get_input<NumericObject>("flow_rate")->get<float>();

        std::uniform_real_distribution<float> distr(0.0, 1.0);
        auto seed = get_input<NumericObject>("seed")->get<float>();

        auto iterations = get_input<NumericObject>("iterations")->get<int>();
        auto iter = get_input<NumericObject>("iter")->get<int>();
        auto i = get_input<NumericObject>("i")->get<int>();
        auto openborder = get_input<NumericObject>("openborder")->get<int>();

        auto perm = get_input<ListObject>("perm")->get2<int>();
        auto p_dirs = get_input<ListObject>("p_dirs")->get2<int>();
        auto x_dirs = get_input<ListObject>("x_dirs")->get2<int>();

        // 初始化网格属性
        auto stablilityMaskName = get_input2<std::string>("stabilitymask");
        if (!terrain->verts.has_attr(stablilityMaskName)) {
            auto &_sta = terrain->verts.add_attr<float>(stablilityMaskName);
            std::fill(_sta.begin(), _sta.end(), 0.0);
        }
        auto &stabilitymask = terrain->verts.attr<float>(stablilityMaskName);

        if (!terrain->verts.has_attr("height") || !terrain->verts.has_attr("_material") ||
            !terrain->verts.has_attr("_temp_material")) {
            zeno::log_error("Node [erode_tumble_material_v2], no such data layer named '{}' or '{}' or '{}'.", "height",
                            "_material", "_temp_material");
        }
        auto &height = terrain->verts.attr<float>("height");
        auto &_material = terrain->verts.attr<float>("_material");
        auto &_temp_material = terrain->verts.attr<float>("_temp_material");

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////
        /// @brief  accelerate cond computation using cuda
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        auto pol = cuda_exec();
        /// @brief  copy host-side attribute
        auto zs_material = to_device_vector(_material, false);
        auto zs_height = to_device_vector(height);
        auto zs_temp_material = to_device_vector(_temp_material);
        auto zs_stabilitymask = to_device_vector(stabilitymask);
        auto zs_perm = to_device_vector(perm);
        auto zs_p_dirs = to_device_vector(p_dirs);
        auto zs_x_dirs = to_device_vector(x_dirs);

        pol(range((std::size_t)nz * (std::size_t)nx),
            [=, _material = view<space>(zs_material), height = view<space>(zs_height),
             _temp_material = view<space>(zs_temp_material), stabilitymask = view<space>(zs_stabilitymask),
             perm = view<space>(zs_perm), p_dirs = view<space>(zs_p_dirs),
             x_dirs = view<space>(zs_x_dirs)] __device__(std::size_t idx) mutable {
                auto id_z = idx / nx; // outer index
                auto id_x = idx % nx; // inner index

                int iterseed = iter * 134775813;
                int color = perm[i];

                int is_red = ((id_z & 1) == 1) && (color == 1);
                int is_green = ((id_x & 1) == 1) && (color == 2);
                int is_blue = ((id_z & 1) == 0) && (color == 3);
                int is_yellow = ((id_x & 1) == 0) && (color == 4);
                int is_x_turn_x = ((id_x & 1) == 1) && ((color == 5) || (color == 6));
                int is_x_turn_y = ((id_x & 1) == 0) && ((color == 7) || (color == 8));
                int dxs[] = {0, p_dirs[0], 0, p_dirs[0], x_dirs[0], x_dirs[1], x_dirs[0], x_dirs[1]};
                int dzs[] = {p_dirs[1], 0, p_dirs[1], 0, x_dirs[0], -x_dirs[1], x_dirs[0], -x_dirs[1]};

                if (is_red || is_green || is_blue || is_yellow || is_x_turn_x || is_x_turn_y) {
                    int idx = Pos2Idx(id_x, id_z, nx);
                    int dx = dxs[color - 1];
                    int dz = dzs[color - 1];
                    int bound_x = nx;
                    int bound_z = nz;
                    int clamp_x = bound_x - 1;
                    int clamp_z = bound_z - 1;

                    flow_rate = zs::clamp(flow_rate, 0.0f, 1.0f);

                    float i_material = _temp_material[idx];
                    float i_height = height[idx];

                    int samplex = zs::clamp(id_x + dx, 0, clamp_x);
                    int samplez = zs::clamp(id_z + dz, 0, clamp_z);
                    int validsource = (samplex == id_x + dx) && (samplez == id_z + dz);

                    if (validsource) {
                        int same_node = !validsource;

                        validsource = validsource || !openborder;

                        int j_idx = Pos2Idx(samplex, samplez, nx);

                        float j_material = validsource ? _temp_material[j_idx] : 0.0f;
                        float j_height = height[j_idx];

                        float _repose_angle = repose_angle;
                        _repose_angle = zs::clamp(_repose_angle, 0.0f, 90.0f);
                        float delta_x = cellSize * (dx && dz ? 1.4142136f : 1.0f);
                        float static_diff =
                            _repose_angle < 90.0f ? zs::tan(_repose_angle * M_PI / 180.0) * delta_x : 1e10f;
                        float m_diff = (j_height + j_material) - (i_height + i_material);
                        int cidx = 0;
                        int cidz = 0;

                        float c_height = 0.0f;
                        float c_material = 0.0f;
                        float n_material = 0.0f;

                        int c_idx = 0;
                        int n_idx = 0;

                        int dx_check = 0;
                        int dz_check = 0;

                        if (m_diff > 0.0f) {
                            cidx = samplex;
                            cidz = samplez;

                            c_height = j_height;
                            c_material = j_material;
                            n_material = i_material;

                            c_idx = j_idx;
                            n_idx = idx;

                            dx_check = -dx;
                            dz_check = -dz;
                        } else {
                            cidx = id_x;
                            cidz = id_z;

                            c_height = i_height;
                            c_material = i_material;
                            n_material = j_material;

                            c_idx = idx;
                            n_idx = j_idx;

                            dx_check = dx;
                            dz_check = dz;
                        }

                        float sum_diffs[] = {0.0f, 0.0f};
                        float dir_probs[] = {0.0f, 0.0f};
                        float dir_prob = 0.0f;
                        for (int diff_idx = 0; diff_idx < 2; diff_idx++) {
                            for (int tmp_dz = -1; tmp_dz <= 1; tmp_dz++) {
                                for (int tmp_dx = -1; tmp_dx <= 1; tmp_dx++) {
                                    if (!tmp_dx && !tmp_dz)
                                        continue;

                                    int tmp_samplex = zs::clamp(cidx + tmp_dx, 0, clamp_x);
                                    int tmp_samplez = zs::clamp(cidz + tmp_dz, 0, clamp_z);
                                    int tmp_validsource =
                                        (tmp_samplex == (cidx + tmp_dx)) && (tmp_samplez == (cidz + tmp_dz));
                                    tmp_validsource = tmp_validsource || !openborder;
                                    int tmp_j_idx = Pos2Idx(tmp_samplex, tmp_samplez, nx);

                                    float n_material = tmp_validsource ? _temp_material[tmp_j_idx] : 0.0f;
                                    float n_height = height[tmp_j_idx];
                                    float tmp_h_diff = n_height - (c_height);
                                    float tmp_m_diff = (n_height + n_material) - (c_height + c_material);
                                    float tmp_diff = diff_idx == 0 ? tmp_h_diff : tmp_m_diff;
                                    float _gridbias = gridbias;
                                    _gridbias = zs::clamp(_gridbias, -1.0f, 1.0f);

                                    if (tmp_dx && tmp_dz)
                                        tmp_diff *= zs::clamp(1.0f - _gridbias, 0.0f, 1.0f) / 1.4142136f;
                                    else
                                        tmp_diff *= zs::clamp(1.0f + _gridbias, 0.0f, 1.0f);

                                    if (tmp_diff <= 0.0f) {
                                        if ((dx_check == tmp_dx) && (dz_check == tmp_dz))
                                            dir_probs[diff_idx] = tmp_diff;

                                        if (diff_idx && dir_prob > tmp_diff)
                                            dir_prob = tmp_diff;

                                        sum_diffs[diff_idx] += tmp_diff;
                                    }
                                }
                            }

                            if (diff_idx && (dir_prob > 0.001f || dir_prob < -0.001f))
                                dir_prob = dir_probs[diff_idx] / dir_prob;

                            if (sum_diffs[diff_idx] > 0.001f || sum_diffs[diff_idx] < -0.001f)
                                dir_probs[diff_idx] = dir_probs[diff_idx] / sum_diffs[diff_idx];
                        }

                        float movable_mat = (m_diff < 0.0f) ? -m_diff : m_diff;
                        float stability_val = 0.0f;
                        stability_val = zs::clamp(stabilitymask[c_idx], 0.0f, 1.0f);

                        if (stability_val > 0.01f)
                            movable_mat = zs::clamp(movable_mat * (1.0f - stability_val) * 0.5f, 0.0f, c_material);
                        else
                            movable_mat = zs::clamp((movable_mat - static_diff) * 0.5f, 0.0f, c_material);

                        float l_rat = dir_probs[1];
                        if (quant_amt > 0.001)
                            movable_mat =
                                zs::clamp(quant_amt * zs::ceil((movable_mat * l_rat) / quant_amt), 0.0f, c_material);
                        else
                            movable_mat *= l_rat;

                        float diff = (m_diff > 0.0f) ? movable_mat : -movable_mat;

                        int cond = 0;
                        if (dir_prob >= 1.0f)
                            cond = 1;
                        else {
                            dir_prob = dir_prob * dir_prob * dir_prob * dir_prob;
                            unsigned int cutoff = (unsigned int)(dir_prob * 4294967295.0);
                            unsigned int randval = erode_random(seed, (idx + nx * nz) * 8 + color + iterseed);
                            cond = randval < cutoff;
                        }

                        if (!cond || same_node)
                            diff = 0.0f;

                        diff *= flow_rate;
                        float abs_diff = (diff < 0.0f) ? -diff : diff;
                        _material[c_idx] = c_material - abs_diff;
                        _material[n_idx] = n_material + abs_diff;
                    }
                }
            });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(_material, zs_material);

        set_output("prim_2DGrid", std::move(terrain));
    }
};

ZENDEFNODE(zs_erode_tumble_material_v2, {/* inputs: */ {
                                             "prim_2DGrid",

                                             {"string", "stabilitymask", "_stability"},
                                             {"ListObject", "perm"},
                                             {"ListObject", "p_dirs"},
                                             {"ListObject", "x_dirs"},

                                             {"float", "seed", "15231.3"},
                                             {"int", "iterations", "0"},
                                             {"int", "iter", "0"},
                                             {"int", "i", "0"},

                                             {"int", "openborder", "0"},
                                             {"float", "gridbias", "0.0"},

                                             // 崩塌流淌相关
                                             {"float", "repose_angle", "15.0"},
                                             {"float", "quant_amt", "0.25"},
                                             {"float", "flow_rate", "1.0"},
                                         },
                                         /* outputs: */
                                         {
                                             "prim_2DGrid",
                                         },
                                         /* params: */
                                         {
                                             //{"string", "stabilitymask", "_stability"},
                                         },
                                         /* category: */
                                         {
                                             "erode",
                                         }});

// 崩塌 + flow
struct zs_erode_tumble_material_v3 : INode {
    void apply() override {

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////

        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);

        // 获取面板参数
        auto gridbias = get_input<NumericObject>("gridbias")->get<float>();
        auto repose_angle = get_input<NumericObject>("repose_angle")->get<float>();
        auto quant_amt = get_input<NumericObject>("quant_amt")->get<float>();
        auto flow_rate = get_input<NumericObject>("flow_rate")->get<float>();

        std::uniform_real_distribution<float> distr(0.0, 1.0);
        auto seed = get_input<NumericObject>("seed")->get<float>();

        auto iterations = get_input<NumericObject>("iterations")->get<int>();
        auto iter = get_input<NumericObject>("iter")->get<int>();
        auto i = get_input<NumericObject>("i")->get<int>();
        auto openborder = get_input<NumericObject>("openborder")->get<int>();

        auto perm = get_input<ListObject>("perm")->get2<int>();
        auto p_dirs = get_input<ListObject>("p_dirs")->get2<int>();
        auto x_dirs = get_input<ListObject>("x_dirs")->get2<int>();

        // 初始化网格属性
        auto stablilityMaskName = get_input2<std::string>("stabilitymask");
        if (!terrain->verts.has_attr(stablilityMaskName)) {
            auto &_sta = terrain->verts.add_attr<float>(stablilityMaskName);
            std::fill(_sta.begin(), _sta.end(), 0.0);
        }
        auto &stabilitymask = terrain->verts.attr<float>(stablilityMaskName);

        if (!terrain->verts.has_attr("height") || !terrain->verts.has_attr("_material") ||
            !terrain->verts.has_attr("_temp_material") || !terrain->verts.has_attr("flowdir")) {
            zeno::log_error("Node [erode_tumble_material_v3], no such data layer named '{}' or '{}' or '{}' or "
                            "'{}'.",
                            "height", "_material", "_temp_material", "flowdir");
        }
        auto &height = terrain->verts.attr<float>("height");
        auto &_material = terrain->verts.attr<float>("_material");
        auto &_temp_material = terrain->verts.attr<float>("_temp_material");
        auto &flowdir = terrain->verts.attr<vec3f>("flowdir");

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////
        /// @brief  accelerate cond computation using cuda
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        auto pol = cuda_exec();
        /// @brief  copy host-side attribute
        auto zs_material = to_device_vector(_material, false);
        auto zs_height = to_device_vector(height);
        auto zs_temp_material = to_device_vector(_temp_material);
        auto zs_flowdir = to_device_vector(flowdir);
        auto zs_stabilitymask = to_device_vector(stabilitymask);
        auto zs_perm = to_device_vector(perm);
        auto zs_p_dirs = to_device_vector(p_dirs);
        auto zs_x_dirs = to_device_vector(x_dirs);

        pol(range((std::size_t)nz * (std::size_t)nx),
            [=, _material = view<space>(zs_material), height = view<space>(zs_height),
             _temp_material = view<space>(zs_temp_material), flowdir = view<space>(zs_flowdir),
             stabilitymask = view<space>(zs_stabilitymask), perm = view<space>(zs_perm),
             p_dirs = view<space>(zs_p_dirs), x_dirs = view<space>(zs_x_dirs)] __device__(std::size_t idx) mutable {
                auto id_z = idx / nx; // outer index
                auto id_x = idx % nx; // inner index

                int iterseed = iter * 134775813;
                int color = perm[i];

                int is_red = ((id_z & 1) == 1) && (color == 1);
                int is_green = ((id_x & 1) == 1) && (color == 2);
                int is_blue = ((id_z & 1) == 0) && (color == 3);
                int is_yellow = ((id_x & 1) == 0) && (color == 4);
                int is_x_turn_x = ((id_x & 1) == 1) && ((color == 5) || (color == 6));
                int is_x_turn_y = ((id_x & 1) == 0) && ((color == 7) || (color == 8));
                int dxs[] = {0, p_dirs[0], 0, p_dirs[0], x_dirs[0], x_dirs[1], x_dirs[0], x_dirs[1]};
                int dzs[] = {p_dirs[1], 0, p_dirs[1], 0, x_dirs[0], -x_dirs[1], x_dirs[0], -x_dirs[1]};

                if (is_red || is_green || is_blue || is_yellow || is_x_turn_x || is_x_turn_y) {
                    int idx = Pos2Idx(id_x, id_z, nx);
                    int dx = dxs[color - 1];
                    int dz = dzs[color - 1];
                    int bound_x = nx;
                    int bound_z = nz;
                    int clamp_x = bound_x - 1;
                    int clamp_z = bound_z - 1;

                    flow_rate = zs::clamp(flow_rate, 0.0f, 1.0f);

                    // CALC_FLOW
                    float diff_x = 0.0f;
                    float diff_z = 0.0f;

                    float i_material = _temp_material[idx];
                    float i_height = height[idx];

                    int samplex = zs::clamp(id_x + dx, 0, clamp_x);
                    int samplez = zs::clamp(id_z + dz, 0, clamp_z);
                    int validsource = (samplex == id_x + dx) && (samplez == id_z + dz);

                    if (validsource) {
                        int same_node = !validsource;

                        validsource = validsource || !openborder;

                        int j_idx = Pos2Idx(samplex, samplez, nx);

                        float j_material = validsource ? _temp_material[j_idx] : 0.0f;
                        float j_height = height[j_idx];

                        float _repose_angle = repose_angle;
                        _repose_angle = zs::clamp(_repose_angle, 0.0f, 90.0f);
                        float delta_x = cellSize * (dx && dz ? 1.4142136f : 1.0f);

                        float static_diff =
                            _repose_angle < 90.0f ? zs::tan(_repose_angle * M_PI / 180.0) * delta_x : 1e10f;

                        float m_diff = (j_height + j_material) - (i_height + i_material);

                        int cidx = 0;
                        int cidz = 0;

                        float c_height = 0.0f;
                        float c_material = 0.0f;
                        float n_material = 0.0f;

                        int c_idx = 0;
                        int n_idx = 0;

                        int dx_check = 0;
                        int dz_check = 0;

                        if (m_diff > 0.0f) {
                            cidx = samplex;
                            cidz = samplez;

                            c_height = j_height;
                            c_material = j_material;
                            n_material = i_material;

                            c_idx = j_idx;
                            n_idx = idx;

                            dx_check = -dx;
                            dz_check = -dz;
                        } else {
                            cidx = id_x;
                            cidz = id_z;

                            c_height = i_height;
                            c_material = i_material;
                            n_material = j_material;

                            c_idx = idx;
                            n_idx = j_idx;

                            dx_check = dx;
                            dz_check = dz;
                        }

                        float sum_diffs[] = {0.0f, 0.0f};
                        float dir_probs[] = {0.0f, 0.0f};
                        float dir_prob = 0.0f;
                        for (int diff_idx = 0; diff_idx < 2; diff_idx++) {
                            for (int tmp_dz = -1; tmp_dz <= 1; tmp_dz++) {
                                for (int tmp_dx = -1; tmp_dx <= 1; tmp_dx++) {
                                    if (!tmp_dx && !tmp_dz)
                                        continue;

                                    int tmp_samplex = zs::clamp(cidx + tmp_dx, 0, clamp_x);
                                    int tmp_samplez = zs::clamp(cidz + tmp_dz, 0, clamp_z);
                                    int tmp_validsource =
                                        (tmp_samplex == (cidx + tmp_dx)) && (tmp_samplez == (cidz + tmp_dz));

                                    tmp_validsource = tmp_validsource || !openborder;
                                    int tmp_j_idx = Pos2Idx(tmp_samplex, tmp_samplez, nx);

                                    float n_material = tmp_validsource ? _temp_material[tmp_j_idx] : 0.0f;
                                    float n_height = height[tmp_j_idx];
                                    float tmp_h_diff = n_height - (c_height);
                                    float tmp_m_diff = (n_height + n_material) - (c_height + c_material);
                                    float tmp_diff = diff_idx == 0 ? tmp_h_diff : tmp_m_diff;
                                    float _gridbias = gridbias;

                                    _gridbias = zs::clamp(_gridbias, -1.0f, 1.0f);

                                    if (tmp_dx && tmp_dz)
                                        tmp_diff *= zs::clamp(1.0f - _gridbias, 0.0f, 1.0f) / 1.4142136f;
                                    else
                                        tmp_diff *= zs::clamp(1.0f + _gridbias, 0.0f, 1.0f);

                                    if (tmp_diff <= 0.0f) {
                                        if ((dx_check == tmp_dx) && (dz_check == tmp_dz))
                                            dir_probs[diff_idx] = tmp_diff;

                                        if (diff_idx && dir_prob > tmp_diff)
                                            dir_prob = tmp_diff;

                                        sum_diffs[diff_idx] += tmp_diff;
                                    }
                                }
                            }

                            if (diff_idx && (dir_prob > 0.001f || dir_prob < -0.001f))
                                dir_prob = dir_probs[diff_idx] / dir_prob;

                            if (sum_diffs[diff_idx] > 0.001f || sum_diffs[diff_idx] < -0.001f)
                                dir_probs[diff_idx] = dir_probs[diff_idx] / sum_diffs[diff_idx];
                        }

                        float movable_mat = (m_diff < 0.0f) ? -m_diff : m_diff;
                        float stability_val = 0.0f;
                        stability_val = zs::clamp(stabilitymask[c_idx], 0.0f, 1.0f);

                        if (stability_val > 0.01f)
                            movable_mat = zs::clamp(movable_mat * (1.0f - stability_val) * 0.5f, 0.0f, c_material);
                        else
                            movable_mat = zs::clamp((movable_mat - static_diff) * 0.5f, 0.0f, c_material);

                        float l_rat = dir_probs[1];
                        if (quant_amt > 0.001)
                            movable_mat =
                                zs::clamp(quant_amt * zs::ceil((movable_mat * l_rat) / quant_amt), 0.0f, c_material);
                        else
                            movable_mat *= l_rat;

                        float diff = (m_diff > 0.0f) ? movable_mat : -movable_mat;

                        int cond = 0;
                        if (dir_prob >= 1.0f)
                            cond = 1;
                        else {
                            dir_prob = dir_prob * dir_prob * dir_prob * dir_prob;
                            unsigned int cutoff = (unsigned int)(dir_prob * 4294967295.0);
                            unsigned int randval = erode_random(seed, (idx + nx * nz) * 8 + color + iterseed);
                            cond = randval < cutoff;
                        }

                        if (!cond || same_node)
                            diff = 0.0f;

                        diff *= flow_rate;

                        // CALC_FLOW
                        diff_x += (float)dx * diff;
                        diff_z += (float)dz * diff;
                        diff_x *= -1.0f;
                        diff_z *= -1.0f;

                        float abs_diff = (diff < 0.0f) ? -diff : diff;
                        _material[c_idx] = c_material - abs_diff;
                        _material[n_idx] = n_material + abs_diff;

                        // CALC_FLOW
                        float abs_c_x = flowdir[c_idx][0];
                        abs_c_x = (abs_c_x < 0.0f) ? -abs_c_x : abs_c_x;
                        float abs_c_z = flowdir[c_idx][2];
                        abs_c_z = (abs_c_z < 0.0f) ? -abs_c_z : abs_c_z;
                        flowdir[c_idx][0] += diff_x * 1.0f / (1.0f + abs_c_x);
                        flowdir[c_idx][2] += diff_z * 1.0f / (1.0f + abs_c_z);
                    }
                }
            });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(_material, zs_material);
        retrieve_device_vector(flowdir, zs_flowdir);

        set_output("prim_2DGrid", std::move(terrain));
    }
};

ZENDEFNODE(zs_erode_tumble_material_v3, {/* inputs: */ {
                                             "prim_2DGrid",

                                             {"string", "stabilitymask", "_stability"},
                                             {"ListObject", "perm"},
                                             {"ListObject", "p_dirs"},
                                             {"ListObject", "x_dirs"},

                                             {"float", "seed", "15231.3"},
                                             {"int", "iterations", "0"},
                                             {"int", "iter", "0"},
                                             {"int", "i", "0"},

                                             {"int", "openborder", "0"},
                                             {"float", "gridbias", "0.0"},

                                             // 崩塌流淌相关
                                             {"float", "repose_angle", "0.0"},
                                             {"float", "quant_amt", "0.0"},
                                             {"float", "flow_rate", "1.0"},
                                         },
                                         /* outputs: */
                                         {
                                             "prim_2DGrid",
                                         },
                                         /* params: */
                                         {
                                             //{"string", "stabilitymask", "_stability"},
                                         },
                                         /* category: */
                                         {
                                             "erode",
                                         }});

// 崩塌 + 侵蚀
struct zs_erode_tumble_material_v4 : INode {
    void apply() override {

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////

        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);

        // 获取面板参数
        // 侵蚀主参数
        auto global_erosionrate = get_input<NumericObject>("global_erosionrate")->get<float>(); // 1 全局侵蚀率
        auto erodability = get_input<NumericObject>("erodability")->get<float>();               // 1.0 侵蚀能力
        auto erosionrate = get_input<NumericObject>("erosionrate")->get<float>();               // 0.4 侵蚀率
        auto bank_angle = get_input<NumericObject>("bank_angle")->get<float>(); // 70.0 河堤侵蚀角度
        auto seed = get_input<NumericObject>("seed")->get<float>();             // 12.34

        // 高级参数
        auto removalrate = get_input<NumericObject>("removalrate")->get<float>(); // 0.0 风化率/水吸收率
        auto max_debris_depth = get_input<NumericObject>("max_debris_depth")->get<float>(); // 5	碎屑最大深度
        auto gridbias = get_input<NumericObject>("gridbias")->get<float>();                 // 0.0

        // 侵蚀能力调整
        auto max_erodability_iteration = get_input<NumericObject>("max_erodability_iteration")->get<int>();     // 5
        auto initial_erodability_factor = get_input<NumericObject>("initial_erodability_factor")->get<float>(); // 0.5
        auto slope_contribution_factor = get_input<NumericObject>("slope_contribution_factor")->get<float>();   // 0.8

        // 河床参数
        auto bed_erosionrate_factor =
            get_input<NumericObject>("bed_erosionrate_factor")->get<float>();           // 1 河床侵蚀率因子
        auto depositionrate = get_input<NumericObject>("depositionrate")->get<float>(); // 0.01 沉积率
        auto sedimentcap = get_input<NumericObject>("sedimentcap")
                               ->get<float>(); // 10.0 高度差转变为沉积物的比率 / 泥沙容量，每单位流动水可携带的泥沙量

        // 河堤参数
        auto bank_erosionrate_factor =
            get_input<NumericObject>("bank_erosionrate_factor")->get<float>(); // 1.0 河堤侵蚀率因子
        auto max_bank_bed_ratio = get_input<NumericObject>("max_bank_bed_ratio")
                                      ->get<float>(); // 0.5 The maximum of bank to bed water column height ratio
        // 高于这个比值的河岸将不会在侵蚀中被视为河岸，会停止侵蚀
        // 河流控制
        auto quant_amt = get_input<NumericObject>("quant_amt")->get<float>(); // 0.05 流量维持率，越高流量越稳定
        auto iterations = get_input<NumericObject>("iterations")->get<int>(); // 流淌的总迭代次数

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        std::uniform_real_distribution<float> distr(0.0, 1.0);
        auto iter = get_input<NumericObject>("iter")->get<int>();
        auto i = get_input<NumericObject>("i")->get<int>();
        auto openborder = get_input<NumericObject>("openborder")->get<int>();

        auto perm = get_input<ListObject>("perm")->get2<int>();
        auto p_dirs = get_input<ListObject>("p_dirs")->get2<int>();
        auto x_dirs = get_input<ListObject>("x_dirs")->get2<int>();

        // 初始化网格属性
        if (!terrain->verts.has_attr("_height") || !terrain->verts.has_attr("_temp_height") ||
            !terrain->verts.has_attr("_material") || !terrain->verts.has_attr("_temp_material") ||
            !terrain->verts.has_attr("_debris") || !terrain->verts.has_attr("_temp_debris") ||
            !terrain->verts.has_attr("_sediment")) {
            zeno::log_error("Node [erode_tumble_material_v4], no such data layer named '{}' or '{}' or '{}' or '{}' or "
                            "'{}' or '{}' or '{}'.",
                            "_height", "_temp_height", "_material", "_temp_material", "_debris", "_temp_debris",
                            "_sediment");
        }
        auto &_height = terrain->verts.attr<float>("_height");
        auto &_temp_height = terrain->verts.attr<float>("_temp_height");
        auto &_material = terrain->verts.attr<float>("_material");
        auto &_temp_material = terrain->verts.attr<float>("_temp_material");
        auto &_debris = terrain->verts.attr<float>("_debris");
        auto &_temp_debris = terrain->verts.attr<float>("_temp_debris");
        auto &_sediment = terrain->verts.attr<float>("_sediment");

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////
        /// @brief  accelerate cond computation using cuda
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        auto pol = cuda_exec();
        /// @brief  copy host-side attribute
        auto zs_height = to_device_vector(_height);
        auto zs_temp_height = to_device_vector(_temp_height);
        auto zs_material = to_device_vector(_material, false);
        auto zs_temp_material = to_device_vector(_temp_material);
        auto zs_debris = to_device_vector(_debris, false);
        auto zs_temp_debris = to_device_vector(_temp_debris);
        auto zs_sediment = to_device_vector(_sediment);
        auto zs_perm = to_device_vector(perm);
        auto zs_p_dirs = to_device_vector(p_dirs);
        auto zs_x_dirs = to_device_vector(x_dirs);

        pol(range((std::size_t)nz * (std::size_t)nx),
            [=, _height = view<space>(zs_height), _temp_height = view<space>(zs_temp_height),
             _material = view<space>(zs_material), _temp_material = view<space>(zs_temp_material),
             _debris = view<space>(zs_debris), _temp_debris = view<space>(zs_temp_debris),
             _sediment = view<space>(zs_sediment), perm = view<space>(zs_perm), p_dirs = view<space>(zs_p_dirs),
             x_dirs = view<space>(zs_x_dirs)] __device__(std::size_t idx) mutable {
                auto id_z = idx / nx; // outer index
                auto id_x = idx % nx; // inner index

                int iterseed = iter * 134775813;
                int color = perm[i];
                int is_red = ((id_z & 1) == 1) && (color == 1);
                int is_green = ((id_x & 1) == 1) && (color == 2);
                int is_blue = ((id_z & 1) == 0) && (color == 3);
                int is_yellow = ((id_x & 1) == 0) && (color == 4);
                int is_x_turn_x = ((id_x & 1) == 1) && ((color == 5) || (color == 6));
                int is_x_turn_y = ((id_x & 1) == 0) && ((color == 7) || (color == 8));
                int dxs[] = {0, p_dirs[0], 0, p_dirs[0], x_dirs[0], x_dirs[1], x_dirs[0], x_dirs[1]};
                int dzs[] = {p_dirs[1], 0, p_dirs[1], 0, x_dirs[0], -x_dirs[1], x_dirs[0], -x_dirs[1]};

                if (is_red || is_green || is_blue || is_yellow || is_x_turn_x || is_x_turn_y) {
                    int idx = Pos2Idx(id_x, id_z, nx);
                    int dx = dxs[color - 1];
                    int dz = dzs[color - 1];
                    int bound_x = nx;
                    int bound_z = nz;
                    int clamp_x = bound_x - 1;
                    int clamp_z = bound_z - 1;

                    float i_height = _temp_height[idx];
                    float i_material = _temp_material[idx];
                    float i_debris = _temp_debris[idx];
                    float i_sediment = _sediment[idx];

                    int samplex = zs::clamp(id_x + dx, 0, clamp_x);
                    int samplez = zs::clamp(id_z + dz, 0, clamp_z);
                    int validsource = (samplex == id_x + dx) && (samplez == id_z + dz);

                    if (validsource) {
                        validsource = validsource || !openborder;

                        int j_idx = Pos2Idx(samplex, samplez, nx);

                        float j_height = _temp_height[j_idx];
                        float j_material = validsource ? _temp_material[j_idx] : 0.0f;
                        float j_debris = validsource ? _temp_debris[j_idx] : 0.0f;

                        float j_sediment = validsource ? _sediment[j_idx] : 0.0f;
                        float m_diff = (j_height + j_debris + j_material) - (i_height + i_debris + i_material);
                        float delta_x = cellSize * (dx && dz ? 1.4142136f : 1.0f);

                        int cidx = 0;
                        int cidz = 0;

                        float c_height = 0.0f;

                        float c_material = 0.0f;
                        float n_material = 0.0f;

                        float c_sediment = 0.0f;
                        float n_sediment = 0.0f;

                        float c_debris = 0.0f;
                        float n_debris = 0.0f;

                        float h_diff = 0.0f;

                        int c_idx = 0;
                        int n_idx = 0;
                        int dx_check = 0;
                        int dz_check = 0;
                        int is_mh_diff_same_sign = 0;

                        if (m_diff > 0.0f) {
                            cidx = samplex;
                            cidz = samplez;

                            c_height = j_height;
                            c_material = j_material;
                            n_material = i_material;
                            c_sediment = j_sediment;
                            n_sediment = i_sediment;
                            c_debris = j_debris;
                            n_debris = i_debris;

                            c_idx = j_idx;
                            n_idx = idx;

                            dx_check = -dx;
                            dz_check = -dz;

                            h_diff = j_height + j_debris - (i_height + i_debris);
                            is_mh_diff_same_sign = (h_diff * m_diff) > 0.0f;
                        } else {
                            cidx = id_x;
                            cidz = id_z;

                            c_height = i_height;
                            c_material = i_material;
                            n_material = j_material;
                            c_sediment = i_sediment;
                            n_sediment = j_sediment;
                            c_debris = i_debris;
                            n_debris = j_debris;

                            c_idx = idx;
                            n_idx = j_idx;

                            dx_check = dx;
                            dz_check = dz;

                            h_diff = i_height + i_debris - (j_height + j_debris);
                            is_mh_diff_same_sign = (h_diff * m_diff) > 0.0f;
                        }
                        h_diff = (h_diff < 0.0f) ? -h_diff : h_diff;

                        float sum_diffs[] = {0.0f, 0.0f};
                        float dir_probs[] = {0.0f, 0.0f};
                        float dir_prob = 0.0f;
                        for (int diff_idx = 0; diff_idx < 2; diff_idx++) {
                            for (int tmp_dz = -1; tmp_dz <= 1; tmp_dz++) {
                                for (int tmp_dx = -1; tmp_dx <= 1; tmp_dx++) {
                                    if (!tmp_dx && !tmp_dz)
                                        continue;

                                    int tmp_samplex = zs::clamp(cidx + tmp_dx, 0, clamp_x);
                                    int tmp_samplez = zs::clamp(cidz + tmp_dz, 0, clamp_z);

                                    int tmp_validsource =
                                        (tmp_samplex == (cidx + tmp_dx)) && (tmp_samplez == (cidz + tmp_dz));
                                    tmp_validsource = tmp_validsource || !openborder;
                                    int tmp_j_idx = Pos2Idx(tmp_samplex, tmp_samplez, nx);

                                    float tmp_n_material = tmp_validsource ? _temp_material[tmp_j_idx] : 0.0f;
                                    float tmp_n_debris = tmp_validsource ? _temp_debris[tmp_j_idx] : 0.0f;

                                    float n_height = _temp_height[tmp_j_idx];
                                    float tmp_h_diff = n_height + tmp_n_debris - (c_height + c_debris);
                                    float tmp_m_diff =
                                        (n_height + tmp_n_debris + tmp_n_material) - (c_height + c_debris + c_material);
                                    float tmp_diff = diff_idx == 0 ? tmp_h_diff : tmp_m_diff;
                                    float _gridbias = gridbias;
                                    _gridbias = zs::clamp(_gridbias, -1.0f, 1.0f);

                                    if (tmp_dx && tmp_dz)
                                        tmp_diff *= zs::clamp(1.0f - _gridbias, 0.0f, 1.0f) / 1.4142136f;
                                    else
                                        tmp_diff *= zs::clamp(1.0f + _gridbias, 0.0f, 1.0f);

                                    if (tmp_diff <= 0.0f) {
                                        if ((dx_check == tmp_dx) && (dz_check == tmp_dz))
                                            dir_probs[diff_idx] = tmp_diff;

                                        if (diff_idx && (tmp_diff < dir_prob))
                                            dir_prob = tmp_diff;

                                        sum_diffs[diff_idx] += tmp_diff;
                                    }
                                }
                            }

                            if (diff_idx && (dir_prob > 0.001f || dir_prob < -0.001f))
                                dir_prob = dir_probs[diff_idx] / dir_prob;
                            else
                                dir_prob = 0.0f;

                            if (sum_diffs[diff_idx] > 0.001f || sum_diffs[diff_idx] < -0.001f)
                                dir_probs[diff_idx] = dir_probs[diff_idx] / sum_diffs[diff_idx];
                            else
                                dir_probs[diff_idx] = 0.0f;
                        }

                        float movable_mat = (m_diff < 0.0f) ? -m_diff : m_diff;
                        movable_mat = zs::clamp(movable_mat * 0.5f, 0.0f, c_material);
                        float l_rat = dir_probs[1];

                        if (quant_amt > 0.001)
                            movable_mat =
                                zs::clamp(quant_amt * zs::ceil((movable_mat * l_rat) / quant_amt), 0.0f, c_material);
                        else
                            movable_mat *= l_rat;

                        float diff = (m_diff > 0.0f) ? movable_mat : -movable_mat;

                        int cond = 0;
                        if (dir_prob >= 1.0f)
                            cond = 1;
                        else {
                            dir_prob = dir_prob * dir_prob * dir_prob * dir_prob;
                            unsigned int cutoff = (unsigned int)(dir_prob * 4294967295.0);
                            unsigned int randval = erode_random(seed, (idx + nx * nz) * 8 + color + iterseed);
                            cond = randval < cutoff;
                        }

                        if (!cond)
                            diff = 0.0f;

                        float slope_cont = (delta_x > 0.0f) ? (h_diff / delta_x) : 0.0f;
                        float kd_factor = zs::clamp((1 / (1 + (slope_contribution_factor * slope_cont))), 0.0f, 1.0f);
                        float norm_iter = zs::clamp(((float)iter / (float)max_erodability_iteration), 0.0f, 1.0f);
                        float ks_factor = zs::clamp((1 - (slope_contribution_factor * zs::exp(-slope_cont))) *
                                                        zs::sqrt(dir_probs[0]) *
                                                        (initial_erodability_factor +
                                                         ((1.0f - initial_erodability_factor) * zs::sqrt(norm_iter))),
                                                    0.0f, 1.0f);

                        float c_ks = global_erosionrate * erosionrate * erodability * ks_factor;

                        float n_kd = depositionrate * kd_factor;
                        n_kd = zs::clamp(n_kd, 0.0f, 1.0f);

                        float _removalrate = removalrate;
                        float bedrock_density = 1.0f - _removalrate;
                        float abs_diff = (diff < 0.0f) ? -diff : diff;
                        float sediment_limit = sedimentcap * abs_diff;
                        float ent_check_diff = sediment_limit - c_sediment;

                        if (ent_check_diff > 0.0f) {
                            float dissolve_amt = c_ks * bed_erosionrate_factor * abs_diff;
                            float dissolved_debris = zs::min(c_debris, dissolve_amt);
                            _debris[c_idx] -= dissolved_debris;
                            _height[c_idx] -= (dissolve_amt - dissolved_debris);
                            _sediment[c_idx] -= c_sediment / 2;
                            if (bedrock_density > 0.0f) {
                                float newsediment = c_sediment / 2 + (dissolve_amt * bedrock_density);
                                if (n_sediment + newsediment > max_debris_depth) {
                                    float rollback = n_sediment + newsediment - max_debris_depth;
                                    rollback = zs::min(rollback, newsediment);
                                    _height[c_idx] += rollback / bedrock_density;
                                    newsediment -= rollback;
                                }
                                _sediment[n_idx] += newsediment;
                            }
                        } else {
                            float c_kd = depositionrate * kd_factor;
                            c_kd = zs::clamp(c_kd, 0.0f, 1.0f);
                            {
                                _debris[c_idx] += (c_kd * -ent_check_diff);
                                _sediment[c_idx] = (1 - c_kd) * -ent_check_diff;

                                n_sediment += sediment_limit;
                                _debris[n_idx] += (n_kd * n_sediment);
                                _sediment[n_idx] = (1 - n_kd) * n_sediment;
                            }

                            int b_idx = 0;
                            int r_idx = 0;
                            float b_material = 0.0f;
                            float r_material = 0.0f;
                            float b_debris = 0.0f;
                            float r_debris = 0.0f;
                            float r_sediment = 0.0f;

                            if (is_mh_diff_same_sign) {
                                b_idx = c_idx;
                                r_idx = n_idx;

                                b_material = c_material;
                                r_material = n_material;

                                b_debris = c_debris;
                                r_debris = n_debris;

                                r_sediment = n_sediment;
                            } else {
                                b_idx = n_idx;
                                r_idx = c_idx;

                                b_material = n_material;
                                r_material = c_material;

                                b_debris = n_debris;
                                r_debris = c_debris;

                                r_sediment = c_sediment;
                            }

                            float erosion_per_unit_water =
                                global_erosionrate * erosionrate * bed_erosionrate_factor * erodability * ks_factor;
                            if (r_material != 0.0f && (b_material / r_material) < max_bank_bed_ratio &&
                                r_sediment > (erosion_per_unit_water * max_bank_bed_ratio)) {
                                float height_to_erode = global_erosionrate * erosionrate * bank_erosionrate_factor *
                                                        erodability * ks_factor;

                                float _bank_angle = bank_angle;

                                _bank_angle = zs::clamp(_bank_angle, 0.0f, 90.0f);
                                float safe_diff =
                                    _bank_angle < 90.0f ? zs::tan(_bank_angle * M_PI / 180.0) * delta_x : 1e10f;
                                float target_height_removal = (h_diff - safe_diff) < 0.0f ? 0.0f : h_diff - safe_diff;

                                float dissolve_amt = zs::clamp(height_to_erode, 0.0f, target_height_removal);
                                float dissolved_debris = zs::min(b_debris, dissolve_amt);

                                _debris[b_idx] -= dissolved_debris;

                                float division = 1 / (1 + safe_diff);

                                _height[b_idx] -= (dissolve_amt - dissolved_debris);

                                if (bedrock_density > 0.0f) {
                                    float newdebris = (1 - division) * (dissolve_amt * bedrock_density);
                                    if (b_debris + newdebris > max_debris_depth) {
                                        float rollback = b_debris + newdebris - max_debris_depth;
                                        rollback = zs::min(rollback, newdebris);
                                        _height[b_idx] += rollback / bedrock_density;
                                        newdebris -= rollback;
                                    }
                                    _debris[b_idx] += newdebris;

                                    newdebris = division * (dissolve_amt * bedrock_density);

                                    if (r_debris + newdebris > max_debris_depth) {
                                        float rollback = r_debris + newdebris - max_debris_depth;
                                        rollback = zs::min(rollback, newdebris);
                                        _height[b_idx] += rollback / bedrock_density;
                                        newdebris -= rollback;
                                    }
                                    _debris[r_idx] += newdebris;
                                }
                            }
                        }

                        _material[idx] = i_material + diff;
                        _material[j_idx] = j_material - diff;
                    }
                }
            });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(_height, zs_height);
        retrieve_device_vector(_material, zs_material);
        retrieve_device_vector(_debris, zs_debris);
        retrieve_device_vector(_sediment, zs_sediment);

        set_output("prim_2DGrid", std::move(terrain));
    }
};
ZENDEFNODE(zs_erode_tumble_material_v4,
           {/* inputs: */ {
                "prim_2DGrid",

                {"ListObject", "perm"},
                {"ListObject", "p_dirs"},
                {"ListObject", "x_dirs"},

                {"float", "seed", "12.34"},
                {"int", "iterations", "40"}, // 流淌的总迭代次数
                {"int", "iter", "0"},
                {"int", "i", "0"},

                {"int", "openborder", "0"},
                {"float", "gridbias", "0.0"},

                // 侵蚀主参数
                {"float", "global_erosionrate", "1.0"}, // 全局侵蚀率
                {"float", "erodability", "1.0"},        // 侵蚀能力
                {"float", "erosionrate", "0.4"},        // 侵蚀率
                {"float", "bank_angle", "70.0"},        // 河堤侵蚀角度

                // 高级参数
                {"float", "removalrate", "0.1"},      // 风化率/水吸收率
                {"float", "max_debris_depth", "5.0"}, // 碎屑最大深度

                // 侵蚀能力调整
                {"int", "max_erodability_iteration", "5"},      // 最大侵蚀能力迭代次数
                {"float", "initial_erodability_factor", "0.5"}, // 初始侵蚀能力因子
                {"float", "slope_contribution_factor",
                 "0.8"}, // “地面斜率”对“侵蚀”和“沉积”的影响，“地面斜率大” -> 侵蚀因子大，沉积因子小

                // 河床参数
                {"float", "bed_erosionrate_factor", "1.0"}, // 河床侵蚀率因子
                {"float", "depositionrate", "0.01"},        // 沉积率
                {"float", "sedimentcap", "10.0"}, // 高度差转变为沉积物的比率 / 泥沙容量，每单位流动水可携带的泥沙量

                // 河堤参数
                {"float", "bank_erosionrate_factor", "1.0"}, // 河堤侵蚀率因子
                {"float", "max_bank_bed_ratio", "0.5"}, // 高于这个比值的河岸将不会在侵蚀中被视为河岸，会停止侵蚀

                // 河网控制
                {"float", "quant_amt", "0.05"}, // 流量维持率，越高河流流量越稳定
            },
            /* outputs: */
            {
                "prim_2DGrid",
            },
            /* params: */
            {

            },
            /* category: */
            {
                "erode",
            }});

__forceinline__ __device__ float fit(const float data, const float ss, const float se, const float ds, const float de) {
    float b = zs::limits<float>::epsilon();
    b = zs::max(zs::abs(se - ss), b);
    b = se - ss >= 0 ? b : -b;
    float alpha = (data - ss) / b;
    return ds + (de - ds) * alpha;
}

__forceinline__ __device__ float chramp(const float inputData) {
    float data = zs::min(zs::max(inputData, 0.0f), 1.0f);
    float outputData = 0;
    if (data <= 0.1) {
        outputData = fit(data, 0, 0.1, 0, 1);
    } else if (data >= 0.9) {
        outputData = fit(data, 0.9, 1.0, 1, 0);
    } else {
        outputData = 1;
    }
    return outputData;
}

struct zs_HF_maskByFeature : INode {
    void apply() override {

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 初始化
        ////////////////////////////////////////////////////////////////////////////////////////

        // 初始化网格
        auto terrain = get_input<PrimitiveObject>("prim_2DGrid");
        int nx, nz;
        auto &ud = terrain->userData();
        if ((!ud.has<int>("nx")) || (!ud.has<int>("nz")))
            zeno::log_error("no such UserData named '{}' and '{}'.", "nx", "nz");
        nx = ud.get2<int>("nx");
        nz = ud.get2<int>("nz");
        auto &pos = terrain->verts;
        float cellSize = std::abs(pos[0][0] - pos[1][0]);

        // 获取面板参数
        auto heightLayer = get_input2<std::string>("height_layer");
        auto maskLayer = get_input2<std::string>("mask_layer");
        auto smoothRadius = get_input2<int>("smooth_radius");

        auto useSlope = get_input2<bool>("use_slope");
        auto minSlope = get_input2<float>("min_slopeangle");
        auto maxSlope = get_input2<float>("max_slopeangle");

        auto useDir = get_input2<bool>("use_direction");
        auto goalAngle = get_input2<float>("goal_angle");
        auto angleSpread = get_input2<float>("angle_spread");

        auto useHeight = get_input2<bool>("use_height");
        auto minHeight = get_input2<float>("min_height");
        auto maxHeight = get_input2<float>("max_height");

        // 初始化网格属性
        if (!terrain->verts.has_attr(heightLayer) || !terrain->verts.has_attr(maskLayer)) {
            zeno::log_error("Node [HF_maskByFeature], no such data layer named '{}' or '{}'.", heightLayer, maskLayer);
        }
        auto &height = terrain->verts.attr<float>(heightLayer);
        auto &mask = terrain->verts.attr<float>(maskLayer);

        auto &_grad = terrain->verts.add_attr<vec3f>("_grad");
        std::fill(_grad.begin(), _grad.end(), vec3f(0, 0, 0));

        ////////////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////
        // 计算
        ////////////////////////////////////////////////////////////////////////////////////////

        /// @brief  accelerate cond computation using cuda
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        auto pol = cuda_exec();
        /// @brief  copy host-side attribute
        auto zs_height = to_device_vector(height);
        auto zs_mask = to_device_vector(mask, false);
        auto zs_grad = to_device_vector(_grad);

        pol(range((std::size_t)nz * (std::size_t)nx),
            [=, height = view<space>(zs_height), mask = view<space>(zs_mask),
             _grad = view<space>(zs_grad)] __device__(std::size_t idx) mutable {
                using vec3f = zs::vec<float, 3>;

                auto id_z = idx / nx; // outer index
                auto id_x = idx % nx; // inner index

                // int idx = Pos2Idx(id_x, id_z, nx);
                int idx_xl, idx_xr, idx_zl, idx_zr, scale = 0;

                if (id_x == 0) {
                    idx_xl = idx;
                    idx_xr = Pos2Idx(id_x + 1, id_z, nx);
                    scale = 1;
                } else if (id_x == nx - 1) {
                    idx_xl = Pos2Idx(id_x - 1, id_z, nx);
                    idx_xr = idx;
                    scale = 1;
                } else {
                    idx_xl = Pos2Idx(id_x - 1, id_z, nx);
                    idx_xr = Pos2Idx(id_x + 1, id_z, nx);
                    scale = 2;
                }

                if (id_z == 0) {
                    idx_zl = idx;
                    idx_zr = Pos2Idx(id_x, id_z + 1, nx);
                    scale = 1;
                } else if (id_x == nz - 1) {
                    idx_zl = Pos2Idx(id_x, id_z - 1, nx);
                    idx_zr = idx;
                    scale = 1;
                } else {
                    idx_zl = Pos2Idx(id_x, id_z - 1, nx);
                    idx_zr = Pos2Idx(id_x, id_z + 1, nx);
                    scale = 2;
                }

                _grad[idx][0] = (height[idx_xr] - height[idx_xl]) / (scale * cellSize);
                _grad[idx][2] = (height[idx_zr] - height[idx_zl]) / (scale * cellSize);

                vec3f dx = zs::normalizeSafe(vec3f(1, 0, _grad[idx][0]));
                vec3f dy = zs::normalizeSafe(vec3f(0, 1, _grad[idx][2]));
                vec3f n = zs::normalizeSafe(dx.cross(dy));

                mask[idx] = 1;
                if (!useSlope && !useDir && !useHeight) // &&
                                                        //                    //!useCurvature &&
                                                        //                    //!useOcclusion)
                {
                    mask[idx] = 0;
                }

                if (useSlope) {
                    float slope = 180 * zs::acos(n[2]) / M_PI;
                    slope = fit(slope, minSlope, maxSlope, 0, 1);
                    slope = chramp(slope);
                    mask[idx] *= slope;
                }

                if (useDir) {
                    float direction = 180 * zs::atan2(n[0], n[1]) / M_PI;
                    direction -= goalAngle;
                    direction -= 360 * zs::floor(direction / 360); // Get in range -180 to 180
                    direction -= 180;
                    direction = fit(direction, -angleSpread, angleSpread, 0, 1);
                    direction = chramp(direction);
                    mask[idx] *= direction;
                }

                if (useHeight) {
                    float h = fit(height[idx], minHeight, maxHeight, 0, 1);
                    mask[idx] *= chramp(h);
                }
            });

        /// @brief  write back to host-side attribute
        retrieve_device_vector(mask, zs_mask);
        retrieve_device_vector(_grad, zs_grad);

        set_output("prim_2DGrid", std::move(terrain));
    }
};
ZENDEFNODE(zs_HF_maskByFeature, {/* inputs: */ {
                                     "prim_2DGrid",
                                     {"string", "height_layer", "height"},
                                     {"string", "mask_layer", "mask"},
                                     {"int", "smooth_radius", "1"},
                                     {"bool", "use_slope", "0"},
                                     {"float", "min_slopeangle", "0"},
                                     {"float", "max_slopeangle", "90"},
                                     //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                                     {"bool", "use_direction", "0"},
                                     {"float", "goal_angle", "0"},
                                     {"float", "angle_spread", "30"},
                                     //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                                     {"bool", "use_height", "0"},
                                     {"float", "min_height", "0"},
                                     {"float", "max_height", "1"},
                                 },
                                 /* outputs: */
                                 {
                                     "prim_2DGrid",
                                 },
                                 /* params: */
                                 {},
                                 /* category: */
                                 {
                                     "erode",
                                 }});

} // namespace zeno