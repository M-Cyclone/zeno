#pragma once

#include "../../Structures.hpp"

namespace zeno {
    template<typename T>
    constexpr T doublearea(T a,T b,T c) {
        T s = (a + b + c)/2;
        return 2*zs::sqrt(s*(s-a)*(s-b)*(s-c));
    }

    template<typename T>
    constexpr T area(T a,T b,T c) {
        return doublearea(a,b,c)/2;
    }

    template<typename T>
    constexpr T volume(zs::vec<T, 6> l) {
        T u = l(0);

        T v = l(1);
        T w = l(2);
        T U = l(3);
        T V = l(4);
        T W = l(5);
        T X = (w - U + v)*(U + v + w);
        T x = (U - v + w)*(v - w + U);
        T Y = (u - V + w)*(V + w + u);
        T y = (V - w + u)*(w - u + V);
        T Z = (v - W + u)*(W + u + v);
        T z = (W - u + v)*(u - v + W);
        T a = zs::sqrt(x*Y*Z);
        T b = zs::sqrt(y*Z*X);
        T c = zs::sqrt(z*X*Y);
        T d = zs::sqrt(x*y*z);
        T vol = zs::sqrt(
        (-a + b + c + d)*
        ( a - b + c + d)*
        ( a + b - c + d)*
        ( a + b + c - d))/
        (192.*u*v*w);

        return vol;
    }

    template<typename T>
    constexpr void dihedral_angle_intrinsic(const zs::vec<T, 6>& l,const zs::vec<T, 4>& s,zs::vec<T, 6>& theta,zs::vec<T, 6>& cos_theta) {
        zs::vec<T, 6> H_sqr{};
        H_sqr[0] = (1./16.) * (4.*l(3)*l(3)*l(0)*l(0) - zs::sqr((l(1)*l(1) + l(4)*l(4)) - (l(2)*l(1) + l(5)*l(5))));
        H_sqr[1] = (1./16.) * (4.*l(4)*l(4)*l(1)*l(1) - zs::sqr((l(2)*l(2) + l(5)*l(5)) - (l(3)*l(3) + l(0)*l(0))));
        H_sqr[2] = (1./16.) * (4.*l(5)*l(5)*l(2)*l(2) - zs::sqr((l(3)*l(3) + l(0)*l(0)) - (l(4)*l(4) + l(1)*l(1))));
        H_sqr[3] = (1./16.) * (4.*l(0)*l(0)*l(3)*l(3) - zs::sqr((l(4)*l(4) + l(1)*l(1)) - (l(5)*l(5) + l(2)*l(2))));
        H_sqr[4] = (1./16.) * (4.*l(1)*l(1)*l(4)*l(4) - zs::sqr((l(5)*l(5) + l(2)*l(2)) - (l(0)*l(0) + l(3)*l(3))));
        H_sqr[5] = (1./16.) * (4.*l(2)*l(2)*l(5)*l(5) - zs::sqr((l(0)*l(0) + l(3)*l(3)) - (l(1)*l(1) + l(4)*l(4))));

        cos_theta(0) = (H_sqr(0) - s(1)*s(1) - s(2)*s(2)) / (-2.*s(1) * s(2));
        cos_theta(1) = (H_sqr(1) - s(2)*s(2) - s(0)*s(0)) / (-2.*s(2) * s(0));
        cos_theta(2) = (H_sqr(2) - s(0)*s(0) - s(1)*s(1)) / (-2.*s(0) * s(1));
        cos_theta(3) = (H_sqr(3) - s(3)*s(3) - s(0)*s(0)) / (-2.*s(3) * s(0));
        cos_theta(4) = (H_sqr(4) - s(3)*s(3) - s(1)*s(1)) / (-2.*s(3) * s(1));
        cos_theta(5) = (H_sqr(5) - s(3)*s(3) - s(2)*s(2)) / (-2.*s(3) * s(2));

        theta(0) = zs::acos(cos_theta(0));  
        theta(1) = zs::acos(cos_theta(1)); 
        theta(2) = zs::acos(cos_theta(2)); 
        theta(3) = zs::acos(cos_theta(3)); 
        theta(4) = zs::acos(cos_theta(4)); 
        theta(5) = zs::acos(cos_theta(5));       
    }

    template <typename T,typename Pol,int codim = 3>
    constexpr void compute_cotmatrix(Pol &pol,const typename ZenoParticles::particles_t &eles,
        const typename ZenoParticles::particles_t &verts, const zs::SmallString& xTag, 
        zs::TileVector<T,32>& etemp, const zs::SmallString& HTag,
        zs::wrapv<codim> = {}) {

        using namespace zs;
        static_assert(codim >= 3 && codim <=4, "invalid co-dimension!\n");
        constexpr auto space = Pol::exec_tag::value;

        #if ZS_ENABLE_CUDA && defined(__CUDACC__)
            static_assert(space == execspace_e::cuda,
                    "specified policy and compiler not match");
        #else
            static_assert(space != execspace_e::cuda,
                    "specified policy and compiler not match");
        #endif

        if(!verts.hasProperty(xTag)){
            printf("the verts buffer does not contain specified channel\n");
        }   

        // if(!etemp.hasProperty(HTag)){
        //     printf("the etemp buffer does not contain specified channel\n");
        // }  

        etemp.append_channels(pol,{{HTag,codim*codim}});

        // zs::Vector<T> C{eles.get_allocator(),eles.size()*codim*(codim-1)/2};

        // compute cotangent entries
        pol(zs::range(etemp.size()),
            [eles = proxy<space>({},eles),verts = proxy<space>({},verts),
            etemp = proxy<space>({},etemp),xTag,HTag,codim_v = wrapv<codim>{}] ZS_LAMBDA(int ei) mutable {
                constexpr int cdim = RM_CVREF_T(codim_v)::value;
                constexpr int ne = cdim*(cdim-1)/2;
                auto inds = eles.template pack<cdim>("inds",ei).template reinterpret_bits<int>();
                
                using IV = zs::vec<int,ne*2>;
                using TV = zs::vec<T, ne>;

                TV C;
                IV edges;
                // compute the cotangent entris
                if constexpr (cdim == 3){
                    edges = IV{1,2,2,0,0,1};
                    zs::vec<T,cdim> l;
                    for(size_t i = 0;i != ne;++i)
                        l[i] = (verts.pack<3>(xTag,inds[edges[i*2+0]]) - verts.pack<3>(xTag,inds[edges[i*2+1]])).norm();
                    auto dblA = doublearea(l[0],l[1],l[2]);// check here, double area
                    for(size_t i = 0;i != ne;++i)
                        C[i] = (l[edges[2*i+0]] + l[edges[2*i+1]] - l[3 - edges[2*i+0] - edges[2*i+1]])/dblA/4.0;
                }
                if constexpr (cdim == 4){
                    edges = IV{1,2,2,0,0,1,3,0,3,1,3,2};
                    zs::vec<T,ne> l;
                    for(int i = 0;i != ne;++i)
                        l[i] = (verts.pack<3>(xTag,inds[edges[i*2+0]]) - verts.pack<3>(xTag,inds[edges[i*2+1]])).norm();
                    zs::vec<T, 4> s{ 
                        area(l[1],l[2],l[3]),
                        area(l[0],l[2],l[4]),
                        area(l[0],l[1],l[5]),
                        area(l[3],l[4],l[5])};

                    zs::vec<T,ne> cos_theta,theta;
                    dihedral_angle_intrinsic(l,s,theta,cos_theta);

                    T vol = volume(l);
                    zs::vec<T, 6> sin_theta;
                    for(size_t i = 0;i != ne;++i)
                        sin_theta(i) = vol / ((2./(3.*l(3 - edges[i*2 + 0] - edges[i*2 + 1]))) * s(edges[i*2 + 0]) * s(edges[i*2 + 1]));
                    C = (1./6.) * l * cos_theta / sin_theta;
                }

                constexpr int cdim2 = cdim*cdim;
                etemp.template tuple<cdim2>(HTag,ei) = zs::vec<T,cdim2>::zeros();
                for(size_t i = 0;i != ne;++i){
                    int source = edges(i*2 + 0);
                    int dest = edges(i*2 + 1);
                    etemp(HTag,ei*cdim*cdim + cdim*source + dest) += C(i); 
                    etemp(HTag,ei*cdim*cdim + cdim*dest + source) += C(i); 
                    etemp(HTag,ei*cdim*cdim + cdim*source + source) -= C(i); 
                    etemp(HTag,ei*cdim*cdim + cdim*dest + dest) -= C(i); 
                }
        });


    }

};