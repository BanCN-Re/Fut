#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sdk_report.py - extract + diff a Dumper-7 SDK dump against the old D:\\xingyun baseline.

Usage:
    python sdk_report.py <sdk_root> [--baseline <old_sdk_root>] [--out report.md]

<sdk_root> is the folder Dumper-7 created, i.e. the one containing
CppSDK/, Dumpspace/, Mappings/.
"""
import argparse
import json
import os
import re
import sys
from datetime import datetime

# --- baseline: D:\xingyun\flavia_cn\cn_sdk\4.25.4-0+++UE4+Release-4.25-PM ---
OLD = {
    "GObjects":       0x0723A4B0,
    "GNames":         0x07220F80,
    "GWorld":         0x06E99018,
    "AppendString":   0x0282FE60,
    "ProcessEvent":   0x02A1D8F0,
    "ProcessEventIdx": 0x45,
}

# key classes whose member offsets we want to pull out of the generated headers
WANT_CLASSES = [
    "ACyCharacterBase",
    "UCyBaseAttributeSet",
    "APMPlayerState",
    "ACyPlayerState",
    "ACyWeaponBase",
    "UCyWeaponManagerBaseComponent",
    "APMCharacter",
    "AActor",
    "ACharacter",
    "APawn",
    "AController",
    "APlayerController",
    "APlayerCameraManager",
    "APlayerState",
    "USceneComponent",
    "UWorld",
    "ULevel",
    "UGameInstance",
    "ULocalPlayer",
    "UCharacterMovementComponent",
    "UCyFireComponent",
]

# fields we care about, per class
WANT_FIELDS = {
    "ACyCharacterBase": ["AbilitySystem", "AttributeTeamID", "AttributeSkinID",
                          "InventoryComponent", "LifeComponent", "LifeStateComponent",
                          "SkinComponent"],
    "UCyBaseAttributeSet": ["Health", "HealthMax", "Armor", "ArmorMax",
                             "ExtraArmor", "ExtraArmorMax", "TotalHealthMax"],
    "APMPlayerState": ["TeamIndex", "Rank", "PlayerPing", "RoleSkinId",
                        "SuitRoleSkinID", "InitGameTeamId"],
    "ACyPlayerState": ["RoleSkinId", "SuitRoleSkinID", "ShopComponent",
                        "PlayerNamePrivate", "Score"],
    "ACyWeaponBase": ["WeaponSkinId", "MeshComponent", "AttackComponent"],
    "UCyWeaponManagerBaseComponent": ["CurrentWeapon", "Inventory"],
    "APMCharacter": ["RoleID", "FPPCameraRotateOffset", "Rescuer"],
    "AActor": ["RootComponent", "bHidden"],
    "ACharacter": ["Mesh", "CapsuleComponent"],
    "APawn": ["PlayerState", "Controller"],
    "USceneComponent": ["RelativeLocation", "ComponentToWorld", "RelativeRotation",
                         "RelativeScale3D"],
    "AController": ["ControlRotation", "Character"],
    "APlayerController": ["AcknowledgedPawn", "PlayerCameraManager"],
    "APlayerCameraManager": ["CameraCache", "LastFrameCameraCache", "DefaultFOV",
                              "DefaultAspectRatio"],
    "APlayerState": ["PlayerNamePrivate", "Score", "PingInMs"],
    "UWorld": ["PersistentLevel", "OwningGameInstance", "Levels"],
    "ULevel": ["Actors"],
    "UGameInstance": ["LocalPlayers"],
    "ULocalPlayer": ["PlayerController"],
    "UCharacterMovementComponent": ["MaxWalkSpeed", "JumpZVelocity",
                                     "MaxAcceleration", "MaxWalkSpeedCrouched"],
    "UCyFireComponent": ["RecoilProcessor", "RecoilProcessorMap"],
}


def read_text(path):
    for enc in ("utf-8", "utf-8-sig", "latin-1"):
        try:
            with open(path, "r", encoding=enc, errors="strict") as f:
                return f.read()
        except (UnicodeDecodeError, LookupError):
            continue
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def find_offsets_info(root):
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if fn.lower() == "offsetsinfo.json":
                return os.path.join(dirpath, fn)
    return None


def parse_offsets_info(path):
    data = json.loads(read_text(path))
    out = {}
    for k, v in data.get("data", []):
        out[k] = v
    return out


def parse_basic_hpp(root):
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if fn.lower() == "basic.hpp":
                txt = read_text(os.path.join(dirpath, fn))
                res = {}
                for name in ("GObjects", "GNames", "GWorld", "AppendString",
                             "ProcessEvent", "ProcessEventIdx"):
                    m = re.search(
                        r"constexpr\s+int32\s+%s\s*=\s*(0x[0-9A-Fa-f]+)" % name, txt)
                    if m:
                        res[name] = int(m.group(1), 16)
                return res
    return {}


def class_headers(root):
    """map class name -> (path, text) for the generated *_classes.hpp files."""
    out = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if not fn.endswith("_classes.hpp"):
                continue
            p = os.path.join(dirpath, fn)
            out[p] = read_text(p)
    return out


def extract_class_block(text, cls):
    """Return the body of 'class <cls> [final] : public ... { ... };'.

    The declaration can carry specifiers (final, alignment) and a long base
    list, so allow any non-{,; run of characters between the name and '{'.
    """
    # The generated headers can carry alignment/specifier macros between the
    # keyword and the name, e.g. 'class SDK_ALIGN(0x10) ACharacter : public APawn'
    # or 'class alignas(0x10) USceneComponent : public UActorComponent', and
    # 'final' before the base list. Allow any run of specifier tokens.
    pat = re.compile(
        r"(?:^|\n)[ \t]*(?:class|struct)\s+"
        r"(?:[A-Za-z_][A-Za-z0-9_]*\s*(?:\([^)]*\))?\s+)*"
        r"%s\b[^{;]*\{" % re.escape(cls))
    m = pat.search(text)
    if not m:
        return None
    start = m.end()
    depth = 1
    i = start
    while i < len(text) and depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    return text[start:i - 1]


def extract_fields(body):
    """member declarations look like:
       char Padding_0000[0x10]; // 0x0000(0x0010)
       class UAbilitySystemComponent* AbilitySystem; // 0x0860(0x0008)
    """
    fields = {}
    for line in body.splitlines():
        m = re.search(r"//\s*0x([0-9A-Fa-f]+)\(0x([0-9A-Fa-f]+)\)", line)
        if not m:
            continue
        off = int(m.group(1), 16)
        size = int(m.group(2), 16)
        decl = line.split("//")[0].strip()
        if not decl or decl.startswith("//"):
            continue
        # last identifier before ; or [
        nm = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*;", decl)
        if not nm:
            continue
        fields[nm.group(1)] = (off, size)
    return fields


def struct_size(text, cls):
    m = re.search(r"//\s*0x([0-9A-Fa-f]+)\s*$", text, re.M)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sdk_root")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    root = args.sdk_root
    if not os.path.isdir(root):
        print("not a directory: %s" % root, file=sys.stderr)
        return 2

    lines = []
    def P(s=""):
        lines.append(s)
        print(s)

    P("# Dumper-7 SDK 抓取报告")
    P()
    P("- SDK 根目录: `%s`" % os.path.abspath(root))
    P("- 生成时间: %s" % datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    P()

    # ---- 1. offsets -------------------------------------------------------
    oi = find_offsets_info(root)
    new = {}
    if oi:
        P("## 1. 全局偏移（Dumpspace/OffsetsInfo.json）")
        P()
        P("来源: `%s`" % os.path.relpath(oi, root))
        P()
        raw = parse_offsets_info(oi)
        mapping = {
            "OFFSET_GOBJECTS": "GObjects",
            "OFFSET_GNAMES": "GNames",
            "OFFSET_GWORLD": "GWorld",
            "OFFSET_APPENDSTRING": "AppendString",
            "OFFSET_PROCESSEVENT": "ProcessEvent",
            "INDEX_PROCESSEVENT": "ProcessEventIdx",
        }
        for k, v in raw.items():
            if k in mapping:
                new[mapping[k]] = v
        P("| 项 | 新值 | 旧值(D:\\xingyun) | 变化 |")
        P("|---|---|---|---|")
        for k in ("GObjects", "GNames", "GWorld", "AppendString",
                  "ProcessEvent", "ProcessEventIdx"):
            nv = new.get(k)
            ov = OLD.get(k)
            if nv is None:
                P("| %s | (未找到) | 0x%X | ? |" % (k, ov))
                continue
            if ov is None:
                P("| %s | 0x%X | - | NEW |" % (k, nv))
            elif nv == ov:
                P("| %s | 0x%X | 0x%X | 相同 |" % (k, nv, ov))
            else:
                P("| %s | 0x%X | 0x%X | **%+d (0x%X)** |" % (k, nv, ov, nv - ov, abs(nv - ov)))
        P()
        P("原始 data: `%s`" % json.dumps(raw, ensure_ascii=False))
        P()
    else:
        P("## 1. 全局偏移")
        P()
        P("**未找到 OffsetsInfo.json** —— dump 可能未完成。")
        P()

    bh = parse_basic_hpp(root)
    if bh:
        P("## 2. Basic.hpp 交叉核对")
        P()
        P("| 项 | Basic.hpp | OffsetsInfo | 一致 |")
        P("|---|---|---|---|")
        for k in ("GObjects", "GNames", "GWorld", "AppendString",
                  "ProcessEvent", "ProcessEventIdx"):
            if k in bh:
                same = (new.get(k) == bh[k])
                P("| %s | 0x%X | %s | %s |" % (
                    k, bh[k],
                    ("0x%X" % new[k]) if k in new else "-",
                    "OK" if same else "**不一致**"))
        P()

    # ---- 3. class member offsets -----------------------------------------
    headers = class_headers(root)
    P("## 3. 关键类成员偏移")
    P()
    P("扫描 %d 个 `*_classes.hpp`。" % len(headers))
    P()
    found_any = False
    for cls in WANT_CLASSES:
        body = None
        for _p, txt in headers.items():
            body = extract_class_block(txt, cls)
            if body:
                break
        if not body:
            continue
        fields = extract_fields(body)
        want = WANT_FIELDS.get(cls)
        sel = {}
        if want:
            for w in want:
                if w in fields:
                    sel[w] = fields[w]
        if not sel:
            continue
        found_any = True
        P("### %s" % cls)
        P()
        P("| 成员 | 偏移 | 大小 |")
        P("|---|---|---|")
        for w, (off, size) in sorted(sel.items(), key=lambda kv: kv[1][0]):
            P("| %s | 0x%X | 0x%X |" % (w, off, size))
        P()

    if not found_any:
        P("(未匹配到任何目标类 —— 检查 PMGame_classes.hpp 是否已生成)")
        P()

    # ---- 4. inventory ----------------------------------------------------
    P("## 4. 产物清单")
    P()
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        depth = 0 if rel == "." else rel.count(os.sep) + 1
        if depth > 1:
            dirnames[:] = []
            continue
        P("- `%s/` — %d 项" % (rel if rel != "." else ".", len(filenames)))
    P()

    out = args.out or os.path.join(root, "..", "sdk_report.md")
    out = os.path.abspath(out)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n[written] %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
