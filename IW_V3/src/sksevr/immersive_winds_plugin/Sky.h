#pragma once

#include "skse64/GameReferences.h"

class TESWeather;
class TESClimate;
class TESRegion;

namespace ImmersiveWinds
{
	// 0x2C8    
	class Sky
	{
	public:
		virtual ~Sky();

		//static Sky* GetSingleton(); //unused

		// members
		// vtbl 0x0
		void                      * unkBSMultiBoundNode;             // 0x8   BSMultiBoundNode
		NiNode                    * unkNode;                         // 0x10  NiNode
		UInt64                      unk18[5];                        // 0x18
		TESClimate                * climate;                         // 0x40
		TESWeather                * currentWeather;                  // 0x48  current weather active or being transitioned to
		TESWeather                * outgoingWeather;                 // 0x50  previous weather during transition
		TESWeather                * nextWeather;                     // 0x58  next weather in queue, transition hasn't begun yet
		UInt64                      unk60;                           // 0x60
		TESRegion                 * unk68;                           // 0x68
																	 // following are SkyObjects
		void                      * atmosphere;                      // 0x70  Atmosphere 
		void                      * stars;                           // 0x78  Stars
		void                      * sun;                             // 0x80  Sun
		void                      * clouds;                          // 0x88  Clouds
		void                      * masser;                          // 0x90  Moon
		void                      * secunda;                         // 0x98  Moon
		void                      * precipitation;                   // 0xA0  Precipitation
																	 // a whole bunch of unknown floats, some may be UInt32 too
		float                       unkA8[(0x1B0 - 0xA8) >> 2];      // 0xA8
		float                       timeOfDay;                       // 0x1B0 time of day as float 0.0~24.0
		float                       unk1B4;                          // 0x1B4
		float                       transition;                      // 0x1B8 weather transition amount 0.0 -> 1.0
		UInt32                      skyMode;                         // 0x1BC
		void                      * unk1C0;                          // 0x1C0
		float                       unk1BC[(0x278 - 0x1C8) >> 2];    // 0x1C8
		void                      * skyEffectController;             // 0x278 SkyEffectController : ReferenceEffectController
		UInt64                      unk280[3];                       // 0x280
																	 //BSTArray<NiSourceTexture> textures;                        // 0x298 //says same type as tarray in BSTextureSet.h
		tArray<NiSourceTexture>     textures;                        // 0x298 //
		UnkArray                    unkArray2B0;                     // 0x2B0

	};
	STATIC_ASSERT(offsetof(Sky, unkBSMultiBoundNode) == 0x8);
	STATIC_ASSERT(offsetof(Sky, currentWeather) == 0x48);
	STATIC_ASSERT(offsetof(Sky, transition) == 0x1B8);
	STATIC_ASSERT(sizeof(Sky) == 0x2C8);


	struct DynamicAttenuationCharacteristics
	{
		UInt32 unk1;
		UInt32 unk2;


		float	minDistance;	// 08
		float	maxDistance;	// 0C
		UInt8	curve[5];		// 10
		UInt8	pad15;			// 15
		UInt16	pad16;			// 16
	};
	STATIC_ASSERT(sizeof(DynamicAttenuationCharacteristics) == 0x18);


	class BGSSoundOutput : public TESForm
	{
	public:
		enum { kTypeID = kFormType_SoundOutput };

		struct SpeakerArrays	// ONAM
		{
			struct Channel
			{
				UInt8	l;		// 0
				UInt8	r;		// 1
				UInt8	c;		// 2
				UInt8	lfe;	// 3
				UInt8	rl;		// 4
				UInt8	rr;		// 5
				UInt8	bl;		// 6
				UInt8	br;		// 7
			};
			STATIC_ASSERT(sizeof(Channel) == 0x8);

			Channel channels[3];
		};
		STATIC_ASSERT(sizeof(SpeakerArrays) == 0x18);

		// parents
		BSISoundOutputModel	soundOutputModel;	// 20

												// members
		UInt32	unk28;		// 28
		UInt32	unk2C;		// 2C
		UInt64	unk30;		// 30
		SpeakerArrays*						speakerOutputs;	// 38 - ONAM
	};
}
