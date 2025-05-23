#include "Spatial_FSPH.h"

#include <thrust/count.h>
#include <thrust/execution_policy.h>
#include <numeric>

#include<iostream>

#undef max
#undef min
#define NOMINMAX

using namespace PBD;

Spatial_FSPH::Spatial_FSPH(const Real radius, const unsigned int numBoundry, const unsigned int numParticles)
{
	hipDeviceProp_t devProp;
	hipGetDeviceProperties(&devProp, 0);
	std::cout << "Name: " << devProp.name << std::endl;
	std::cout << "Total global memory: " << devProp.totalGlobalMem << std::endl;
	std::cout << "regsPerBlock: " << devProp.totalConstMem << std::endl;
	std::cout << "sharedMemPerBlock: " << devProp.sharedMemPerBlock << std::endl;
	std::cout << "regsPerBlock: " << devProp.regsPerBlock << std::endl;
	std::cout << "warpSize: " << devProp.warpSize << std::endl;
	std::cout << "maxThreadsPerBlock: " << devProp.maxThreadsPerBlock << std::endl;
	std::cout << "maxThreadsDim x: " << devProp.maxThreadsDim[0] << std::endl;
	std::cout << "maxThreadsDim y: " << devProp.maxThreadsDim[1] << std::endl;
	std::cout << "maxThreadsDim z: " << devProp.maxThreadsDim[2] << std::endl;
	std::cout << "maxGridSize x: " << devProp.maxGridSize[0] << std::endl;
	std::cout << "maxGridSize y: " << devProp.maxGridSize[1] << std::endl;
	std::cout << "maxGridSize z: " << devProp.maxGridSize[2] << std::endl;
	std::cout << "multiProcessorCount: " << devProp.multiProcessorCount << std::endl;
	std::cout << "canMapHostMemory: " << devProp.canMapHostMemory << std::endl;
	std::cout << "concurrentKernels: " << devProp.concurrentKernels << std::endl;
	//std::cout << "asyncEngineCount: " << devProp.asyncEngineCount << std::endl;
	std::cout << "maxThreadsPerMultiProcessor: " << devProp.maxThreadsPerMultiProcessor << std::endl;
	//std::cout << "sharedMemPerMultiprocessor: " << devProp.sharedMemPerMultiprocessor << std::endl;
	//std::cout << "regsPerMultiprocessor: " << devProp.regsPerMultiprocessor << std::endl;
	std::cout << "managedMemory: " << devProp.managedMemory << std::endl;
	//std::cout << "hostNativeAtomicSupported: " << devProp.hostNativeAtomicSupported << std::endl;
	std::cout << "pageableMemoryAccess: " << devProp.pageableMemoryAccess << std::endl;
	std::cout << "pageableMemoryAccessUsesHostPageTables: " << devProp.pageableMemoryAccessUsesHostPageTables << std::endl;
	//std::cout << "maxBlocksPerMultiProcessor: " << devProp.maxBlocksPerMultiProcessor << std::endl;
	std::cout << "maxSharedMemoryPerMultiProcessor: " << devProp.maxSharedMemoryPerMultiProcessor << std::endl;

	


	nump_ = numBoundry+numParticles;
	buff_capacity_ = nump_;
	device_buff_.allocate(buff_capacity_, sph::kBuffTypeDevice);
	host_buff_.allocate(buff_capacity_, sph::kBuffTypeHostPinned);
	device_buff_temp_.allocateSubBuffer(&device_buff_);

	// ## INITIALIZE SYSTEM
	sysPara.kernel = radius;
	sysPara.kernel_2 = sysPara.kernel * sysPara.kernel;

	sysPara.cell_size = sysPara.kernel;
	sysPara.grid_size = make_ushort3(
		(ushort)ceil((containerWidth + 0.01f) / sysPara.cell_size),
		(ushort)ceil((containerHeight + 0.01f) / sysPara.cell_size),
		(ushort)ceil((containerDepth + 0.01f) / sysPara.cell_size)
	);
#ifndef CPUCACHEOPT
	CUDA_SAFE_CALL(hipHostMalloc(&part2Idx, nump_ * sizeof(int)));// * 64)); //Increase allocation size to force hip to actually allocate. What the hell HIP?
	CUDA_SAFE_CALL(hipHostMalloc(&idx2Part, nump_ * sizeof(int)));// * 64)); //Increase allocation size to force hip to actually allocate. What the hell HIP?
#endif
	transSysParaToDevice(&sysPara);
	
	//BuffInit(device_buff_.get_buff_list(), nump_);
	std::cout << "Number of particles: " << nump_ << std::endl;

	arrangement_ = new sph::Arrangement(device_buff_, device_buff_temp_, nump_, buff_capacity_, sysPara.cell_size, sysPara.grid_size);
	printf("Arrangement success\n");
	printf("System initialization success\n");

	neigh = new Neighbours(nump_);
	printf("Created Neighbours\n");
}

Spatial_FSPH::~Spatial_FSPH()
{
#ifndef CPUCACHEOPT
	hipHostFree(part2Idx);
	hipHostFree(idx2Part);
	part2Idx = nullptr;
	idx2Part = nullptr;
#endif
	delete arrangement_;
	delete neigh;
	arrangement_ = nullptr;
	neigh = nullptr;
}

void Spatial_FSPH::cleanup()
{

}

unsigned int** Spatial_FSPH::getNeighbors() const
{
	
}

unsigned int Spatial_FSPH::getNumParticles() const
{
	
}

//void Spatial_FSPH::setRadius(const Real radius)
//{
//	
//}

//Real Spatial_FSPH::getRadius() const
//{
//	
//}

void Spatial_FSPH::update()
{
	m_currentTimestamp++;
}

void Spatial_FSPH::neighborhoodSearch(Vector3r* x)
{
	//TODO Add points from the test data
	void* tmp0 = &host_buff_.get_buff_list().position_d[0];
	//void* tmp1 = host_buff_.get_buff_list().final_position;
	std::copy(x[0].data(), x[0].data() + nump_ * 3, (float*)tmp0);
	//std::copy(x[0].data(), x[0].data() + nump_ * 3, (float*)tmp1);

	/*host_buff_.get_buff_list().position_d[nump_] = pos_d;
	host_buff_.get_buff_list().final_position[nump_] = position * 0.9;*/

	host_buff_.transfer(device_buff_, 0, nump_, hipMemcpyHostToDevice);
	//printf("Host buffer transfer success\n");

	
	float3 f0 = thrust::reduce(thrust::device, device_buff_.get_buff_list().position_d, device_buff_.get_buff_list().position_d + nump_);
	//printf("GPU sum: (%f,%f,%f)\n", f0.x, f0.y, f0.z);
	Vector3r f1 = std::accumulate(x, x + nump_, Vector3r(0.0f, 0.0f, 0.0f));
	//printf("CPU sum: (%f,%f,%f)\n", f1[0], f1[1], f1[2]);

	int* d_index = arrangement_->getDevCellIndex();
	int* offset_data = arrangement_->getDevOffsetData();
	int* cell_offset = arrangement_->getDevCellOffset();
	int* cell_offsetM = arrangement_->getDevCellOffsetM();
	int* cell_nump = arrangement_->getDevCellNumP();
	int middle = nump_;

	middle = arrangement_->arrangeHybridMode9M();
	//printf("Arranged HybridMode success\n");

	f0 = thrust::reduce(thrust::device, device_buff_.get_buff_list().position_d, device_buff_.get_buff_list().position_d + nump_);
	//printf("GPU sum: (%f,%f,%f)\n", f0.x, f0.y, f0.z);
	float3 f2 = std::accumulate(host_buff_.get_buff_list().position_d, host_buff_.get_buff_list().position_d + nump_, make_float3(0.0f, 0.0f, 0.0f));
	//printf("CPUHost sum: (%f,%f,%f)\n", f2.x, f2.y, f2.z);
	f1 = std::accumulate(x, x + nump_, Vector3r(0.0f, 0.0f, 0.0f));
	//printf("CPUx sum: (%f,%f,%f)\n", f1[0], f1[1], f1[2]);

	sph::ParticleIdxRange tra_range(0, middle);      // [0, middle)
	//printf("Gotten middle\n");

	sph::computeNeighbours(
		cell_offsetM,
		tra_range,
		device_buff_.get_buff_list(),
		d_index,
		cell_offset,
		cell_nump,
		arrangement_->getBlockTasks(),
		arrangement_->getNumBlockSMSMode(),
#ifdef CPUCACHEOPT 
		arrangement_->part2Idx,
		arrangement_->idx2Part,
#endif
		neigh
	);
	//printf("Computed neighbours\n");

	//CUDA_SAFE_CALL(hipMemcpy(host_buff_.get_buff_list().position_d, device_buff_.get_buff_list().position_d, nump_ * sizeof(float3), hipMemcpyDeviceToHost));
#ifndef CPUCACHEOPT
	CUDA_SAFE_CALL(hipMemcpy(part2Idx, arrangement_->part2Idx, nump_ * sizeof(int), hipMemcpyDeviceToHost));
	CUDA_SAFE_CALL(hipMemcpy(idx2Part, arrangement_->idx2Part, nump_ * sizeof(int), hipMemcpyDeviceToHost));
#endif
}

void Spatial_FSPH::neighborhoodSearch(Vector3r* x, const unsigned int numParticles, const unsigned int numBoundaryParticles, Vector3r* boundaryX)
{
	//TODO Add points from the test data
	void* tmp0 = &host_buff_.get_buff_list().position_d[0];
	//void* tmp1 = host_buff_.get_buff_list().final_position;
#ifdef TAKETIME
	START_TIMING("Unnecessary Copy");
#endif // TAKETIME
	std::copy(x[0].data(), x[0].data() + numParticles * 3, (float*)tmp0);
	std::copy(boundaryX[0].data(), boundaryX[0].data() + numBoundaryParticles * 3, (float*)tmp0 + numParticles * 3);
#ifdef TAKETIME
	STOP_TIMING_AVG;
#endif // TAKETIME	
	/*std::copy(x[0].data(), x[0].data() + nump_ * 3, (float*)tmp1);
	std::copy(boundaryX[0].data(), boundaryX[0].data() + numBoundaryParticles * 3, (float*)tmp1 + nump_ * 3);*/
#ifdef TAKETIME
	START_TIMING("Copy to GPU");
#endif // TAKETIME
	host_buff_.transfer(device_buff_, 0, nump_, hipMemcpyHostToDevice);
	//printf("Host buffer transfer success\n");
#ifdef TAKETIME
	STOP_TIMING_AVG;
#endif // TAKETIME	

	int* d_index = arrangement_->getDevCellIndex();
	int* offset_data = arrangement_->getDevOffsetData();
	int* cell_offset = arrangement_->getDevCellOffset();
	int* cell_offsetM = arrangement_->getDevCellOffsetM();
	int* cell_nump = arrangement_->getDevCellNumP();
	int middle = nump_;

#ifdef TAKETIME
	START_TIMING("Arrange data");
#endif // TAKETIME
	middle = arrangement_->arrangeHybridMode9M();
	//printf("Arranged HybridMode success\n");
#ifdef TAKETIME
	STOP_TIMING_AVG;
#endif // TAKETIME	

	//float3 f0 = thrust::reduce(thrust::device, device_buff_.get_buff_list().position_d, device_buff_.get_buff_list().position_d + nump_);
	////printf("GPU sum: (%f,%f,%f)\n", f0.x, f0.y, f0.z);
	//float3 f2 = std::accumulate(host_buff_.get_buff_list().position_d, host_buff_.get_buff_list().position_d + nump_, make_float3(0.0f, 0.0f, 0.0f));
	////printf("CPUHost sum: (%f,%f,%f)\n", f2.x, f2.y, f2.z);
	//Vector3r f1x = std::accumulate(x, x + numParticles, Vector3r(0.0f, 0.0f, 0.0f));
	//Vector3r f1 = std::accumulate(boundaryX, boundaryX + numBoundaryParticles, f1x);
	////printf("CPUx sum: (%f,%f,%f)\n", f1[0], f1[1], f1[2]);

	sph::ParticleIdxRange tra_range(0, middle);      // [0, middle)
	//printf("Gotten middle\n");

	sph::computeNeighbours(
		cell_offsetM,
		tra_range,
		device_buff_.get_buff_list(),
		d_index,
		cell_offset,
		cell_nump,
		arrangement_->getBlockTasks(),
		arrangement_->getNumBlockSMSMode(),
#ifdef CPUCACHEOPT 
		arrangement_->part2Idx,
		arrangement_->idx2Part,
#endif
		neigh
	);
	//printf("Computed neighbours\n");

#ifndef CPUCACHEOPT
#ifdef TAKETIME
	START_TIMING("Copy sorting indexes to CPU");
#endif // TAKETIME
	//CUDA_SAFE_CALL(hipMemcpy(host_buff_.get_buff_list().position_d, device_buff_.get_buff_list().position_d, nump_ * sizeof(float3), hipMemcpyDeviceToHost));
	CUDA_SAFE_CALL(hipMemcpy(part2Idx, arrangement_->part2Idx, nump_ * sizeof(int), hipMemcpyDeviceToHost));
	CUDA_SAFE_CALL(hipMemcpy(idx2Part, arrangement_->idx2Part, nump_ * sizeof(int), hipMemcpyDeviceToHost));
#ifdef TAKETIME
	STOP_TIMING_AVG;
#endif // TAKETIME	
#endif
}