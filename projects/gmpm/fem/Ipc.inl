
namespace zeno {

void CodimStepping::FEMSystem::computeBarrierGradientAndHessian(
    zs::CudaExecutionPolicy &pol, const zs::SmallString &gTag) {
  using namespace zs;
  constexpr auto space = execspace_e::cuda;
  T activeGap2 = dHat * dHat + (T)2.0 * xi * dHat;
  using Vec12View = zs::vec_view<T, zs::integer_seq<int, 12>>;
  using Vec9View = zs::vec_view<T, zs::integer_seq<int, 9>>;
  using Vec6View = zs::vec_view<T, zs::integer_seq<int, 6>>;
  auto numPP = nPP.getVal();
  pol(range(numPP),
      [verts = proxy<space>({}, verts), vtemp = proxy<space>({}, vtemp),
       tempPP = proxy<space>({}, tempPP), PP = proxy<space>(PP), gTag,
       xi2 = xi * xi, dHat = dHat, activeGap2,
       kappa = kappa] __device__(int ppi) mutable {
        auto pp = PP[ppi];
        auto x0 = vtemp.pack<3>("xn", pp[0]);
        auto x1 = vtemp.pack<3>("xn", pp[1]);
#if 0
    auto ppGrad = dist_grad_pp(x0, x1);
    auto dist2 = dist2_pp(x0, x1);
    if (dist2 < xi2)
      printf("dist already smaller than xi!\n");
    auto barrierDistGrad = zs::barrier_gradient(dist2 - xi2, activeGap2, kappa);
    auto grad = ppGrad * (-barrierDistGrad);
    // gradient
    for (int d = 0; d != 3; ++d) {
      atomic_add(exec_cuda, &vtemp(gTag, d, pp[0]), grad(0, d));
      atomic_add(exec_cuda, &vtemp(gTag, d, pp[1]), grad(1, d));
    }
    // hessian
    auto ppHess = dist_hess_pp(x0, x1);
    auto ppGrad_ = Vec6View{ppGrad.data()};
    ppHess = (zs::barrier_hessian(dist2 - xi2, activeGap2, kappa) *
                  dyadic_prod(ppGrad_, ppGrad_) +
              barrierDistGrad * ppHess);
    // make pd
    make_pd(ppHess);
#else
#endif
        // rotate and project
        mat3 BCbasis[2];
        int BCorder[2];
        for (int i = 0; i != 2; ++i) {
          BCbasis[i] = vtemp.pack<3, 3>("BCbasis", pp[i]);
          BCorder[i] = vtemp("BCorder", pp[i]);
        }
        for (int vi = 0; vi != 2; ++vi) {
          int offsetI = vi * 3;
          for (int vj = 0; vj != 2; ++vj) {
            int offsetJ = vj * 3;
            mat3 tmp{};
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                tmp(i, j) = ppHess(offsetI + i, offsetJ + j);
            // rotate
            tmp = BCbasis[vi].transpose() * tmp * BCbasis[vj];
            // project
            if (BCorder[vi] > 0 || BCorder[vj] > 0) {
              if (vi == vj) {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = (i == j ? 1 : 0);
              } else {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = 0;
              }
            }
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                ppHess(offsetI + i, offsetJ + j) = tmp(i, j);
          }
        }
        // pp[0], pp[1]
        tempPP.tuple<36>("H", ppi) = ppHess;
        /// construct P
        for (int vi = 0; vi != 2; ++vi) {
          for (int i = 0; i != 3; ++i)
            for (int j = 0; j != 3; ++j) {
              atomic_add(exec_cuda, &vtemp("P", i * 3 + j, pp[vi]),
                         ppHess(vi * 3 + i, vi * 3 + j));
            }
        }
      });
  auto numPE = nPE.getVal();
  pol(range(numPE),
      [verts = proxy<space>({}, verts), vtemp = proxy<space>({}, vtemp),
       tempPE = proxy<space>({}, tempPE), PE = proxy<space>(PE), gTag,
       xi2 = xi * xi, dHat = dHat, activeGap2,
       kappa = kappa] __device__(int pei) mutable {
        auto pe = PE[pei];
        auto p = vtemp.pack<3>("xn", pe[0]);
        auto e0 = vtemp.pack<3>("xn", pe[1]);
        auto e1 = vtemp.pack<3>("xn", pe[2]);
#if 0
        auto peGrad = dist_grad_pe(p, e0, e1);
        auto dist2 = dist2_pe(p, e0, e1);
        if (dist2 < xi2)
          printf("dist already smaller than xi!\n");
        auto barrierDistGrad = barrier_gradient(dist2 - xi2, activeGap2, kappa);
        auto grad = peGrad * (-barrierDistGrad);
        // gradient
        for (int d = 0; d != 3; ++d) {
          atomic_add(exec_cuda, &vtemp(gTag, d, pe[0]), grad(0, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, pe[1]), grad(1, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, pe[2]), grad(2, d));
        }
        // hessian
        auto peHess = dist_hess_pe(p, e0, e1);
        auto peGrad_ = Vec9View{peGrad.data()};
        peHess = (zs::barrier_hessian(dist2 - xi2, activeGap2, kappa) *
                      dyadic_prod(peGrad_, peGrad_) +
                  barrierDistGrad * peHess);
        // make pd
        make_pd(peHess);
#else
#endif
        // rotate and project
        mat3 BCbasis[3];
        int BCorder[3];
        for (int i = 0; i != 3; ++i) {
          BCbasis[i] = vtemp.pack<3, 3>("BCbasis", pe[i]);
          BCorder[i] = vtemp("BCorder", pe[i]);
        }
        for (int vi = 0; vi != 3; ++vi) {
          int offsetI = vi * 3;
          for (int vj = 0; vj != 3; ++vj) {
            int offsetJ = vj * 3;
            mat3 tmp{};
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                tmp(i, j) = peHess(offsetI + i, offsetJ + j);
            // rotate
            tmp = BCbasis[vi].transpose() * tmp * BCbasis[vj];
            // project
            if (BCorder[vi] > 0 || BCorder[vj] > 0) {
              if (vi == vj) {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = (i == j ? 1 : 0);
              } else {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = 0;
              }
            }
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                peHess(offsetI + i, offsetJ + j) = tmp(i, j);
          }
        }
        // pe[0], pe[1], pe[2]
        tempPE.tuple<81>("H", pei) = peHess;
        /// construct P
        for (int vi = 0; vi != 3; ++vi) {
          for (int i = 0; i != 3; ++i)
            for (int j = 0; j != 3; ++j) {
              atomic_add(exec_cuda, &vtemp("P", i * 3 + j, pe[vi]),
                         peHess(vi * 3 + i, vi * 3 + j));
            }
        }
      });
  auto numPT = nPT.getVal();
  pol(range(numPT),
      [verts = proxy<space>({}, verts), vtemp = proxy<space>({}, vtemp),
       tempPT = proxy<space>({}, tempPT), PT = proxy<space>(PT), gTag,
       xi2 = xi * xi, dHat = dHat, activeGap2,
       kappa = kappa] __device__(int pti) mutable {
        auto pt = PT[pti];
        auto p = vtemp.pack<3>("xn", pt[0]);
        auto t0 = vtemp.pack<3>("xn", pt[1]);
        auto t1 = vtemp.pack<3>("xn", pt[2]);
        auto t2 = vtemp.pack<3>("xn", pt[3]);
#if 0
        auto ptGrad = dist_grad_pt(p, t0, t1, t2);
        auto dist2 = dist2_pt(p, t0, t1, t2);
        if (dist2 < xi2)
          printf("dist already smaller than xi!\n");
        auto barrierDistGrad = barrier_gradient(dist2 - xi2, activeGap2, kappa);
        auto grad = ptGrad * (-barrierDistGrad);
        // gradient
        for (int d = 0; d != 3; ++d) {
          atomic_add(exec_cuda, &vtemp(gTag, d, pt[0]), grad(0, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, pt[1]), grad(1, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, pt[2]), grad(2, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, pt[3]), grad(3, d));
        }
        // hessian
        auto ptHess = dist_hess_pt(p, t0, t1, t2);
        auto ptGrad_ = Vec12View{ptGrad.data()};
        ptHess = (zs::barrier_hessian(dist2 - xi2, activeGap2, kappa) *
                      dyadic_prod(ptGrad_, ptGrad_) +
                  barrierDistGrad * ptHess);
        // make pd
        make_pd(ptHess);
#else
#endif
        // rotate and project
        mat3 BCbasis[4];
        int BCorder[4];
        for (int i = 0; i != 4; ++i) {
          BCbasis[i] = vtemp.pack<3, 3>("BCbasis", pt[i]);
          BCorder[i] = vtemp("BCorder", pt[i]);
        }
        for (int vi = 0; vi != 4; ++vi) {
          int offsetI = vi * 3;
          for (int vj = 0; vj != 4; ++vj) {
            int offsetJ = vj * 3;
            mat3 tmp{};
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                tmp(i, j) = ptHess(offsetI + i, offsetJ + j);
            // rotate
            tmp = BCbasis[vi].transpose() * tmp * BCbasis[vj];
            // project
            if (BCorder[vi] > 0 || BCorder[vj] > 0) {
              if (vi == vj) {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = (i == j ? 1 : 0);
              } else {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = 0;
              }
            }
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                ptHess(offsetI + i, offsetJ + j) = tmp(i, j);
          }
        }
        // pt[0], pt[1], pt[2], pt[3]
        tempPT.tuple<144>("H", pti) = ptHess;
        /// construct P
        for (int vi = 0; vi != 4; ++vi) {
          for (int i = 0; i != 3; ++i)
            for (int j = 0; j != 3; ++j) {
              atomic_add(exec_cuda, &vtemp("P", i * 3 + j, pt[vi]),
                         ptHess(vi * 3 + i, vi * 3 + j));
            }
        }
      });
  auto numEE = nEE.getVal();
  pol(range(numEE),
      [verts = proxy<space>({}, verts), vtemp = proxy<space>({}, vtemp),
       tempEE = proxy<space>({}, tempEE), EE = proxy<space>(EE), gTag,
       xi2 = xi * xi, dHat = dHat, activeGap2,
       kappa = kappa] __device__(int eei) mutable {
        auto ee = EE[eei];
        auto ea0 = vtemp.pack<3>("xn", ee[0]);
        auto ea1 = vtemp.pack<3>("xn", ee[1]);
        auto eb0 = vtemp.pack<3>("xn", ee[2]);
        auto eb1 = vtemp.pack<3>("xn", ee[3]);
#if 0
        auto eeGrad = dist_grad_ee(ea0, ea1, eb0, eb1);
        auto dist2 = dist2_ee(ea0, ea1, eb0, eb1);
        if (dist2 < xi2)
          printf("dist already smaller than xi!\n");
        auto barrierDistGrad = barrier_gradient(dist2 - xi2, activeGap2, kappa);
        auto grad = eeGrad * (-barrierDistGrad);
        // gradient
        for (int d = 0; d != 3; ++d) {
          atomic_add(exec_cuda, &vtemp(gTag, d, ee[0]), grad(0, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, ee[1]), grad(1, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, ee[2]), grad(2, d));
          atomic_add(exec_cuda, &vtemp(gTag, d, ee[3]), grad(3, d));
        }
        // hessian
        auto eeHess = dist_hess_ee(ea0, ea1, eb0, eb1);
        auto eeGrad_ = Vec12View{eeGrad.data()};
        eeHess = (zs::barrier_hessian(dist2 - xi2, activeGap2, kappa) *
                      dyadic_prod(eeGrad_, eeGrad_) +
                  barrierDistGrad * eeHess);
        // make pd
        make_pd(eeHess);
#else
#endif
        // rotate and project
        mat3 BCbasis[4];
        int BCorder[4];
        for (int i = 0; i != 4; ++i) {
          BCbasis[i] = vtemp.pack<3, 3>("BCbasis", ee[i]);
          BCorder[i] = vtemp("BCorder", ee[i]);
        }
        for (int vi = 0; vi != 4; ++vi) {
          int offsetI = vi * 3;
          for (int vj = 0; vj != 4; ++vj) {
            int offsetJ = vj * 3;
            mat3 tmp{};
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                tmp(i, j) = eeHess(offsetI + i, offsetJ + j);
            // rotate
            tmp = BCbasis[vi].transpose() * tmp * BCbasis[vj];
            // project
            if (BCorder[vi] > 0 || BCorder[vj] > 0) {
              if (vi == vj) {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = (i == j ? 1 : 0);
              } else {
                for (int i = 0; i != BCorder[vi]; ++i)
                  for (int j = 0; j != BCorder[vj]; ++j)
                    tmp(i, j) = 0;
              }
            }
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j)
                eeHess(offsetI + i, offsetJ + j) = tmp(i, j);
          }
        }
        // ee[0], ee[1], ee[2], ee[3]
        tempEE.tuple<144>("H", eei) = eeHess;
        /// construct P
        for (int vi = 0; vi != 4; ++vi) {
          for (int i = 0; i != 3; ++i)
            for (int j = 0; j != 3; ++j) {
              atomic_add(exec_cuda, &vtemp("P", i * 3 + j, ee[vi]),
                         eeHess(vi * 3 + i, vi * 3 + j));
            }
        }
      });
  return;
}

} // namespace zeno