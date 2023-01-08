
#include "SandboxTerrainController.h"
#include "VoxelDataInfo.hpp"
#include "TerrainZoneComponent.h"
#include "TerrainData.hpp"
#include "TerrainServerComponent.h"


struct TZoneEditHandler {
	bool changed = false;
	bool bNoise = false;
	float Strength;

	FVector Origin;
	FRotator Rotator;

	ASandboxTerrainController* Controller = nullptr;

	static FVector GetVoxelRelativePos(const TVoxelData* Vd, const FVector& Origin, const int X, const int Y, const int Z) {
		FVector Pos = Vd->voxelIndexToVector(X, Y, Z);
		Pos += Vd->getOrigin();
		Pos -= Origin;
		return Pos;
	}

	float Noise(const FVector& Pos) {
		if (Controller) {
			static const float NoisePositionScale = 0.5f;
			static const float NoiseValueScale = 0.05f;
			const float Noise = Controller->NormalizedPerlinNoise(Pos, NoisePositionScale, NoiseValueScale);
			return Noise;
		} 

		return 0;
	}

	float Extend;
};


void ASandboxTerrainController::DigCylinder(const FVector& Origin, const float Radius, const float Length, const FRotator& Rotator, const bool bNoise) {
	if (GetNetMode() != NM_Standalone) {
		UE_LOG(LogSandboxTerrain, Error, TEXT("Not implemented yet"));
		return;
	}

	struct ZoneHandler : TZoneEditHandler {
		TMap<uint16, FSandboxTerrainMaterial>* MaterialMapPtr;
		float Length;
		float Radius;
		FRotator Rotator;
		bool bNoise;

		bool operator()(TVoxelData* Vd) {
			changed = false;
			bool bIsRotator = !Rotator.IsZero();
			Rotator = Rotator.GetInverse();

			Vd->forEachWithCache([&](int X, int Y, int Z) {
				FVector V = TZoneEditHandler::GetVoxelRelativePos(Vd, Origin, X, Y, Z);
				if (bIsRotator) {
					V = Rotator.RotateVector(V);
				}

				const float R = std::sqrt(V.X * V.X + V.Y * V.Y);

				if (R < Extend + 20 && V.Z < Length && V.Z > -Length) {
					float OldDensity = Vd->getDensity(X, Y, Z);
					float Density = 1 / (1 + exp((Radius - R) / 10));
					//float Density = 1 - exp(-pow(R, 2) / (Radius * 100));

					if (bNoise) {
						Density += Noise(V);
					}

					if (OldDensity > Density) {
						Vd->setDensity(X, Y, Z, Density);
					}

					changed = true;
				}
			}, USBT_ENABLE_LOD);

			return changed;
		}
	} Zh;

	Zh.MaterialMapPtr = &MaterialMap;
	Zh.Origin = Origin;
	Zh.Extend = Radius;
	Zh.Radius = Radius;
	Zh.Rotator = Rotator;
	Zh.Length = Length;
	Zh.bNoise = bNoise;
	ASandboxTerrainController::PerformTerrainChange(Zh);
}

void ASandboxTerrainController::DigTerrainRoundHole(const FVector& Origin, float Radius, bool bNoise) {
	if (GetNetMode() == NM_Client) {
		//return;
		UE_LOG(LogSandboxTerrain, Log, TEXT("Client: TEST %f %f %f"), Origin.X, Origin.Y, Origin.Z);
	}

	if (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer) {
		if (TerrainServerComponent) {
			TEditTerrainParam EditParam(Origin);
			TerrainServerComponent->SendToAllVdEdit(EditParam);
		}
	}

	struct ZoneHandler : TZoneEditHandler {
		TMap<uint16, FSandboxTerrainMaterial>* MaterialMapPtr;
		UWorld* World;

		bool operator()(TVoxelData* Vd) {
			changed = false;

			Vd->forEachWithCache([&](int X, int Y, int Z) {
				float OldDensity = Vd->getDensity(X, Y, Z);
				FVector V = TZoneEditHandler::GetVoxelRelativePos(Vd, Origin, X, Y, Z);
				float R = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
				if (R < Extend + 20) {
					//unsigned short  MatId = Vd->getMaterial(X, Y, Z);
					//FSandboxTerrainMaterial& Mat = MaterialMapPtr->FindOrAdd(MatId);

					float Density = 1 / (1 + exp((Extend - R) / 10));
					if (bNoise) {
						const float N = Noise(V) * 10;
						Density += N;
					}

					if (OldDensity > Density) {
						Vd->setDensity(X, Y, Z, Density);
					}

					changed = true;
				}
			}, USBT_ENABLE_LOD);

			return changed;
		}
	} Zh;

	Zh.MaterialMapPtr = &MaterialMap;
	Zh.Origin = Origin;
	Zh.Extend = Radius;
	Zh.bNoise = bNoise;
	Zh.World = GetWorld();
	Zh.Controller = this;
	ASandboxTerrainController::PerformTerrainChange(Zh);

}

void ASandboxTerrainController::DigTerrainCubeHoleComplex(const FVector& Origin, const FBox& Box, float Extend, const FRotator& Rotator) {
	if (GetNetMode() != NM_Standalone) {
		UE_LOG(LogSandboxTerrain, Error, TEXT("Not implemented yet"));
		return;
	}

	struct ZoneHandler : TZoneEditHandler {
		TMap<uint16, FSandboxTerrainMaterial>* MaterialMapPtr;
		FRotator Rotator;
		FBox Box;
		UWorld* World;

		bool operator()(TVoxelData* vd) {
			changed = false;
			bool bIsRotator = !Rotator.IsZero();

			const float ExtendXP = Box.Max.X;
			const float ExtendYP = Box.Max.Y;
			const float ExtendZP = Box.Max.Z;
			const float ExtendXN = Box.Min.X;
			const float ExtendYN = Box.Min.Y;
			const float ExtendZN = Box.Min.Z;

			static const float E = 50;
			FBox Box2 = Box.ExpandBy(E);

			vd->forEachWithCache([&](int x, int y, int z) {
				FVector L = vd->voxelIndexToVector(x, y, z) + vd->getOrigin();
				FVector P = L - Origin;

				if (bIsRotator) {
					P = Rotator.RotateVector(P);
				}

				float OldDensity = vd->getDensity(x, y, z);
				bool bIsIntersect = FMath::PointBoxIntersection(P, Box2);
				if (bIsIntersect) {
					float R = 0;
					static float D = 100;
					static float T = 50;

					if (FMath::Abs(P.X - ExtendXP) < T || FMath::Abs(-P.X + ExtendXN) < T) {
						const float DensityXP = 1 / (1 + exp((ExtendXP - P.X) / D));
						const float DensityXN = 1 / (1 + exp((-ExtendXN + P.X) / D));
						const float DensityX = (DensityXP + DensityXN);
						R = DensityX + Noise(L);
					}

					if (FMath::Abs(P.Y - ExtendYP) < T || FMath::Abs(-P.Y + ExtendYN) < T) {
						if (R < 0.5f) {
							const float DensityYP = 1 / (1 + exp((ExtendYP - P.Y) / D));
							const float DensityYN = 1 / (1 + exp((-ExtendYN + P.Y) / D));
							const float DensityY = (DensityYP + DensityYN);
							R = DensityY + Noise(L);
						}
					}

					if (FMath::Abs(P.Z - ExtendZP) < T || FMath::Abs(-P.Z + ExtendZN) < T) {
						if (R < 0.5f) {
							const float DensityZP = 1 / (1 + exp((ExtendZP - P.Z) / D));
							const float DensityZN = 1 / (1 + exp((-ExtendZN + P.Z) / D));
							const float DensityZ = (DensityZP + DensityZN);
							R = DensityZ; // + Noise(L); // camera in tunnel issue
						}
					}

					if (R > 1) {
						R = 1;
					}

					if (R < 0) {
						R = 0;
					}

					if (OldDensity > R) {
						vd->setDensity(x, y, z, R);
					}

					changed = true;
				}
			}, USBT_ENABLE_LOD);

			return changed;
		}
	} Zh;

	Zh.MaterialMapPtr = &MaterialMap;
	Zh.Origin = Origin;
	Zh.Extend = Extend;
	Zh.Rotator = Rotator;
	Zh.Box = Box;
	Zh.World = GetWorld();
	Zh.Controller = this;

	ASandboxTerrainController::PerformTerrainChange(Zh);
}


void ASandboxTerrainController::DigTerrainCubeHole(const FVector& Origin, float Extend, const FRotator& Rotator) {
	if (GetNetMode() != NM_Standalone) {
		UE_LOG(LogSandboxTerrain, Error, TEXT("Not implemented yet"));
		return;
	}

	struct ZoneHandler : TZoneEditHandler {
		TMap<uint16, FSandboxTerrainMaterial>* MaterialMapPtr;
		FRotator Rotator;

		bool operator()(TVoxelData* vd) {
			changed = false;

			bool bIsRotator = !Rotator.IsZero();
			FBox Box(FVector(-(Extend + 20)), FVector(Extend + 20));
			vd->forEachWithCache([&](int x, int y, int z) {
				FVector o = vd->voxelIndexToVector(x, y, z);
				o += vd->getOrigin();
				o -= Origin;

				if (bIsRotator) {
					o = Rotator.RotateVector(o);
				}

				bool bIsIntersect = FMath::PointBoxIntersection(o, Box);
				if (bIsIntersect) {
					unsigned short  MatId = vd->getMaterial(x, y, z);
					FSandboxTerrainMaterial& Mat = MaterialMapPtr->FindOrAdd(MatId);

					const float DensityXP = 1 / (1 + exp((Extend - o.X) / 10));
					const float DensityXN = 1 / (1 + exp((-Extend - o.X) / 10));
					const float DensityYP = 1 / (1 + exp((Extend - o.Y) / 10));
					const float DensityYN = 1 / (1 + exp((-Extend - o.Y) / 10));
					const float DensityZP = 1 / (1 + exp((Extend - o.Z) / 10));
					const float DensityZN = 1 / (1 + exp((-Extend - o.Z) / 10));
					const float Density = DensityXP * DensityXN * DensityYP * DensityYN * DensityZP * DensityZN;
					vd->setDensity(x, y, z, Density);
					changed = true;
				}
			}, USBT_ENABLE_LOD);

			return changed;
		}
	} Zh;

	Zh.MaterialMapPtr = &MaterialMap;
	Zh.Origin = Origin;
	Zh.Extend = Extend;
	Zh.Rotator = Rotator;
	ASandboxTerrainController::PerformTerrainChange(Zh);
}

void ASandboxTerrainController::FillTerrainCube(const FVector& Origin, float Extend, int MatId) {
	if (GetNetMode() != NM_Standalone) {
		UE_LOG(LogSandboxTerrain, Error, TEXT("Not implemented yet"));
		return;
	}

	struct ZoneHandler : TZoneEditHandler {
		int newMaterialId;
		bool operator()(TVoxelData* vd) {
			changed = false;

			vd->forEachWithCache([&](int x, int y, int z) {
				FVector o = vd->voxelIndexToVector(x, y, z);
				o += vd->getOrigin();
				o -= Origin;
				if (o.X < Extend && o.X > -Extend && o.Y < Extend && o.Y > -Extend && o.Z < Extend && o.Z > -Extend) {
					vd->setDensity(x, y, z, 1);
					changed = true;
				}

				float radiusMargin = Extend + 20;
				if (o.X < radiusMargin && o.X > -radiusMargin && o.Y < radiusMargin && o.Y > -radiusMargin && o.Z < radiusMargin && o.Z > -radiusMargin) {
					vd->setMaterial(x, y, z, newMaterialId);
				}
			}, USBT_ENABLE_LOD);

			return changed;
		}
	} Zh;

	Zh.newMaterialId = MatId;
	Zh.Origin = Origin;
	Zh.Extend = Extend;
	ASandboxTerrainController::PerformTerrainChange(Zh);
}

void ASandboxTerrainController::FillTerrainRound(const FVector& Origin, float Extend, int MatId) {
	if (GetNetMode() != NM_Standalone) {
		UE_LOG(LogSandboxTerrain, Error, TEXT("Not implemented yet"));
		return;
	}

	struct ZoneHandler : TZoneEditHandler {
		int newMaterialId;
		bool operator()(TVoxelData* vd) {
			changed = false;

			vd->forEachWithCache([&](int x, int y, int z) {
				float density = vd->getDensity(x, y, z);
				FVector o = vd->voxelIndexToVector(x, y, z);
				o += vd->getOrigin();
				o -= Origin;

				float rl = std::sqrt(o.X * o.X + o.Y * o.Y + o.Z * o.Z);
				if (rl < Extend) {
					//2^-((x^2)/20)
					float d = density + 1 / rl * Strength;
					vd->setDensity(x, y, z, d);
					changed = true;
				}

				if (rl < Extend + 20) {
					vd->setMaterial(x, y, z, newMaterialId);
				}
			}, USBT_ENABLE_LOD);

			return changed;
		}
	} Zh;

	Zh.newMaterialId = MatId;
	Zh.Strength = 10;
	Zh.Origin = Origin;
	Zh.Extend = Extend;
	ASandboxTerrainController::PerformTerrainChange(Zh);
}

//======================================================================================================================================================================
// Edit Terrain
//======================================================================================================================================================================

void ASandboxTerrainController::RemoveInstanceAtMesh(UHierarchicalInstancedStaticMeshComponent* InstancedMeshComp, int32 ItemIndex) {
	InstancedMeshComp->RemoveInstance(ItemIndex);
	TArray<USceneComponent*> Parents;
	InstancedMeshComp->GetParentComponents(Parents);
	if (Parents.Num() > 0) {
		UTerrainZoneComponent* Zone = Cast<UTerrainZoneComponent>(Parents[0]);
		if (Zone) {
			FVector ZonePos = Zone->GetComponentLocation();
			TVoxelIndex ZoneIndex = GetZoneIndex(ZonePos);
			MarkZoneNeedsToSaveObjects(ZoneIndex);
		}
	}
}

template<class H>
void ASandboxTerrainController::PerformTerrainChange(H Handler) {
	AddAsyncTask([=] {
		EditTerrain(Handler);
	});

	TArray<struct FOverlapResult> Result;
	FCollisionQueryParams CollisionQueryParams = FCollisionQueryParams::DefaultQueryParam;
	CollisionQueryParams.bTraceComplex = false;
	CollisionQueryParams.bSkipNarrowPhase = true;

	double Start = FPlatformTime::Seconds();

	bool bIsOverlap = GetWorld()->OverlapMultiByChannel(Result, Handler.Origin, FQuat(), ECC_Visibility, FCollisionShape::MakeSphere(Handler.Extend * 1.5f)); // ECC_Visibility
	double End = FPlatformTime::Seconds();
	double Time = (End - Start) * 1000;
	UE_LOG(LogSandboxTerrain, Log, TEXT("Trace terrain -> %f ms"), Time);

	if (bIsOverlap) {
		for (FOverlapResult& Overlap : Result) {
			if (Cast<ASandboxTerrainController>(Overlap.GetActor())) {
				UHierarchicalInstancedStaticMeshComponent* InstancedMesh = Cast<UHierarchicalInstancedStaticMeshComponent>(Overlap.GetComponent());
				if (InstancedMesh) {
					//UE_LOG(LogSandboxTerrain, Warning, TEXT("InstancedMesh: %s -> %d"), *InstancedMesh->GetName(), Overlap.ItemIndex);
					//FTransform Transform;
					//InstancedMesh->GetInstanceTransform(Overlap.ItemIndex, Transform, true);
					//DrawDebugPoint(GetWorld(), Transform.GetLocation(), 5.f, FColor(255, 255, 255, 0), false, 10);

					RemoveInstanceAtMesh(InstancedMesh, Overlap.ItemIndex); //overhead
					//InstancedMesh->RemoveInstance(Overlap.ItemIndex);
				}
			} else {
				OnOverlapActorTerrainEdit(Overlap, Handler.Origin);
			}
		}
	}

	// UE5 bad collision performance workaround
	PerformEachZone(Handler.Origin, Handler.Extend, [&](TVoxelIndex ZoneIndex, FVector Origin, TVoxelDataInfoPtr VoxelDataInfo) {
		UTerrainZoneComponent* Zone = GetZoneByVectorIndex(ZoneIndex);
		if (Zone) {
			TArray<USceneComponent*> Childs;
			Zone->GetChildrenComponents(true, Childs);
			for (USceneComponent* Child : Childs) {
				UHierarchicalInstancedStaticMeshComponent* InstancedMesh = Cast<UHierarchicalInstancedStaticMeshComponent>(Child);
				if (InstancedMesh && !InstancedMesh->IsCollisionEnabled()) {
					TArray<int32> Instances = InstancedMesh->GetInstancesOverlappingSphere(Handler.Origin, Handler.Extend, true);
					for (int32 Instance : Instances) {
						InstancedMesh->RemoveInstance(Instance);
					}
				}
			}
		}
	});
}

void ASandboxTerrainController::PerformEachZone(const FVector& Origin, const float Extend, std::function<void(TVoxelIndex, FVector, TVoxelDataInfoPtr)> Function) {
	static const float V[3] = { -1, 0, 1 };
	TVoxelIndex BaseZoneIndex = GetZoneIndex(Origin);

	for (float X : V) {
		for (float Y : V) {
			for (float Z : V) {
				TVoxelIndex ZoneIndex = BaseZoneIndex + TVoxelIndex(X, Y, Z);
				UTerrainZoneComponent* Zone = GetZoneByVectorIndex(ZoneIndex);
				TVoxelDataInfoPtr VoxelDataInfo = GetVoxelDataInfo(ZoneIndex);

				// check zone bounds
				FVector ZoneOrigin = GetZonePos(ZoneIndex);
				constexpr float ZoneVolumeSize = USBT_ZONE_SIZE / 2;
				FVector Upper(ZoneOrigin.X + ZoneVolumeSize, ZoneOrigin.Y + ZoneVolumeSize, ZoneOrigin.Z + ZoneVolumeSize);
				FVector Lower(ZoneOrigin.X - ZoneVolumeSize, ZoneOrigin.Y - ZoneVolumeSize, ZoneOrigin.Z - ZoneVolumeSize);

				if (FMath::SphereAABBIntersection(FSphere(Origin, Extend * 2.f), FBox(Lower, Upper))) {
					Function(ZoneIndex, ZoneOrigin, GetVoxelDataInfo(ZoneIndex));
				}
			}
		}
	}

}

template<class H>
void ASandboxTerrainController::PerformZoneEditHandler(TVoxelDataInfoPtr VdInfoPtr, H Handler, std::function<void(TMeshDataPtr)> OnComplete) {
	bool bIsChanged = Handler(VdInfoPtr->Vd);
	//if (bIsChanged) {
		VdInfoPtr->SetChanged();
		VdInfoPtr->Vd->setCacheToValid();
		TMeshDataPtr MeshDataPtr = GenerateMesh(VdInfoPtr->Vd);
		VdInfoPtr->ResetLastMeshRegenerationTime();
		OnComplete(MeshDataPtr);
	//}
}

void ASandboxTerrainController::IncrementChangeCounter(const TVoxelIndex& ZoneIndex) {
	// TODO client rcv sync
	const std::lock_guard<std::mutex> Lock(ModifiedVdMapMutex);
	TZoneModificationData& Data = ModifiedVdMap.FindOrAdd(ZoneIndex);
	Data.ChangeCounter++;
}

TMap<TVoxelIndex, TZoneModificationData> ModifiedVdMap;

// TODO refactor concurency according new terrain data system
template<class H>
void ASandboxTerrainController::EditTerrain(const H& ZoneHandler) {
	double Start = FPlatformTime::Seconds();

	static float ZoneVolumeSize = USBT_ZONE_SIZE / 2;
	TVoxelIndex BaseZoneIndex = GetZoneIndex(ZoneHandler.Origin);

	bool bIsValid = true;

	PerformEachZone(ZoneHandler.Origin, ZoneHandler.Extend, [&](TVoxelIndex ZoneIndex, FVector Origin, TVoxelDataInfoPtr VoxelDataInfo) {
		if (VoxelDataInfo->DataState == TVoxelDataState::UNDEFINED) {
			UE_LOG(LogSandboxTerrain, Warning, TEXT("Zone: %d %d %d -> Invalid zone vd state (UNDEFINED)"), ZoneIndex.X, ZoneIndex.Y, ZoneIndex.Z);

			AsyncTask(ENamedThreads::GameThread, [=]() {
				DrawDebugBox(GetWorld(), GetZonePos(ZoneIndex), FVector(USBT_ZONE_SIZE / 2), FColor(255, 0, 0, 0), false, 5);
			});

			bIsValid = false;
		}
	});

	if (!bIsValid) {
		return;
	}

	PerformEachZone(ZoneHandler.Origin, ZoneHandler.Extend, [&](TVoxelIndex ZoneIndex, FVector Origin, TVoxelDataInfoPtr VoxelDataInfo) {
		VoxelDataInfo->Lock();
		UTerrainZoneComponent* Zone = GetZoneByVectorIndex(ZoneIndex);

		if (VoxelDataInfo->DataState == TVoxelDataState::UNDEFINED) {
			UE_LOG(LogSandboxTerrain, Warning, TEXT("Zone: %d %d %d -> UNDEFINED"), ZoneIndex.X, ZoneIndex.Y, ZoneIndex.Z);
			VoxelDataInfo->Unlock();
			return;
		}

		if (VoxelDataInfo->DataState == TVoxelDataState::READY_TO_LOAD) {
			TVoxelData* Vd = LoadVoxelDataByIndex(ZoneIndex);
			if (Vd) {
				VoxelDataInfo->Vd = Vd;
				VoxelDataInfo->DataState = TVoxelDataState::LOADED;
			} else {
				// TODO check ungenerated flags
				VoxelDataInfo->DataState = TVoxelDataState::UNGENERATED;
			}
		}

		if (VoxelDataInfo->DataState == TVoxelDataState::UNGENERATED) {
			VoxelDataInfo->DataState = TVoxelDataState::GENERATION_IN_PROGRESS;

			if (VoxelDataInfo->Vd == nullptr) {
				UE_LOG(LogSandboxTerrain, Warning, TEXT("Zone: %d %d %d -> UNGENERATED"), ZoneIndex.X, ZoneIndex.Y, ZoneIndex.Z);

				TVoxelData* NewVd = NewVoxelData();
				NewVd->setOrigin(GetZonePos(ZoneIndex));
				VoxelDataInfo->Vd = NewVd;

			} else {
				UE_LOG(LogSandboxTerrain, Warning, TEXT("Zone: %d %d %d -> UNGENERATED but Vd is not null"), ZoneIndex.X, ZoneIndex.Y, ZoneIndex.Z);
			}

			GetTerrainGenerator()->ForceGenerateZone(VoxelDataInfo->Vd, ZoneIndex);
			VoxelDataInfo->DataState = TVoxelDataState::GENERATED;
		}

		if (VoxelDataInfo->DataState == TVoxelDataState::LOADED || VoxelDataInfo->DataState == TVoxelDataState::GENERATED) {
			IncrementChangeCounter(ZoneIndex);
			if (Zone == nullptr) {
				PerformZoneEditHandler(VoxelDataInfo, ZoneHandler, [&](TMeshDataPtr MeshDataPtr) {
					ExecGameThreadAddZoneAndApplyMesh(ZoneIndex, MeshDataPtr, false, true);
				});
			} else {
				PerformZoneEditHandler(VoxelDataInfo, ZoneHandler, [&](TMeshDataPtr MeshDataPtr) {
					ExecGameThreadZoneApplyMesh(ZoneIndex, Zone, MeshDataPtr);
				});
			}
		}

		VoxelDataInfo->Unlock();
	});

	double End = FPlatformTime::Seconds();
	double Time = (End - Start) * 1000;
	UE_LOG(LogSandboxTerrain, Log, TEXT("Edit terrain -> %f ms"), Time);
}