

#include "SandboxObject.h"

ASandboxObject::ASandboxObject() {
	PrimaryActorTick.bCanEverTick = true;
	SandboxRootMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SandboxRootMesh"));
	MaxStackSize = 100;
	bStackable = true;
	RootComponent = SandboxRootMesh;
}

static const FString DefaultSandboxObjectName = FString(TEXT("Sandbox object"));

FString ASandboxObject::GetSandboxName() {
	return DefaultSandboxObjectName;
}

uint64 ASandboxObject::GetSandboxClassId() const {
	return SandboxClassId; 
}

int ASandboxObject::GetSandboxTypeId() const {
	return 0; 
}

int ASandboxObject::GetMaxStackSize() {
	if (!bStackable) {
		return 1;
	}

	return MaxStackSize; // default stack size
}

UTexture2D* ASandboxObject::GetSandboxIconTexture() {
	return NULL; 
}

void ASandboxObject::TickInInventoryActive(float DeltaTime, UWorld* World, const FHitResult& HitResult) {

}

void ASandboxObject::ActionInInventoryActive(UWorld* World, const FHitResult& HitResult) {

}

void ASandboxObject::ActionInInventoryActive2(UWorld* World, const FHitResult& HitResult) {

}

bool ASandboxObject::CanTake(AActor* actor) {
	
	TArray<UContainerComponent*> ContainerComponents;
	GetComponents<UContainerComponent>(ContainerComponents);

	for (const auto& ContainerComponent : ContainerComponents) {
		if (!ContainerComponent->IsEmpty()) {
			return false;
		}
	}
	
	return true; 
}

void ASandboxObject::InformTerrainChange(int32 Item) {

}


UContainerComponent* ASandboxObject::GetContainer(const FString& Name) { 
	return GetFirstComponentByName<UContainerComponent>(Name);
}

bool ASandboxObject::IsInteractive(const APawn* Source) {
	return false; // no;
}

void ASandboxObject::MainInteraction(const APawn* Source) {
	// do nothing
}


void ASandboxObject::SetProperty(FString Key, FString Value) {
	PropertyMap.Add(Key, Value);
}

FString ASandboxObject::GetProperty(FString Key) {
	return PropertyMap.FindRef(Key);
}

void ASandboxObject::RemoveProperty(FString Key) {
	PropertyMap.Remove(Key);
}

void ASandboxObject::PostLoadProperties() {

}

void ASandboxObject::OnPlaceToWorld() {

}

bool ASandboxObject::PlaceToWorldClcPosition(const FVector& SourcePos, const FRotator& SourceRotation, const FHitResult& Res, FVector& Location, FRotator& Rotation, bool bFinal) const {
	Location = Res.Location;
	Rotation.Pitch = 0;
	Rotation.Roll = 0;
	Rotation.Yaw = SourceRotation.Yaw;
	return true;
}
