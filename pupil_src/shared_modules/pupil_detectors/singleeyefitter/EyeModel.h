
#ifndef EYEMODEL_H__
#define EYEMODEL_H__

#include <ceres/ceres.h>
#include <ceres/problem.h>
#include <ceres/autodiff_cost_function.h>
#include <ceres/solver.h>
#include <ceres/jet.h>

#include "common/types.h"
#include "mathHelper.h"
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <list>
#include <atomic>

namespace singleeyefitter {


    /*
        Observation class

        Hold data which is precalculated for every new observation
        Every observation is shared between different models

    */
    class Observation {
        std::shared_ptr<const Detector2DResult> mObservation2D;
        std::pair<Circle,Circle> mUnprojectedCirclePair;
        Line mProjectedCircleGaze;


    public:
        Observation(std::shared_ptr<const Detector2DResult> observation, double focalLength) :
            mObservation2D(observation)
        {
                const double circleRadius = 1.0;
                // Do a per-image unprojection of the pupil ellipse into the two fixed
                // sized circles that would project onto it. The size of the circles
                // doesn't matter here, only their center and normal does.
                mUnprojectedCirclePair = unproject(mObservation2D->ellipse, circleRadius , focalLength);
                 // Get projected circles and gaze vectors
                //
                // Project the circle centers and gaze vectors down back onto the image
                // plane. We're only using them as line parametrisations, so it doesn't
                // matter which of the two centers/gaze vectors we use, as the
                // two gazes are parallel and the centers are co-linear.
                const auto& c = mUnprojectedCirclePair.first.center;
                const auto& v = mUnprojectedCirclePair.first.normal;
                Vector2 cProj = project(c, focalLength);
                Vector2 vProj = project(v + c, focalLength) - cProj;
                vProj.normalize();
                mProjectedCircleGaze = Line(cProj, vProj);

        }
        Observation( const Observation& that ) = delete; // forbid copying
        Observation( Observation&& that ) = delete; // forbid moving
        Observation() = delete; // forbid default construction
        const std::shared_ptr<const Detector2DResult> getObservation2D() const { return mObservation2D;};
        const std::pair<Circle,Circle>& getUnprojectedCirclePair() const { return mUnprojectedCirclePair; };
        const Line& getProjectedCircleGaze() const { return mProjectedCircleGaze; };

    };

    typedef std::shared_ptr<const Observation> ObservationPtr;


class EyeModel {

        typedef singleeyefitter::Sphere<double> Sphere;

    public:

        // CONSTRUCTORS
        EyeModel( int modelId, double timestamp,  double focalLength, Vector3 cameraCenter, int initialUncheckedPupils = 5, double binResolution = 0.05  );
        EyeModel(const EyeModel&) = delete;
        //EyeModel(EyeModel&&); // we need a explicit 1/Move constructor because of the mutex
        ~EyeModel();

        // DOOR TO THE OUTER WORLD
        std::pair<Circle, double> presentObservation(const ObservationPtr observation, double averageFramerate  );

        // FOR CONTROLLED FITTING
        int addObservation(const ObservationPtr);
        Detector3DResultRefraction run_optimization();
        Circle predictSingleObservation(const ObservationPtr);
        void setSphereCenter(std::vector<double>);
        void setFitHyperParameters(int);

        // GETTER
        Sphere getSphere() const;
        Sphere getInitialSphere() const;

        // Describing how good different properties of the Eye are
        double getMaturity() const ; // How much spatial variance there is
        double getConfidence() const; // How much do we believe in this model
        double getPerformance() const; // How much do we believe in this model
        double getPerformanceGradient() const;
        double getSolverFit() const ; // The residual of the sphere calculation
        std::vector<double> getOptimizedParameters() const;
        std::vector<double> getCostPerPupil() const;
        int getNumResidualBlocks() const;
        Detector3DResultRefraction getRefractionResult() const;

        int getModelID() const { return mModelID; };
        double getBirthTimestamp() const { return mBirthTimestamp; };

        // ----- Visualization --------
        std::vector<Vector3> getBinPositions() const {return mBinPositions;};
        // ----- Visualization END --------

    private:

        Detector3DResultRefraction mResult;

        struct PupilParams {

            double theta, psi, radius;
            PupilParams() : theta(0), psi(0), radius(0) {};
            PupilParams(double theta, double psi, double radius) : theta(theta), psi(psi), radius(radius){};

        };

        struct Pupil {

            Circle mCircle;
            PupilParams mParams;
            const ObservationPtr mObservationPtr;
            //Pupil( const ObservationPtr observationPtr ) : mObservationPtr( observationPtr ){};
            Pupil( const ObservationPtr observationPtr) : mObservationPtr( observationPtr ){
                mParams = PupilParams(0,0,0);
                optimizedParams[0] = 0;
                optimizedParams[1] = 0;
                optimizedParams[2] = 0;
            };
            Pupil( const ObservationPtr observationPtr, PupilParams params ) : mObservationPtr( observationPtr ){
                mParams = PupilParams(params.theta, params.psi, params.radius);
                optimizedParams[0] = params.theta;
                optimizedParams[1] = params.psi;
                optimizedParams[2] = params.radius;
            };
            ceres::ResidualBlockId mResidualBlockId;
            double * const optimizedParams = static_cast<double * const>(malloc(3*sizeof(double)));
            int ceres_toggle = 0;

        };

        Sphere findSphereCenter( bool use_ransac = true);
        Sphere initialiseModel();
        double refineWithEdges( Sphere& sphere  );
        bool tryTransferNewObservations();


        ConfidenceValue calculateModelOberservationFit(const Circle&  unprojectedCircle, const Circle& initialisedCircle, double confidence) const;
        void updatePerformance( const ConfidenceValue& observation_fit,  double averageFramerate);

        double calculateModelFit(const Circle&  unprojectedCircle, const Circle& optimizedCircle) const;
        bool isSpatialRelevant(const Circle& circle);

        const Circle& selectUnprojectedCircle(const Sphere& sphere, const std::pair<const Circle, const Circle>& circles) const;
        void initialiseSingleObservation( const Sphere& sphere, Pupil& pupil) const;
        Circle getIntersectedCircle( const Sphere& sphere, const Circle& circle) const;
        Circle getInitialCircle( const Sphere& sphere, const Circle& circle ) const;
        std::pair<PupilParams, double> getRefractedCircle( const Sphere& sphere, const Circle& unrefracted_circle, const ObservationPtr observation ) const;

        //Circle circleFromParams( CircleParams& params) const;
        Circle circleFromParams(const Sphere& eye,const  PupilParams& params) const;

        std::unordered_map<Vector2, bool, math::matrix_hash<Vector2>> mSpatialBins;
        std::vector<Vector3> mBinPositions; // for visualization

        mutable std::mutex mModelMutex;
        std::mutex mPupilMutex;
        mutable std::mutex mRefractionMutex;
        std::thread mWorker;
        Clock::time_point mLastModelRefinementTime;

        // Factors which describe how good certain properties of the model are
        //std::list<double> mModelSupports; // values to calculate the average
        double mSolverFit; // Residual of Ceres sovler , Thread sensitive
        math::WMA<double> mPerformance; // moving Average of model support
        float mPerformanceWindowSize;  // in seconds
        double mPerformanceGradient;
        Clock::time_point mLastPerformanceCalculationTime; // for the gradient calculation when need keep track of the time

        const double mFocalLength;
        const Vector3 mCameraCenter;
        const int mInitialUncheckedPupils;
        const double mBinResolution;
        const int mTotalBins;
        const int mModelID;
        double mBirthTimestamp;

        double mEdgeNumber; //Number of edges per pupil used during optimization
        double mStrikes;
        double mCenterWeightInitial;
        double mCenterWeightFinal;
        int mIterationNumbers[5];
        double mResidualsAveragedFraction;
        double mOutlierFactor;
        int mStartRemoveNumber;
        double mCauchyLossScale;


        double mEyeballRadius;
        double mCorneaRadius;
        double mIrisRadius;
        double mInitialCorneaRadius;
        double mInitialIrisRadius;

        std::vector<double> mCostPerBlock;  // Here we store after each optimization the current cost per residual block
        double * const eye_params = static_cast<double * const>(malloc(5*sizeof(double)));
        std::vector<double> mOptimizedParams;
        void removePupilFromOptimization(std::vector<Pupil>::iterator iter);


        Sphere mSphere;                       // Thread sensitive
        Sphere mInitialSphere;                // Thread sensitive
        std::vector<Pupil> mSupportingPupils; // just used within the worker thread, Thread sensitive
        int mSupportingPupilSize;             // Thread sensitive, use this to get the SupportedPupil size

        // observations are saved here and only if needed transferred to mSupportingPupils
        // since mSupportingPupils needs a mutex
        std::vector<Pupil> mSupportingPupilsToAdd;
};


} // singleeyefitter
#endif /* end of include guard: EYEMODEL_H__ */
