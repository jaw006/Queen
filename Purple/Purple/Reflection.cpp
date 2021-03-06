#include "Reflection.h"
#include "MentoCarlo.h"
#include "Sampler.h"
#include <MathUtil.hpp>

namespace {

using namespace RxLib;
using namespace Purple;


// BxDF Utility Functions
ColorRGB FrDiel(float cosi, float cost, const ColorRGB &etai, const ColorRGB &etat) 
{
	ColorRGB Rparl = ((etat * cosi) - (etai * cost)) /
			((etat * cosi) + (etai * cost));
	ColorRGB Rperp = ((etai * cosi) - (etat * cost)) /
			((etai * cosi) + (etat * cost));
	return (Rparl*Rparl + Rperp*Rperp) / 2.f;
}


ColorRGB FrCond(float cosi, const ColorRGB &eta, const ColorRGB &k) 
{
	ColorRGB tmp = (eta*eta + k*k) * cosi*cosi;
	ColorRGB Rparl2 = (tmp - (2.f * eta * cosi) + ColorRGB(1.0f)) /
		(tmp + (2.f * eta * cosi) + ColorRGB(1.0f));
	ColorRGB tmp_f = eta*eta + k*k;
	ColorRGB Rperp2 =
		(tmp_f - (2.f * eta * cosi) + ColorRGB(cosi*cosi)) /
		(tmp_f + (2.f * eta * cosi) + ColorRGB(cosi*cosi));
	return (Rparl2 + Rperp2) / 2.f;
}


}

namespace Purple {

using namespace RxLib;

BSDFSampleOffsets::BSDFSampleOffsets( int count, Sample* sample )
	: nSamples(count)
{
	componentOffset = sample->Add1D(nSamples);
	dirOffset = sample->Add2D(nSamples);
}


BSDFSample::BSDFSample( const  Sample* sample, const BSDFSampleOffsets& offset, uint32_t n )
{
	uDir[0] = sample->TwoD[offset.dirOffset][2*n];
	uDir[1] = sample->TwoD[offset.dirOffset][2*n+1];
	uComponent = sample->OneD[offset.componentOffset][n];
}

ColorRGB BxDF::Sample( const float3& wo, float3* wi, float u1, float u2, float* pdf ) const
{
	*wi = CosineSampleHemisphere(u1, u2);
	if (wo.Z() < 0.) wi->Z() *= -1.f;
	*pdf = Pdf(wo, *wi);
	return Eval(wo, *wi);
}

//----------------------------------------------------------------------------------------
ColorRGB FresnelConductor::Evaluate( float cosi ) const
{
	 return FrCond(fabsf(cosi), eta, k);
}

ColorRGB FresnelDielectric::Evaluate( float cosi ) const
{
	// Compute Fresnel reflectance for dielectric
	cosi = Clamp(cosi, -1.0f, 1.0f);

	// Compute indices of refraction for dielectric
	bool entering = cosi > 0.;
	float ei = eta_i, et = eta_t;
	if (!entering)
		std::swap(ei, et);

	// Compute _sint_ using Snell's law
	float sint = ei/et * sqrtf(std::max(0.f, 1.f - cosi*cosi));
	if (sint >= 1.0f) 
	{
		// Handle total internal reflection
		return ColorRGB(1.0f);
	}
	else 
	{
		float cost = sqrtf(std::max(0.f, 1.f - sint*sint));
		return FrDiel(fabsf(cosi), cost, ei, et);
	}
}

//--------------------------------------------------------------------------------------------
ColorRGB BxDF::Rho( const float3& wo, int32_t numSamples, const float* samples ) const
{
	ColorRGB r(0.0f, 0.0f, 0.0f);
	for (int32_t i = 0; i < numSamples; ++i )
	{
		float3 wi; 
		float pdf = 0.0f;
		ColorRGB f = Sample(wo, &wi, samples[2 * i], samples[2 * i + 1], &pdf);
		
		if (pdf > 0.0f)
		{
			r += AbsCosTheta(wi) * f / pdf;
		}
	}
	return r / float(numSamples);
}

ColorRGB BxDF::Rho( int32_t numSamples, const float* samples1, const float* samples2 ) const
{
	ColorRGB r(0.0f, 0.0f, 0.0f);
	for (int32_t i = 0; i < numSamples; ++i)
	{
		float3 wo = UniformSampleHemisphere(samples1[2 * i], samples2[2 * i + 1]);

		float pdf_i, pdf_o = UniformHemispherePdf();
		float3 wi; 
		ColorRGB f = Sample(wo, &wi, samples2[2 * i], samples2[2 * i + 1], &pdf_i);
		if (pdf_i > 0.0f)
			r += f * AbsCosTheta(wi) * AbsCosTheta(wo) / (pdf_i * pdf_o);
	}

	return r / (Mathf::PI*numSamples);
}

float BxDF::Pdf( const float3& wo, const float3& wi ) const
{
	return SameHemisphere(wo, wi) ? CosineHemispherePdf(AbsCosTheta(wi)) : 0.f;
	//return CosineHemispherePdf(AbsCosTheta(wi));
}

ColorRGB OrenNayar::Eval( const float3& wo, const float3& wi ) const
{
	float sinthetai = SinTheta(wi);
	float sinthetao = SinTheta(wo);

	float maxcos = 0.0f;
	if (sinthetai > 1e-4 && sinthetao > 1e-4) 
	{
		float sinphii = SinPhi(wi), cosphii = CosPhi(wi);
		float sinphio = SinPhi(wo), cosphio = CosPhi(wo);
		float dcos = cosphii * cosphio + sinphii * sinphio;
		maxcos = (std::max)(0.0f, dcos);
	}

	float sinalpha, tanbeta;
	if (AbsCosTheta(wi) > AbsCosTheta(wo))
	{
		sinalpha = sinthetao;
		tanbeta = sinthetai / AbsCosTheta(wi);
	}
	else
	{
		sinalpha = sinthetai;
		tanbeta = sinthetao / AbsCosTheta(wo);
	}

	return mR * Mathf::INV_PI * (A + B * maxcos * sinalpha * tanbeta);
}


float TorranceSparrow::G( const float3& wo, const float3& wi, const float3& wh ) const
{
	float NdotWh = AbsCosTheta(wh);
	float NdotWo = AbsCosTheta(wo);
	float NdotWi = AbsCosTheta(wi);
	float OdotWh = fabsf(Dot(wo, wh));
	
	return std::min(1.0f, std::min(2.0f * NdotWh * NdotWo / OdotWh, 2.0f * NdotWh * NdotWi / OdotWh));
}

ColorRGB TorranceSparrow::Eval( const float3& wo, const float3& wi ) const
{
	float cosThetaO = AbsCosTheta(wo);
	float cosThetaI = AbsCosTheta(wi);

	if (cosThetaO == 0.0f || cosThetaI == 0.0f)
		return ColorRGB(0.0f, 0.0f, 0.0f);

	float3 wh = Normalize(wo + wi);

	// Compute distribution term
	float cosThetaH = Dot(wi, wh);

	return mR* mFresnel->Evaluate(cosThetaH) * mD->D(wh) * G(wo, wi, wh) / 
		          (4.0f * cosThetaI * cosThetaO);
}

ColorRGB TorranceSparrow::Sample( const float3& wo, float3* wi, float u1, float u2, float* pdf ) const
{
	mD->Sample_f(wo, wi, u1, u2, pdf);
	
	if (!SameHemisphere(wo, *wi)) 
		return ColorRGB(0.0f, 0.0f, 0.0f);

	return Eval(wo, *wi);
}

float TorranceSparrow::Pdf( const float3& wo, const float3& wi ) const
{
	if (!SameHemisphere(wo, wi))
		return 0.0f;

	return mD->Pdf(wo, wi);
}

void Blinn::Sample_f( const float3& wo, float3* wi, float u1, float u2, float* pdf ) const
{
	float cosTheta = powf(u1, 1.0f / (mExponent + 1.0f));
	float sinTheta = sqrtf(std::max(0.0f, 1- cosTheta * cosTheta));
	float phi = Mathf::TWO_PI * u2;

	float3 wh = SphericalDirection(cosTheta, sinTheta, phi);
	if (!SameHemisphere(wh, wo))
		wh = -wh;

	*wi = 2.0f * Dot(wo, wh) * wh - wo;

	float blinPdf = (mExponent + 1.0f) * powf(AbsCosTheta(wh), mExponent) / 
		(Mathf::TWO_PI * 4.0f * Dot(wo, wh));

	if (Dot(wo, wh) <= 0.f) 
		*pdf = 0.f;

	*pdf = blinPdf;
}

float Blinn::Pdf( const float3& wo, const float3& wi ) const
{
	float3 wh = Normalize(wo + wi);

	if (Dot(wo, wh) <= 0.f) 
		return 0.0f;

	return (mExponent + 1.0f) * powf(AbsCosTheta(wh), mExponent) / 
		(Mathf::TWO_PI * 4.0f * Dot(wo, wh));
}


BSDF::BSDF( const DifferentialGeometry &dgs, const float3& ngeom, float e /*= 1.0f*/ )
	: dgShading(dgs), mGeoNormal(ngeom), eta(e)
{
	mNormal = dgShading.Normal;

	mTangent = Normalize(dgShading.dpdu);
	mBitangent = Cross(mNormal, mTangent);

	//mBitangent = Normalize(dgShading.dpdu);
	//mTangent = Cross(mBitangent, mNormal);

	//mBitangent = Normalize(dgShading.dpdu);
	//mTangent = Cross(mNormal, mBitangent);
	mNumBxDFs = 0;
}

ColorRGB BSDF::Eval( const float3& woW, const float3& wiW, BSDFType flags /*= BSDF_All*/ ) const
{
	float3 wi = WorldToLocal(wiW), wo = WorldToLocal(woW);

	if (Dot(wiW, mGeoNormal) * Dot(woW, mGeoNormal) > 0) // ignore BTDFs
		flags = BSDFType(flags & ~BSDF_Transmission);
	else                                                 // ignore BRDFs
		flags = BSDFType(flags & ~BSDF_Reflection);

	ColorRGB retVal = ColorRGB::Black;
	
	for (int i = 0; i < mNumBxDFs; ++i)
	{
		if (mBxDFs[i]->MatchFlags(flags))
			retVal += mBxDFs[i]->Eval(wo, wi);
	}

	return retVal;
}

ColorRGB BSDF::Sample( const float3& woW, float3* wiW, const BSDFSample& bsdfSample, float *pdf, uint32_t bsdfFlags /*= BSDF_All*/, uint32_t* sampledType /*= NULL*/ ) const
{
	ColorRGB retVal;

	int matchingComps = NumComponents(bsdfFlags);

	if (matchingComps == 0)
	{
		*pdf = 0.0f;
		if(sampledType) *sampledType = (0);
		return ColorRGB::Black;
	}

	int which = std::min(Floor2Int(matchingComps * bsdfSample.uComponent), matchingComps-1);

	BxDF* bxdf = NULL;

	for (int i = 0; i < mNumBxDFs; ++i)
	{
		if (mBxDFs[i]->MatchFlags(bsdfFlags) && which-- == 0) 
		{
			bxdf = mBxDFs[i];
			break;
		}
	}
	assert(bxdf);

	float3 wo = WorldToLocal(woW);
	float3 wi;
	*pdf = 0.f;
	retVal = bxdf->Sample(wo, &wi, bsdfSample.uDir[0], bsdfSample.uDir[1], pdf);
	
	if (*pdf == 0.f)
	{
		if(sampledType) *sampledType = BSDFType(0);
		return ColorRGB::Black;
	}

	if (sampledType) *sampledType = bxdf->BxDFType;
	*wiW = LocalToWorld(wi);


	if ( !(bsdfFlags & BSDF_Specular) && matchingComps > 1)
	{
		for (int i = 0; i < mNumBxDFs; ++i)
		{
			if (mBxDFs[i] != bxdf && mBxDFs[i]->MatchFlags(bsdfFlags)) 
				*pdf += mBxDFs[i]->Pdf(wo, wi);
		}
	}

	if (matchingComps > 1)
		*pdf /= float(matchingComps);


	if ( !(bsdfFlags & BSDF_Specular) )
	{
		retVal = ColorRGB::Black;
		if (Dot(*wiW, mGeoNormal) * Dot(woW, mGeoNormal) > 0) // ignore BTDFs
			bsdfFlags = (bsdfFlags & ~BSDF_Transmission);
		else                                                  // ignore BRDFs
			bsdfFlags = (bsdfFlags & ~BSDF_Reflection);

		for (int i = 0; i < mNumBxDFs; ++i)
		{
			if (mBxDFs[i]->MatchFlags(bsdfFlags))
				retVal += mBxDFs[i]->Eval(wo, wi);
		}

	}

	return retVal;
}

float BSDF::Pdf( const float3& woW, const float3& wiW, BSDFType flags /*= BSDF_All*/ ) const
{
	float3 wi = WorldToLocal(wiW), wo = WorldToLocal(woW);
	float pdf = 0.f;
	int matchingComps = 0;
	for (int i = 0; i < mNumBxDFs; ++i)
	{
		if (mBxDFs[i]->MatchFlags(flags)) 
		{
			++matchingComps;
			pdf += mBxDFs[i]->Pdf(wo, wi);
		}
	}
	return matchingComps > 0 ? pdf / matchingComps : 0.0f;
}

ColorRGB BSDF::Rho( Random& rng, BSDFType flags /*= BSDF_All*/, int sqrtSamples /*= 6*/ ) const
{
	int nSamples = sqrtSamples * sqrtSamples;
	float *s1 = (float*)alloca(2 * nSamples);
	StratifiedSample2D(s1, sqrtSamples, sqrtSamples, rng);
	float *s2 = (float*)alloca(2 * nSamples);
	StratifiedSample2D(s2, sqrtSamples, sqrtSamples, rng);

	ColorRGB retVal = ColorRGB::Black;
	for (int i = 0; i < mNumBxDFs; ++i)
	{
		if (mBxDFs[i]->MatchFlags(flags)) 
		{
			retVal += mBxDFs[i]->Rho(nSamples, s1, s2);
		}
	}
	
	return retVal;
}

ColorRGB BSDF::Rho( const float3& wo, Random& rng, BSDFType flags /*= BSDF_All*/, int sqrtSamples /*= 6*/ ) const
{
	int nSamples = sqrtSamples * sqrtSamples;
	float *s1 = (float*)alloca(2 * nSamples);
	StratifiedSample2D(s1, sqrtSamples, sqrtSamples, rng);

	ColorRGB retVal = ColorRGB::Black;
	for (int i = 0; i < mNumBxDFs; ++i)
	{
		if (mBxDFs[i]->MatchFlags(flags)) 
		{
			retVal += mBxDFs[i]->Rho(wo, nSamples, s1);
		}
	}

	return retVal;
}

int BSDF::NumComponents( uint32_t bsdfFlags ) const
{
	int num = 0;
	for (int i = 0; i < mNumBxDFs; ++i)
	{
		if (mBxDFs[i]->MatchFlags(bsdfFlags))
			++num;
	}

	return num;
}

ColorRGB SpecularReflection::Sample( const float3& wo, float3* wi, float u1, float u2, float* pdf ) const
{
	*wi = ReflectDirection(wo);
	*pdf = 1.0f;
	return mFresnel->Evaluate(CosTheta(wo)) * mR / AbsCosTheta(*wi);
}

ColorRGB SpecularTransmission::Sample( const float3& wo, float3* wi, float u1, float u2, float* pdf ) const
{
	bool entering = CosTheta(wo) > 0.;
	float ei = eta_i, et = eta_t;
	if (!entering)
		std::swap(ei, et);

	// Compute transmitted ray direction
	float sini2 = SinTheta2(wo);
	float eta = ei / et;
	float sint2 = eta * eta * sini2;

	// Handle total internal reflection for transmission
	if (sint2 >= 1.0f) 
		return ColorRGB::Black;

	float cost = sqrtf(std::max(0.f, 1.f - sint2));
	if (entering) 
		cost = -cost;

	float sintOverSini = eta;
	*wi = float3(sintOverSini * -wo.X(), sintOverSini * -wo.Y(), cost);
	*pdf = 1.0f;

	ColorRGB F = mFresnel.Evaluate(CosTheta(wo));
	return (ColorRGB(1.0f)-F) * mT / AbsCosTheta(*wi);
}

ColorRGB Phong::Eval( const float3& wo, const float3& wi ) const
{
	ColorRGB result(0.0f);

	// diffuse
	result += mKd * Mathf::INV_PI;

	float s, ss;

	// specular
	float alpha = Dot(wo, ReflectDirection(wi));
	if (alpha > 0.0f)
	{
		 s = (mExponent + 2) * Mathf::INV_TWO_PI;
		 ss = powf(alpha, mExponent);
		result += mKs* ((mExponent + 2) * Mathf::INV_TWO_PI * powf(alpha, mExponent));
	}

	return result;
}

float Phong::Pdf( const float3& wo, const float3& wi ) const
{
	if( !SameHemisphere(wo, wi) )
		return 0.0f; 

	float diffuseProb = 0.0f, specProb = 0.0f;

	diffuseProb = CosineHemispherePdf(AbsCosTheta(wi));


	float alpha = Dot(wo, ReflectDirection(wi));
	if (alpha > 0.0f)
	{
		specProb = powf(alpha, mExponent) * (mExponent + 1.0f) / (2.0f * Mathf::PI);
		
		float specularSamplingWeight = Luminance(mKs)  / (Luminance(mKs) + Luminance(mKd));
		return specularSamplingWeight * specProb + (1-specularSamplingWeight) * diffuseProb;
	}
	else
		return diffuseProb;

}

ColorRGB Phong::Sample( const float3& wo, float3* wi, float u1, float u2, float* pdf ) const
{
	float specularSamplingWeight = Luminance(mKs)  / (Luminance(mKs) + Luminance(mKd));

	bool choseSpecular = true;

	if (u1 <= specularSamplingWeight)
		u1 /= specularSamplingWeight;
	else
	{
		choseSpecular = false;
		u1 = (u1 - specularSamplingWeight) / (1-specularSamplingWeight);
	}

	if (choseSpecular)
	{
		float3 R = ReflectDirection(wo);
		
	
		/* Sample from a Phong lobe centered around (0, 0, 1) */
		float sinAlpha = sqrtf( (std::max(0.f, 1-std::pow(u2, 2/(mExponent + 1)))) );
		float cosAlpha = powf(u2, 1/(mExponent + 1));
		float phi = (2.0f * Mathf::PI) * u1;

		*wi = SphericalDirection(cosAlpha, sinAlpha, phi);
		
		if (wo.Z() < 0.)
			wi->Z() *= -1.f;

		/*float3 Axis0;
		float3 Axis1;
		float3 Axis2 = R;

		CoordinateSystem(R, &Axis0, &Axis1);
		*wi = Axis0 * wi->X() + Axis1 * wi->Y() + Axis2 * wi->Z();*/

		/*if (wi->Z() < 0)
			return ColorRGB(0.0f);*/
	}
	else
	{
		*wi = CosineSampleHemisphere(u1, u2);

		if (wo.Z() < 0.) wi->Z() *= -1.f;

		/*if (wi->Z() < 0)
		return ColorRGB(0.0f);*/	
	}

	*pdf = Pdf(wo, *wi);
	
	if (*pdf == 0.f)
		return ColorRGB(0.0f);
	else
		return Eval(wo, *wi);
}

}

