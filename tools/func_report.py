#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
func_report.py - extract coordinate / transform / projection function RVAs
from a Dumper-7 Dumpspace/FunctionsInfo.json dump.

Usage:
    python func_report.py <sdk_root> [--out funcs.md]
"""
import argparse
import json
import os
import re
import sys
from datetime import datetime

# functions we care about, grouped, matched as substrings of the function name
GROUPS = [
    ("位置 / 旋转（Actor 级）", [
        "K2_GetActorLocation", "K2_GetActorRotation", "GetActorLocation",
        "GetActorRotation", "GetTransform", "GetActorBounds", "GetActorEyesViewPoint",
        "GetActorForwardVector", "GetActorRightVector", "GetActorUpVector",
        "GetVelocity", "GetDistanceTo", "GetHorizontalDistanceTo",
        "GetSquaredDistanceTo", "GetVerticalDistanceTo", "GetDotProductTo",
        "SetActorLocation", "SetActorRotation", "SetActorTransform",
        "K2_SetActorLocation", "K2_SetActorRotation", "K2_SetActorTransform",
        "K2_TeleportTo", "K2_AddActorWorldOffset", "K2_AddActorWorldRotation",
        "K2_AddActorLocalOffset", "K2_AddActorLocalRotation",
    ]),
    ("位置 / 旋转（组件级）", [
        "K2_GetComponentLocation", "K2_GetComponentRotation",
        "K2_GetComponentToWorld", "GetComponentTransform", "GetComponentLocation",
        "GetComponentRotation", "GetComponentVelocity", "GetForwardVector",
        "GetRightVector", "GetUpVector", "GetSocketLocation", "GetSocketRotation",
        "GetSocketTransform", "GetSocketQuaternion", "K2_SetRelativeLocation",
        "K2_SetWorldLocation", "SetRelativeLocation", "SetWorldLocation",
        "SetRelativeRotation", "SetWorldRotation", "SetRelativeTransform",
        "SetWorldTransform", "AddRelativeLocation", "AddWorldOffset",
        "AddLocalOffset", "AddRelativeRotation", "AddWorldRotation",
        "AddLocalRotation", "GetComponentBounds", "GetAttachSocketName",
        "GetAttachParent",
    ]),
    ("骨骼 / 网格（坐标来源）", [
        "GetBoneLocation", "GetBoneRotation", "GetBoneMatrix", "GetBoneName",
        "GetBoneQuaternion", "GetSocketBoneName", "GetNumBones",
        "GetBoneTransform", "GetBoneAxis", "BoneIsChildOf",
        "GetSkeletalMesh", "GetSkinnedAsset", "GetComponentSpaceTransforms",
        "GetBoneSpaceTransforms", "GetSocketMatrix",
    ]),
    ("世界坐标 → 屏幕投影", [
        "ProjectWorldLocationToScreen", "ProjectWorldToScreen",
        "Project", "DeprojectScreenToWorld", "DeprojectScreenPositionToWorld",
        "GetViewportSize", "GetMousePosition", "GetScreenPosition",
        "WorldToScreen",
    ]),
    ("相机 / 视角", [
        "GetCameraLocation", "GetCameraRotation", "GetCameraCachePOV",
        "GetPlayerViewPoint", "GetActorEyesViewPoint", "GetViewTarget",
        "GetPlayerCameraManager", "GetCameraManager", "GetViewPoint",
        "GetFOVAngle", "GetCameraCacheTime", "GetCameraView",
        "GetPlayerCameraLocation", "GetPlayerCameraRotation",
    ]),
    ("玩家 / 控制器 取坐标链", [
        "GetPlayerCharacter", "GetPlayerPawn", "GetPlayerController",
        "GetPawn", "GetControlledPawn", "GetAcknowledgedPawn",
        "K2_GetActorLocation", "GetActorLocation", "GetRootComponent",
        "K2_GetRootComponent", "GetDefaultPawn", "GetLocalPlayer",
        "GetOwningPlayerController", "GetOwningPlayer",
    ]),
    ("移动组件", [
        "GetActorFeetLocation", "GetActorLocation", "Velocity",
        "GetMaxSpeed", "GetGravityZ", "GetActorNavLocation",
        "K2_GetActorLocation", "GetLastUpdateLocation", "GetLastUpdateRotation",
        "GetLastUpdateVelocity", "GetActorFeetLocationBasedOnFeet",
    ]),
    ("变换 / 数学", [
        "MakeTransform", "BreakTransform", "MakeVector", "BreakVector",
        "MakeRotator", "BreakRotator", "ComposeTransforms",
        "InvertTransform", "TransformLocation", "TransformDirection",
        "InverseTransformLocation", "InverseTransformDirection",
        "TransformRotation", "InverseTransformRotation",
        "GetActorRelativeScale3D", "SetActorScale3D", "SetActorRelativeScale3D",
        "VInterpTo", "RInterpTo", "FindLookAtRotation",
        "GetRotationXVector", "GetForwardVector", "Normal",
    ]),
]


def read_json(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return json.load(f)


def iter_functions(data):
    """yield (class, name, rva, flags) from Dumper-7 FunctionsInfo data."""
    for entry in data:
        if not isinstance(entry, dict):
            continue
        for cls, funcs in entry.items():
            if not isinstance(funcs, list):
                continue
            for item in funcs:
                if not isinstance(item, dict):
                    continue
                for name, payload in item.items():
                    if not isinstance(payload, list) or len(payload) < 4:
                        continue
                    rva = payload[2]
                    flags = payload[3] if len(payload) > 3 else ""
                    if isinstance(rva, int):
                        yield cls, name, rva, flags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sdk_root")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    root = args.sdk_root
    fi = None
    for dp, _dn, fn in os.walk(root):
        for f in fn:
            if f.lower() == "functionsinfo.json":
                fi = os.path.join(dp, f)
                break
        if fi:
            break
    if not fi:
        print("FunctionsInfo.json not found under %s" % root, file=sys.stderr)
        return 2

    data = read_json(fi).get("data", [])
    funcs = list(iter_functions(data))
    print("parsed %d functions from %s" % (len(funcs), os.path.relpath(fi, root)))

    lines = []
    def P(s=""):
        lines.append(s)

    P("# Dumper-7 坐标 / 变换 / 投影 函数表")
    P()
    P("- SDK: `%s`" % os.path.abspath(root))
    P("- 来源: `%s`" % os.path.relpath(fi, root))
    P("- 生成时间: %s" % datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    P("- 共解析 **%d** 个函数（全部类）" % len(funcs))
    P()
    P("> RVA 为相对镜像基址（`0x140000000`）的偏移；实际地址 = 基址 + RVA。")
    P()

    seen_global = set()
    for title, pats in GROUPS:
        rows = []
        for cls, name, rva, flags in funcs:
            for p in pats:
                if p.lower() in name.lower():
                    key = (cls, name, rva)
                    if key in seen_global:
                        continue
                    seen_global.add(key)
                    rows.append((cls, name, rva, flags))
                    break
        if not rows:
            continue
        rows.sort(key=lambda r: (r[0], r[2]))
        P("## %s" % title)
        P()
        P("| 类 | 函数 | RVA | 地址(base 0x140000000) | 标志 |")
        P("|---|---|---|---|---|")
        for cls, name, rva, flags in rows:
            fl = flags if len(flags) < 90 else flags[:87] + "..."
            P("| `%s` | `%s` | `0x%X` | `0x%X` | %s |" % (
                cls, name, rva, 0x140000000 + rva, fl))
        P()

    # key single functions called out explicitly
    P("## 速查（最常用的几个）")
    P()
    keys = ["K2_GetActorLocation", "K2_GetActorRotation", "GetActorBounds",
            "GetTransform", "K2_GetRootComponent",
            "K2_GetComponentLocation", "GetComponentTransform",
            "ProjectWorldLocationToScreen", "ProjectWorldToScreen",
            "GetPlayerCameraManager", "GetCameraLocation", "GetCameraRotation",
            "GetBoneLocation", "GetBoneMatrix", "GetSocketLocation",
            "GetPlayerCharacter", "GetPlayerController", "GetActorFeetLocation",
            "GetVelocity", "GetActorForwardVector"]
    P("| 函数 | 类 | RVA |")
    P("|---|---|---|")
    for k in keys:
        hit = None
        for cls, name, rva, flags in funcs:
            if name == k:
                hit = (cls, name, rva)
                break
        if not hit:
            for cls, name, rva, flags in funcs:
                if k.lower() in name.lower():
                    hit = (cls, name, rva)
                    break
        if hit:
            P("| `%s` | `%s` | `0x%X` |" % (hit[1], hit[0], hit[2]))
        else:
            P("| `%s` | (未找到) | - |" % k)
    P()

    out = args.out or os.path.join(root, "..", "funcs.md")
    out = os.path.abspath(out)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("[written] %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
