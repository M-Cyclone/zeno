#pragma once

#include "base_force_model.h"
#include "cubicBspline.h"
/**
 * @class <BSplineIsotropicMuscle>
 * @brief A force model base class for inheritance, which defines the common interfaces needed for defining all the force model derived classes 
 * 
 * The current version only support isotropic elasto and damping model. The force model base class is unaware of the TetMesh, the input to all its
 * function is deformation gradient of individual element, instead of its deformaed shape.
 */
class BSplineIsotropicMuscle : public MuscleForceModel {
public:
    /**
     * @brief The constructor of BSplineIsotropicMuscle class
     */
    BSplineIsotropicMuscle(std::shared_ptr<UniformCubicBasisSpline> _I1Spline,
        std::shared_ptr<UniformCubicBasisSpline> _I2Spline,
        std::shared_ptr<UniformCubicBasisSpline> _I3Spline) : MuscleForceModel(),I1Spline(_I1Spline),I2Spline(_I2Spline),I3Spline(_I3Spline){}

    // the default constructor intialize the spline with stable neohookean elastic model
    BSplineIsotropicMuscle(FEM_Scaler YoungModulus,FEM_Scaler PossonRatio) : MuscleForceModel() {
        I1Spline = std::make_shared<UniformCubicBasisSpline>();
        I2Spline = std::make_shared<UniformCubicBasisSpline>();
        I3Spline = std::make_shared<UniformCubicBasisSpline>();

        Vec2d inner_range = Vec2d(0.5,2);
        size_t nm_interps = 6;
        VecXd I1Interps = VecXd::Zero(nm_interps);
        VecXd I2Interps = VecXd::Zero(nm_interps);
        VecXd I3Interps = VecXd::Zero(nm_interps);

        FEM_Scaler inner_width = inner_range[1] - inner_range[0];
        FEM_Scaler step = inner_width / (nm_interps - 1);

        VecXd interp_u = VecXd::Zero(nm_interps);
        for(size_t i = 0;i < nm_interps;++i)
            interp_u[i] = inner_range[0] + step * i;

        FEM_Scaler mu = Enu2Mu(YoungModulus,PossonRatio);
        FEM_Scaler lambda = Enu2Lambda(YoungModulus,PossonRatio);

        // neohookean model
        for(size_t i = 0;i < nm_interps;++i)
            I1Interps[i] = 0;
        for(size_t i = 0;i < nm_interps;++i)
            I2Interps[i] = mu / 2;
        for(size_t i = 0;i < nm_interps;++i)
            I3Interps[i] = -mu + lambda * (interp_u[i] - 1);

        I1Spline->Interpolate(I1Interps,inner_range);
        I2Spline->Interpolate(I2Interps,inner_range);
        I3Spline->Interpolate(I3Interps,inner_range);
    }
    /**
     * @brief destructor method.
     */
    virtual ~BSplineIsotropicMuscle(){}
    /**
     * @brief An interface for defining the potential energy of anisotropic force model, all the force models should inherit this method and implement their 
     * own version of element-wise potential energy defination.
     * @param activation the activation level along the fiber direction
     * @param fiber_direction the three orthogonal fiber directions
     * @param F the deformation gradient
     * @param energy the potential energy output
     */
    void ComputePhi(const Mat3x3d& activation,
        const Vec3d& aniso_weight,const Mat3x3d& fiber_direction,const FEM_Scaler& YoungModulus,const FEM_Scaler& PossonRatio,
        const Mat3x3d& F,FEM_Scaler& psi) const override {
            Mat3x3d ActInv = activation.inverse();
            Mat3x3d FAct = F * ActInv;

            FEM_Scaler mu = Enu2Mu(YoungModulus,PossonRatio);
            FEM_Scaler lambda = Enu2Lambda(YoungModulus,PossonRatio);

            Vec3d Is;
            EvalIsoInvarients(FAct,Is);

            psi = I1Spline->EvalIntegrationOnKnot(Is[0]);
            psi += I2Spline->EvalIntegrationOnKnot(Is[1]);
            psi += I3Spline->EvalIntegrationOnKnot(Is[2]);
    }
    /**
     * @brief An interface for defining the potential energy of force model, all the force models should inherit this method and implement their
     * own version of element-wise potential energy defination and element-wise energy gradient.
     * @param activation the activation level along the three orthogonal fiber directions
     * @param fiber_direction the three orthogonal fiber directions
     * @param F the deformation gradient
     * @param energy the potential energy output
     * @param the derivative of potential w.r.t the deformed shape for elasto model or nodal velocities for damping model
     */
    void ComputePhiDeriv(const Mat3x3d& activation,
        const Vec3d& aniso_weight,const Mat3x3d& fiber_direction,const FEM_Scaler& YoungModulus,const FEM_Scaler& PossonRatio,
        const Mat3x3d& F,FEM_Scaler &psi,Vec9d &dpsi) const override {
            Mat3x3d ActInv = activation.inverse();
            Mat3x3d FAct = F * ActInv;

            FEM_Scaler mu = Enu2Mu(YoungModulus,PossonRatio);
            FEM_Scaler lambda = Enu2Lambda(YoungModulus,PossonRatio);

            Vec3d Is;
            std::array<Vec9d,3> gs;
            EvalIsoInvarientsDeriv(FAct,Is,gs);

            psi = I1Spline->EvalIntegrationOnKnot(Is[0]);
            psi += I2Spline->EvalIntegrationOnKnot(Is[1]);
            psi += I3Spline->EvalIntegrationOnKnot(Is[2]);

            Vec3d dphi;
            dphi << I1Spline->EvalOnKnot(Is[0]),I2Spline->EvalOnKnot(Is[1]),I3Spline->EvalOnKnot(Is[2]);

            Mat9x9d dFactdF = EvaldFactdF(ActInv); 

            dpsi = dphi[0] * gs[0] + dphi[1] * gs[1] + dphi[2] * gs[2];     
            dpsi = dFactdF.transpose() * dpsi;            
    }
    /**
     * @brief An interface for defining the potential energy of force model, all the force models should inherit this method and implement their
     * own version of element-wise potential energy defination, element-wise energy gradient and 12x12 element-wise energy hessian w.r.t deformed shape.
     * @param activation the activation level along the three orthogonal fiber directions
     * @param fiber_direction the three orthogonal fiber directions
     * @param <F> the deformation gradient
     * @param <energy> the potential energy output
     * @param <derivative> the derivative of potential energy w.r.t the deformation gradient
     * @param <Hessian> the hessian of potential energy w.r.t the deformed shape for elasto model or nodal velocities for damping model
     * @param <enforcing_spd> decide whether we should enforce the SPD of hessian matrix
     */
    void ComputePhiDerivHessian(const Mat3x3d& activation,
            const Vec3d& aniso_weight,const Mat3x3d& fiber_direction,const FEM_Scaler& YoungModulus,const FEM_Scaler& PossonRatio,
            const Mat3x3d &F,FEM_Scaler& psi,Vec9d &dpsi, Mat9x9d &ddpsi,bool enforcing_spd = true) const override {

        Mat3x3d ActInv = activation.inverse();
        Mat3x3d FAct = F * ActInv;

        FEM_Scaler mu = Enu2Mu(YoungModulus,PossonRatio);
        FEM_Scaler lambda = Enu2Lambda(YoungModulus,PossonRatio);

        Vec3d Is;
        std::array<Vec9d,3> gs;
        EvalIsoInvarientsDeriv(FAct,Is,gs);

        psi = I1Spline->EvalIntegrationOnKnot(Is[0]);
        psi += I2Spline->EvalIntegrationOnKnot(Is[1]);
        psi += I3Spline->EvalIntegrationOnKnot(Is[2]);

        Vec3d dphi;
        dphi << I1Spline->EvalOnKnot(Is[0]),I2Spline->EvalOnKnot(Is[1]),I3Spline->EvalOnKnot(Is[2]);

        Mat9x9d dFactdF = EvaldFactdF(ActInv); 

        dpsi = dphi[0] * gs[0] + dphi[1] * gs[1] + dphi[2] * gs[2];     
        dpsi = dFactdF.transpose() * dpsi;  

        Vec9d eigen_vecs[9];
        Vec9d eigen_vals;
            
        // compute the eigen system of Hessian
        // FAct = Mat3x3d::Random();
        // FAct = FAct.transpose() * FAct;
        ComputeIsoEigenSystem(YoungModulus,PossonRatio, FAct, eigen_vals, eigen_vecs);  

        // std::cout << "BSpline IsoEigenSystem : " << std::endl;
        
        // for(size_t i = 0;i < 9;++i)
        //     std::cout << "L[" << i << "] : " << eigen_vals[i] << std::endl;
        // for(size_t i = 0;i < 9;++i)
        //     std::cout << "V[" << i << "] : " << eigen_vecs[i].transpose() << std::endl;

        // throw std::runtime_error("ISO_EIGEN_SYSTEM_CHECK");

        if (enforcing_spd) {
            for (size_t i = 0; i < 9; ++i)
                eigen_vals[i] = eigen_vals[i] > 1e-12 ? eigen_vals[i] : 1e-12;
        }
        ddpsi.setZero();
        for(size_t i = 0;i < 9;++i){
            ddpsi += eigen_vals[i] * eigen_vecs[i] * eigen_vecs[i].transpose();
            // std::cout << "UPDATE<" << i << "> : " << std::endl;
            // std::cout << eigen_vals[i] * eigen_vecs[i] * eigen_vecs[i].transpose() << std::endl;
        }


        // std::cout << "OUTPUT DDPSI : " << std::endl << ddpsi << std::endl;

        ddpsi = dFactdF.transpose() * ddpsi * dFactdF; 
    }

    void ComputeIsoEigenSystem(FEM_Scaler YoungModulus,FEM_Scaler PossonRatio,
        const Mat3x3d& F,
        Vec9d& eigen_vals,
        Vec9d eigen_vecs[9]) const {
        FEM_Scaler mu = Enu2Mu(YoungModulus,PossonRatio);
        FEM_Scaler lambda = Enu2Lambda(YoungModulus,PossonRatio);

        Mat3x3d U,V;
        Vec3d s;

        DiffSVD::SVD_Decomposition(F, U, s, V);

        Vec3d Is;
        EvalIsoInvarients(s, Is);

        // compute the eigen system of Hessian
        Vec3d l_scale, l_twist, l_flip;

        Mat3x3d A;

        ComputeIsoStretchingMatrix(lambda, mu, Is, s, A);
        // std::cout << "BSpline Stretching Matrix : " << std::endl << A << std::endl;

        Mat3x3d U_proj;
        DiffSVD::SYM_Eigen_Decomposition(A, l_scale, U_proj);

        size_t offset = 0;
        //scale
        eigen_vals[offset++] = l_scale[0];
        eigen_vals[offset++] = l_scale[1];
        eigen_vals[offset++] = l_scale[2];
        // flip
        eigen_vals[offset++] = 2 * I1Spline->EvalOnKnot(Is[0]) / (s[0] + s[1]) + 2 * I2Spline->EvalOnKnot(Is[1]) + s[2] * I3Spline->EvalOnKnot(Is[2]);
        eigen_vals[offset++] = 2 * I1Spline->EvalOnKnot(Is[0]) / (s[1] + s[2]) + 2 * I2Spline->EvalOnKnot(Is[1]) + s[0] * I3Spline->EvalOnKnot(Is[2]);
        eigen_vals[offset++] = 2 * I1Spline->EvalOnKnot(Is[0]) / (s[0] + s[2]) + 2 * I2Spline->EvalOnKnot(Is[1]) + s[1] * I3Spline->EvalOnKnot(Is[2]);
        //twist     
        eigen_vals[offset++] = 2 * I2Spline->EvalOnKnot(Is[1]) - s[2] * I3Spline->EvalOnKnot(Is[2]);
        eigen_vals[offset++] = 2 * I2Spline->EvalOnKnot(Is[1]) - s[0] * I3Spline->EvalOnKnot(Is[2]);
        eigen_vals[offset++] = 2 * I2Spline->EvalOnKnot(Is[1]) - s[1] * I3Spline->EvalOnKnot(Is[2]);

        Mat9x3d proj_space;
        for (size_t i = 0; i < 3; ++i)
            proj_space.col(i) = MatHelper::VEC(U * Qs[i] * V.transpose());    

        for (size_t i = 0; i < 3; ++i)
            eigen_vecs[i] = proj_space * U_proj.col(i);

        // compute the flip and twist eigen matrix
        for (size_t i = 3; i < 9; ++i) {
            Mat3x3d Qi = U * Qs[i] * V.transpose();
            eigen_vecs[i] = MatHelper::VEC(Qi);
        }
    }


    inline Mat9x9d EvaldFactdF(const Mat3x3d& Act_inv) const {
        Mat9x9d M = Mat9x9d::Zero();
        
        M(0,0) = M(1,1) = M(2,2) = Act_inv(0,0);
        M(3,0) = M(4,1) = M(5,2) = Act_inv(0,1);
        M(6,0) = M(7,1) = M(8,2) = Act_inv(0,2);

        M(0,3) = M(1,4) = M(2,5) = Act_inv(1,0);
        M(3,3) = M(4,4) = M(5,5) = Act_inv(1,1);
        M(6,3) = M(7,4) = M(8,5) = Act_inv(1,2);

        M(0,6) = M(1,7) = M(2,8) = Act_inv(2,0);
        M(3,6) = M(4,7) = M(5,8) = Act_inv(2,1);
        M(6,6) = M(7,7) = M(8,8) = Act_inv(2,2);

        return M;
    }

private:
// skip the influence of I2spline here
    void ComputeIsoStretchingMatrix(FEM_Scaler lambda,FEM_Scaler mu,const Vec3d& Is,const Vec3d& sigma,Mat3x3d& A) const {
        A(0,0) = I1Spline->EvalDerivativeOnKnot(Is[0]) + 2*I2Spline->EvalOnKnot(Is[1]) + 4*sigma[0]*sigma[0]*I2Spline->EvalDerivativeOnKnot(Is[1]) + I3Spline->EvalDerivativeOnKnot(Is[2])*Is[2]*Is[2]/sigma[0]/sigma[0];
        A(1,1) = I1Spline->EvalDerivativeOnKnot(Is[0]) + 2*I2Spline->EvalOnKnot(Is[1]) + 4*sigma[1]*sigma[1]*I2Spline->EvalDerivativeOnKnot(Is[1]) + I3Spline->EvalDerivativeOnKnot(Is[2])*Is[2]*Is[2]/sigma[1]/sigma[1];
        A(2,2) = I1Spline->EvalDerivativeOnKnot(Is[0]) + 2*I2Spline->EvalOnKnot(Is[1]) + 4*sigma[2]*sigma[2]*I2Spline->EvalDerivativeOnKnot(Is[1]) + I3Spline->EvalDerivativeOnKnot(Is[2])*Is[2]*Is[2]/sigma[2]/sigma[2];

        A(0,1) = I1Spline->EvalDerivativeOnKnot(Is[0]) + 4*sigma[0]*sigma[1]*I2Spline->EvalDerivativeOnKnot(Is[1]) +  I3Spline->EvalOnKnot(Is[2]) * sigma[2] + I3Spline->EvalDerivativeOnKnot(Is[2]) * Is[2] * sigma[2];
        A(0,2) = I1Spline->EvalDerivativeOnKnot(Is[0]) + 4*sigma[0]*sigma[2]*I2Spline->EvalDerivativeOnKnot(Is[1]) +  I3Spline->EvalOnKnot(Is[2]) * sigma[1] + I3Spline->EvalDerivativeOnKnot(Is[2]) * Is[2] * sigma[1];
        A(1,2) = I1Spline->EvalDerivativeOnKnot(Is[0]) + 4*sigma[1]*sigma[2]*I2Spline->EvalDerivativeOnKnot(Is[1]) +  I3Spline->EvalOnKnot(Is[2]) * sigma[0] + I3Spline->EvalDerivativeOnKnot(Is[2]) * Is[2] * sigma[0];

        A(1,0) = A(0,1);
        A(2,0) = A(0,2);
        A(2,1) = A(1,2);
    }

    // we skip the influence of I2Spline for the simplicity
    std::shared_ptr<UniformCubicBasisSpline> I1Spline,I2Spline,I3Spline;
};