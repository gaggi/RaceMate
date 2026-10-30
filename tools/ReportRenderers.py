"""Summarize the GDI-only deterministic export matrix (standard library only)."""
import csv,json,statistics as st,sys
from pathlib import Path
root=Path(sys.argv[1] if len(sys.argv)>1 else "build/qa/gdi-export")
rows=list(csv.DictReader((root/"raw.csv").open(encoding="utf-8-sig")))
meta=json.loads((root/"environment.json").read_text(encoding="utf-8-sig"))
def num(r,k):
 v=r.get(k,""); return float(v.replace(",",".")) if v else None
def vals(g,k): return [n for r in g if (n:=num(r,k)) is not None]
def spread(v):
 if len(v)<2:return 0
 q=st.quantiles(v,n=4,method="inclusive");return q[2]-q[0]
def cell(v): return f"{st.median(v):.3f} ({spread(v):.3f})" if v else "N/A"
def artifact(r,suffix): return root/(r["ArtifactPrefix"]+suffix)
names={1:"Uniform / off",2:"Uniform / straight",3:"Uniform / premultiplied",4:"Background / off",5:"Background / straight",6:"Background / premultiplied"}
groups={k:[r for r in rows if int(r["Configuration"])==k and r["Single"]=="-1"] for k in names}
errors=[]
for r in rows:
 for owner in ("Race","Dwm"):
  r[owner+"Gpu"]=str(sum(num(r,owner+"Gpu"+e+"MsPerSec") or 0 for e in ("3D","Copy","Other")))
 r["TotalGpu"]=str(num(r,"RaceGpu")+num(r,"DwmGpu"))
for kind,rate in ((0,10),(1,1),(5,20)):
 hashes=set(); paints=set()
 for k,g in groups.items():
  for r in g:
   if num(r,f"K{kind}_refreshes")!=round(num(r,"Seconds"))*rate: errors.append("refresh count PID "+r["Pid"])
   hashes.add(r[f"K{kind}_scene_hash"])
   stages=list(csv.DictReader(artifact(r,"-stages.csv").open()))
   name={0:"Relative",1:"Standings",5:"Track Map"}[kind]
   p=next((s for s in stages if s["stage"]==f"overlay {name}:   drawing"),None)
   if p: paints.add(int(p["calls"]))
 if len(hashes)>1: errors.append(f"kind {kind} scene hash mismatch")
 if len(paints)>1: errors.append(f"kind {kind} repaint count mismatch")
# Audit completeness independently of the PowerShell run/resume checks.
import hashlib,re
for k,g in groups.items():
 if len(g)!=int(meta['Blocks']): errors.append(f"configuration {k}: expected {meta['Blocks']} main runs, got {len(g)}")
 if len({r['Block'] for r in g})!=len(g): errors.append(f"configuration {k}: duplicate blocks")
for r in rows:
 path=artifact(r,'-validation.txt')
 if not path.exists(): errors.append(f"missing validation {r['ArtifactPrefix']}"); continue
 check=path.read_text()
 if re.search(r'mismatches=[1-9]|mismatched pixels=[1-9]|readback=FAIL',check): errors.append(f"pixel validation {r['ArtifactPrefix']}")
 if int(r['Configuration']) in (2,3,5,6) and check.count('shared Spout readback=PASS')!=(3 if r['Single']=='-1' else 1): errors.append(f"missing GPU readback {r['ArtifactPrefix']}")
exe=Path('build/gdi-export/RaceMateDiagnostics.exe')
if exe.exists() and hashlib.sha256(exe.read_bytes()).hexdigest().upper()!=meta['ExeSha256']: errors.append('executable SHA256 changed')
(root/'audit.json').write_text(json.dumps({'valid':not errors,'errors':errors,'main_runs':sum(map(len,groups.values())),'single_runs':sum(r['Single']!='-1' for r in rows),'runs_per_configuration':{k:len(g) for k,g in groups.items()},'exe_sha256':meta['ExeSha256']},indent=2)+'\n',encoding='utf-8')
lines=["# GDI incremental alpha and Spout export measurement","",f"Hardware: {meta['Cpu'].strip()}, {meta['Gpu'][0]['Name']}. Values are median (IQR). {sum(len(g) for g in groups.values())} main runs; {sum(r['Single']!='-1' for r in rows)} single-overlay runs.","","Validity: "+("; ".join(errors) if errors else "PASS — deterministic scenes, equal requested and actual redraw counts."),"", "Visible overlays over a fixed checkerboard, 90% opacity, rounded corners, Relative 328×217 @10Hz, Standings 472×345 @1Hz, Track Map 440×296 @20Hz. Fresh process, 3s warmup, 20s measurement, randomized complete blocks. Spout receivers/readback only after counters stop.","", "CPU time in ms/s is deliberately excluded: prior counters had zero medians and 15.6ms accounting artifacts. DWM cycles are N/A when Windows denies access. GPU engine times can overlap and their sum is not a vendor-wide utilization reading.","",f"The one-core percentage is only an approximate cycle-budget equivalent using the specified/nominal {meta['ClockGHz']}GHz: Mcycles/s ÷ (GHz×1000) ×100. It is NOT a verified CPU-time conversion; CPU cycle timer implementations and clocks can vary.","", "## Main results", "", "| # / configuration | n | Race Mcycles/s | DWM Mcycles/s | Approx. one core % | Race GPU ms/s | DWM GPU ms/s | Total GPU ms/s |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
for k,g in groups.items():
 if g: lines.append(f"| {k}: {names[k]} | {len(g)} | "+" | ".join(cell(vals(g,f)) for f in ("RaceMillionCyclesPerSec","DwmMillionCyclesPerSec","ApproxOneCorePercent","RaceGpu","DwmGpu","TotalGpu"))+" |")
old_path=Path("build/qa/renderers/raw.csv")
if old_path.exists():
 old=list(csv.DictReader(old_path.open(encoding="utf-8-sig")))
 oldg={k:[r for r in old if r['Single']=='-1' and int(r['Configuration'])==k] for k in range(1,12)}
 lines += ["", "## Previous exporter and target comparison", "", "Historical runs used the same default three-overlay scene, but were taken in an earlier session and include runs with zero DWM GPU activity. Their display state was not continuously verified. Values below are historical targets, not a controlled same-state A/B comparison; reported differences cannot be attributed solely to the code change. Background straight had no old variant-B measurement: old #5 used full redraw A, so that comparison combines renderer and export improvements.","", "| New # | New Mcycles/s | Old counterpart | Old Mcycles/s | Observed cycle difference |", "|---|---:|---|---:|---:|"]
 for k,oldk,label in ((1,1,"old baseline"),(2,2,"old uniform straight"),(3,3,"old uniform premultiplied"),(4,6,"old background B, off"),(5,5,"old background A + straight (confounded)"),(6,7,"old background B + premultiplied")):
  if groups[k] and oldg[oldk]:
   current=st.median(vals(groups[k],"RaceMillionCyclesPerSec")); before=st.median(vals(oldg[oldk],"RaceMillionCyclesPerSec"))
   lines.append(f"| {k} | {current:.3f} | {label} | {before:.3f} | {100*(1-current/before):.1f}% |")
 for k,oldk in ((3,9),(6,11)):
  if groups[k]:
   target=st.median(vals(oldg[oldk],"RaceMillionCyclesPerSec"));current=st.median(vals(groups[k],"RaceMillionCyclesPerSec"))
   lines.append(f"| {k} | {current:.3f} | archived D2D #{oldk} target | {target:.3f} | {100*(1-current/target):.1f}% |")
if old_path.exists():
 lines += ["", "### Historical display-state caveat", "", "These archived configurations contain DWM-zero runs. For transparency the subset with a positive DWM delta is shown separately, without replacing the requested original target values. Positive activity alone does not prove that the screen stayed active for the entire run. The subset is exploratory and has fewer than ten repeats; desktop/background GPU load also differs between sessions.", "", "| Archived # | All runs | DWM-zero runs | Positive-DWM subset n | Subset Race Mcycles/s | Subset Race GPU ms/s | Subset DWM GPU ms/s |", "|---|---:|---:|---:|---:|---:|---:|"]
 for k in (1,2,3,6,7,9,11):
  g=oldg[k]
  def gpu(r,owner): return sum(num(r,owner+'Gpu'+e+'MsPerSec') or 0 for e in ('3D','Copy','Other'))
  active=[r for r in g if gpu(r,'Dwm')>0]
  lines.append(f"| {k} | {len(g)} | {len(g)-len(active)} | {len(active)} | {cell(vals(active,'RaceMillionCyclesPerSec'))} | {cell([gpu(r,'Race') for r in active])} | {cell([gpu(r,'Dwm') for r in active])} |")
lines += ["", "## Export preparation on the UI thread", "", "Median per submitted image in µs (IQR across runs). Worker conversion is reported separately below. Old and new captures are from different sessions; these are observed costs, not an isolated scheduling experiment.", "", "| New # | Overlay | Old UI prep µs/image | New UI prep µs/image |", "|---|---|---:|---:|"]
for k,oldk in ((2,2),(3,3),(5,5),(6,7)):
 for name in ('Relative','Standings','Track Map'):
  samples=[]
  for folder,rs in ((Path('build/qa/renderers'),oldg[oldk] if old_path.exists() else []),(root,groups[k])):
   values=[]
   for r in rs:
    path=folder/(r['ArtifactPrefix']+'-stages.csv')
    if path.exists():
     for e in csv.DictReader(path.open()):
      if e['stage']==f'overlay {name}: Spout image preparation and queue' and int(e['calls']): values.append(float(e['total_us'])/int(e['calls']))
   samples.append(cell(values))
  lines.append(f"| {k} | {name} | "+" | ".join(samples)+" |")
if (root/'display-idle-correction.json').exists():
 lines += ["", "## Display-idle correction", "", "The display was switched off during the first attempt; the user confirmed this and switched it back on. DWM GPU counters stopped while the display was off. The power plan also has a 900s display-idle timeout, but these traces cannot establish whether the initial switch-off was manual or automatic. Those 43 partially/fully idle main runs and the idle single-overlay runs are excluded; complete original data remains in ../gdi-export-display-idle-original. Seventeen fully active-display runs were retained and the other 43 rerun with the identical executable. No run was excluded for being slow.", "", "The script now holds a thread-scoped display/system power request, explicitly wakes the monitor, and rejects a run with no DWM GPU activity. Power-plan settings are not modified. Short single-overlay runs were omitted from the corrected final series to avoid further unnecessary delay; per-overlay timings come from all 60 main runs.", "", "An excluded transition run (old block 3, configuration 4, step 107) recorded 160.222ms wall time, including 159.092ms in Present, 0.895ms drawing, no export, no resource/cache creation. Its DWM GPU activity collapsed during the same interval and remained frozen until the monitor was switched back on. This points to the display power transition rather than Spout; it is not an ETW proof of the underlying Windows wait. The record remains available at ../gdi-export-display-idle-original/b3-c4-s-1-p43308-outliers.csv."]
lines += ["", "## Paired feature costs", "", "Differences pair runs in the same randomized block. Magnitude > IQR is descriptive, not a significance test. Positive means additional cost.", "", "| Difference | Race Mcycles/s delta (IQR) | GPU engine ms/s delta (IQR) | Cycle magnitude > IQR | GPU magnitude > IQR |", "|---|---:|---:|---|---|"]
for title,a,b in (("Spout straight (2−1)",2,1),("Spout premultiplied (3−1)",3,1),("Background Spout straight (5−4)",5,4),("Background Spout premultiplied (6−4)",6,4),("Background only (4−1)",4,1),("Background + premultiplied Spout (6−1)",6,1)):
 aa={r['Block']:r for r in groups[a]};bb={r['Block']:r for r in groups[b]};keys=aa.keys()&bb.keys()
 diffs=[[num(aa[x],field)-num(bb[x],field) for x in keys] for field in ("RaceMillionCyclesPerSec","TotalGpu")]
 lines.append(f"| {title} | "+" | ".join(cell(v) for v in diffs)+" | "+" | ".join(("yes" if abs(st.median(v))>spread(v) else "no") if v else "N/A" for v in diffs)+" |")
lines += ["", "## Per-overlay refresh cost", "", "QPC wall duration includes submission and possible scheduling delays; thread cycles are raw. These scopes include scene, drawing, present and queue preparation. Worker conversion/upload/send is separate.", "", "| # | Overlay | Refreshes/run | QPC median µs (IQR) | QPC maximum µs | Median thread cycles (IQR) |", "|---|---|---:|---:|---:|---:|"]
for k,g in groups.items():
 for kind,name in ((0,"Relative"),(1,"Standings"),(5,"Track Map")):
  if g: lines.append(f"| {k} | {name} | {cell(vals(g,f'K{kind}_refreshes'))} | {cell(vals(g,f'K{kind}_median_wall_us'))} | {max(vals(g,f'K{kind}_max_wall_us')):.1f} | {cell(vals(g,f'K{kind}_median_thread_cycles'))} |")
lines += ["", "## UI refreshes over 5 ms", "", "Stage times are QPC durations. Drawing includes composition; do not sum nested stages. The per-outlier present field times UpdateLayeredWindow only. Uniform-alpha WM_PAINT screen copy is available as an aggregate profiler stage, not separately for each outlier. UpdateWindow/GdiFlush, region bookkeeping and time outside the named scopes are therefore not fully attributed in these traces; no scheduling cause is asserted for that residual. A long wall duration with ordinary cycle counts suggests off-CPU scheduling, but is not proof without an OS scheduling trace.", "", "| # / block | kind | step | wall µs | cycles | scene | draw | compose | present | export prep | buffer/font create | map cache |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
outliers=[]
for r in rows:
 path=artifact(r,"-outliers.csv")
 if not path.exists():continue
 for e in csv.DictReader(path.open()):
  outliers.append(dict(configuration=r['Configuration'],block=r['Block'],single=r['Single'],**e))
  lines.append(f"| {r['Configuration']} / {r['Block']} | "+" | ".join(e[f] for f in ("kind","step","wall_us","thread_cycles","scene_us","drawing_us","composition_us","present_us","spout_preparation_us","buffer_create_us","map_cache_us"))+" |")
if outliers:
 with (root/'outliers.csv').open('w',newline='') as f:
  writer=csv.DictWriter(f,fieldnames=list(outliers[0]));writer.writeheader();writer.writerows(outliers)
if old_path.exists():
 lines += ["", "### Context for the historical 29/34/158ms events", "", "The archived 158ms and 34ms events occur in runs with partial DWM GPU activity near the transition between active and zero-activity blocks. The 29ms event occurred while DWM activity was zero. Together with the newly localized 159ms Present delay at display switch-off this makes a display/presentation transition a plausible contributor, particularly for the historical 158ms event. Individual old stage traces are absent, so this is correlation, not a proven retrospective cause.", "", "| Archived # | Block | Overlay kind | Max refresh µs | DWM GPU ms/s |", "|---|---:|---:|---:|---:|"]
 for r in old:
  if r['Single']!='-1': continue
  for kind in (0,1,5):
   if num(r,f'K{kind}_max_wall_us')>25000:
    lines.append(f"| {r['Configuration']} | {r['Block']} | {kind} | {num(r,f'K{kind}_max_wall_us'):.1f} | {gpu(r,'Dwm'):.3f} |")
lines += ["", "## Worker conversion/upload/send", "", "Steady-state work is off the render thread. Raw pixels are copied once into a reusable pending canvas; conversion writes the persistent export image. Only dirty pixels are processed. One bounding upload box avoids many driver calls for disjoint dirty rectangles. Spout still copies the complete GPU texture to its shared texture. Buffer sizes and option changes require a full update.", "", "| # | kind | Worker frames/s | processed pixels/s | upload bytes/s | conversion µs/frame | upload µs/frame | send µs/frame |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
for k,g in groups.items():
 for kind in (0,1,5):
  collected=[]
  for r in g:
   for e in csv.DictReader(artifact(r,'-worker.csv').open()):
    if int(e['kind'])==kind and float(e['frames']):
     n=float(e['frames']); t=num(r,'Seconds'); collected.append([n/t,float(e['processed_pixels'])/t,float(e['upload_bytes'])/t,float(e['conversion_us'])/n,float(e['upload_us'])/n,float(e['send_us'])/n])
  if collected: lines.append(f"| {k} | {kind} | "+" | ".join(cell([c[i] for c in collected]) for i in range(6))+" |")
lines += ["", "Worker outliers and cold creation events are retained in each run's -worker-outliers.csv and -worker-warmup.csv. Cold events are excluded from the 20s totals. Historical 29/34/158ms maxima cannot be retrospectively attributed because their individual stage traces were not recorded.", "", "## Single-overlay runs", "", "Single short runs have no dispersion estimate and include fixed preparation/desktop overhead; they cannot be added to predict the combined process.", "", "| # | kind | Race Mcycles/s | GPU engine ms/s |", "|---|---:|---:|---:|"]
for r in rows:
 if r['Single']!='-1':lines.append(f"| {r['Configuration']} | {r['Single']} | {num(r,'RaceMillionCyclesPerSec'):.3f} | {num(r,'TotalGpu'):.3f} |")
lines += ["", "## Validation and archive", "", "See audit.json, native per-run validation files, gdi-alpha-regression.txt and export test logs. The pre-improvement prototype and all old measurements are committed only on prototype/d2d-renderer (20e98f6); main remains at its original commit. All implementation changes on main are uncommitted.", "", "CPU conversion uses exact reciprocal multiplication and opacity tables; no per-pixel division/modulo/region query in the exporter. Corner masks are built only on geometry changes. Merged queue updates retain the union of dirty pixels so no changes are lost when the worker falls behind.", "", "Reproduce: tools/MeasureRenderers.ps1, then python tools/ReportRenderers.py build/qa/gdi-export. Optionally start the script from an already elevated PowerShell for DWM access; it does not elevate itself. N/A remains valid if access is denied. ClockGHz is an explicitly approximate budget denominator, not a measurement of elapsed CPU time."]
(root/'report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f"Wrote {root/'report.md'}; errors={len(errors)}")
