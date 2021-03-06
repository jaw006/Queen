#ifndef Integrator_h__
#define Integrator_h__

#include "Prerequisites.h"
#include "Ray.h"
#include "Light.h"
#include "Reflection.h"

namespace Purple {

enum LightStrategy 
{ 
	LS_Sample_All_Uniform,
	LS_Sample_One_Uniform 
};

ColorRGB SpecularReflect(const RayDifferential& ray, BSDF* bsdf, Random& rng, const DifferentialGeometry& isect,
						 const Renderer* renderer, const Scene* scene, const Sample *sample, MemoryArena &arena);

ColorRGB SpecularTransmit(const RayDifferential& ray, BSDF* bsdf, Random& rng, const DifferentialGeometry& isect,
						  const Renderer* renderer, const Scene* scene, const Sample *sample, MemoryArena &arena);


ColorRGB EstimateDirect(const Scene* scene, const Renderer* renderer, MemoryArena& arena, const Light* light, const float3& p,
						const float3& n, const float3& wo, float time, const BSDF* bsdf, Random& rng, const LightSample& lightSample,
						const BSDFSample& bsdfSample, uint32_t bsdfflags);

ColorRGB UniformSampleAllLights(const Scene* scene, const Renderer* renderer, MemoryArena& arena, const float3& p,
								const float3& n, const float3& wo, float time, BSDF *bsdf, const Sample* sample, Random &rng,
								const LightSampleOffsets *lightSampleOffset = NULL, const BSDFSampleOffsets *bsdfSampleOffset = NULL);

ColorRGB UniformSampleOneLight(const Scene* scene, const Renderer* renderer, MemoryArena& arena, const float3& p, const float3& n,
							   const float3& wo, float time, BSDF *bsdf, const Sample* sample, Random &rng, int lightNumOffset = -1,
							   const LightSampleOffsets *lightSampleOffset = NULL, const BSDFSampleOffsets *bsdfSampleOffset = NULL);

class Integrator
{
public:
	virtual ~Integrator(void) {}
	
	/**
	 * @brief Use this method to do some initialization after scene is loaded.
	 */
	virtual void Preprocess(const Random& rng, const Scene& scene) { }

	/**
	 * @brief Integrator may need samples to integral light or BRDF, use this method to init samples
	 */
	virtual void RequestSamples(Sampler* sampler, Sample* sample, const Scene* scene) {}

	virtual std::string GetIntegratorName() const = 0;
};

class SurfaceIntegrator : public Integrator
{
public:
	virtual ~SurfaceIntegrator(void) { }

	virtual ColorRGB Li(const Scene* scene, const Renderer* renderer, const RayDifferential& ray, const DifferentialGeometry& isect,
			const Sample* sample, Random& rng, MemoryArena& arena) const = 0;
};


class WhittedIntegrator : public SurfaceIntegrator 
{
public:
	WhittedIntegrator(int32_t md = 5) : mMaxDepth(md) {}

	ColorRGB Li(const Scene* scene, const Renderer* renderer, const RayDifferential& ray, const DifferentialGeometry& isect,
		const Sample* sample, Random& rng, MemoryArena& arena) const;

	std::string GetIntegratorName() const { return std::string("Whitted"); }

private:
	int32_t mMaxDepth;
};

class DirectLightingIntegrator : public SurfaceIntegrator
{
public:
	DirectLightingIntegrator(LightStrategy ls = LS_Sample_All_Uniform, int32_t md = 5);
	~DirectLightingIntegrator();

	ColorRGB Li(const Scene* scene, const Renderer* renderer, const RayDifferential& ray, const DifferentialGeometry& isect,
		const Sample* sample, Random& rng, MemoryArena& arena) const;

	void RequestSamples(Sampler* sampler, Sample* sample, const Scene* scene);

	std::string GetIntegratorName() const { return std::string("Direct Lighting"); }

private:
	LightSampleOffsets* mLightSampleOffsets;
	BSDFSampleOffsets* mBSDFSampleOffsets;
	uint32_t mLightNumOffset;

	LightStrategy mLightStrategy;
	int32_t mMaxDepth;
};

class PathIntegrator : public SurfaceIntegrator
{
public:
	PathIntegrator(int32_t md = 5);
	~PathIntegrator();

	ColorRGB Li(const Scene* scene, const Renderer* renderer, const RayDifferential& ray, const DifferentialGeometry& isect,
		const Sample* sample, Random& rng, MemoryArena& arena) const;

	void RequestSamples(Sampler* sampler, Sample* sample, const Scene* scene);

	std::string GetIntegratorName() const { return std::string("Path Tracing"); }

private:

    const static int SAMPLE_DEPTH = 3;

	uint32_t mLightNumOffset[SAMPLE_DEPTH];
	LightSampleOffsets mLightSampleOffsets[SAMPLE_DEPTH];
	BSDFSampleOffsets mBSDFSampleOffsets[SAMPLE_DEPTH];
	BSDFSampleOffsets mPathSampleOffsets[SAMPLE_DEPTH];
	
	int32_t mMaxDepth;
};



}




#endif // Integrator_h__

