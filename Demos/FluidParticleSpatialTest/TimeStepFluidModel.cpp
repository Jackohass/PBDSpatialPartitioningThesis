#include "TimeStepFluidModel.h"
#include "Simulation/TimeManager.h"
#include "PositionBasedDynamics/PositionBasedFluids.h"
#include "PositionBasedDynamics/TimeIntegration.h"
#include "PositionBasedDynamics/SPHKernels.h"
#include "Simulation/Simulation.h"
#include "Utils/Timing.h"

#include <set>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace PBD;
using namespace std;

TimeStepFluidModel::TimeStepFluidModel()
{

}

TimeStepFluidModel::~TimeStepFluidModel(void)
{

}

int numRuns = 0;
int neighborSum = 0;
void TimeStepFluidModel::step(FluidModel &model)
{
	//START_TIMING("simulation step");
	TimeManager *tm = TimeManager::getCurrent ();
	const Real h = tm->getTimeStepSize();
	ParticleData &pd = model.getParticles();

	clearAccelerations(model);

	// Update time step size by CFL condition
	updateTimeStepSizeCFL(model, static_cast<Real>(0.0001), static_cast<Real>(0.005));

	// Time integration
	for (unsigned int i = 0; i < pd.size(); i++)
	{ 
		model.getDeltaX(i).setZero();
		pd.getLastPosition(i) = pd.getOldPosition(i);
		pd.getOldPosition(i) = pd.getPosition(i);
		TimeIntegration::semiImplicitEuler(h, pd.getMass(i), pd.getPosition(i), pd.getVelocity(i), pd.getAcceleration(i));
	}

	// Perform neighborhood search
#if defined(TAKETIME) || defined(MINIMUMTIMING)
	START_TIMING("neighborhood search");
#endif // TAKETIME
#if defined(FSPH)
	model.getNeighborhoodSearch()->neighborhoodSearch(&model.getParticles().getPosition(0), model.getParticles().size(), model.numBoundaryParticles(), &model.getBoundaryX(0));
	//model.getNeighborhoodSearch()->neighborhoodSearch(&model.getParticles().getPosition(0), model.numBoundaryParticles(), &model.getBoundaryX(0));

#elif defined(nSearch)
	//const float* tmp = model.getParticles().getPosition(0).data();
	model.getNeighborhoodSearch()->neighborhoodSearch(&model.getParticles().getPosition(0), model.getParticles().size(), &model.getBoundaryX(0), model.numBoundaryParticles());

#else
	model.getNeighborhoodSearch()->neighborhoodSearch(&model.getParticles().getPosition(0), model.numBoundaryParticles(), &model.getBoundaryX(0));

#endif
#if defined(TAKETIME) || defined(MINIMUMTIMING)
	STOP_TIMING_AVG;
#endif // TAKETIME

	numRuns++;
	int sum = 0;
	for (int i = 0; i < model.getParticles().size(); i++)
	{
#if defined(FSPH)
		const int sortIdx = model.getNeighborhoodSearch()->partIdx(i);
#elif defined(nSearch)
		const int sortIdx = i;
#else
		const int sortIdx = i;
#endif
		sum += model.getNeighborhoodSearch()->n_neighbors(sortIdx);
	}
	neighborSum += sum;
	printf("Running Average: %f\n", (double)neighborSum / (double)numRuns);

	// Solve density constraint
#ifdef TAKETIME
	START_TIMING("constraint projection");
#endif // TAKETIME
	constraintProjection(model);
#ifdef TAKETIME
	STOP_TIMING_AVG;
#endif // TAKETIME

	// Update velocities	
	for (unsigned int i = 0; i < pd.size(); i++)
	{
		if (m_velocityUpdateMethod == 0)
			TimeIntegration::velocityUpdateFirstOrder(h, pd.getMass(i), pd.getPosition(i), pd.getOldPosition(i), pd.getVelocity(i));
		else
			TimeIntegration::velocityUpdateSecondOrder(h, pd.getMass(i), pd.getPosition(i), pd.getOldPosition(i), pd.getLastPosition(i), pd.getVelocity(i));
	}

	// Compute viscosity 
#ifdef TAKETIME
	START_TIMING("XSPH viscosity computation");
#endif // TAKETIME
	computeXSPHViscosity(model);
#ifdef TAKETIME
	STOP_TIMING_AVG;
#endif // TAKETIME

	// Compute new time	
	tm->setTime (tm->getTime () + h);
	model.getNeighborhoodSearch()->update();
	//STOP_TIMING_AVG;
}


/** Clear accelerations and add gravitation.
 */
void TimeStepFluidModel::clearAccelerations(FluidModel &model)
{
	ParticleData &pd = model.getParticles();
	const unsigned int count = pd.size();
	Simulation* sim = Simulation::getCurrent();
	const Vector3r grav(sim->getVecValue<Real>(Simulation::GRAVITATION));
	for (unsigned int i=0; i < count; i++)
	{
		// Clear accelerations of dynamic particles
		if (pd.getMass(i) != 0.0)
		{
			Vector3r &a = pd.getAcceleration(i);
			a = grav;
		}
	}
}

/** Update time step size by CFL condition.
*/
void TimeStepFluidModel::updateTimeStepSizeCFL(FluidModel &model, const Real minTimeStepSize, const Real maxTimeStepSize)
{
	const Real radius = model.getParticleRadius();
	const Real cflFactor = 1.0;
	Real h = TimeManager::getCurrent()->getTimeStepSize();

	// Approximate max. position change due to current velocities
	Real maxVel = static_cast<Real>(0.1);
	ParticleData &pd = model.getParticles();
	const unsigned int numParticles = pd.size();
	const Real diameter = static_cast<Real>(2.0)*radius;
	for (unsigned int i = 0; i < numParticles; i++)
	{
		const Vector3r &vel = pd.getVelocity(i);
		const Vector3r &accel = pd.getAcceleration(i);
		const Real velMag = (vel + accel*h).squaredNorm();
		if (velMag > maxVel)
			maxVel = velMag;
	}

	// Approximate max. time step size 		
	h = cflFactor * static_cast<Real>(0.4) * (diameter / (sqrt(maxVel)));

	h = min(h, maxTimeStepSize);
	h = max(h, minTimeStepSize);

	TimeManager::getCurrent()->setTimeStepSize(h);
}

/** Compute viscosity accelerations.
*/
void TimeStepFluidModel::computeXSPHViscosity(FluidModel &model)
{
	ParticleData &pd = model.getParticles();
	const unsigned int numParticles = pd.size();

	const Real viscosity = model.getViscosity();
	const Real h = TimeManager::getCurrent()->getTimeStepSize();

	#pragma omp parallel default(shared)
	{
		#pragma omp for schedule(static)
		for (int i = 0; i < (int)numParticles; i++)
		{
#if defined(FSPH)
			const int sortIdx = model.getNeighborhoodSearch()->partIdx(i);
#elif defined(nSearch)
			const int sortIdx = i;
#else
			const int sortIdx = i;
#endif
			const Vector3r& xi = pd.getPosition(i);
			Vector3r& vi = pd.getVelocity(i);
			const Real density_i = model.getDensity(i);

			const unsigned int numNeighbors = model.getNeighborhoodSearch()->n_neighbors(sortIdx);
			for (unsigned int j = 0; j < numNeighbors; j++)
			{
				//const unsigned int neighborIndex = model.getNeighborhoodSearch()->invNeighbor(sortIdx, j);
				const unsigned int neighborIndex = model.getNeighborhoodSearch()->neighbor(sortIdx, j);
				if (neighborIndex < numParticles)		// Test if fluid particle
				{
					// Viscosity
					const Vector3r& xj = pd.getPosition(neighborIndex);
					const Vector3r& vj = pd.getVelocity(neighborIndex);
					const Real density_j = model.getDensity(neighborIndex);
					vi -= viscosity * (pd.getMass(neighborIndex) / density_j) * (vi - vj) * CubicKernel::W(xi - xj);
				}
			}
		}
	}
}

void TimeStepFluidModel::reset()
{

}

/** Solve density constraint.
*/
void TimeStepFluidModel::constraintProjection(FluidModel &model)
{
	const unsigned int maxIter = 5;
	unsigned int iter = 0;

	ParticleData &pd = model.getParticles();
	const unsigned int nParticles = pd.size();
#if defined(FSPH)

#elif defined(nSearch)

#else
	unsigned int** neighbors = model.getNeighborhoodSearch()->getNeighbors();
	unsigned int* numNeighbors = model.getNeighborhoodSearch()->getNumNeighbors();
#endif

	while (iter < maxIter)
	{
		Real avg_density_err = 0.0;

		#pragma omp parallel default(shared)
		{
			#pragma omp for schedule(static)  
			for (int i = 0; i < (int)nParticles; i++)
			{
				Real density_err;
#if defined(FSPH) || defined(nSearch)
#if defined(FSPH)
				int sortIdx = model.getNeighborhoodSearch()->partIdx(i);
#else
				int sortIdx = i;
#endif
				Real& density = model.getDensity(i);
				const Real* mass = &pd.getMass(0);
				const Vector3r* boundaryX = &model.getBoundaryX(0);
				const Real* boundaryPsi = &model.getBoundaryPsi(0);
				const Vector3r* x = &pd.getPosition(0);
				const Real density0 = model.getDensity0();

				// Compute current density for particle i
				density = mass[i] * CubicKernel::W_zero();
				for (unsigned int j = 0; j < model.getNeighborhoodSearch()->n_neighbors(sortIdx); j++)
				{
					//const unsigned int neighborIndex = model.getNeighborhoodSearch()->invNeighbor(sortIdx, j);
					const unsigned int neighborIndex = model.getNeighborhoodSearch()->neighbor(sortIdx, j);
					if (neighborIndex < nParticles)		// Test if fluid particle
					{
						density += mass[neighborIndex] * CubicKernel::W(x[i] - x[neighborIndex]);
					}
					else
					{
						// Boundary: Akinci2012
						density += boundaryPsi[neighborIndex - nParticles] * CubicKernel::W(x[i] - boundaryX[neighborIndex - nParticles]);
					}
				}
				density_err = std::max(density, density0) - density0;

				/*----*/
				const Real constDensity = density;
				Real& lambda = model.getLambda(i);

				const Real eps = static_cast<Real>(1.0e-6);

				// Evaluate constraint function
				const Real C = std::max(constDensity / density0 - static_cast<Real>(1.0), static_cast<Real>(0.0));			// clamp to prevent particle clumping at surface

				if (C != 0.0)
				{
					// Compute gradients dC/dx_j 
					Real sum_grad_C2 = 0.0;
					Vector3r gradC_i(0.0, 0.0, 0.0);

					for (unsigned int j = 0; j < model.getNeighborhoodSearch()->n_neighbors(sortIdx); j++)
					{
						const unsigned int neighborIndex = model.getNeighborhoodSearch()->neighbor(sortIdx, j);
						if (neighborIndex < nParticles)		// Test if fluid particle
						{
							const Vector3r gradC_j = -mass[neighborIndex] / density0 * CubicKernel::gradW(x[i] - x[neighborIndex]);
							sum_grad_C2 += gradC_j.squaredNorm();
							gradC_i -= gradC_j;
						}
						else
						{
							// Boundary: Akinci2012
							const Vector3r gradC_j = -boundaryPsi[neighborIndex - nParticles] / density0 * CubicKernel::gradW(x[i] - boundaryX[neighborIndex - nParticles]);
							sum_grad_C2 += gradC_j.squaredNorm();
							gradC_i -= gradC_j;
						}
					}

					sum_grad_C2 += gradC_i.squaredNorm();

					// Compute lambda
					lambda = -C / (sum_grad_C2 + eps);
				}
				else
					lambda = 0.0;
#else
				PositionBasedFluids::computePBFDensity(i, nParticles, &pd.getPosition(0), &pd.getMass(0), &model.getBoundaryX(0), 
					&model.getBoundaryPsi(0), numNeighbors[i], neighbors[i], model.getDensity0(), true, density_err, model.getDensity(i));
				PositionBasedFluids::computePBFLagrangeMultiplier(i, nParticles, &pd.getPosition(0), &pd.getMass(0), &model.getBoundaryX(0), 
					&model.getBoundaryPsi(0), model.getDensity(i), numNeighbors[i], neighbors[i], model.getDensity0(), true, model.getLambda(i));
#endif

				
			}
		}
		
		#pragma omp parallel default(shared)
		{
			#pragma omp for schedule(static)  
			for (int i = 0; i < (int)nParticles; i++)
			{
				Vector3r corr;
#if defined(FSPH) || defined(nSearch)
#if defined(FSPH)
				int sortIdx = model.getNeighborhoodSearch()->partIdx(i);
#else
				int sortIdx = i;
#endif
				/*PositionBasedFluids::solveDensityConstraint(corrIdx, nParticles, &pd.getPosition(0), &pd.getMass(0), &model.getBoundaryX(0), &model.getBoundaryPsi(0),
					model.getNeighborhoodSearch()->n_neighbors(corrIdx), model.getNeighborhoodSearch()->neighbors(corrIdx), model.getDensity0(), true, &model.getLambda(0), corr);*/
				const Real* mass = &pd.getMass(0);
				const Vector3r* boundaryX = &model.getBoundaryX(0);
				const Real* boundaryPsi = &model.getBoundaryPsi(0);
				const Vector3r* x = &pd.getPosition(0);
				const Real density0 = model.getDensity0();
				const Real* lambda = &model.getLambda(0);

				// Compute position correction
				corr.setZero();
				for (unsigned int j = 0; j < model.getNeighborhoodSearch()->n_neighbors(sortIdx); j++)
				{
					//const unsigned int neighborIndex = model.getNeighborhoodSearch()->invNeighbor(sortIdx, j);
					const unsigned int neighborIndex = model.getNeighborhoodSearch()->neighbor(sortIdx, j);
					if (neighborIndex < nParticles)		// Test if fluid particle
					{
						const Vector3r gradC_j = -mass[neighborIndex] / density0 * CubicKernel::gradW(x[i] - x[neighborIndex]);
						corr -= (lambda[i] + lambda[neighborIndex]) * gradC_j;
					}
					else
					{
						// Boundary: Akinci2012
						const Vector3r gradC_j = -boundaryPsi[neighborIndex - nParticles] / density0 * CubicKernel::gradW(x[i] - boundaryX[neighborIndex - nParticles]);
						corr -= (lambda[i]) * gradC_j;
					}
				}

				

#else
				PositionBasedFluids::solveDensityConstraint(i, nParticles, &pd.getPosition(0), &pd.getMass(0), &model.getBoundaryX(0), &model.getBoundaryPsi(0), 
					numNeighbors[i], neighbors[i], model.getDensity0(), true, &model.getLambda(0), corr);
#endif
				model.getDeltaX(i) = corr;
				
			}
		}

		#pragma omp parallel default(shared)
		{
			#pragma omp for schedule(static)  
			for (int i = 0; i < (int)nParticles; i++)
			{
				pd.getPosition(i) += model.getDeltaX(i);
			}
		}

		iter++;
	}
}

