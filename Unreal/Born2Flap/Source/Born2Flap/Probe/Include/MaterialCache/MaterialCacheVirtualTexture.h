// Project-local stub. The shipped MeshPartitionCompiledSection.h includes
// "MaterialCache/MaterialCacheVirtualTexture.h" (used only as a TObjectPtr
// property type), but the MaterialCache plugin is absent from this UE 5.8
// engine install and the prebuilt MeshPartition binaries do not import it.
// Consumers only need the type name to compile; no symbols are referenced.
#pragma once

#include "CoreMinimal.h"

class UMaterialCacheVirtualTexture;
