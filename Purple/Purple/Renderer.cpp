#include "Renderer.h"
#include "Camera.h"
#include "Sampler.h"
#include "Random.h"
#include "Shape.h"
#include "Scene.h"
#include "Light.h"
#include "DifferentialGeometry.h"
#include "Integrator.h"
#include "MemoryArena.h"
#include "threadpool.h"

#define TilesPackageSize 16

namespace Purple {

SamplerRenderer::SamplerRenderer( Sampler* sampler, Camera* cam, SurfaceIntegrator* si )
{
	mMainSampler = sampler;
	mCamera = cam;
	mSurfaceIntegrator = si;
}

SamplerRenderer::~SamplerRenderer()
{

}

ColorRGB SamplerRenderer::Li( const Scene *scene, const RayDifferential &ray, const Sample *sample, Random& rng, MemoryArena &arena, DifferentialGeometry* isect /*= NULL*/, ColorRGB* T /*= NULL*/ ) const
{
	// Allocate local variables for _isect_ and _T_ if needed
	ColorRGB localT;
	if (!T) T = &localT;
	DifferentialGeometry localIsect;
	if (!isect) isect = &localIsect;
	ColorRGB Li = ColorRGB::Black;
	if (scene->Intersect(ray, isect))
	{
		Li = mSurfaceIntegrator->Li(scene, this, ray, *isect, sample,rng, arena);
	}
	else 
	{
		// Handle ray that doesn't intersect any geometry
		for (uint32_t i = 0; i < scene->Lights.size(); ++i)
			Li += scene->Lights[i]->Le(ray);
	}
	
	return *T * Li;
}

ColorRGB SamplerRenderer::Transmittance( const Scene *scene, const RayDifferential &ray, const Sample *sample, Random &rng, MemoryArena &arena ) const
{
	return ColorRGB::Black;
}


void SamplerRenderer::Render( const Scene *scene )
{
	//// Compute number of _SamplerRendererTask_s to create for rendering
	int nPixels = mCamera->Width * mCamera->Height;

	/*int nTasks = (std::max)(int(32 * GetNumWorkThreads()), nPixels / (128*128));*/
	int nTasks = nPixels / (128*128);
		
	// Allocate and initialize _sample_
	Sample* sample = new Sample(mMainSampler, mSurfaceIntegrator, scene);

	std::atomic<int> workingPackage = 0;
	pool& tp = GlobalThreadPool();
	for (size_t iCore = 0; iCore < tp.size(); ++iCore)
	{
		//tp.schedule(std::bind(&SamplerRenderer::TileRender, this, scene, sample, std::ref(workingPackage), nTasks));

		std::bind(&SamplerRenderer::TileRender, this, scene, sample, std::ref(workingPackage), nTasks)();
	}
	tp.wait();

	delete sample;
}

void SamplerRenderer::TileRender( const Scene* scene, const Sample* sample, std::atomic<int32_t>& workingPackage, int32_t numTiles )
{
	int32_t numPackages = (numTiles + TilesPackageSize - 1) / TilesPackageSize;
	int32_t localWorkingPackage = workingPackage ++;

	while (localWorkingPackage < numPackages)
	{
		const int32_t start = localWorkingPackage * TilesPackageSize;
		const int32_t end = (std::min)(numTiles, start + TilesPackageSize);

		for (int32_t iTile = start; iTile < end; ++iTile)
		{
			// Declare local variables used for rendering loop
			MemoryArena arena;
			Random rng(iTile);

			Sampler* sampler = mMainSampler->GetSubSampler(iTile, numTiles);

			// Allocate space for samples and intersections
			int maxSamples = sampler->GetSampleCount();

			Sample *samples = sample->Duplicate(maxSamples);

			RayDifferential *rays = new RayDifferential[maxSamples];
			ColorRGB *Ls = new ColorRGB[maxSamples];
			ColorRGB *Ts = new ColorRGB[maxSamples];
			DifferentialGeometry* isects = new DifferentialGeometry[maxSamples];

			// Get samples from _Sampler_ and update image
			int sampleCount;
			while ((sampleCount = sampler->GetMoreSamples(samples, rng)) > 0)
			{
				// Generate camera rays and compute radiance along rays
				for (int i = 0; i < sampleCount; ++i)
				{
					float rayWeight = mCamera->GenerateRayDifferential(samples[i].ImageSample, samples[i].LensSample, &rays[i]);
					rays[i].ScaleDifferentials(1.f / sqrtf(float(sampler->SamplesPerPixel)));

					// Evaluate radiance along camera ray
					if (rayWeight > 0.f)
					{
						Ls[i] = rayWeight * Li(scene, rays[i], &samples[i], rng, arena, &isects[i], &Ts[i]);
					}
					else
					{
						Ls[i] = ColorRGB::Black;
						Ts[i] = ColorRGB::White;
					}

				}

				// Free _MemoryArena_ memory from computing image sample values
				arena.FreeAll();
			}

			delete sampler;
			delete [] samples;
			delete [] rays;
			delete [] Ls;
			delete [] Ts;
			delete [] isects;

		}

		localWorkingPackage = workingPackage++;
	}
}



}