#pragma once

#include "Common/Common.h"

#include "Utils/Timing.h"

#include "sph_kernel.cuh.hip"
#include "sph_arrangement.cuh.hip"
#include "sph_particle.h.hip"

namespace PBD
{
	class Spatial_FSPH
	{
	public:
		Spatial_FSPH(const Real radius = 0.1, const unsigned int numBoundry = 0, const unsigned int numParticles = 0);
		~Spatial_FSPH();

		void cleanup();
		void neighborhoodSearch(Vector3r* x);
		void neighborhoodSearch(Vector3r* x, const unsigned int numParticles, const unsigned int numBoundaryParticles, Vector3r* boundaryX);
		void update();
		FORCE_INLINE unsigned int** getNeighbors() const;
		//const unsigned int getMaxNeighbors() const { return m_maxNeighbors; }

		FORCE_INLINE unsigned int getNumParticles() const;
		/*void setRadius(const Real radius);
		Real getRadius() const;*/

		FORCE_INLINE unsigned int n_neighbors(unsigned int i) const
		{
			return neigh->counts[i];
		}

		FORCE_INLINE int* getNumNeighbors() const
		{
			return neigh->counts;
		}
		/*unsigned int* neighbors(unsigned int i) const
		{
			return nSearch.point_set(particleIndex).neighbor_list(particleIndex, i);
		}*/
		/*FORCE_INLINE unsigned int neighbor(unsigned int i, unsigned int k) const
		{
			return neigh->neighbors[neigh->offsets[i] + k];
		}*/
#ifndef CPUCACHEOPT
		FORCE_INLINE unsigned int partIdx(unsigned int i) const
		{
			return idx2Part[i];
		}
		FORCE_INLINE unsigned int idxIdx(unsigned int i) const
		{
			return part2Idx[i];
		}
		FORCE_INLINE unsigned int neighbor(unsigned int i, unsigned int k) const
		{
			return idxIdx(neigh->neighbors[neigh->offsets[i] + k]);
		}
#endif
#ifdef CPUCACHEOPT
		FORCE_INLINE constexpr const unsigned int partIdx(const unsigned int i) const
		{
			return i;
		}
		FORCE_INLINE unsigned int neighbor(unsigned int i, unsigned int k) const
		{
			return neigh->neighbors[neigh->offsets[i] + k];
		}
#endif

	private:
		unsigned int m_currentTimestamp;

		Neighbours* neigh;

		std::size_t const N = 120;

#ifndef CPUCACHEOPT
		int* idx2Part;
		int* part2Idx;
#endif
		/*float const r_omega = static_cast<float>(0.15);
		float const r_omega2 = r_omega * r_omega;
		float const radius = static_cast<float>(2.0) * (static_cast<float>(2.0) * r_omega / static_cast<float>(N - 1));*/

		uint buff_capacity_ = 881968;
		uint nump_ = 0U;
		uint nBoundry = 0U;
		sph::ParticleBufferObject host_buff_;
		sph::ParticleBufferObject device_buff_;
		sph::ParticleBufferObject device_buff_temp_;

		sph::Arrangement* arrangement_;
		sph::SystemParameter sysPara;
	};
}

