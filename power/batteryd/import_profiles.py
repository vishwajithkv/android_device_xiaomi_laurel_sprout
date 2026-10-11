#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Source transformation, not a build or validation command.
import argparse
import hashlib
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('stock_tree', type=Path)
args = parser.parse_args()
root = args.stock_tree / 'arch/arm64/boot/dts/xiaomi/laurel_sprout/battery'
out = ['// SPDX-License-Identifier: GPL-2.0-only',
       '// Generated from LineageOS android_kernel_xiaomi_sm6125,',
       '// 77d2912bc00eda19182a95a24bbe23543c792814. Preserve stock profile data.',
       '#pragma once', '#include <cstddef>', '#include <cstdint>',
       'struct Table { const int *temps; size_t cols; const int *soc; size_t rows; const int *uv; };',
       'struct Profile { unsigned ohm; uint64_t fingerprint; Table discharge, charge; const int *fcc; };']
for name, ohm in [('Sunwoda',68000), ('Feimaotui',330000), ('default-battery',0)]:
    path = root / f'batterydata-wingtech-{name}-4v4-4040mah.dtsi'
    data = path.read_text()
    tag = 'p' + str(ohm)
    def values(block, prop):
        raw = re.search(r'qcom,'+prop+r'\s*=\s*([^;]+);', block)[1]
        return [int(n) for n in re.findall(r'-?\d+', raw)]
    tables=[]
    for field in ['pc-temp-v1-lut','pc-temp-v2-lut']:
        block = re.search(r'qcom,'+field+r'\s*\{([^}]+)',data)[1]
        temps, soc, uv = (values(block,p) for p in ['lut-col-legend','lut-row-legend','lut-data'])
        if len(uv)!=len(temps)*len(soc):
            raise ValueError('Unexpected stock table dimensions')
        ident=tag+('_d' if field.endswith('v1-lut') else '_c')
        for label, arr in [('temps',temps),('soc',soc),('uv',[v*100 for v in uv])]:
            out.append(f'static constexpr int {ident}_{label}[] = {{'+','.join(map(str,arr))+'};')
        tables.append('{'+f'{ident}_temps,{len(temps)},{ident}_soc,{len(soc)},{ident}_uv'+'}')
    block=re.search(r'qcom,fcc1-temp-lut\s*\{([^}]+)',data)[1]
    fcc=values(block,'lut-data')
    out.append(f'static constexpr int {tag}_fcc[] = {{'+','.join(str(n*1000) for n in fcc)+'};')
    fingerprint=int.from_bytes(hashlib.sha256(path.read_bytes()).digest()[:8],'little')
    out.append(f'static constexpr Profile {tag} = {{{ohm},UINT64_C({fingerprint}),'+','.join(tables)+f',{tag}_fcc'+'};')
out.append('static constexpr const Profile *profiles[] = { &p68000, &p330000, &p0 };')
Path(__file__).with_name('profiles.h').write_text('\n'.join(out)+'\n')
