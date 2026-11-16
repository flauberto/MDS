#!/usr/bin/env python3
"""Converte a pasta 'Click and Guide Samples' para Assets/sounds e gera Source/Engine/SoundIndex.h.
Uso: python3 tools/import_sounds.py "<pasta Click and Guide Samples>"   (requer ffmpeg)"""
import sys, os, re, glob, subprocess, shutil
src = sys.argv[1]; root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(root, 'Assets', 'sounds'); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
TRIM = 'silenceremove=stop_periods=1:stop_duration=0.05:stop_threshold=-60dB'
def conv(f, dest, ogg):
    args = ['ffmpeg', '-v', 'error', '-y', '-i', f, '-ac', '1', '-ar', '44100', '-af', TRIM]
    args += ['-c:a', 'libvorbis', '-q:a', '5'] if ogg else ['-c:a', 'pcm_s16le']
    subprocess.check_call(args + [dest])
slug = lambda s: re.sub(r'[^a-z0-9]+', '_', s.lower()).strip('_')

kits = {}
for f in sorted(glob.glob(os.path.join(src, 'Click Tracks', '*.wav'))):
    m = re.match(r'New Click -\s+(\w+)-(accents|quarter|eighth|sixteenth)\.wav', os.path.basename(f))
    if not m: continue
    part = {'accents': 'a', 'quarter': 'q', 'eighth': 'e', 'sixteenth': 's'}[m.group(2)]
    kits.setdefault(m.group(1), []).append(part)
    conv(f, os.path.join(out, f'ck_{m.group(1)}_{part}.wav'), False)

PT = {'intro': 'Intro', 'verse': 'Verso', 'pre_chorus': 'Pré-refrão', 'chorus': 'Refrão', 'post_chorus': 'Pós-refrão', 'bridge': 'Ponte',
      'refrain': 'Estribilho', 'tag': 'Tag', 'turnaround': 'Turnaround', 'vamp': 'Vamp', 'solo': 'Solo', 'instrumental': 'Instrumental',
      'interlude': 'Interlúdio', 'outro': 'Outro', 'ending': 'Final', 'acapella': 'A capella', 'breakdown': 'Breakdown', 'exhortation': 'Exortação', 'rap': 'Rap',
      'all_in': 'Todos entram', 'bass': 'Baixo', 'big_ending': 'Final grande', 'break': 'Pausa', 'build': 'Crescendo', 'channel': 'Channel', 'click': 'Click',
      'drums_in': 'Entra bateria', 'drums': 'Bateria', 'guitar': 'Guitarra', 'hits': 'Ataques', 'hold': 'Segura', 'key_change_down': 'Tom abaixo',
      'key_change_up': 'Tom acima', 'keys': 'Teclas', 'last_time': 'Última vez', 'pad': 'Pad', 'slowly_build': 'Crescendo lento', 'softly': 'Suave',
      'swell': 'Swell', 'ad_lib': 'Ad lib', 'worship_freely': 'Adoração livre'}
cues = {}   # id -> [group, en label]
order = []
for lang, folder, prefix, group in [('pt', 'Portugese Guides/Seções', 'Portugese - ', 0), ('en', 'English Guides/Song Sections', 'English Female - ', 0),
                                    ('pt', 'Portugese Guides/Dinâmica', 'Portugese - ', 1), ('en', 'English Guides/Dynamic Cues', 'English Female - ', 1)]:
    for f in sorted(glob.glob(os.path.join(src, folder, '*.wav'))):
        name = os.path.basename(f)[len(prefix):-4]
        if name.isdigit(): conv(f, os.path.join(out, f'num_{lang}_{name}.ogg'), True); continue
        cid = slug(name)
        if cid not in cues: cues[cid] = [group, name]; order.append(cid)
        dest = os.path.join(out, f'cue_{lang}_{cid}.ogg')
        if not os.path.exists(dest): conv(f, dest, True)
def pt_label(cid, en):
    m = re.match(r'(.*?)_(\d)$', cid)
    base, num = (m.group(1), ' ' + m.group(2)) if m else (cid, '')
    return PT.get(base, en) + num
def key(c):   # secoes em ordem musical, depois dinamicas em ordem alfabetica
    pri = ['intro', 'verse', 'pre_chorus', 'chorus', 'post_chorus', 'bridge', 'refrain', 'tag', 'turnaround', 'vamp', 'solo', 'instrumental', 'interlude', 'breakdown', 'acapella', 'exhortation', 'rap', 'outro', 'ending']
    base = re.sub(r'_\d$', '', c)
    return (cues[c][0], pri.index(base) if base in pri else 99, c)
order.sort(key=key)
with open(os.path.join(root, 'Source', 'Engine', 'SoundIndex.h'), 'w', encoding='utf-8') as h:
    h.write('// Gerado por tools/import_sounds.py - nao editar a mao\n#pragma once\nnamespace bt\n{\n')
    h.write('struct CueDef { const char* id; const char* pt; const char* en; int group; };   // group: 0 = secao, 1 = dinamica\n')
    h.write('static const char* const kClickKits[] = { ' + ', '.join(f'"{k}"' for k in sorted(kits)) + ' };\n')
    h.write(f'constexpr int kNumClickKits = {len(kits)};\n')
    h.write('static const CueDef kCues[] = {\n')
    for c in order:
        esc = ''.join(ch if ord(ch) < 128 else ''.join('\\x%02x' % b for b in ch.encode()) + '" "' for ch in pt_label(c, cues[c][1]))
        h.write(f'    {{ "{c}", "{esc}", "{cues[c][1]}", {cues[c][0]} }},\n')
    h.write('};\n' + f'constexpr int kNumCues = {len(order)};\n' + '}\n')
print(len(kits), 'kits;', len(order), 'guias;', len(os.listdir(out)), 'arquivos;', sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out)) // 1024, 'kB')
print({k: ''.join(v) for k, v in kits.items()})
