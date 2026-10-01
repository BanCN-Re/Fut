# Dumper-7 坐标 / 变换 / 投影 函数表

- SDK: `D:\Work\Fut\sdk\4.25.4-0+++UE4+Release-4.25-PM`
- 来源: `Dumpspace\FunctionsInfo.json`
- 生成时间: 2026-09-30 20:44:10
- 共解析 **14517** 个函数（全部类）

> RVA 为相对镜像基址（`0x140000000`）的偏移；实际地址 = 基址 + RVA。

## 位置 / 旋转（Actor 级）

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `AALSBaseCharacter` | `SetActorLocationAndTargetRotation` | `0x10E4C30` | `0x1410E4C30` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `AActor` | `GetActorBounds` | `0x439EFA0` | `0x14439EFA0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetActorEyesViewPoint` | `0x439F150` | `0x14439F150` | Native|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetActorForwardVector` | `0x439F240` | `0x14439F240` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetActorRightVector` | `0x439F2F0` | `0x14439F2F0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetActorUpVector` | `0x439F400` | `0x14439F400` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetDistanceTo` | `0x439F9C0` | `0x14439F9C0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetDotProductTo` | `0x439FA60` | `0x14439FA60` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetHorizontalDistanceTo` | `0x439FB30` | `0x14439FB30` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetSquaredDistanceTo` | `0x43A0240` | `0x1443A0240` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetTransform` | `0x43A0310` | `0x1443A0310` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetVelocity` | `0x43A03C0` | `0x1443A03C0` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetVerticalDistanceTo` | `0x43A0400` | `0x1443A0400` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `K2_AddActorLocalOffset` | `0x43A05F0` | `0x1443A05F0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_AddActorLocalRotation` | `0x43A0790` | `0x1443A0790` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_AddActorWorldOffset` | `0x43A0B40` | `0x1443A0B40` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_AddActorWorldRotation` | `0x43A0CE0` | `0x1443A0CE0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_GetActorLocation` | `0x43A1830` | `0x1443A1830` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `K2_GetActorRotation` | `0x43A1910` | `0x1443A1910` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `K2_SetActorLocation` | `0x43A1AF0` | `0x1443A1AF0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_SetActorLocationAndRotation` | `0x43A1CA0` | `0x1443A1CA0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_SetActorRotation` | `0x43A23F0` | `0x1443A23F0` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `AActor` | `K2_SetActorTransform` | `0x43A24D0` | `0x1443A24D0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `AActor` | `K2_TeleportTo` | `0x43A26F0` | `0x1443A26F0` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `ACyCharacterPlayer` | `SetActorLocationAndTargetRotation` | `0x237DD20` | `0x14237DD20` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `IGizmoTransformSource` | `GetTransform` | `0x49C6340` | `0x1449C6340` | Native|Public|HasDefaults|Const |
| `IMovieSceneTransformOrigin` | `BP_GetTransformOrigin` | `0x2A7DA10` | `0x142A7DA10` | Event|Protected|HasDefaults|BlueprintCallable|BlueprintEvent|BlueprintPure|Const |
| `UKismetSystemLibrary` | `GetActorBounds` | `0x4473D70` | `0x144473D70` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `ULiveLinkBlueprintLibrary` | `GetTransformByIndex` | `0xC94820` | `0x140C94820` | Final|Native|Static|Private|HasOutParams|BlueprintCallable|BlueprintPure |
| `ULiveLinkBlueprintLibrary` | `GetTransformByName` | `0xC949B0` | `0x140C949B0` | Final|Native|Static|Private|HasOutParams|BlueprintCallable|BlueprintPure |
| `UPhysicalAnimationComponent` | `GetBodyTargetTransform` | `0x44BA680` | `0x1444BA680` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetTransformAtDistanceAlongSpline` | `0x4504B30` | `0x144504B30` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetTransformAtSplineInputKey` | `0x4504C90` | `0x144504C90` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetTransformAtSplinePoint` | `0x4504DF0` | `0x144504DF0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetTransformAtTime` | `0x4504F40` | `0x144504F40` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |

## 位置 / 旋转（组件级）

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `AActor` | `GetAttachParentActor` | `0x439F5A0` | `0x14439F5A0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `GetAttachParentSocketName` | `0x439F5D0` | `0x14439F5D0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UKismetMathLibrary` | `GetForwardVector` | `0x4442C80` | `0x144442C80` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `GetRightVector` | `0x4443650` | `0x144443650` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `GetUpVector` | `0x4443ED0` | `0x144443ED0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetSystemLibrary` | `GetComponentBounds` | `0x4474230` | `0x144474230` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `USceneComponent` | `GetForwardVector` | `0x44C1110` | `0x1444C1110` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetAttachParent` | `0x44E3250` | `0x1444E3250` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetAttachSocketName` | `0x44E3270` | `0x1444E3270` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetComponentVelocity` | `0x44E3450` | `0x1444E3450` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetRightVector` | `0x44E3690` | `0x1444E3690` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetSocketLocation` | `0x44E3700` | `0x1444E3700` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetSocketQuaternion` | `0x44E37B0` | `0x1444E37B0` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetSocketRotation` | `0x44E3850` | `0x1444E3850` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetSocketTransform` | `0x44E3900` | `0x1444E3900` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `GetUpVector` | `0x44E3A10` | `0x1444E3A10` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `K2_AddLocalOffset` | `0x44E3C70` | `0x1444E3C70` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_AddLocalRotation` | `0x44E3E10` | `0x1444E3E10` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_AddRelativeLocation` | `0x44E41C0` | `0x1444E41C0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_AddRelativeRotation` | `0x44E4360` | `0x1444E4360` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_AddWorldOffset` | `0x44E4500` | `0x1444E4500` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_AddWorldRotation` | `0x44E46A0` | `0x1444E46A0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_GetComponentLocation` | `0x44E4EA0` | `0x1444E4EA0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `K2_GetComponentRotation` | `0x44E4EE0` | `0x1444E4EE0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `K2_GetComponentToWorld` | `0x44E4F60` | `0x1444E4F60` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USceneComponent` | `K2_SetRelativeLocation` | `0x44E4FE0` | `0x1444E4FE0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetRelativeLocationAndRotation` | `0x44E5180` | `0x1444E5180` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetRelativeRotation` | `0x44E5370` | `0x1444E5370` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetRelativeTransform` | `0x44E5510` | `0x1444E5510` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetWorldLocation` | `0x44E5720` | `0x1444E5720` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetWorldLocationAndRotation` | `0x44E58C0` | `0x1444E58C0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetWorldRotation` | `0x44E5AB0` | `0x1444E5AB0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `K2_SetWorldTransform` | `0x44E5C50` | `0x1444E5C50` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USceneComponent` | `ResetRelativeTransform` | `0x44E60A0` | `0x1444E60A0` | Final|Native|Public|BlueprintCallable |
| `USkeletalMeshSocket` | `GetSocketLocation` | `0x44F47F0` | `0x1444F47F0` | Final|RequiredAPI|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetRightVectorAtDistanceAlongSpline` | `0x45038C0` | `0x1445038C0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetRightVectorAtSplineInputKey` | `0x45039A0` | `0x1445039A0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetRightVectorAtSplinePoint` | `0x4503A80` | `0x144503A80` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetRightVectorAtTime` | `0x4503B50` | `0x144503B50` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetUpVectorAtDistanceAlongSpline` | `0x45050E0` | `0x1445050E0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetUpVectorAtSplineInputKey` | `0x45051C0` | `0x1445051C0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetUpVectorAtSplinePoint` | `0x45052A0` | `0x1445052A0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `GetUpVectorAtTime` | `0x4505370` | `0x144505370` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `USplineComponent` | `SetWorldLocationAtSplinePoint` | `0x4506CE0` | `0x144506CE0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |

## 骨骼 / 网格（坐标来源）

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `UPoseableMeshComponent` | `GetBoneLocationByName` | `0x44D04B0` | `0x1444D04B0` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UPoseableMeshComponent` | `GetBoneRotationByName` | `0x44D0580` | `0x1444D0580` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UPoseableMeshComponent` | `GetBoneTransformByName` | `0x44D0720` | `0x1444D0720` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `USkinnedMeshComponent` | `BoneIsChildOf` | `0x44F3EA0` | `0x1444F3EA0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `USkinnedMeshComponent` | `GetBoneName` | `0x44F42A0` | `0x1444F42A0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `USkinnedMeshComponent` | `GetNumBones` | `0x44F44B0` | `0x1444F44B0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `USkinnedMeshComponent` | `GetSocketBoneName` | `0x44F4760` | `0x1444F4760` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |

## 世界坐标 → 屏幕投影

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `ACyProjectileBase` | `MulticastProjectileLifeOver` | `0xAB4380` | `0x140AB4380` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyProjectileBase` | `MulticastProjectileBounceExplode` | `0x24D2BE0` | `0x1424D2BE0` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyProjectileBase` | `MulticastProjectileExplode` | `0x24D2CA0` | `0x1424D2CA0` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyProjectileBase` | `MulticastProjectileHit` | `0x24D2D60` | `0x1424D2D60` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyProjectileBase` | `MulticastProjectilePreExplode` | `0x24D2E20` | `0x1424D2E20` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyProjectileBase` | `MulticastProjectileStop` | `0x24D2EE0` | `0x1424D2EE0` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyProjectileBase` | `MulticastProjectileThrough` | `0x24D2F00` | `0x1424D2F00` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `ACyWeaponBase` | `MulticastProjectile` | `0x25D59D0` | `0x1425D59D0` | Net|NetReliable|Native|Event|NetMulticast|Public |
| `ACyWeaponBase` | `ServerProjectile` | `0x25D6110` | `0x1425D6110` | Net|NetReliable|Native|Event|Public|NetServer |
| `AHUD` | `Deproject` | `0x44184C0` | `0x1444184C0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AHUD` | `Project` | `0x441A740` | `0x14441A740` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `APlayerController` | `DeprojectMousePositionToWorld` | `0x44C8500` | `0x1444C8500` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `APlayerController` | `DeprojectScreenPositionToWorld` | `0x44C8600` | `0x1444C8600` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `APlayerController` | `GetMousePosition` | `0x44C9A40` | `0x1444C9A40` | Final|Native|Public|HasOutParams|BlueprintCallable|BlueprintPure|Const |
| `APlayerController` | `GetViewportSize` | `0x44C9B50` | `0x1444C9B50` | Final|Native|Public|HasOutParams|BlueprintCallable|BlueprintPure|Const |
| `APlayerController` | `ProjectWorldLocationToScreen` | `0x44CA420` | `0x1444CA420` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AProjectile_E_Chiyo_C` | `ExecuteUbergraph_Projectile_E_Chiyo` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `AProjectile_E_Maddelena_Escort2_C` | `ExecuteUbergraph_Projectile_E_Maddelena_Escort2` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `AProjectile_E_Ming_Escort2_C` | `ExecuteUbergraph_Projectile_E_Ming_Escort2` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `AProjectile_Katana_Sickle_C` | `ExecuteUbergraph_Projectile_Katana_Sickle` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `AProjectile_Katana_Wowowo_C` | `ExecuteUbergraph_Projectile_Katana_Wowowo` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `AProjectile_Q_Kanami_C` | `ExecuteUbergraph_Projectile_Q_Kanami` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `AProjectile_Q_Maddelena_Hextech_C` | `ExecuteUbergraph_Projectile_Q_Maddelena_Hextech` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_KokonaShiki::AProjectile_E_KokonaShiki_C` | `ExecuteUbergraph_Projectile_E_KokonaShiki` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_KokonaShiki_0::AProjectile_E_KokonaShiki_C` | `ExecuteUbergraph_Projectile_E_KokonaShiki` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Lawine::AProjectile_E_Lawine_C` | `ExecuteUbergraph_Projectile_E_Lawine` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Lawine_0::AProjectile_E_Lawine_C` | `ExecuteUbergraph_Projectile_E_Lawine` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Maddelena::AProjectile_E_Maddelena_C` | `ExecuteUbergraph_Projectile_E_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Maddelena_0::AProjectile_E_Maddelena_C` | `ExecuteUbergraph_Projectile_E_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Maddelena_1::AProjectile_E_Maddelena_C` | `ExecuteUbergraph_Projectile_E_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Maddelena_2::AProjectile_E_Maddelena_C` | `ExecuteUbergraph_Projectile_E_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Ming::AProjectile_E_Ming_C` | `ExecuteUbergraph_Projectile_E_Ming` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_E_Ming_0::AProjectile_E_Ming_C` | `ExecuteUbergraph_Projectile_E_Ming` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_Q_Lawine::AProjectile_Q_Lawine_C` | `ExecuteUbergraph_Projectile_Q_Lawine` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_Q_Lawine_0::AProjectile_Q_Lawine_C` | `ExecuteUbergraph_Projectile_Q_Lawine` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_Q_Maddelena::AProjectile_Q_Maddelena_C` | `ExecuteUbergraph_Projectile_Q_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_Q_Maddelena_0::AProjectile_Q_Maddelena_C` | `ExecuteUbergraph_Projectile_Q_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_Q_Ming::AProjectile_Q_Ming_C` | `ExecuteUbergraph_Projectile_Q_Ming` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_Q_Ming_0::AProjectile_Q_Ming_C` | `ExecuteUbergraph_Projectile_Q_Ming` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_X_Maddelena::AProjectile_X_Maddelena_C` | `ExecuteUbergraph_Projectile_X_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_X_Maddelena_0::AProjectile_X_Maddelena_C` | `ExecuteUbergraph_Projectile_X_Maddelena` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `Projectile_X_Michele::AProjectile_X_Michele_C` | `ExecuteUbergraph_Projectile_X_Michele` | `0x2A7DA10` | `0x142A7DA10` | Final|UbergraphFunction |
| `UAkWaapiCalls` | `RegisterWaapiProjectLoadedCallback` | `0x1384490` | `0x141384490` | Final|Native|Static|Public|HasOutParams|BlueprintCallable |
| `UBlueprintPathsLibrary` | `GetProjectFilePath` | `0x43CD720` | `0x1443CD720` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `HasProjectPersistentDownloadDir` | `0x43CD890` | `0x1443CD890` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `IsProjectFilePathSet` | `0x43CD990` | `0x1443CD990` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectConfigDir` | `0x43CF240` | `0x1443CF240` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectContentDir` | `0x43CF2B0` | `0x1443CF2B0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectDir` | `0x43CF320` | `0x1443CF320` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectIntermediateDir` | `0x43CF390` | `0x1443CF390` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectLogDir` | `0x43CF400` | `0x1443CF400` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectModsDir` | `0x43CF470` | `0x1443CF470` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectPersistentDownloadDir` | `0x43CF4E0` | `0x1443CF4E0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectPluginsDir` | `0x43CF550` | `0x1443CF550` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectSavedDir` | `0x43CF5C0` | `0x1443CF5C0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `ProjectUserDir` | `0x43CF630` | `0x1443CF630` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `SetProjectFilePath` | `0x43CFBD0` | `0x1443CFBD0` | Final|Native|Static|Public|BlueprintCallable |
| `UCameraComponent` | `SetProjectionMode` | `0x43D7630` | `0x1443D7630` | Final|Native|Public|BlueprintCallable |
| `UCanvas` | `K2_Deproject` | `0x43DD360` | `0x1443DD360` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UCanvas` | `K2_Project` | `0x43DE820` | `0x1443DE820` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UCyAIAttackComponent` | `MulticastProjectile` | `0x231F060` | `0x14231F060` | Net|NetReliable|Native|Event|NetMulticast|Protected |
| `UCyAttackComponent` | `ComputeProjectileLimitDistance` | `0x2334E00` | `0x142334E00` | Final|Native|Public|BlueprintCallable |
| `UCyAttackComponent` | `GetDefaultProjectile` | `0x23356D0` | `0x1423356D0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UCyFireComponent` | `GetProjectileAttackDirection` | `0x23E4B20` | `0x1423E4B20` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCyFireComponent` | `GetProjectileAttackLocation` | `0x23E4B60` | `0x1423E4B60` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCySearchTargetProjectileExpired` | `ProjectileSearchTarget` | `0x251F620` | `0x14251F620` | Final|Native|Protected |
| `UCySickleComponent` | `OnProjectileBounce` | `0x2530860` | `0x142530860` | Native|Protected|HasOutParams|HasDefaults |
| `UFlibPatchParserHelper` | `GetProjectFilePath` | `0x12123E0` | `0x1412123E0` | Final|Native|Static|Public|BlueprintCallable |
| `UFlibPatchParserHelper` | `GetProjectIniFiles` | `0x1212450` | `0x141212450` | Final|Native|Static|Public|BlueprintCallable |
| `UFlibPatchParserHelper` | `GetProjectName` | `0x1212620` | `0x141212620` | Final|Native|Static|Public|BlueprintCallable |
| `UGameplayStatics` | `BlueprintSuggestProjectileVelocity` | `0x4406050` | `0x144406050` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UGameplayStatics` | `Blueprint_PredictProjectilePath_Advanced` | `0x4406340` | `0x144406340` | Final|Native|Static|Public|HasOutParams|BlueprintCallable |
| `UGameplayStatics` | `Blueprint_PredictProjectilePath_ByObjectType` | `0x44065F0` | `0x1444065F0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UGameplayStatics` | `Blueprint_PredictProjectilePath_ByTraceChannel` | `0x4406BD0` | `0x144406BD0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UGameplayStatics` | `DeprojectScreenToWorld` | `0x4408270` | `0x144408270` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `GetViewProjectionMatrix` | `0x440A7A0` | `0x14440A7A0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `ProjectWorldToScreen` | `0x440CDC0` | `0x14440CDC0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `SuggestProjectileVelocity_CustomArc` | `0x4410A70` | `0x144410A70` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UKismetMathLibrary` | `ProjectPointOnToPlane` | `0x4451A40` | `0x144451A40` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `ProjectVectorOnToPlane` | `0x4451BC0` | `0x144451BC0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `ProjectVectorOnToVector` | `0x4451CF0` | `0x144451CF0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector_GetProjection` | `0x445CA50` | `0x14445CA50` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector_ProjectOnToNormal` | `0x445D8D0` | `0x14445D8D0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetSystemLibrary` | `GetProjectContentDirectory` | `0x4475B50` | `0x144475B50` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UKismetSystemLibrary` | `GetProjectDirectory` | `0x4475BC0` | `0x144475BC0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UKismetSystemLibrary` | `GetProjectSavedDirectory` | `0x4475C30` | `0x144475C30` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UNavigationSystemV1` | `K2_ProjectPointToNavigation` | `0x464A390` | `0x14464A390` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UNavigationSystemV1` | `ProjectPointToNavigation` | `0x464A900` | `0x14464A900` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UPMAIHelpSDKSubSystem` | `GetProjectContentFullDir` | `0x25F4840` | `0x1425F4840` | Final|Native|Public |
| `UPMBlueprintLibrary` | `ProjectWorldLocationToScreenPosition` | `0x260BBA0` | `0x14260BBA0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UPMGameplayStatics` | `PMProjectWorldToScreen` | `0x2654D00` | `0x142654D00` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UProjectileMovementComponent` | `OnProjectileBounceDelegate__DelegateSignature` | `0x2A7DA10` | `0x142A7DA10` | MulticastDelegate|Public|Delegate|HasOutParams|HasDefaults |
| `UProjectileMovementComponent` | `OnProjectileStopDelegate__DelegateSignature` | `0x2A7DA10` | `0x142A7DA10` | MulticastDelegate|Public|Delegate|HasOutParams |
| `UWidgetLayoutLibrary` | `GetMousePositionOnPlatform` | `0x3A3FE40` | `0x143A3FE40` | Final|Native|Static|Public|HasDefaults|BlueprintCallable |
| `UWidgetLayoutLibrary` | `GetMousePositionOnViewport` | `0x3A3FE80` | `0x143A3FE80` | Final|Native|Static|Public|HasDefaults|BlueprintCallable |
| `UWidgetLayoutLibrary` | `GetMousePositionScaledByDPI` | `0x3A3FF10` | `0x143A3FF10` | Final|BlueprintCosmetic|Native|Static|Public|HasOutParams|BlueprintCallable|BlueprintPure |
| `UWidgetLayoutLibrary` | `GetViewportSize` | `0x3A40290` | `0x143A40290` | Final|BlueprintCosmetic|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UWidgetLayoutLibrary` | `ProjectWorldLocationToWidgetPosition` | `0x3A40820` | `0x143A40820` | Final|BlueprintCosmetic|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable... |

## 相机 / 视角

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `AController` | `GetViewTarget` | `0x43EAF50` | `0x1443EAF50` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `ACyWeaponBase` | `GetPlayerCameraManager` | `0x25D4A30` | `0x1425D4A30` | Final|Native|Public|Const |
| `APlayerCameraManager` | `GetFOVAngle` | `0x43E1EA0` | `0x1443E1EA0` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `APlayerCameraManager` | `GetCameraLocation` | `0x44C0CA0` | `0x1444C0CA0` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `APlayerCameraManager` | `GetCameraRotation` | `0x44C0CE0` | `0x1444C0CE0` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCameraComponent` | `GetCameraView` | `0x43D6CB0` | `0x1443D6CB0` | Native|Public|HasOutParams|BlueprintCallable |
| `UCameraModifier` | `GetViewTarget` | `0x3A37450` | `0x143A37450` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UGameplayStatics` | `GetPlayerCameraManager` | `0x440A020` | `0x14440A020` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |

## 玩家 / 控制器 取坐标链

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `AActor` | `K2_GetRootComponent` | `0x23909A0` | `0x1423909A0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AController` | `K2_GetPawn` | `0x43EB060` | `0x1443EB060` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `AGameModeBase` | `GetDefaultPawnClassForController` | `0x4409780` | `0x144409780` | Native|Event|Public|BlueprintCallable|BlueprintEvent |
| `AHUD` | `GetOwningPlayerController` | `0x142ADE0` | `0x14142ADE0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `APMHUD` | `GetPlayerCharacter` | `0x266A850` | `0x14266A850` | Final|Native|Protected|Const |
| `APMPlayerController` | `GetPlayerCharacter` | `0x26AF640` | `0x1426AF640` | Final|Native|Public |
| `APlayerCameraManager` | `GetOwningPlayerController` | `0x44C10A0` | `0x1444C10A0` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UAnimInstance` | `TryGetPawnOwner` | `0x43B0920` | `0x1443B0920` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UCyCharacterSpringArmComponent` | `GetPlayerController` | `0x237D200` | `0x14237D200` | Final|Native|Public |
| `UGameplayStatics` | `GetPlayerCharacter` | `0x440A0E0` | `0x14440A0E0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `GetPlayerController` | `0x440A1A0` | `0x14440A1A0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `GetPlayerControllerFromID` | `0x440A260` | `0x14440A260` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `GetPlayerControllerID` | `0x440A320` | `0x14440A320` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UGameplayStatics` | `GetPlayerPawn` | `0x440A3A0` | `0x14440A3A0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UPMBaseUserWidget` | `K2_GetOwningPlayer` | `0x2601B40` | `0x142601B40` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UPMBaseUserWidget` | `K2_GetOwningPlayerState` | `0x2601B70` | `0x142601B70` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UPMLS_Director_MatchBegin_Team` | `GetPawns` | `0x2680670` | `0x142680670` | Final|Native|Public|BlueprintCallable |
| `UPawnMovementComponent` | `GetPawnOwner` | `0x44BA900` | `0x1444BA900` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `USubsystemBlueprintLibrary` | `GetLocalPlayerSubSystemFromPlayerController` | `0x45112F0` | `0x1445112F0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `USubsystemBlueprintLibrary` | `GetLocalPlayerSubsystem` | `0x45113B0` | `0x1445113B0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UUserWidget` | `GetOwningPlayerPawn` | `0x3A33600` | `0x143A33600` | Final|BlueprintCosmetic|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UWidget` | `GetOwningPlayer` | `0x3A37450` | `0x143A37450` | BlueprintCosmetic|Native|Public|BlueprintCallable|BlueprintPure|Const |

## 移动组件

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `UAbilityTask_WaitVelocityChange` | `CreateWaitVelocityChange` | `0xB710B0` | `0x140B710B0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable |
| `UAvoidanceManager` | `GetAvoidanceVelocityForComponent` | `0x43C7A20` | `0x1443C7A20` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UCharacterMovementComponent` | `CalcVelocity` | `0x43E1980` | `0x1443E1980` | Native|Public|BlueprintCallable |
| `UCharacterMovementComponent` | `GetImpartedMovementBaseVelocity` | `0x43E1DB0` | `0x1443E1DB0` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCharacterMovementComponent` | `GetLastUpdateLocation` | `0x43E1DF0` | `0x1443E1DF0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCharacterMovementComponent` | `GetLastUpdateRotation` | `0x43E1E20` | `0x1443E1E20` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCharacterMovementComponent` | `GetLastUpdateVelocity` | `0x43E1E70` | `0x1443E1E70` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UCyCharacterAttributeSet` | `OnRep_ZVelocityMax` | `0x2375E80` | `0x142375E80` | Native|Protected |
| `UFlyingNavFunctionLibrary` | `GetActorFeetLocation` | `0x11DA5A0` | `0x1411DA5A0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetAnimationLibrary` | `K2_CalculateVelocityFromPositionHistory` | `0x38EDB10` | `0x1438EDB10` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UKismetAnimationLibrary` | `K2_CalculateVelocityFromSockets` | `0x38EDD70` | `0x1438EDD70` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UMovementComponent` | `GetGravityZ` | `0x44A4D20` | `0x1444A4D20` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UMovementComponent` | `GetMaxSpeed` | `0x44A4D60` | `0x1444A4D60` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UMovementComponent` | `K2_GetMaxSpeedModifier` | `0x44A4F00` | `0x1444A4F00` | Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UPhysicsConstraintComponent` | `SetAngularVelocityDrive` | `0x44BD2A0` | `0x1444BD2A0` | Final|Native|Public|BlueprintCallable |
| `UPhysicsConstraintComponent` | `SetAngularVelocityDriveSLERP` | `0x44BD370` | `0x1444BD370` | Final|Native|Public|BlueprintCallable |
| `UPhysicsConstraintComponent` | `SetAngularVelocityDriveTwistAndSwing` | `0x44BD400` | `0x1444BD400` | Final|Native|Public|BlueprintCallable |
| `UPhysicsConstraintComponent` | `SetAngularVelocityTarget` | `0x44BD4D0` | `0x1444BD4D0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UPhysicsConstraintComponent` | `SetLinearVelocityDrive` | `0x44BDF70` | `0x1444BDF70` | Final|Native|Public|BlueprintCallable |
| `UPhysicsConstraintComponent` | `SetLinearVelocityTarget` | `0x44BE090` | `0x1444BE090` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `GetPhysicsAngularVelocity` | `0x44D5B50` | `0x1444D5B50` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UPrimitiveComponent` | `GetPhysicsAngularVelocityInDegrees` | `0x44D5B50` | `0x1444D5B50` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UPrimitiveComponent` | `GetPhysicsAngularVelocityInRadians` | `0x44D5C20` | `0x1444D5C20` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UPrimitiveComponent` | `GetPhysicsLinearVelocity` | `0x44D5CC0` | `0x1444D5CC0` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `GetPhysicsLinearVelocityAtPoint` | `0x44D5D60` | `0x1444D5D60` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetAllPhysicsAngularVelocityInDegrees` | `0x44D71A0` | `0x1444D71A0` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetAllPhysicsAngularVelocityInRadians` | `0x44D72C0` | `0x1444D72C0` | Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetAllPhysicsLinearVelocity` | `0x44D73A0` | `0x1444D73A0` | Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsAngularVelocity` | `0x44D8970` | `0x1444D8970` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsAngularVelocityInDegrees` | `0x44D8970` | `0x1444D8970` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsAngularVelocityInRadians` | `0x44D8AB0` | `0x1444D8AB0` | Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsLinearVelocity` | `0x44D8BD0` | `0x1444D8BD0` | Native|Public|HasDefaults|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsMaxAngularVelocity` | `0x44D8CF0` | `0x1444D8CF0` | Final|Native|Public|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsMaxAngularVelocityInDegrees` | `0x44D8CF0` | `0x1444D8CF0` | Final|Native|Public|BlueprintCallable |
| `UPrimitiveComponent` | `SetPhysicsMaxAngularVelocityInRadians` | `0x44D8E00` | `0x1444D8E00` | Final|Native|Public|BlueprintCallable |
| `UProjectileMovementComponent` | `IsVelocityUnderSimulationThreshold` | `0x44DB080` | `0x1444DB080` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UProjectileMovementComponent` | `LimitVelocity` | `0x44DB0E0` | `0x1444DB0E0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UProjectileMovementComponent` | `SetVelocityInLocalSpace` | `0x44DB530` | `0x1444DB530` | Native|Public|HasDefaults|BlueprintCallable |
| `USkeletalMeshComponent` | `SetAllMotorsAngularVelocityDrive` | `0x44EE6C0` | `0x1444EE6C0` | Final|Native|Public|BlueprintCallable |

## 变换 / 数学

| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |
|---|---|---|---|---|
| `AActor` | `GetActorRelativeScale3D` | `0x439F2B0` | `0x14439F2B0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `AActor` | `SetActorRelativeScale3D` | `0x43A2D90` | `0x1443A2D90` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `AActor` | `SetActorScale3D` | `0x43A2E20` | `0x1443A2E20` | Final|Native|Public|HasDefaults|BlueprintCallable |
| `ACyApartmentGarageKitsCabinet` | `SpawnNormalGarageKits` | `0x232D230` | `0x14232D230` | Final|Native|Public|BlueprintCallable |
| `ACyWaterPoloSparSpawnManager` | `EnterNormalPhase` | `0x25D0690` | `0x1425D0690` | Final|Native|Public|BlueprintCallable |
| `ACyWaterPoloSparSpawnManager` | `SpawnNormalSparByCurrentPhase` | `0x25D0B30` | `0x1425D0B30` | Final|Native|Public|BlueprintCallable |
| `APMWhackMoleGameManager` | `OnNormalMoleStateChange` | `0x2718F80` | `0x142718F80` | Final|Native|Public |
| `UAbilitySystemBlueprintLibrary` | `GetGameplayCueEndLocationAndNormal` | `0xB58CD0` | `0x140B58CD0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `NormalizeDirectoryName` | `0x43CEF10` | `0x1443CEF10` | Final|Native|Static|Public|HasOutParams|BlueprintCallable|BlueprintPure |
| `UBlueprintPathsLibrary` | `NormalizeFilename` | `0x43CF070` | `0x1443CF070` | Final|Native|Static|Public|HasOutParams|BlueprintCallable|BlueprintPure |
| `UCyClientEventTrackSubsystem` | `SetPlayerShouldProcessNormalBombGuide` | `0x2391500` | `0x142391500` | Final|Native|Public |
| `UCyCommonNavigationButton` | `SetBarNormalStyle` | `0x2395910` | `0x142395910` | Final|Native|Public|BlueprintCallable |
| `UCyUWPracticeChallengePanelPC` | `OnClickNormalChallenge` | `0x25B96D0` | `0x1425B96D0` | Final|Native|Public |
| `UCyUWPracticeChallengePanelPC` | `OnHoveredNormalChallenge` | `0x25B98F0` | `0x1425B98F0` | Final|Native|Public |
| `UGameUserSettings` | `GetResolutionScaleNormalized` | `0x4414480` | `0x144414480` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `UGameUserSettings` | `SetResolutionScaleNormalized` | `0x44154C0` | `0x1444154C0` | Final|Native|Public|BlueprintCallable |
| `UKismetMathLibrary` | `BreakRotator` | `0x443A290` | `0x14443A290` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `BreakTransform` | `0x443A860` | `0x14443A860` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `BreakVector2D` | `0x443AAB0` | `0x14443AAB0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `BreakVector4` | `0x443ABD0` | `0x14443ABD0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `BreakVector` | `0x443ADC0` | `0x14443ADC0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `ComposeTransforms` | `0x443B8C0` | `0x14443B8C0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `FindLookAtRotation` | `0x44419C0` | `0x1444419C0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MirrorVectorByNormal` | `0x4443560` | `0x144443560` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `InverseTransformDirection` | `0x44454F0` | `0x1444454F0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `InverseTransformLocation` | `0x44456A0` | `0x1444456A0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `InverseTransformRotation` | `0x44458E0` | `0x1444458E0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `InvertTransform` | `0x4445A40` | `0x144445A40` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `LinePlaneIntersection_OriginNormal` | `0x4446E80` | `0x144446E80` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MakeVector4` | `0x4448540` | `0x144448540` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MakePlaneFromPointAndNormal` | `0x4448950` | `0x144448950` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MakeRotator` | `0x4449880` | `0x144449880` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MakeTransform` | `0x4449C90` | `0x144449C90` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MakeVector2D` | `0x4449E80` | `0x144449E80` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `MakeVector` | `0x4449F40` | `0x144449F40` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Normal2D` | `0x444F7E0` | `0x14444F7E0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Normal` | `0x444F8F0` | `0x14444F8F0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `NormalSafe2D` | `0x444FAC0` | `0x14444FAC0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Normalize2D` | `0x444FC00` | `0x14444FC00` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UKismetMathLibrary` | `NormalizeAxis` | `0x444FD80` | `0x14444FD80` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `NormalizeToRange` | `0x444FE00` | `0x14444FE00` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `NormalizedDeltaRotator` | `0x444FF10` | `0x14444FF10` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Quat_IsNormalized` | `0x4452A60` | `0x144452A60` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Quat_Normalize` | `0x4452C70` | `0x144452C70` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UKismetMathLibrary` | `Quat_Normalized` | `0x4452DB0` | `0x144452DB0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `RInterpTo` | `0x4453D50` | `0x144453D50` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `RInterpTo_Constant` | `0x4453ED0` | `0x144453ED0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `TransformDirection` | `0x4458B50` | `0x144458B50` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `TransformLocation` | `0x4458D00` | `0x144458D00` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `TransformRotation` | `0x4458F00` | `0x144458F00` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `VInterpTo` | `0x44594A0` | `0x1444594A0` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `VInterpTo_Constant` | `0x4459620` | `0x144459620` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector4_IsNormal3` | `0x445A620` | `0x14445A620` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector4_Normal3` | `0x445AB00` | `0x14445AB00` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector4_NormalUnsafe3` | `0x445ACB0` | `0x14445ACB0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector4_Normalize3` | `0x445AE00` | `0x14445AE00` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UKismetMathLibrary` | `Vector_IsNormal` | `0x445CEE0` | `0x14445CEE0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector_Normal2D` | `0x445D410` | `0x14445D410` | Final|Native|Static|Public|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector_NormalUnsafe` | `0x445D5E0` | `0x14445D5E0` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable|BlueprintPure |
| `UKismetMathLibrary` | `Vector_Normalize` | `0x445D720` | `0x14445D720` | Final|Native|Static|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `UKismetSystemLibrary` | `NormalizeFilename` | `0x447B3E0` | `0x14447B3E0` | Final|Native|Static|Public|BlueprintCallable|BlueprintPure |
| `UMovementComponent` | `ConstrainNormalToPlane` | `0x44A4C70` | `0x1444A4C70` | Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UMovementComponent` | `GetPlaneConstraintNormal` | `0x44A4DF0` | `0x1444A4DF0` | Final|Native|Public|HasDefaults|BlueprintCallable|BlueprintPure|Const |
| `UMovementComponent` | `SetPlaneConstraintNormal` | `0x44A56A0` | `0x1444A56A0` | Native|Public|HasDefaults|BlueprintCallable |
| `UPMWidgetBlueprintLibrary` | `SelectNormalKeyDisplay` | `0x2719500` | `0x142719500` | Final|Native|Static|Public|HasOutParams|BlueprintCallable |
| `UPhysicsSpringComponent` | `GetNormalizedCompressionScalar` | `0x44C1040` | `0x1444C1040` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |
| `URuntimeBlueprintMeshBuilder` | `GetNormal` | `0x126FF30` | `0x14126FF30` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `URuntimeBlueprintMeshBuilder` | `SetNormal` | `0x1272120` | `0x141272120` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `URuntimeBlueprintMeshBuilder` | `SetNormalTangent` | `0x1272260` | `0x141272260` | Final|Native|Public|HasOutParams|HasDefaults|BlueprintCallable |
| `USlider` | `GetNormalizedValue` | `0x3A2D1C0` | `0x143A2D1C0` | Final|Native|Public|BlueprintCallable|BlueprintPure|Const |

## 速查（最常用的几个）

| 函数 | 类 | RVA |
|---|---|---|
| `K2_GetActorLocation` | `AActor` | `0x43A1830` |
| `K2_GetActorRotation` | `AActor` | `0x43A1910` |
| `GetActorBounds` | `AActor` | `0x439EFA0` |
| `GetTransform` | `AActor` | `0x43A0310` |
| `K2_GetRootComponent` | `AActor` | `0x23909A0` |
| `K2_GetComponentLocation` | `USceneComponent` | `0x44E4EA0` |
| `GetComponentTransform` | (未找到) | - |
| `ProjectWorldLocationToScreen` | `APlayerController` | `0x44CA420` |
| `ProjectWorldToScreen` | `UGameplayStatics` | `0x440CDC0` |
| `GetPlayerCameraManager` | `UGameplayStatics` | `0x440A020` |
| `GetCameraLocation` | `APlayerCameraManager` | `0x44C0CA0` |
| `GetCameraRotation` | `APlayerCameraManager` | `0x44C0CE0` |
| `GetBoneLocationByName` | `UPoseableMeshComponent` | `0x44D04B0` |
| `GetBoneMatrix` | (未找到) | - |
| `GetSocketLocation` | `USceneComponent` | `0x44E3700` |
| `GetPlayerCharacter` | `UGameplayStatics` | `0x440A0E0` |
| `GetPlayerController` | `UGameplayStatics` | `0x440A1A0` |
| `GetActorFeetLocation` | `UFlyingNavFunctionLibrary` | `0x11DA5A0` |
| `GetVelocity` | `AActor` | `0x43A03C0` |
| `GetActorForwardVector` | `AActor` | `0x439F240` |

